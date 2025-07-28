#ifndef RTP_PACKET_SINK_WRAPPER_H_
#define RTP_PACKET_SINK_WRAPPER_H_

#include <memory>

#include <node-addon-api/napi.h>
#include "api/rtp_receiver_interface.h"

// Попередні оголошення, щоб не включати зайві заголовки
namespace cricket {
class MediaChannel;
}

namespace webrtc {
class RtpPacketSinkInterface;
}

// Структура для передачі даних між потоками
struct RtpPacketData {
  std::unique_ptr<uint8_t[]> data;
  size_t length;
  uint32_t timestamp;
};

class RtpPacketSinkWrapper : public Napi::ObjectWrap<RtpPacketSinkWrapper> {
 public:
  static void Init(Napi::Env env, Napi::Object exports);
  RtpPacketSinkWrapper(const Napi::CallbackInfo& info);
  ~RtpPacketSinkWrapper() override;

 private:
  void Stop(const Napi::CallbackInfo&);
  void _Stop();
  cricket::MediaChannel* GetMediaChannel();

  static Napi::FunctionReference audio_constructor;
  static Napi::FunctionReference video_constructor;

  Napi::ObjectReference _pcRef;
  Napi::ObjectReference _receiverRef;
  Napi::FunctionReference _onpacket;

  std::unique_ptr<webrtc::RtpPacketSinkInterface> _sink;
};

#endif  // RTP_PACKET_SINK_WRAPPER_H_
