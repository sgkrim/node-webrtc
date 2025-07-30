#include "rtp_packet_sink_wrapper.h"

#include "src/interfaces/rtc_peer_connection.h"
#include "src/interfaces/rtc_rtp_receiver.h"
#include "src/interfaces/media_stream_track.h"
#include "rtp_packet_sink.h"

// ДОДАНО: Включення для логування
#include <iostream>

#include "pc/peer_connection.h"
#include "pc/channel_manager.h"
#include "pc/channel.h"
#include "media/base/media_channel.h"
#include "api/rtp_transceiver_interface.h"


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

Napi::Object RtpPacketSinkWrapper::Init(Napi::Env env, Napi::Object exports) {
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

  return exports;
}

RtpPacketSinkWrapper::RtpPacketSinkWrapper(const Napi::CallbackInfo& info)
  : Napi::ObjectWrap<RtpPacketSinkWrapper>(info) {

  // ЗМІНЕНО: Більш детальна перевірка аргументів конструктора
  if (info.Length() != 3) {
      Napi::TypeError::New(info.Env(), "RTCRawSink constructor expects exactly 3 arguments: (peerConnection, rtpReceiver, callback)").ThrowAsJavaScriptException();
      return;
  }
  if (!info[0].IsObject()) {
      Napi::TypeError::New(info.Env(), "Argument 1: peerConnection must be an object.").ThrowAsJavaScriptException();
      return;
  }
  if (!info[1].IsObject()) {
      Napi::TypeError::New(info.Env(), "Argument 2: rtpReceiver must be an object.").ThrowAsJavaScriptException();
      return;
  }
  if (!info[2].IsFunction()) {
      Napi::TypeError::New(info.Env(), "Argument 3: callback must be a function.").ThrowAsJavaScriptException();
      return;
  }

  _pcRef = Napi::Persistent(info[0].As<Napi::Object>());
  _receiverRef = Napi::Persistent(info[1].As<Napi::Object>());
  _onpacket = Napi::Persistent(info[2].As<Napi::Function>());

  _sink = std::make_unique<RtpPacketSink>([this](const webrtc::RtpPacketReceived& packet) {
    auto* packet_data = new RtpPacketData();
    packet_data->length = packet.size();
    packet_data->data = std::unique_ptr<uint8_t[]>(new uint8_t[packet.size()]);
    memcpy(packet_data->data.get(), packet.data(), packet.size());
    packet_data->timestamp = packet.Timestamp();

    (new OnPacketWorker(_onpacket.Value(), packet_data))->Queue();
  });

  // ДОДАНО: Логування для відстеження моменту створення
  std::cout << "RtpPacketSinkWrapper: Constructor called. Attempting to get MediaChannel." << std::endl;

  // Отримуємо MediaChannel. Якщо щось піде не так, GetMediaChannel сам викине виняток.
  cricket::MediaChannel* channel = GetMediaChannel();

  if (channel) {
      std::cout << "RtpPacketSinkWrapper: MediaChannel found! Setting sink." << std::endl;
      channel->SetRawRtpPacketSink(_sink.get());
  } else {
      // ДОДАНО: Логування, якщо канал не знайдено, але помилки не було.
      // Це може бути нормальним, якщо SDP ще не узгоджено.
      std::cout << "RtpPacketSinkWrapper: MediaChannel not found yet (this might be normal if SDP is not settled)." << std::endl;
  }
}

RtpPacketSinkWrapper::~RtpPacketSinkWrapper() {
  _Stop();
}

