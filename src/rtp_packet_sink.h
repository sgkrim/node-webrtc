#ifndef SRC_RTPSINK_H_
#define SRC_RTPSINK_H_

#include "call/rtp_packet_sink_interface.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include <functional>
#include <vector>

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
 public:
  // Колбек тепер буде викликатись з повним RTP-пакетом
  using OnPacketCallback = std::function<void(const std::vector<uint8_t>&)>;

  explicit RtpPacketSink(OnPacketCallback on_packet)
      : _on_packet(on_packet) {}

  ~RtpPacketSink() override = default;

  // Цей метод тепер просто перенаправляє повний пакет
  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override {
    if (_on_packet) {
      // ✅ ВИПРАВЛЕННЯ:
      // Отримуємо вказівник на дані та їх розмір
      const uint8_t* data = packet.data();
      size_t size = packet.size();

      // Створюємо вектор, копіюючи дані з пам'яті за допомогою вказівника та розміру
      std::vector<uint8_t> full_packet(data, data + size);

      _on_packet(full_packet);
    }
  }

 private:
  OnPacketCallback _on_packet;
};

#endif  // SRC_RTPSINK_H_
