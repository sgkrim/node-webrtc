#ifndef ENCODED_VIDEO_SINK_WRAPPER_H_
#define ENCODED_VIDEO_SINK_WRAPPER_H_

#include <node-addon-api/napi.h>
#include "src/encoded_video_sink.h"
#include "src/rtp_receiver.h"

class EncodedVideoSinkWrapper : public Napi::ObjectWrap<EncodedVideoSinkWrapper> {
 public:
  static void Init(Napi::Env env, Napi::Object exports);
  EncodedVideoSinkWrapper(const Napi::CallbackInfo& info);
  ~EncodedVideoSinkWrapper() override;

 private:
  void Stop(const Napi::CallbackInfo& info);
  static Napi::FunctionReference constructor;

  rtc::scoped_refptr<webrtc::RtpReceiverInterface> _receiver;
  std::unique_ptr<EncodedVideoSink> _sink;
  Napi::ThreadSafeFunction _onencodedframe;
};

#endif  // ENCODED_VIDEO_SINK_WRAPPER_H_