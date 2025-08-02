#ifndef SRC_RTPSINK_H_
#define SRC_RTPSINK_H_

#include "call/rtp_packet_sink_interface.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include <functional>

/**
 * RtpPacketSink - це простий C++ клас, який реалізує інтерфейс libwebrtc
 * для отримання сирих RTP-пакетів. Він викликає наданий C++ callback
 * для кожного отриманого пакету. Цей клас нічого не знає про Node.js або N-API.
 */
class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
 public:
  using OnRtpPacketCallback = std::function<void(const webrtc::RtpPacketReceived&)>;

  /**
   * Конструктор приймає лише C++ лямбду (callback).
   * @param on_packet функція, яка буде викликана при отриманні пакету.
   */
  explicit RtpPacketSink(OnRtpPacketCallback on_packet)
      : _on_packet(on_packet) {}

  /**
   * Деструктор за замовчуванням.
   */
  ~RtpPacketSink() override = default;

  /**
   * Метод, який викликається libwebrtc при отриманні нового RTP-пакету.
   * @param packet отриманий RTP-пакет.
   */
  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override {
    if (_on_packet) {
      _on_packet(packet);
    }
  }

 private:
  OnRtpPacketCallback _on_packet;
};

#endif  // SRC_RTPSINK_H_
