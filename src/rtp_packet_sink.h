#ifndef SRC_RTPSINK_H_
#define SRC_RTPSINK_H_

#include "call/rtp_packet_sink_interface.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include <functional>
#include <vector>

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
 public:
  // Колбек приймає лише один аргумент - буфер з даними
  using OnFrameCallback = std::function<void(const std::vector<uint8_t>&)>;

  // Конструктор приймає колбек і payload_type для фільтрації
  explicit RtpPacketSink(OnFrameCallback on_frame, uint8_t payload_type)
      : _on_frame(on_frame), _payload_type(payload_type) {}

  ~RtpPacketSink() override = default;

  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override {
    if (packet.PayloadType() != _payload_type) {
        return;
    }

    auto payload = packet.payload();
    std::vector<uint8_t> frame(payload.begin(), payload.end());
    if (_on_frame) {
      _on_frame(frame);
    }
  }

 private:
  OnFrameCallback _on_frame;
  uint8_t _payload_type;
};
#endif
