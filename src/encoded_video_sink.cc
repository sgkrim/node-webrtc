#include "encoded_video_sink.h"

#include <api/video/video_frame.h>
#include <rtc_base/logging.h>
#include <modules/video_coding/include/video_codec_interface.h>

EncodedVideoSink::EncodedVideoSink(OnEncodedVideoFrameCallback on_frame)
    : on_frame_(std::move(on_frame)) {
    RTC_LOG(LS_INFO) << "EncodedVideoSink created";
}

EncodedVideoSink::~EncodedVideoSink() {
    RTC_LOG(LS_INFO) << "EncodedVideoSink destroyed";
}

void EncodedVideoSink::OnEncodedFrame(std::unique_ptr<webrtc::TransformableFrameInterface> frame) {
    webrtc::MutexLock lock(&mutex_);
    if (on_frame_ && frame->GetData().size() > 0) {
        // Перетворюємо TransformableFrameInterface в EncodedImage
        auto encoded_image = webrtc::EncodedImage();
        encoded_image.SetEncodedData(webrtc::EncodedImageBuffer::Create(frame->GetData().data(), frame->GetData().size()));
        encoded_image.SetTimestamp(frame->GetTimestamp());
        encoded_image._frameType = frame->IsKeyFrame() ? webrtc::VideoFrameType::kVideoFrameKey : webrtc::VideoFrameType::kVideoFrameDelta;

        // Інформація про кодек та розміри, на жаль, не передається напряму.
        // Її потрібно отримувати з SDP або параметрів RtpReceiver.
        // Для прикладу, ми залишаємо їх нульовими.
        encoded_image._encodedWidth = 0;
        encoded_image._encodedHeight = 0;

        on_frame_(encoded_image);
    }
}