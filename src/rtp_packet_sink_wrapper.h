#ifndef SRC_RTCRTPPACKETSINKWRAPPER_H_
#define SRC_RTCRTPPACKETSINKWRAPPER_H_

#include "rtp_packet_sink.h"
#include <node-addon-api/napi.h>
#include <webrtc/api/rtp_receiver_interface.h>
#include <webrtc/api/peer_connection_interface.h>

// Структура для передачі даних в асинхронний воркер
struct RtpPacketData {
  std::unique_ptr<uint8_t[]> data;
  size_t length;
  uint32_t timestamp;
};

class RtpPacketSinkWrapper : public Napi::ObjectWrap<RtpPacketSinkWrapper> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  RtpPacketSinkWrapper(const Napi::CallbackInfo& info);
  ~RtpPacketSinkWrapper() override;

 private:
  static Napi::FunctionReference audio_constructor;
  static Napi::FunctionReference video_constructor;

  void _Stop();
  cricket::MediaChannel* GetMediaChannel();

  // Оголошення методів, доступних з JavaScript
  void Stop(const Napi::CallbackInfo& info);

  // ДОДАНО: Оголошення нового методу Start
  Napi::Value Start(const Napi::CallbackInfo& info);

  std::unique_ptr<RtpPacketSink> _sink;
  Napi::FunctionReference _onpacket;
  Napi::ObjectReference _pcRef;
  Napi::ObjectReference _receiverRef;
};

#endif  // SRC_RTCRTPPACKETSINKWRAPPER_H_
