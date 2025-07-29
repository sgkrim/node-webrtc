#ifndef RTP_PACKET_SINK_H_
#define RTP_PACKET_SINK_H_

#include <functional>
//#include <mutex>
#include "rtc_base/critical_section.h"
// Шлях до rtp_packet_sink_interface.h для версії M81
#include "call/rtp_packet_sink_interface.h"
// Шлях до rtp_packet_received.h, де визначено клас RtpPacketReceived
#include "modules/rtp_rtcp/source/rtp_packet_received.h"

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
public:
  using OnRtpPacketCallback = std::function<void(const uint8_t* data, size_t length, uint32_t timestamp)>;

  explicit RtpPacketSink(OnRtpPacketCallback on_packet);
  ~RtpPacketSink() override;

  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override;

private:
  rtc::CriticalSection _crit;
  OnRtpPacketCallback _on_packet;
};

#endif  // RTP_PACKET_SINK_H_
