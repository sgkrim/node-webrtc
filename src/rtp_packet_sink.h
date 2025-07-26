#ifndef RTP_PACKET_SINK_H_
#define RTP_PACKET_SINK_H_

#include <functional>
#include <call/rtp_packet_sink_interface.h>
#include <api/rtp_headers.h>
#include <rtc_base/synchronization/mutex.h>

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
public:
  using OnRtpPacketCallback = std::function<void(const uint8_t* data, size_t length, uint32_t timestamp)>;

  explicit RtpPacketSink(OnRtpPacketCallback on_packet);
  ~RtpPacketSink() override;

  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override;

private:
  webrtc::Mutex _mutex;
  OnRtpPacketCallback _on_packet RTC_GUARDED_BY(_mutex);
};

#endif  // RTP_PACKET_SINK_H_