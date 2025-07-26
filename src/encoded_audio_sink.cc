#include "encoded_audio_sink.h"

#include <api/video/video_frame.h> // Потрібно для деяких типів
#include <rtc_base/logging.h>

EncodedAudioSink::EncodedAudioSink(OnEncodedAudioFrameCallback on_frame)
    : on_frame_(std::move(on_frame)) {
  RTC_LOG(LS_INFO) << "EncodedAudioSink created";
}

EncodedAudioSink::~EncodedAudioSink() {
  RTC_LOG(LS_INFO) << "EncodedAudioSink destroyed";
}

void EncodedAudioSink::OnEncodedFrame(std::unique_ptr<webrtc::TransformableFrameInterface> frame) {
  webrtc::MutexLock lock(&mutex_);
  if (on_frame_) {
    rtc::ArrayView<const uint8_t> payload = frame->GetData();
    on_frame_(payload.data(), payload.size());
  }
}