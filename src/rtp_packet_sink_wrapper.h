#ifndef RTP_PACKET_SINK_WRAPPER_H_
#define RTP_PACKET_SINK_WRAPPER_H_

#include <node-addon-api/napi.h>
#include "src/rtp_packet_sink.h"
#include "src/interfaces/rtc_rtp_receiver.h" // потрібно для JS

class RtpPacketSinkWrapper : public Napi::ObjectWrap<RtpPacketSinkWrapper> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  RtpPacketSinkWrapper(const Napi::CallbackInfo& info);
  ~RtpPacketSinkWrapper() override;

 private:
  void Stop(const Napi::CallbackInfo& info);
  static Napi::FunctionReference audio_constructor;
  static Napi::FunctionReference video_constructor;

  rtc::scoped_refptr<webrtc::RtpReceiverInterface> _receiver;
  std::unique_ptr<RtpPacketSink> _sink;
  Napi::ThreadSafeFunction _onpacket;
};

#endif  // RTP_PACKET_SINK_WRAPPER_H_