#include "encoded_audio_sink_wrapper.h"
#include <rtc_base/logging.h>

Napi::FunctionReference EncodedAudioSinkWrapper::constructor;

void EncodedAudioSinkWrapper::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "EncodedAudioSink", {
    InstanceMethod("stop", &EncodedAudioSinkWrapper::Stop)
  });
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("RTCAudioOpusSink", func); // Експортуємо під бажаною назвою
}

EncodedAudioSinkWrapper::EncodedAudioSinkWrapper(const Napi::CallbackInfo& info)
  : Napi::ObjectWrap<EncodedAudioSinkWrapper>(info) {
  if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsFunction()) {
    Napi::TypeError::New(info.Env(), "Expected (rtpReceiver, callback)").ThrowAsJavaScriptException();
    return;
  }

  auto rtpReceiverWrapper = node_webrtc::RTCRtpReceiver::Unwrap(info[0].As<Napi::Object>());
  _receiver = rtpReceiverWrapper->receiver();

  Napi::Function js_callback = info[1].As<Napi::Function>();
  _onencodedframe = Napi::ThreadSafeFunction::New(
      info.Env(), js_callback, "OnEncodedAudioFrame", 0, 1, [](Napi::Env) {});

  _sink = std::make_unique<EncodedAudioSink>([this](const uint8_t* data, size_t length) {
    auto* copied_data = new std::pair<uint8_t*, size_t>(new uint8_t[length], length);
    memcpy(copied_data->first, data, length);
    _onencodedframe.BlockingCall(copied_data, [](Napi::Env env, Napi::Function jsCallback, std::pair<uint8_t*, size_t>* data) {
        jsCallback.Call({Napi::Buffer<uint8_t>::New(env, data->first, data->second, [](Napi::Env, uint8_t* d) { delete[] d; })});
        delete data;
    });
  });

  _receiver->SetEncodedFrameSink(_sink.get());
}

EncodedAudioSinkWrapper::~EncodedAudioSinkWrapper() {
  Stop({});
}

void EncodedAudioSinkWrapper::Stop(const Napi::CallbackInfo& info) {
    if (_receiver) {
        _receiver->SetEncodedFrameSink(nullptr);
        _receiver = nullptr;
    }
    if (_onencodedframe) {
      _onencodedframe.Release();
      _onencodedframe = nullptr;
    }
}