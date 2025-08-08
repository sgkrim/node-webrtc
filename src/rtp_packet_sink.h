#ifndef SRC_RTPSINK_H_
#define SRC_RTPSINK_H_

// ДОДАНО: Необхідні заголовки для std::function та std::vector
#include <functional>
#include <vector>

#include "call/rtp_packet_sink_interface.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include <cstdint> // Гарна практика - додавати для uint32_t, uint8_t

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
 public:
  // Колбек приймає і кадр, і його RTP timestamp
  using OnFrameCallback = std::function<void(const std::vector<uint8_t>&, uint32_t)>;

  explicit RtpPacketSink(OnFrameCallback on_frame, uint8_t payload_type)
      : _on_frame(on_frame), _payload_type(payload_type) {}

  ~RtpPacketSink() override = default;

  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override {
    if (packet.PayloadType() != _payload_type) {
        return;
    }

    // Просто беремо payload і timestamp з поточного пакета
    auto payload = packet.payload();
    std::vector<uint8_t> frame(payload.begin(), payload.end());
    uint32_t rtp_timestamp = packet.Timestamp();

    if (_on_frame) {
      // І передаємо їх далі
      _on_frame(frame, rtp_timestamp);
    }
  }

 private:
  OnFrameCallback _on_frame;
  uint8_t _payload_type;
};
#endif
