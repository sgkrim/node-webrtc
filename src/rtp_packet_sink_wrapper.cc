#include "rtp_packet_sink_wrapper.h"

#include <memory>

// Включаємо всі необхідні заголовки для доступу до внутрішніх API
#include "src/interfaces/rtc_peer_connection.h"
#include "src/interfaces/rtc_rtp_receiver.h"
#include "src/interfaces/media_stream_track.h"
#include "pc/peer_connection.h"
#include "media/base/channel_manager.h"
#include "media/base/voice_channel.h"
#include "media/base/video_channel.h"


// AsyncWorker для безпечної передачі даних в головний потік Node.js
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

RtpPacketSinkWrapper::RtpPacketSinkWrapper(const Napi::CallbackInfo& info)
  : Napi::ObjectWrap<RtpPacketSinkWrapper>(info) {
  // 1. Перевіряємо аргументи: (peerConnection, rtpReceiver, callback)
  if (info.Length() < 3 || !info[0].IsObject() || !info[1].IsObject() || !info[2].IsFunction()) {
    Napi::TypeError::New(info.Env(), "Expected (peerConnection, rtpReceiver, callback)").ThrowAsJavaScriptException();
    return;
  }

  // 2. Зберігаємо посилання на JS-об'єкти, щоб використати їх пізніше
  _pcRef = Napi::Persistent(info[0].As<Napi::Object>());
  _receiverRef = Napi::Persistent(info[1].As<Napi::Object>());
  _onpacket = Napi::Persistent(info[2].As<Napi::Function>());

  // 3. Створюємо наш sink
  _sink = std::make_unique<RtpPacketSink>([this](const uint8_t* data, size_t length, uint32_t timestamp) {
    auto* packet_data = new RtpPacketData();
    packet_data->length = length;
    packet_data->data = std::unique_ptr<uint8_t[]>(new uint8_t[length]);
    memcpy(packet_data->data.get(), data, length);
    packet_data->timestamp = timestamp;
    (new OnPacketWorker(_onpacket.Value(), packet_data))->Queue();
  });

  // 4. Знаходимо потрібний MediaChannel і встановлюємо sink
  if (auto* channel = GetMediaChannel()) {
    channel->SetRawRtpPacketSink(_sink.get());
  }
}

RtpPacketSinkWrapper::~RtpPacketSinkWrapper() {
  _Stop();
}

void RtpPacketSinkWrapper::_Stop() {
  if (_sink) {
    // Знаходимо канал і видаляємо sink
    if (auto* channel = GetMediaChannel()) {
      channel->SetRawRtpPacketSink(nullptr);
    }
    _sink.reset(); // Звільняємо sink
  }
  if (!_onpacket.IsEmpty()) {
    _onpacket.Reset();
  }
  if (!_pcRef.IsEmpty()) {
    _pcRef.Reset();
  }
  if (!_receiverRef.IsEmpty()) {
    _receiverRef.Reset();
  }
}

void RtpPacketSinkWrapper::Stop(const Napi::CallbackInfo& /* info */) {
  _Stop();
}

// Допоміжний метод для пошуку MediaChannel
cricket::MediaChannel* RtpPacketSinkWrapper::GetMediaChannel() {
  if (_pcRef.IsEmpty() || _receiverRef.IsEmpty()) {
    return nullptr;
  }

  // "Розгортаємо" JS-об'єкти до C++ класів
  auto* pc_wrapper = node_webrtc::RTCPeerConnection::Unwrap(_pcRef.Value());
  auto* receiver_wrapper = node_webrtc::RTCRtpReceiver::Unwrap(_receiverRef.Value());

  if (!pc_wrapper || !receiver_wrapper) {
    return nullptr;
  }

  // Отримуємо доступ до внутрішніх об'єктів WebRTC
  webrtc::PeerConnection* webrtc_pc = pc_wrapper->pc();
  auto receiver_track = receiver_wrapper->receiver()->track();

  if (!webrtc_pc || !receiver_track) {
    return nullptr;
  }

  // Отримуємо менеджер каналів
  cricket::ChannelManager* channel_manager = webrtc_pc->channel_manager();
  if (!channel_manager) {
    return nullptr;
  }

  // Визначаємо тип каналу (аудіо/відео) і повертаємо його
  if (receiver_track->kind() == "audio") {
    return channel_manager->voice_channel();
  }
  if (receiver_track->kind() == "video") {
    return channel_manager->video_channel();
  }

  return nullptr;
}