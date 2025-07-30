#include "rtp_packet_sink_wrapper.h"

#include "src/interfaces/rtc_peer_connection.h"
#include "src/interfaces/rtc_rtp_receiver.h"
#include "src/interfaces/media_stream_track.h"

#include <iostream>

// ВИПРАВЛЕНО: Шляхи підключення без префіксу "webrtc/"
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
    InstanceMethod("stop", &RtpPacketSinkWrapper::Stop),
    InstanceMethod("start", &RtpPacketSinkWrapper::Start)
  });
  audio_constructor = Napi::Persistent(audio_func);
  audio_constructor.SuppressDestruct();

  Napi::Function video_func = DefineClass(env, "RTCRawVideoSink", {
    InstanceMethod("stop", &RtpPacketSinkWrapper::Stop),
    InstanceMethod("start", &RtpPacketSinkWrapper::Start)
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

  if (info.Length() != 3 || !info[0].IsObject() || !info[1].IsObject() || !info[2].IsFunction()) {
    Napi::TypeError::New(info.Env(), "RTCRawSink constructor expects 3 arguments: (peerConnection, rtpReceiver, callback)").ThrowAsJavaScriptException();
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

  std::cout << "RtpPacketSinkWrapper: Constructor finished successfully." << std::endl;
}

RtpPacketSinkWrapper::~RtpPacketSinkWrapper() {
  _Stop();
}

Napi::Value RtpPacketSinkWrapper::Start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  std::cout << "RtpPacketSinkWrapper: Start() called. Attempting to get MediaChannel." << std::endl;

  cricket::MediaChannel* channel = GetMediaChannel();

  if (channel) {
      std::cout << "RtpPacketSinkWrapper: MediaChannel found in Start()! Setting sink." << std::endl;
      channel->SetRawRtpPacketSink(_sink.get());
  } else {
      if (!env.IsExceptionPending()) {
        Napi::Error::New(env, "Failed to start sink: MediaChannel is not available and no specific error was thrown.").ThrowAsJavaScriptException();
      }
  }

  return env.Undefined();
}


void RtpPacketSinkWrapper::_Stop() {
  if (_sink) {
    if (!_pcRef.IsEmpty() && !_receiverRef.IsEmpty()) {
        try {
            if (auto* channel = GetMediaChannel()) {
                channel->SetRawRtpPacketSink(nullptr);
            }
        } catch (const Napi::Error& e) {
            std::cerr << "Caught an error in _Stop while trying to get MediaChannel: " << e.what() << std::endl;
        }
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
    Napi::Error::New(Env(), "GetMediaChannel Error: Failed to unwrap RTCPeerConnection.").ThrowAsJavaScriptException();
    return nullptr;
  }

  auto* receiver_wrapper = node_webrtc::RTCRtpReceiver::Unwrap(_receiverRef.Value());
  if (!receiver_wrapper) {
    Napi::Error::New(Env(), "GetMediaChannel Error: Failed to unwrap RTCRtpReceiver.").ThrowAsJavaScriptException();
    return nullptr;
  }

  webrtc::PeerConnectionInterface* pc_interface = pc_wrapper->pc();
  if (!pc_interface) {
    std::cout << "GetMediaChannel Info: pc_interface is null." << std::endl;
    Napi::Error::New(Env(), "GetMediaChannel Error: The internal PeerConnectionInterface is null.").ThrowAsJavaScriptException();
    return nullptr;
  }

  auto* pc_impl = static_cast<webrtc::PeerConnection*>(pc_interface);

  auto rtp_receiver = receiver_wrapper->receiver();
  if (!rtp_receiver) {
    Napi::Error::New(Env(), "GetMediaChannel Error: The internal RtpReceiverInterface is null.").ThrowAsJavaScriptException();
    return nullptr;
  }

  rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver;
  for (const auto& t : pc_impl->GetTransceivers()) {
      if (t->receiver() == rtp_receiver) {
          transceiver = t;
          break;
      }
  }

  if (!transceiver) {
      Napi::Error::New(Env(), "GetMediaChannel Error: Failed to find a corresponding RTCRtpTransceiver.").ThrowAsJavaScriptException();
      return nullptr;
  }

  if (!transceiver->mid()) {
      Napi::Error::New(Env(), "GetMediaChannel Error: The corresponding RTCRtpTransceiver has no MID (critical error).").ThrowAsJavaScriptException();
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
    Napi::Error::New(Env(), "GetMediaChannel Error: The RtpReceiver has no track.").ThrowAsJavaScriptException();
    return nullptr;
  }

  std::string track_kind = receiver_track->kind();
  if (track_kind == webrtc::MediaStreamTrackInterface::kAudioKind) {
    auto* voice_channel = channel_manager->GetVoiceChannel(transport_name);
    if (!voice_channel) {
        Napi::Error::New(Env(), "GetMediaChannel Error: Failed to find a VoiceChannel for the given MID.").ThrowAsJavaScriptException();
        return nullptr;
    }
    return voice_channel->media_channel();
  } else if (track_kind == webrtc::MediaStreamTrackInterface::kVideoKind) {
    auto* video_channel = channel_manager->GetVideoChannel(transport_name);
    if (!video_channel) {
        Napi::Error::New(Env(), "GetMediaChannel Error: Failed to find a VideoChannel for the given MID.").ThrowAsJavaScriptException();
        return nullptr;
    }
    return video_channel->media_channel();
  }

  return nullptr;
}
