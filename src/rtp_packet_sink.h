#ifndef SRC_RTPSINK_H_
#define SRC_RTPSINK_H_

#include "call/rtp_packet_sink_interface.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include <functional>

// БІЛЬШЕ НЕ ПОТРІБЕН #include "src/node_webrtc.h"

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
 public:
  using OnRtpPacketCallback = std::function<void(const webrtc::RtpPacketReceived&)>;

  // Конструктор тепер приймає ЛИШЕ C++ лямбду
  explicit RtpPacketSink(OnRtpPacketCallback on_packet)
      : _on_packet(on_packet) {}

  // Деструктор тепер порожній
  ~RtpPacketSink() override = default;

  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override {
    if (_on_packet) {
      _on_packet(packet);
    }
  }

 private:
  OnRtpPacketCallback _on_packet;
  // Поле _persistent_callback видалено
};

#endif  // SRC_RTPSINK_H_