#include "rtp_packet_sink_wrapper.h"

#include "src/interfaces/rtc_peer_connection.h"
#include "src/interfaces/rtc_rtp_receiver.h"
#include "src/interfaces/media_stream_track.h"

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

// ЗМІНЕНО: Конструктор тепер приймає лише trackId та callback
RtpPacketSinkWrapper::RtpPacketSinkWrapper(const Napi::CallbackInfo& info)
  : Napi::ObjectWrap<RtpPacketSinkWrapper>(info) {

  if (info.Length() != 2 || !info[0].IsString() || !info[1].IsFunction()) {
    Napi::TypeError::New(info.Env(), "RTCRawSink constructor expects 2 arguments: (trackId, callback)").ThrowAsJavaScriptException();
    return;
  }

  _trackId = info[0].As<Napi::String>().Utf8Value();
  _onpacket = Napi::Persistent(info[1].As<Napi::Function>());

  _sink = std::make_unique<RtpPacketSink>([this](const webrtc::RtpPacketReceived& packet) {
    auto* packet_data = new RtpPacketData();
    packet_data->length = packet.size();
    packet_data->data = std::unique_ptr<uint8_t[]>(new uint8_t[packet.size()]);
    memcpy(packet_data->data.get(), packet.data(), packet.size());
    packet_data->timestamp = packet.Timestamp();
    (new OnPacketWorker(_onpacket.Value(), packet_data))->Queue();
  });

  std::cout << "RtpPacketSinkWrapper: Constructor for track ID " << _trackId << " finished successfully." << std::endl;
}

RtpPacketSinkWrapper::~RtpPacketSinkWrapper() {
  // Деструктор тепер не може викликати _Stop, бо не має доступу до peerConnection
}

// ЗМІНЕНО: Start тепер приймає peerConnection
Napi::Value RtpPacketSinkWrapper::Start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "Start() expects 1 argument: peerConnection").ThrowAsJavaScriptException();
    return env.Undefined();
  }
  Napi::Object pcObject = info[0].As<Napi::Object>();

  std::cout << "RtpPacketSinkWrapper: Start() called. Attempting to get MediaChannel." << std::endl;
  cricket::MediaChannel* channel = GetMediaChannel(pcObject);

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

// ЗМІНЕНО: _Stop тепер приймає peerConnection
void RtpPacketSinkWrapper::_Stop(Napi::Object peerConnection) {
  if (_sink) {
    if (!peerConnection.IsEmpty()) {
        try {
            if (auto* channel = GetMediaChannel(peerConnection)) {
                channel->SetRawRtpPacketSink(nullptr);
            }
        } catch (const Napi::Error& e) {
            // Ігноруємо помилки при зупинці
        }
    }
    _sink.reset();
  }
  if (!_onpacket.IsEmpty()) _onpacket.Reset();
}

// ЗМІНЕНО: Stop тепер приймає peerConnection
void RtpPacketSinkWrapper::Stop(const Napi::CallbackInfo& info) {
  if (info.Length() != 1 || !info[0].IsObject()) {
    Napi::TypeError::New(info.Env(), "Stop() expects 1 argument: peerConnection").ThrowAsJavaScriptException();
    return;
  }
  _Stop(info[0].As<Napi::Object>());
}

// ЗМІНЕНО: GetMediaChannel тепер приймає peerConnection
cricket::MediaChannel* RtpPacketSinkWrapper::GetMediaChannel(Napi::Object pcObject) {
  auto* pc_wrapper = Napi::ObjectWrap<node_webrtc::RTCPeerConnection>::Unwrap(pcObject);
  if (!pc_wrapper) {
    Napi::Error::New(Env(), "GetMediaChannel Error: Failed to unwrap RTCPeerConnection.").ThrowAsJavaScriptException();
    return nullptr;
  }

  webrtc::PeerConnectionInterface* pc_interface = pc_wrapper->pc();
  if (!pc_interface) {
    Napi::Error::New(Env(), "GetMediaChannel Error: The internal PeerConnectionInterface is null.").ThrowAsJavaScriptException();
    return nullptr;
  }

  auto* pc_impl = static_cast<webrtc::PeerConnection*>(pc_interface);

  rtc::scoped_refptr<webrtc::RtpTransceiverInterface> target_transceiver;
  for (const auto& transceiver : pc_impl->GetTransceivers()) {
    if (transceiver && transceiver->receiver() && transceiver->receiver()->track()) {
      if (transceiver->receiver()->track()->id() == _trackId) {
        target_transceiver = transceiver;
        break;
      }
    }
  }

  if (!target_transceiver) {
    std::string error_msg = "GetMediaChannel Error: Failed to find a transceiver for track ID: " + _trackId;
    Napi::Error::New(Env(), error_msg).ThrowAsJavaScriptException();
    return nullptr;
  }

  if (!target_transceiver->mid()) {
      Napi::Error::New(Env(), "GetMediaChannel Error: The corresponding RTCRtpTransceiver has no MID (critical error).").ThrowAsJavaScriptException();
      return nullptr;
  }

  auto transport_name = *target_transceiver->mid();
  auto* channel_manager = pc_impl->channel_manager();
  if (!channel_manager) {
    Napi::Error::New(Env(), "GetMediaChannel Error: Internal ChannelManager is missing.").ThrowAsJavaScriptException();
    return nullptr;
  }

  std::string track_kind = target_transceiver->receiver()->track()->kind();
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
