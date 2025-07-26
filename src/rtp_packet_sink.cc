#include "src/rtp_packet_sink.h"
#include <rtc_base/logging.h>

RtpPacketSink::RtpPacketSink(OnRtpPacketCallback on_packet)
    : _on_packet(std::move(on_packet)) {
  RTC_LOG(LS_INFO) << "RtpPacketSink created";
}

RtpPacketSink::~RtpPacketSink() {
  RTC_LOG(LS_INFO) << "RtpPacketSink destroyed";
}

void RtpPacketSink::OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
  // ВИПРАВЛЕНО: Використовуємо absl::MutexLock замість webrtc::MutexLock
  absl::MutexLock lock(&_mutex);
  if (_on_packet) {
    auto payload = packet.payload();
    auto timestamp = packet.Timestamp();
    _on_packet(payload.data(), payload.size(), timestamp);
  }
}