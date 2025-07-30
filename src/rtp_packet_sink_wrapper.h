#ifndef RTP_PACKET_SINK_WRAPPER_H_
#define RTP_PACKET_SINK_WRAPPER_H_

#include <memory>
#include <string>

#include <node-addon-api/napi.h>

#include "rtp_packet_sink.h"

namespace cricket {
class MediaChannel;
}

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
  // ЗМІНЕНО: Методи тепер приймають peerConnection як аргумент
  void Stop(const Napi::CallbackInfo&);
  Napi::Value Start(const Napi::CallbackInfo& info);

  // ЗМІНЕНО: GetMediaChannel тепер приймає peerConnection як аргумент
  void _Stop(Napi::Object peerConnection);
  cricket::MediaChannel* GetMediaChannel(Napi::Object peerConnection);

  static Napi::FunctionReference audio_constructor;
  static Napi::FunctionReference video_constructor;

  // ЗМІНЕНО: _pcRef видалено
  std::string _trackId;
  Napi::FunctionReference _onpacket;
  std::unique_ptr<RtpPacketSink> _sink;
};

#endif  // RTP_PACKET_SINK_WRAPPER_H_
