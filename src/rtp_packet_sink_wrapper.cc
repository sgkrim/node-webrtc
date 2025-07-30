#include "rtp_packet_sink_wrapper.h"

#include "src/interfaces/rtc_peer_connection.h"
#include "src/interfaces/rtc_rtp_receiver.h"
#include "src/interfaces/media_stream_track.h"
#include "rtp_packet_sink.h"

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
  if (info.Length() < 3 || !info[0].IsObject() || !info[1].IsObject() || !info[2].IsFunction()) {
    Napi::TypeError::New(info.Env(), "Expected (peerConnection, rtpReceiver, callback)").ThrowAsJavaScriptException();
    return;
  }

  _pcRef = Napi::Persistent(info[0].As<Napi::Object>());
  _receiverRef = Napi::Persistent(info[1].As<Napi::Object>());
  _onpacket = Napi::Persistent(info[2].As<Napi::Function>());

  // Лямбда тепер відповідає оновленому RtpPacketSink
  _sink = std::make_unique<RtpPacketSink>([this](const webrtc::RtpPacketReceived& packet) {
    auto* packet_data = new RtpPacketData();
    packet_data->length = packet.size();
    packet_data->data = std::unique_ptr<uint8_t[]>(new uint8_t[packet.size()]);
    memcpy(packet_data->data.get(), packet.data(), packet.size());
    packet_data->timestamp = packet.Timestamp();

    // Створюємо та запускаємо AsyncWorker
    (new OnPacketWorker(_onpacket.Value(), packet_data))->Queue();
  });

  // ВИПРАВЛЕНО: GetMediaChannel тепер сам викидає помилку в разі невдачі.
  // Якщо він повернув `nullptr` без помилки, це означає, що ще рано
  // (наприклад, SDP не узгоджено), і ми просто чекаємо.
  if (auto* channel = GetMediaChannel()) {
    channel->SetRawRtpPacketSink(_sink.get());
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
    return nullptr;
  }

  auto* pc_wrapper = node_webrtc::RTCPeerConnection::Unwrap(_pcRef.Value());
  if (!pc_wrapper) {
    Napi::Error::New(Env(), "Internal error: RTCPeerConnection object is invalid.").ThrowAsJavaScriptException();
    return nullptr;
  }

  auto* receiver_wrapper = node_webrtc::RTCRtpReceiver::Unwrap(_receiverRef.Value());
  if (!receiver_wrapper) {
    Napi::Error::New(Env(), "Internal error: RTCRtpReceiver object is invalid.").ThrowAsJavaScriptException();
    return nullptr;
  }

  webrtc::PeerConnectionInterface* pc_interface = pc_wrapper->pc();
  if (!pc_interface) {
    // PeerConnection ще не створено, це може бути нормальною ситуацією на ранніх етапах.
    // Не викидаємо помилку, а просто повертаємо nullptr.
    return nullptr;
  }

  auto* pc_impl = static_cast<webrtc::PeerConnection*>(pc_interface);

  auto* channel_manager = pc_impl->channel_manager();
  if (!channel_manager) {
    Napi::Error::New(Env(), "Internal error: ChannelManager is missing.").ThrowAsJavaScriptException();
    return nullptr;
  }

  auto rtp_receiver = receiver_wrapper->receiver();
  if (!rtp_receiver) {
    Napi::Error::New(Env(), "Internal error: RtpReceiver is missing.").ThrowAsJavaScriptException();
    return nullptr;
  }

  auto transceivers = pc_impl->GetTransceivers();
  rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver;
  for (const auto& t : transceivers) {
      if (t->receiver() == rtp_receiver) {
          transceiver = t;
          break;
      }
  }

  if (!transceiver) {
      Napi::Error::New(Env(), "Failed to find a corresponding RTCRtpTransceiver for the given RTCRtpReceiver.").ThrowAsJavaScriptException();
      return nullptr;
  }

  if (!transceiver->mid()) {
      // Це найбільш вірогідна причина помилки.
      Napi::Error::New(Env(), "Failed to get MediaChannel: The corresponding RTCRtpTransceiver has no MID. This can happen if the sink is created before the SDP negotiation is complete.").ThrowAsJavaScriptException();
      return nullptr;
  }
  auto transport_name = *transceiver->mid();

  auto receiver_track = rtp_receiver->track();
  if (!receiver_track) {
    Napi::Error::New(Env(), "Internal error: RtpReceiver has no track.").ThrowAsJavaScriptException();
    return nullptr;
  }

  if (std::string(receiver_track->kind()) == webrtc::MediaStreamTrackInterface::kAudioKind) {
    auto* voice_channel = channel_manager->GetVoiceChannel(transport_name);
    if (!voice_channel) {
        Napi::Error::New(Env(), "Failed to find a VoiceChannel for the given track ID (MID).").ThrowAsJavaScriptException();
        return nullptr;
    }
    return voice_channel->media_channel();
  } else if (std::string(receiver_track->kind()) == webrtc::MediaStreamTrackInterface::kVideoKind) {
    auto* video_channel = channel_manager->GetVideoChannel(transport_name);
    if (!video_channel) {
        Napi::Error::New(Env(), "Failed to find a VideoChannel for the given track ID (MID).").ThrowAsJavaScriptException();
        return nullptr;
    }
    return video_channel->media_channel();
  }

  return nullptr;
}
