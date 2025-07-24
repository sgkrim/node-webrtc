#include "audio_opus_sink_wrapper.h"

#include "converters.h" // (якщо потрібні RTCTrack конвертори)
#include "src/common.h" // (наприклад, UnwrapTrack)

Napi::FunctionReference RTCAudioOpusSink::constructor;

Napi::Object RTCAudioOpusSink::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "RTCAudioOpusSink", {
    InstanceMethod("close", &RTCAudioOpusSink::Close)
  });

  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("RTCAudioOpusSink", func);
  return exports;
}

RTCAudioOpusSink::RTCAudioOpusSink(const Napi::CallbackInfo& info)
  : Napi::ObjectWrap<RTCAudioOpusSink>(info) {
  Napi::Env env = info.Env();
  if (!info[0].IsObject()) {
    Napi::TypeError::New(env, "Expected MediaStreamTrack as first argument").ThrowAsJavaScriptException();
    return;
  }

  Napi::Object trackObj = info[0].As<Napi::Object>();
  auto maybeTrack = wrtc::UnwrapTrack(trackObj); // <- ця функція має діставати rtc::scoped_refptr<MediaStreamTrackInterface>

  if (!maybeTrack) {
    Napi::TypeError::New(env, "Invalid MediaStreamTrack").ThrowAsJavaScriptException();
    return;
  }

  track_ = maybeTrack;

  tsfn_ = Napi::ThreadSafeFunction::New(
    env,
    info[1].As<Napi::Function>(), // JS callback
    "OnOpusData",
    0,
    1
  );

  sink_ = std::make_unique<AudioOpusSink>(track_, [this](const uint8_t* data, size_t length, uint32_t timestamp) {
    auto copied = std::make_shared<std::vector<uint8_t>>(data, data + length);
    tsfn_.BlockingCall(copied.get(), [timestamp](Napi::Env env, Napi::Function jsCallback, std::vector<uint8_t>* data) {
      Napi::Buffer<uint8_t> buf = Napi::Buffer<uint8_t>::Copy(env, data->data(), data->size());
      jsCallback.Call({ buf, Napi::Number::New(env, timestamp) });
    });
  });
}

RTCAudioOpusSink::~RTCAudioOpusSink() {
  if (sink_) {
    sink_.reset();
  }
  if (!tsfn_.IsEmpty()) {
    tsfn_.Release();
  }
}

void RTCAudioOpusSink::Close(const Napi::CallbackInfo& info) {
  if (sink_) {
    sink_.reset();
  }
  if (!tsfn_.IsEmpty()) {
    tsfn_.Release();
    tsfn_ = {};
  }
}