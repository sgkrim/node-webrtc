#ifndef RTP_PACKET_SINK_WRAPPER_H_
#define RTP_PACKET_SINK_WRAPPER_H_

#include <node-addon-api/napi.h>
#include "rtp_packet_sink.h"
#include "interfaces/rtc_rtp_receiver.h" // Правильний шлях до файлу

// Структура для безпечної передачі даних між потоками
struct RtpPacketData {
    std::unique_ptr<uint8_t[]> data;
    size_t length;
    uint32_t timestamp;
};

class RtpPacketSinkWrapper : public Napi::ObjectWrap<RtpPacketSinkWrapper> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  RtpPacketSinkWrapper(const Napi::CallbackInfo& info);
  // Прибрано 'override' для сумісності зі старою N-API
  ~RtpPacketSinkWrapper();

 private:
  void Stop(const Napi::CallbackInfo& info);
  // ВИПРАВЛЕНО: Приватний метод для логіки зупинки
  void _Stop();
  static Napi::FunctionReference audio_constructor;
  static Napi::FunctionReference video_constructor;

  rtc::scoped_refptr<webrtc::RtpReceiverInterface> _receiver;
  std::unique_ptr<RtpPacketSink> _sink;
  // Використовуємо FunctionReference замість ThreadSafeFunction
  Napi::FunctionReference _onpacket;
};

#endif  // RTP_PACKET_SINK_WRAPPER_H_