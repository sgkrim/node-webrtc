#ifndef ENCODED_AUDIO_SINK_H_
#define ENCODED_AUDIO_SINK_H_

#include <functional>
#include <api/media_stream_interface.h>
#include <api/rtp_receiver_interface.h>
#include <api/frame_transformer_interface.h>
#include <rtc_base/synchronization/mutex.h>

class EncodedAudioSink : public webrtc::EncodedFrameSinkInterface {
public:
  using OnEncodedAudioFrameCallback = std::function<void(const uint8_t* data, size_t length)>;

  explicit EncodedAudioSink(OnEncodedAudioFrameCallback on_frame);
  ~EncodedAudioSink() override;

  void OnEncodedFrame(std::unique_ptr<webrtc::TransformableFrameInterface> frame) override;

private:
  webrtc::Mutex mutex_;
  OnEncodedAudioFrameCallback on_frame_ RTC_GUARDED_BY(mutex_);
};

#endif  // ENCODED_AUDIO_SINK_H_