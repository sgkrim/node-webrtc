#ifndef ENCODED_VIDEO_SINK_H_
#define ENCODED_VIDEO_SINK_H_

#include <functional>
#include <api/media_stream_interface.h>
#include <api/rtp_receiver_interface.h>
#include <api/frame_transformer_interface.h>
#include <api/video/encoded_image.h>
#include <rtc_base/synchronization/mutex.h>

class EncodedVideoSink : public webrtc::EncodedFrameSinkInterface {
public:
    using OnEncodedVideoFrameCallback = std::function<void(const webrtc::EncodedImage& image)>;

    explicit EncodedVideoSink(OnEncodedVideoFrameCallback on_frame);
    ~EncodedVideoSink() override;

    void OnEncodedFrame(std::unique_ptr<webrtc::TransformableFrameInterface> frame) override;

private:
    webrtc::Mutex mutex_;
    OnEncodedVideoFrameCallback on_frame_ RTC_GUARDED_BY(mutex_);
};

#endif // ENCODED_VIDEO_SINK_H_