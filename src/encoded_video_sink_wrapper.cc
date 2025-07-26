#include "encoded_video_sink_wrapper.h"
#include <rtc_base/logging.h>

// Структура для безпечної передачі даних кадру між потоками
struct EncodedVideoFrameData {
    std::unique_ptr<uint8_t[]> data;
    size_t length;
    uint32_t timestamp;
    bool is_key_frame;
};

Napi::FunctionReference EncodedVideoSinkWrapper::constructor;

void EncodedVideoSinkWrapper::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "EncodedVideoSink", {
    InstanceMethod("stop", &EncodedVideoSinkWrapper::Stop)
  });
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("RTCVideoWebMSink", func); // Експортуємо під бажаною назвою
}

EncodedVideoSinkWrapper::EncodedVideoSinkWrapper(const Napi::CallbackInfo& info)
  : Napi::ObjectWrap<EncodedVideoSinkWrapper>(info) {
  if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsFunction()) {
    Napi::TypeError::New(info.Env(), "Expected (rtpReceiver, callback)").ThrowAsJavaScriptException();
    return;
  }

  auto rtpReceiverWrapper = node_webrtc::RTCRtpReceiver::Unwrap(info[0].As<Napi::Object>());
  _receiver = rtpReceiverWrapper->receiver();

  Napi::Function js_callback = info[1].As<Napi::Function>();
  _onencodedframe = Napi::ThreadSafeFunction::New(
      info.Env(), js_callback, "OnEncodedVideoFrame", 0, 1, [](Napi::Env) {});

  _sink = std::make_unique<EncodedVideoSink>([this](const webrtc::EncodedImage& image) {
    auto* frame_data = new EncodedVideoFrameData();
    frame_data->length = image.size();
    frame_data->data = std::unique_ptr<uint8_t[]>(new uint8_t[image.size()]);
    memcpy(frame_data->data.get(), image.data(), image.size());
    frame_data->timestamp = image.Timestamp();
    frame_data->is_key_frame = image._frameType == webrtc::VideoFrameType::kVideoFrameKey;

    _onencodedframe.BlockingCall(frame_data, [](Napi::Env env, Napi::Function jsCallback, EncodedVideoFrameData* data) {
        Napi::Object frame_obj = Napi::Object::New(env);
        frame_obj.Set("data", Napi::Buffer<uint8_t>::New(env, data->data.get(), data->length, [](Napi::Env, uint8_t* d){ delete[] d; }));
        frame_obj.Set("timestamp", Napi::Number::New(env, data->timestamp));
        frame_obj.Set("isKeyFrame", Napi::Boolean::New(env, data->is_key_frame));
        jsCallback.Call({frame_obj});
        delete data;
    });
  });

  _receiver->SetEncodedFrameSink(_sink.get());
}

EncodedVideoSinkWrapper::~EncodedVideoSinkWrapper() {
  Stop({});
}

void EncodedVideoSinkWrapper::Stop(const Napi::CallbackInfo& info) {
    if (_receiver) {
        _receiver->SetEncodedFrameSink(nullptr);
        _receiver = nullptr;
    }
    if (_onencodedframe) {
      _onencodedframe.Release();
      _onencodedframe = nullptr;
    }
}