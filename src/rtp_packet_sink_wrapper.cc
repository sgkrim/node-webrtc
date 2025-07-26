#include "rtp_packet_sink_wrapper.h"

// --- Необхідні заголовки ---
// Для доступу до конкретної реалізації webrtc::RtpReceiver
#include "pc/rtp_receiver.h"
// Для доступу до обгортки RTCRtpReceiver з node-webrtc
#include "rtp_receiver.h"
// Для доступу до інтерфейсу MediaChannel та методу SetRawRtpPacketSink
#include "media/base/media_channel.h"


/**
 * @brief Асинхронний воркер для безпечної передачі даних з потоку WebRTC
 * в головний потік Node.js (libuv).
 */
class OnPacketWorker : public Napi::AsyncWorker {
 public:
  // Конструктор приймає callback-функцію та вказівник на дані пакета
  OnPacketWorker(const Napi::Function& callback, RtpPacketData* data)
    : Napi::AsyncWorker(callback), _data(data) {}

  ~OnPacketWorker() {}

  // Цей метод виконується в окремому потоці.
  // В нашому випадку вся робота з копіювання даних вже виконана,
  // тому тут нічого робити не потрібно.
  void Execute() override {}

  // Цей метод виконується в головному потоці Node.js після завершення Execute().
  void OnOK() override {
    Napi::HandleScope scope(Env());
    Napi::Object packet_obj = Napi::Object::New(Env());

    // Створюємо Napi::Buffer, який володіє даними.
    // Передаємо кастомний фіналізатор, щоб пам'ять коректно звільнилася (delete[]).
    packet_obj.Set("payload", Napi::Buffer<uint8_t>::New(
      Env(),
      _data->data.release(),
      _data->length,
      [](Napi::Env, uint8_t* d) { delete[] d; }
    ));
    packet_obj.Set("timestamp", Napi::Number::New(Env(), _data->timestamp));

    // Викликаємо JS-callback з об'єктом пакета
    Callback().Call({packet_obj});

    // Звільняємо пам'ять, виділену для структури RtpPacketData
    delete _data;
  }

 private:
  RtpPacketData* _data;
};

// Статичні члени для зберігання конструкторів JS-класів
Napi::FunctionReference RtpPacketSinkWrapper::audio_constructor;
Napi::FunctionReference RtpPacketSinkWrapper::video_constructor;

Napi::Object RtpPacketSinkWrapper::Init(Napi::Env env, Napi::Object exports) {
  // Визначаємо JS-клас для аудіо-сінка
  Napi::Function audio_func = DefineClass(env, "RTCRawAudioSink", {
    InstanceMethod("stop", &RtpPacketSinkWrapper::Stop)
  });
  audio_constructor = Napi::Persistent(audio_func);
  audio_constructor.SuppressDestruct();

  // Визначаємо JS-клас для відео-сінка
  Napi::Function video_func = DefineClass(env, "RTCRawVideoSink", {
    InstanceMethod("stop", &RtpPacketSinkWrapper::Stop)
  });
  video_constructor = Napi::Persistent(video_func);
  video_constructor.SuppressDestruct();

  // Створюємо об'єкт nonstandard та експортуємо класи
  Napi::Object nonstandard = Napi::Object::New(env);
  nonstandard.Set("RTCRawAudioSink", audio_func);
  nonstandard.Set("RTCRawVideoSink", video_func);
  exports.Set("nonstandard", nonstandard);

  return exports;
}

RtpPacketSinkWrapper::RtpPacketSinkWrapper(const Napi::CallbackInfo& info)
  : Napi::ObjectWrap<RtpPacketSinkWrapper>(info) {
  // Перевірка аргументів конструктора
  if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsFunction()) {
    Napi::TypeError::New(info.Env(), "Expected (rtpReceiver, callback)").ThrowAsJavaScriptException();
    return;
  }

  // Отримуємо JS-обгортку RTCRtpReceiver
  auto* rtpReceiverWrapper = node_webrtc::RTCRtpReceiver::Unwrap(info[0].As<Napi::Object>());
  if (!rtpReceiverWrapper) {
    Napi::TypeError::New(info.Env(), "Failed to unwrap RTCRtpReceiver").ThrowAsJavaScriptException();
    return;
  }

  // Зберігаємо розумний вказівник на внутрішній об'єкт WebRTC
  _receiver = rtpReceiverWrapper->receiver();

  // Зберігаємо JS-callback для виклику при отриманні пакетів
  _onpacket.Reset(info[1].As<Napi::Function>());

  // Створюємо наш кастомний sink
  _sink = std::make_unique<RtpPacketSink>([this](const uint8_t* data, size_t length, uint32_t timestamp) {
    // Створюємо структуру для передачі даних в інший потік
    auto* packet_data = new RtpPacketData();
    packet_data->length = length;
    // Копіюємо дані пакета
    packet_data->data = std::unique_ptr<uint8_t[]>(new uint8_t[length]);
    memcpy(packet_data->data.get(), data, length);
    packet_data->timestamp = timestamp;
    // Створюємо та запускаємо асинхронний воркер
    (new OnPacketWorker(_onpacket.Value(), packet_data))->Queue();
  });

  // --- КЛЮЧОВЕ ВИПРАВЛЕННЯ ---
  // Встановлюємо наш sink у media_channel
  if (_receiver) {
    // 1. Отримуємо "сирий" вказівник на інтерфейс з розумного вказівника
    webrtc::RtpReceiverInterface* interface_ptr = _receiver.get();
    // 2. Виконуємо static_cast до конкретного класу реалізації
    auto* internal_impl = static_cast<webrtc::RtpReceiver*>(interface_ptr);
    // 3. Перевіряємо, чи все гаразд, і встановлюємо sink
    if (internal_impl && internal_impl->media_channel()) {
      internal_impl->media_channel()->SetRawRtpPacketSink(_sink.get());
    }
  }
}

RtpPacketSinkWrapper::~RtpPacketSinkWrapper() {
  // Переконуємось, що все зупинено та очищено при знищенні об'єкта
  _Stop();
}

void RtpPacketSinkWrapper::Stop(const Napi::CallbackInfo& /* info */) {
  // ВИПРАВЛЕННЯ: Щоб уникнути попередження про невикористаний параметр,
  // ми просто коментуємо його ім'я.
  _Stop();
}

void RtpPacketSinkWrapper::_Stop() {
  // Перевіряємо, чи потрібно щось зупиняти
  if (_receiver) {
    // --- КЛЮЧОВЕ ВИПРАВЛЕННЯ (аналогічно до конструктора) ---
    // Видаляємо наш sink з media_channel, передаючи nullptr
    webrtc::RtpReceiverInterface* interface_ptr = _receiver.get();
    auto* internal_impl = static_cast<webrtc::RtpReceiver*>(interface_ptr);
    if (internal_impl && internal_impl->media_channel()) {
      internal_impl->media_channel()->SetRawRtpPacketSink(nullptr);
    }
    // Звільняємо наш розумний вказівник
    _receiver = nullptr;
  }

  // Очищуємо посилання на JS-callback, щоб уникнути витоку пам'яті
  if (!_onpacket.IsEmpty()) {
    _onpacket.Reset();
  }
}