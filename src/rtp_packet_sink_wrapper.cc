#include "src/rtp_packet_sink_wrapper.h"
#include <rtc_base/logging.h>

// Структура для безпечної передачі даних між потоками
struct RtpPacketData {
    std::unique_ptr<uint8_t[]> data;
    size_t length;
    uint32_t timestamp;
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
  if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsFunction()) {
    Napi::TypeError::New(info.Env(), "Expected (rtpReceiver, callback)").ThrowAsJavaScriptException();
    return;
  }

  auto rtpReceiverWrapper = node_webrtc::RTCRtpReceiver::Unwrap(info[0].As<Napi::Object>());
  _receiver = rtpReceiverWrapper->receiver();

  Napi::Function js_callback = info[1].As<Napi::Function>();
  _onpacket = Napi::ThreadSafeFunction::New(
      info.Env(), js_callback, "OnRtpPacket", 0, 1, [](Napi::Env) {});

  _sink = std::make_unique<RtpPacketSink>([this](const uint8_t* data, size_t length, uint32_t timestamp) {
    auto* packet_data = new RtpPacketData();
    packet_data->length = length;
    packet_data->data = std::unique_ptr<uint8_t[]>(new uint8_t[length]);
    memcpy(packet_data->data.get(), data, length);
    packet_data->timestamp = timestamp;

    _onpacket.BlockingCall(packet_data, [](Napi::Env env, Napi::Function jsCallback, RtpPacketData* data) {
        Napi::Object packet_obj = Napi::Object::New(env);
        // Створюємо Buffer, який сам звільнить пам'ять
        packet_obj.Set("payload", Napi::Buffer<uint8_t>::New(env, data->data.release(), data->length, [](Napi::Env, uint8_t* d){ delete[] d; }));
        packet_obj.Set("timestamp", Napi::Number::New(env, data->timestamp));
        jsCallback.Call({packet_obj});
        delete data;
    });
  });

  _receiver->SetSink(_sink.get());
}

RtpPacketSinkWrapper::~RtpPacketSinkWrapper() {
  Stop({});
}

void RtpPacketSinkWrapper::Stop(const Napi::CallbackInfo& info) {
    if (_receiver) {
        // У старій версії API, щоб відключити sink, потрібно передати nullptr
        _receiver->SetSink(nullptr);
        _receiver = nullptr;
    }
    if (_onpacket) {
      _onpacket.Release();
      _onpacket = nullptr;
    }
}