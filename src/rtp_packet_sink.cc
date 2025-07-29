#include "rtp_packet_sink.h"
#include <rtc_base/logging.h>

RtpPacketSink::RtpPacketSink(OnRtpPacketCallback on_packet)
    : _on_packet(std::move(on_packet)) {
  RTC_LOG(LS_INFO) << "RtpPacketSink created";
}

RtpPacketSink::~RtpPacketSink() {
  RTC_LOG(LS_INFO) << "RtpPacketSink destroyed";
}

void RtpPacketSink::OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
  // Більше ніяких блокувань. Просто викликаємо callback.
  if (_on_packet) {
    _on_packet(packet);
  }
}
