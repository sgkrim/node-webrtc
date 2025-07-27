#pragma once

#include <napi.h>
#include <memory>
#include "src/rtp_packet_sink.h"

// Попереднє оголошення, щоб не включати сюди важкі заголовки
namespace cricket {
class MediaChannel;
}

class RtpPacketSinkWrapper : public Napi::ObjectWrap<RtpPacketSinkWrapper> {
 public:
  static void Init(Napi::Env env, Napi::Object exports);
  RtpPacketSinkWrapper(const Napi::CallbackInfo& info);
  ~RtpPacketSinkWrapper() override;

 private:
  static Napi::FunctionReference audio_constructor;
  static Napi::FunctionReference video_constructor;

  void Stop(const Napi::CallbackInfo&);
  void _Stop();
  cricket::MediaChannel* GetMediaChannel();

  // Зберігаємо посилання на JS-об'єкти, щоб отримати їх пізніше
  Napi::ObjectReference _pcRef;
  Napi::ObjectReference _receiverRef;

  // Callback для пакетів
  Napi::FunctionReference _onpacket;
  std::unique_ptr<RtpPacketSink> _sink;
};