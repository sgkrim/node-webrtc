#ifndef RTP_PACKET_SINK_WRAPPER_H_
#define RTP_PACKET_SINK_WRAPPER_H_

#include <memory>
#include <string>

#include <node-addon-api/napi.h>

// Попередні оголошення
namespace cricket {
class MediaChannel;
}

namespace webrtc {
class RtpPacketSinkInterface;
}

class RtpPacketSink;

// Структура для передачі даних між потоками
struct RtpPacketData {
  size_t length;
  uint32_t timestamp;
  std::unique_ptr<uint8_t[]> data;
};

class RtpPacketSinkWrapper : public Napi::ObjectWrap<RtpPacketSinkWrapper> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  RtpPacketSinkWrapper(const Napi::CallbackInfo& info);
  ~RtpPacketSinkWrapper();

 private:
  void Stop(const Napi::CallbackInfo&);
  void _Stop();
  cricket::MediaChannel* GetMediaChannel();

  static Napi::FunctionReference audio_constructor;
  static Napi::FunctionReference video_constructor;

  Napi::ObjectReference _pcRef;
  Napi::ObjectReference _receiverRef;

  // ВИПРАВЛЕНО: Використовуємо ThreadSafeFunction для безпеки потоків
  Napi::ThreadSafeFunction _onpacket;

  std::unique_ptr<RtpPacketSink> _sink;
};

#endif  // RTP_PACKET_SINK_WRAPPER_H_
