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

// Ця функція буде викликана в потоці Node.js для безпечної передачі даних
static void CallJs(Napi::Env env, Napi::Function js_callback, RtpPacketData* data) {
  if (env == nullptr || js_callback == nullptr || data == nullptr) {
    delete data;
    return;
  }
  Napi::HandleScope scope(env);
  Napi::Object packet_obj = Napi::Object::New(env);
  packet_obj.Set("payload", Napi::Buffer<uint8_t>::New(
    env,
    data->data.release(),
    data->length,
    [](Napi::Env, uint8_t* d) { delete[] d; }
  ));
  packet_obj.Set("timestamp", Napi::Number::New(env, data->timestamp));
  js_callback.Call({packet_obj});
  delete data;
}

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

  auto js_callback = info[2].As<Napi::Function>();
  _onpacket = Napi::ThreadSafeFunction::New(
      info.Env(),
      js_callback,
      "RtpPacketSinkCallback",
      0,
      1
  );

  // ВИПРАВЛЕНО: Лямбда тепер відповідає оновленому RtpPacketSink
  _sink = std::make_unique<RtpPacketSink>([this](const webrtc::RtpPacketReceived& packet) {
    auto* packet_data = new RtpPacketData();
    packet_data->length = packet.size();
    packet_data->data = std::unique_ptr<uint8_t[]>(new uint8_t[packet.size()]);
    memcpy(packet_data->data.get(), packet.data(), packet.size());
    packet_data->timestamp = packet.Timestamp();

    // Безпечно передаємо дані в потік Node.js через ThreadSafeFunction
    napi_status status = _onpacket.BlockingCall(packet_data, CallJs);
    if (status != napi_ok) {
        delete packet_data;
    }
  });

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
  if (_onpacket) {
    _onpacket.Release();
    _onpacket = nullptr;
  }
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
  auto* receiver_wrapper = node_webrtc::RTCRtpReceiver::Unwrap(_receiverRef.Value());

  if (!pc_wrapper || !receiver_wrapper) {
    return nullptr;
  }

  webrtc::PeerConnectionInterface* pc_interface = pc_wrapper->pc();
  if (!pc_interface) {
    return nullptr;
  }

  auto* pc_impl = static_cast<webrtc::PeerConnection*>(pc_interface);

  auto* channel_manager = pc_impl->channel_manager();
  if (!channel_manager) {
    return nullptr;
  }

  auto rtp_receiver = receiver_wrapper->receiver();
  if (!rtp_receiver) {
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

  if (!transceiver || !transceiver->mid()) {
      return nullptr;
  }
  auto transport_name = *transceiver->mid();

  auto receiver_track = rtp_receiver->track();
  if (!receiver_track) {
    return nullptr;
  }

  if (std::string(receiver_track->kind()) == webrtc::MediaStreamTrackInterface::kAudioKind) {
    auto* voice_channel = channel_manager->GetVoiceChannel(transport_name);
    return voice_channel ? voice_channel->media_channel() : nullptr;
  } else if (std::string(receiver_track->kind()) == webrtc::MediaStreamTrackInterface::kVideoKind) {
    auto* video_channel = channel_manager->GetVideoChannel(transport_name);
    return video_channel ? video_channel->media_channel() : nullptr;
  }

  return nullptr;
}