void RtpPacketSinkWrapper::_Stop() {
  if (_sink) {
    if (auto* channel = GetMediaChannel()) {
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

cricket::MediaChannel* RtpPacketSinkWrapper::GetMediaChannel() {
  if (_pcRef.IsEmpty() || _receiverRef.IsEmpty()) {
    // Це не повинно трапитися, якщо конструктор відпрацював правильно.
    return nullptr;
  }

  // ЗМІНЕНО: Додано перевірки на кожному кроці з детальними повідомленнями про помилки.

  auto* pc_wrapper = node_webrtc::RTCPeerConnection::Unwrap(_pcRef.Value());
  if (!pc_wrapper) {
    Napi::Error::New(Env(), "GetMediaChannel Error: Failed to unwrap RTCPeerConnection. The object might be invalid or garbage collected.").ThrowAsJavaScriptException();
    return nullptr;
  }

  auto* receiver_wrapper = node_webrtc::RTCRtpReceiver::Unwrap(_receiverRef.Value());
  if (!receiver_wrapper) {
    Napi::Error::New(Env(), "GetMediaChannel Error: Failed to unwrap RTCRtpReceiver. The object might be invalid or garbage collected.").ThrowAsJavaScriptException();
    return nullptr;
  }

  webrtc::PeerConnectionInterface* pc_interface = pc_wrapper->pc();
  if (!pc_interface) {
    // Це може бути нормальною ситуацією, якщо setRemoteDescription ще не викликано.
    // Не викидаємо помилку, просто повертаємо nullptr.
    std::cout << "GetMediaChannel Info: pc_interface is null. This is expected before connection setup." << std::endl;
    return nullptr;
  }

  auto* pc_impl = static_cast<webrtc::PeerConnection*>(pc_interface);

  auto rtp_receiver = receiver_wrapper->receiver();
  if (!rtp_receiver) {
    Napi::Error::New(Env(), "GetMediaChannel Error: The internal webrtc::RtpReceiverInterface is null.").ThrowAsJavaScriptException();
    return nullptr;
  }

  // Знаходимо відповідний трансивер для нашого ресивера
  rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver;
  auto transceivers = pc_impl->GetTransceivers();
  for (const auto& t : transceivers) {
      if (t->receiver() == rtp_receiver) {
          transceiver = t;
          break;
      }
  }

  if (!transceiver) {
      Napi::Error::New(Env(), "GetMediaChannel Error: Failed to find a corresponding RTCRtpTransceiver for the given RTCRtpReceiver.").ThrowAsJavaScriptException();
      return nullptr;
  }

  // ЦЕ КЛЮЧОВА ПЕРЕВІРКА!
  if (!transceiver->mid()) {
      // `mid` (Media ID) призначається тільки після успішного обміну SDP.
      // Якщо його немає, значить, ми викликаємо конструктор занадто рано.
      Napi::Error::New(Env(), "GetMediaChannel Error: The corresponding RTCRtpTransceiver has no MID. This is the critical issue. It means the sink is created before the SDP negotiation is complete.").ThrowAsJavaScriptException();
      return nullptr;
  }

  auto transport_name = *transceiver->mid();
  std::cout << "GetMediaChannel Info: Found MID: " << transport_name << std::endl;

  auto* channel_manager = pc_impl->channel_manager();
  if (!channel_manager) {
    Napi::Error::New(Env(), "GetMediaChannel Error: Internal ChannelManager is missing.").ThrowAsJavaScriptException();
    return nullptr;
  }

  auto receiver_track = rtp_receiver->track();
  if (!receiver_track) {
    Napi::Error::New(Env(), "GetMediaChannel Error: The RtpReceiver has no track associated with it.").ThrowAsJavaScriptException();
    return nullptr;
  }

  std::string track_kind = receiver_track->kind();
  std::cout << "GetMediaChannel Info: Track kind is '" << track_kind << "'" << std::endl;

  if (track_kind == webrtc::MediaStreamTrackInterface::kAudioKind) {
    auto* voice_channel = channel_manager->GetVoiceChannel(transport_name);
    if (!voice_channel) {
        std::string error_msg = "GetMediaChannel Error: Failed to find a VoiceChannel for MID: " + transport_name;
        Napi::Error::New(Env(), error_msg).ThrowAsJavaScriptException();
        return nullptr;
    }
    return voice_channel->media_channel();
  } else if (track_kind == webrtc::MediaStreamTrackInterface::kVideoKind) {
    auto* video_channel = channel_manager->GetVideoChannel(transport_name);
    if (!video_channel) {
        std::string error_msg = "GetMediaChannel Error: Failed to find a VideoChannel for MID: " + transport_name;
        Napi::Error::New(Env(), error_msg).ThrowAsJavaScriptException();
        return nullptr;
    }
    return video_channel->media_channel();
  }

  std::cout << "GetMediaChannel Warning: Track kind is neither audio nor video." << std::endl;
  return nullptr;
}
