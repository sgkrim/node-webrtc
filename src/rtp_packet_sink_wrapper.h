#pragma once

#include <node-addon-api/napi.h>
#include <memory>

// Попередні оголошення для уникнення циклічних залежностей
namespace cricket {
class MediaChannel;
}
class RtpPacketSink;

// ВИПРАВЛЕНО: Додано визначення структури, якої не вистачало
struct RtpPacketData {
  std::unique_ptr<uint8_t[]> data;
  size_t length;
  uint32_t timestamp;
};

class RtpPacketSinkWrapper : public Napi::ObjectWrap<RtpPacketSinkWrapper> {
 public:
  static void Init(Napi::Env env, Napi::Object exports);
  RtpPacketSinkWrapper(const Napi::CallbackInfo& info);
  ~RtpPacketSinkWrapper();

 private:
  static Napi::FunctionReference audio_constructor;
  static Napi::FunctionReference video_constructor;

  void Stop(const Napi::CallbackInfo&);
  void _Stop();
  cricket::MediaChannel* GetMediaChannel();

  Napi::ObjectReference _pcRef;
  Napi::ObjectReference _receiverRef;
  Napi::FunctionReference _onpacket;
  std::unique_ptr<RtpPacketSink> _sink;
};
