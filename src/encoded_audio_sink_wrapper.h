#ifndef ENCODED_AUDIO_SINK_WRAPPER_H_
#define ENCODED_AUDIO_SINK_WRAPPER_H_

#include <node-addon-api/napi.h>
#include "src/encoded_audio_sink.h"
#include "src/rtp_receiver.h" // Потрібен для розгортання JS об'єкта

class EncodedAudioSinkWrapper : public Napi::ObjectWrap<EncodedAudioSinkWrapper> {
 public:
  static void Init(Napi::Env env, Napi::Object exports);
  EncodedAudioSinkWrapper(const Napi::CallbackInfo& info);
  ~EncodedAudioSinkWrapper() override;

 private:
  void Stop(const Napi::CallbackInfo& info);
  static Napi::FunctionReference constructor;

  rtc::scoped_refptr<webrtc::RtpReceiverInterface> _receiver;
  std::unique_ptr<EncodedAudioSink> _sink;
  Napi::ThreadSafeFunction _onencodedframe;
};

#endif  // ENCODED_AUDIO_SINK_WRAPPER_H_