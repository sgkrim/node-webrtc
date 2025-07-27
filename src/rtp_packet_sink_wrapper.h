/*
 * rtp_packet_sink_wrapper.h
 */
#pragma once

#include <memory>

#include <node-addon-api/napi.h>

// Попередні оголошення, щоб уникнути циклічних залежностей
namespace cricket {
class MediaChannel;
}

namespace webrtc {
class RtpPacketSinkInterface; // <--- ДОДАНО: Попереднє оголошення
}

// Структура для передачі даних між потоками
struct RtpPacketData {
  std::unique_ptr<uint8_t[]> data;
  size_t length;
  uint32_t timestamp;
};

// Наш головний клас-обгортка
class RtpPacketSinkWrapper
  : public Napi::ObjectWrap<RtpPacketSinkWrapper> {
 public:
  static void Init(Napi::Env env, Napi::Object exports);
  explicit RtpPacketSinkWrapper(const Napi::CallbackInfo& info);
  ~RtpPacketSinkWrapper();

 private:
  static Napi::FunctionReference audio_constructor;
  static Napi::FunctionReference video_constructor;

  void _Stop();
  void Stop(const Napi::CallbackInfo&);

  cricket::MediaChannel* GetMediaChannel();

  Napi::ObjectReference _pcRef;
  Napi::ObjectReference _receiverRef;
  Napi::FunctionReference _onpacket;
  std::unique_ptr<webrtc::RtpPacketSinkInterface> _sink;
};
