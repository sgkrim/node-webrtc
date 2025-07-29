#ifndef RTP_PACKET_SINK_H_
#define RTP_PACKET_SINK_H_

#include <functional>

#include "call/rtp_packet_sink_interface.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
public:
  // Callback тепер приймає RtpPacketReceived, щоб уникнути зайвого копіювання
  using OnRtpPacketCallback = std::function<void(const webrtc::RtpPacketReceived&)>;

  explicit RtpPacketSink(OnRtpPacketCallback on_packet);
  ~RtpPacketSink() override;

  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override;

private:
  // Більше ніяких м'ютексів
  OnRtpPacketCallback _on_packet;
};

#endif  // RTP_PACKET_SINK_H_
