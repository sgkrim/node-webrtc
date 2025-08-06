#ifndef SRC_ENCODEDFRAMESINK_H_
#define SRC_ENCODEDFRAMESINK_H_

#include <functional>
#include <vector>
#include <iostream>

// Правильний шлях до файлу, який ви знайшли
#include "api/video_codecs/video_encoder.h"

class EncodedFrameSink : public webrtc::EncodedImageCallback {
 public:
  using OnEncodedFrameCallback = std::function<void(const std::vector<uint8_t>&, bool)>;

  explicit EncodedFrameSink(OnEncodedFrameCallback on_frame)
      : _on_frame(on_frame) {}

  ~EncodedFrameSink() override = default;

 private:
  // VVV ВИПРАВЛЕНО СИГНАТУРУ МЕТОДУ НАПРОТИЛЕЖНУ ПОПЕРЕДНІЙ VVV
  // Повертаємо Result, як того вимагає базовий клас у вашій версії libwebrtc
  Result OnEncodedImage(
      const webrtc::EncodedImage& encoded_image,
      const webrtc::CodecSpecificInfo* codec_specific_info,
      const webrtc::RTPFragmentationHeader* fragmentation) override { // Тепер override спрацює правильно

    // Прибираємо попередження компілятора про невикористані змінні
    (void)codec_specific_info;
    (void)fragmentation;

    std::vector<uint8_t> frame_data(encoded_image.data(), encoded_image.data() + encoded_image.size());
    bool is_key_frame = encoded_image._frameType == webrtc::VideoFrameType::kVideoFrameKey;

    std::cout << "[EncodedFrameSink] Received a complete frame! Size: " << frame_data.size()
              << ", isKeyFrame: " << is_key_frame << std::endl;

    if (_on_frame) {
      _on_frame(frame_data, is_key_frame);
    }

    // Повертаємо правильний тип
    return Result(Result::Error::OK);
  }

  OnEncodedFrameCallback _on_frame;
};

#endif  // SRC_ENCODEDFRAMESINK_H_
