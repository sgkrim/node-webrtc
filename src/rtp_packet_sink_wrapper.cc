// 1. Включаємо заголовок нашого власного файлу.
#include "rtp_packet_sink_wrapper.h"

// 2. Включаємо заголовки обгорток з node-webrtc.
#include "interfaces/rtc_rtp_receiver.h"

// 3. Включаємо заголовок MediaChannel, оскільки ми працюємо з ним напряму
#include "media/base/media_channel.h"


class OnPacketWorker : public Napi::AsyncWorker {
 public:
  OnPacketWorker(const Napi::Function& callback, RtpPacketData* data)
    : Napi::AsyncWorker(callback), _data(data) {}

  ~OnPacketWorker() {}

  void Execute() override {}

  void OnOK() override {
    Napi::HandleScope scope(Env());
    Napi::Object packet_obj = Napi::Object::New(Env());
    packet_obj.Set("payload", Napi::Buffer<uint8_t>::New(
      Env(), _data->data.release(), _data->length,
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
  if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsFunction()) {
    Napi::TypeError::New(info.Env(), "Expected (rtpReceiver, callback)").ThrowAsJavaScriptException();
    return;
  }

  // Зберігаємо посилання на JS-об'єкт, щоб він не був видалений збирачем сміття
  _receiverWrapperRef = Napi::Persistent(info[0].As<Napi::Object>());

  auto* rtpReceiverWrapper = node_webrtc::RTCRtpReceiver::Unwrap(_receiverWrapperRef.Value());
  if (!rtpReceiverWrapper) {
    Napi::TypeError::New(info.Env(), "Failed to unwrap RTCRtpReceiver").ThrowAsJavaScriptException();
    return;
  }

  _onpacket.Reset(info[1].As<Napi::Function>());

  _sink = std::make_unique<RtpPacketSink>([this](const uint8_t* data, size_t length, uint32_t timestamp) {
    auto* packet_data = new RtpPacketData();
    packet_data->length = length;
    packet_data->data = std::unique_ptr<uint8_t[]>(new uint8_t[length]);
    memcpy(packet_data->data.get(), data, length);
    packet_data->timestamp = timestamp;
    (new OnPacketWorker(_onpacket.Value(), packet_data))->Queue();
  });

  // ВИКОРИСТОВУЄМО НАШ НОВИЙ МЕТОД
  cricket::MediaChannel* channel = rtpReceiverWrapper->media_channel();
  if (channel) {
    channel->SetRawRtpPacketSink(_sink.get());
  }
}

RtpPacketSinkWrapper::~RtpPacketSinkWrapper() {
  _Stop();
}

void RtpPacketSinkWrapper::Stop(const Napi::CallbackInfo& /* info */) {
  _Stop();
}

void RtpPacketSinkWrapper::_Stop() {
  // Перевіряємо, чи є валідне посилання на об'єкт
  if (!_receiverWrapperRef.IsEmpty()) {
    auto* rtpReceiverWrapper = node_webrtc::RTCRtpReceiver::Unwrap(_receiverWrapperRef.Value());
    if (rtpReceiverWrapper) {
        // ВИКОРИСТОВУЄМО НАШ НОВИЙ МЕТОД
        cricket::MediaChannel* channel = rtpReceiverWrapper->media_channel();
        if (channel) {
            channel->SetRawRtpPacketSink(nullptr);
        }
    }
    // Очищуємо посилання
    _receiverWrapperRef.Reset();
  }

  if (!_onpacket.IsEmpty()) {
    _onpacket.Reset();
  }
}
