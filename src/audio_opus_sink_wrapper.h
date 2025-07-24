#ifndef NODE_WEBRTC_AUDIO_OPUS_SINK_WRAPPER_H_
#define NODE_WEBRTC_AUDIO_OPUS_SINK_WRAPPER_H_

#include "audio_opus_sink.h"

#include <napi.h>
#include <api/media_stream_interface.h>
#include <memory>
#include <uv.h>

class RTCAudioOpusSink : public Napi::ObjectWrap<RTCAudioOpusSink> {
public:
  static Napi::FunctionReference constructor;
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  RTCAudioOpusSink(const Napi::CallbackInfo& info);
  ~RTCAudioOpusSink();

private:
  void Close(const Napi::CallbackInfo& info);

  rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track_;
  std::unique_ptr<AudioOpusSink> sink_;

  Napi::ThreadSafeFunction tsfn_;
};

#endif