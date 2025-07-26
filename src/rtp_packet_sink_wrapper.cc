#include "rtp_packet_sink_wrapper.h"
#include <rtc_base/logging.h>
// Додано повний опис RtpReceiverInterface
#include "api/rtp_receiver_interface.h"
// Додано опис MediaChannel
#include "media/base/media_channel.h"
// Додано опис конкретного класу RtpReceiverProxy
#include "pc/rtp_receiver.h"


// Використовуємо AsyncWorker для асинхронних викликів
class OnPacketWorker : public Napi::AsyncWorker {
 public:
  // Приймаємо callback за константним посиланням
  OnPacketWorker(const Napi::Function& callback, RtpPacketData* data)
    : Napi::AsyncWorker(callback), _data(data) {}

  ~OnPacketWorker() {}

  void Execute() override {
    // Робота виконується в іншому потоці, але в нашому випадку
    // дані вже скопійовані, тому тут нічого робити не потрібно.
  }

  void OnOK() override {
    Napi::HandleScope scope(Env());
    Napi::Object packet_obj = Napi::Object::New(Env());

    // Створюємо Buffer, який сам звільнить пам'ять
    packet_obj.Set("payload", Napi::Buffer<uint8_t>::New(Env(), _data->data.release(), _data->length, [](Napi::Env, uint8_t* d){ delete[] d; }));
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

  auto rtpReceiverWrapper = node_webrtc::RTCRtpReceiver::Unwrap(info[0].As<Napi::Object>());
  // Використовуємо новий публічний метод, який ми додали
  _receiver = rtpReceiverWrapper->receiver();

  Napi::Function js_callback = info[1].As<Napi::Function>();
  _onpacket = Napi::Persistent(js_callback);

  _sink = std::make_unique<RtpPacketSink>([this](const uint8_t* data, size_t length, uint32_t timestamp) {
    auto* packet_data = new RtpPacketData();
    packet_data->length = length;
    packet_data->data = std::unique_ptr<uint8_t[]>(new uint8_t[length]);
    memcpy(packet_data->data.get(), data, length);
    packet_data->timestamp = timestamp;

    (new OnPacketWorker(_onpacket.Value(), packet_data))->Queue();
  });

  // ВИПРАВЛЕНО: Використовуємо правильний ланцюжок викликів
  auto* receiver_proxy = static_cast<webrtc::RtpReceiverProxyWithInternal<webrtc::RtpReceiverInterface>*>(_receiver.get());
  if (receiver_proxy && receiver_proxy->internal() && receiver_proxy->internal()->media_channel()) {
    receiver_proxy->internal()->media_channel()->SetRawRtpPacketSink(_sink.get());
  }
}

RtpPacketSinkWrapper::~RtpPacketSinkWrapper() {
  _Stop();
}

void RtpPacketSinkWrapper::Stop(const Napi::CallbackInfo& info) {
    _Stop();
}

// Вся логіка винесена в приватний метод
void RtpPacketSinkWrapper::_Stop() {
    if (_receiver) {
      // ВИПРАВЛЕНО: Використовуємо правильний ланцюжок викликів
      auto* receiver_proxy = static_cast<webrtc::RtpReceiverProxyWithInternal<webrtc::RtpReceiverInterface>*>(_receiver.get());
      if (receiver_proxy && receiver_proxy->internal() && receiver_proxy->internal()->media_channel()) {
        receiver_proxy->internal()->media_channel()->SetRawRtpPacketSink(nullptr);
      }
      _receiver = nullptr;
    }
    if (!_onpacket.IsEmpty()) {
      _onpacket.Reset();
    }
}