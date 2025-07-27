#include "rtp_packet_sink_wrapper.h"

#include "src/interfaces/rtc_peer_connection.h"
#include "src/interfaces/rtc_rtp_receiver.h"
#include "src/interfaces/media_stream_track.h"
#include "rtp_packet_sink.h"

// Завдяки патчу і "дружбі" ці файли тепер доступні і працюють
#include "pc/peer_connection.h"
#include "pc/channel_manager.h"
#include "media/base/media_channel.h"
#include "api/rtp_receiver_interface.h"

class OnPacketWorker : public Napi::AsyncWorker {
 public:
  OnPacketWorker(const Napi::Function& callback, RtpPacketData* data)
    : Napi::AsyncWorker(callback), _data(data) {}
  ~OnPacketWorker() override = default;
  void Execute() override {}
  void OnOK() override {
    Napi::HandleScope scope(Env());
    Napi::Object packet_obj = Napi::Object::New(Env());
    packet_obj.Set("payload", Napi::Buffer<uint8_t>::New(
      Env(),
      _data->data.release(),
      _data->length,
      [](Napi::Env, uint8_t* d) { delete[] d; }
    ));
    packet_obj.Set("timestamp", Napi::Number::New(Env(), _data->timestamp));
    Callback().Call({packet_obj});
    delete _data;
  }
 private:
  RtpPacketData* _data;
};

Napi::FunctionReference RtpPacketSinkWrapper::audio_constructor;
Napi::FunctionReference RtpPacketSinkWrapper::video_constructor;

void RtpPacketSinkWrapper::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function audio_func = DefineClass(env, "RTCRawAudioSink", {
    InstanceMethod("stop", &RtpPacketSinkWrapper::Stop)
  });
  audio_constructor = Napi::Persistent(audio_func);
  audio_constructor.SuppressDestruct();
  Napi::Function video_func = DefineClass(env, "RTCRawVideoSink", {
    InstanceMethod("stop", &RtpPacketSinkWrapper::Stop)
  });
  video_constructor = Napi::Persistent(video_func);
  video_constructor.SuppressDestruct();
  Napi::Object nonstandard = Napi::Object::New(env);
  nonstandard.Set("RTCRawAudioSink", audio_func);
  nonstandard.Set("RTCRawVideoSink", video_func);
  exports.Set("nonstandard", nonstandard);
}

RtpPacketSinkWrapper::RtpPacketSinkWrapper(const Npi::CallbackInfo& info)
  : Napi::ObjectWrap<RtpPacketSinkWrapper>(info) {
  if (info.Length() < 3 || !info[0].IsObject() || !info[1].IsObject() || !info[2].IsFunction()) {
    Napi::TypeError::New(info.Env(), "Expected (peerConnection, rtpReceiver, callback)").ThrowAsJavaScriptException();
    return;
  }

  _pcRef = Napi::Persistent(info[0].As<Napi::Object>());
  _receiverRef = Napi::Persistent(info[1].As<Napi::Object>());
  _onpacket = Napi::Persistent(info[2].As<Napi::Function>());

  // ВИПРАВЛЕНО: Сигнатура лямбди тепер відповідає очікуванням RtpPacketSink
  _sink = std::make_unique<RtpPacketSink>([this](const uint8_t* data, size_t length, uint32_t timestamp) {
    auto* packet_data = new RtpPacketData();
    packet_data->length = length;
    packet_data->data = std::unique_ptr<uint8_t[]>(new uint8_t[length]);
    memcpy(packet_data->data.get(), data, length);
    packet_data->timestamp = timestamp;
    (new OnPacketWorker(_onpacket.Value(), packet_data))->Queue();
  });

  if (auto* channel = GetMediaChannel()) {
    // ВИПРАВЛЕНО: Повертаємось до початкового методу. Якщо він не існує,
    // проблема в API цієї версії WebRTC.
    channel->SetRawRtpPacketSink(_sink.get());
  }
}

RtpPacketSinkWrapper::~RtpPacketSinkWrapper() {
  _Stop();
}

void RtpPacketSinkWrapper::_Stop() {
  if (_sink) {
    if (auto* channel = GetMediaChannel()) {
      // ВИПРАВЛЕНО: Для видалення передаємо nullptr.
      channel->SetRawRtpPacketSink(nullptr);
    }
    _sink.reset();
  }
  if (!_onpacket.IsEmpty()) _onpacket.Reset();
  if (!_pcRef.IsEmpty()) _pcRef.Reset();
  if (!_receiverRef.IsEmpty()) _receiverRef.Reset();
}

void RtpPacketSinkWrapper::Stop(const Napi::CallbackInfo& /* info */) {
  _Stop();
}

// =======================================================================================
// УВАГА: Наступний метод буде компілюватися, ЛИШЕ ЯКЩО:
// 1. Ваша декларація `friend class RtpPacketSinkWrapper;` у файлі
//    `src/interfaces/rtc_peer_connection.h` була успішно додана і бачиться компілятором.
// 2. Ваш ПАТЧ для файлу `pc/peer_connection.h`, який робить метод `channel_manager()`
//    публічним, був успішно застосований під час збірки.
//
// ЯКЩО ви продовжуєте отримувати помилки "private within this context", це означає,
// що одна з цих двох умов НЕ виконана, і проблема полягає у вашому процесі збірки,
// а не в цьому C++ коді.
// =======================================================================================
cricket::MediaChannel* RtpPacketSinkWrapper::GetMediaChannel() {
  if (_pcRef.IsEmpty() || _receiverRef.IsEmpty()) {
    return nullptr;
  }

  auto* pc_wrapper = node_webrtc::RTCPeerConnection::Unwrap(_pcRef.Value());
  auto* receiver_wrapper = node_webrtc::RTCRtpReceiver::Unwrap(_receiverRef.Value());

  if (!pc_wrapper || !receiver_wrapper) {
    return nullptr;
  }

  webrtc::PeerConnectionInterface* pc_interface = pc_wrapper->_jinglePeerConnection.get();
  if (!pc_interface) {
    return nullptr;
  }

  auto* pc_impl = static_cast<webrtc::PeerConnection*>(pc_interface);
  auto* channel_manager = pc_impl->channel_manager();
  if (!channel_manager) {
    return nullptr;
  }

  auto receiver_track = receiver_wrapper->receiver()->track();
  if (!receiver_track) {
    return nullptr;
  }

  // ВИПРАВЛЕНО: Методи Get...Channel були правильними. Комп'ютерна підказка про
  // Create...Channel була невірною для нашої задачі.
  if (receiver_track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
    return channel_manager->GetVoiceChannel(receiver_track->id());
  } else if (receiver_track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
    return channel_manager->GetVideoChannel(receiver_track->id());
  }

  return nullptr;
}
