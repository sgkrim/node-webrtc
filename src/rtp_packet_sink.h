#ifndef RTP_PACKET_SINK_H_
#define RTP_PACKET_SINK_H_

#include <functional>
//#include <mutex>
#include "third_party/abseil-cpp/absl/synchronization/mutex.h"
// Шлях до rtp_packet_sink_interface.h для версії M81
#include "call/rtp_packet_sink_interface.h"
// Шлях до rtp_packet_received.h, де визначено клас RtpPacketReceived
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
// Використовуємо м'ютекс з бібліотеки Abseil
//#include "third_party/abseil-cpp/absl/synchronization/mutex.h"

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
public:
  using OnRtpPacketCallback = std::function<void(const uint8_t* data, size_t length, uint32_t timestamp)>;

  explicit RtpPacketSink(OnRtpPacketCallback on_packet);
  ~RtpPacketSink() override;

  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override;

private:
  // Використовуємо absl::Mutex замість webrtc::Mutex
  absl::Mutex _mutex;
  OnRtpPacketCallback _on_packet RTC_GUARDED_BY(_mutex);
};

#endif  // RTP_PACKET_SINK_H_
