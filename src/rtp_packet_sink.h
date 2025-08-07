#ifndef SRC_RTPSINK_H_
#define SRC_RTPSINK_H_

#include "call/rtp_packet_sink_interface.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include <functional>
#include <vector>

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
 public:
  using OnFrameCallback = std::function<void(const std::vector<uint8_t>&)>;

  explicit RtpPacketSink(OnFrameCallback on_frame) : _on_frame(on_frame) {}
  ~RtpPacketSink() override = default;

  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override {

    if((int)packet.PayloadType() != 111){
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
};
#endif
