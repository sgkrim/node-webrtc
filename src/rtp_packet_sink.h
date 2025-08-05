#ifndef SRC_RTPSINK_H_
#define SRC_RTPSINK_H_

#include "call/rtp_packet_sink_interface.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include <functional>
#include <vector>

// Перерахування для вибору режиму роботи
enum class SinkMode {
  RtpPacket, // Режим передачі повних RTP-пакетів
  CodecFrame // Режим збирання та передачі чистих кадрів кодека
};

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
 public:
  using OnDataCallback = std::function<void(const std::vector<uint8_t>&)>;

  // Конструктор тепер приймає режим роботи, payload_type для фільтрації та прапорець is_audio
  explicit RtpPacketSink(OnDataCallback on_data, SinkMode mode, uint8_t payload_type, bool is_audio = false)
      : _on_data(on_data), _mode(mode), _payload_type(payload_type), _is_audio(is_audio) {}

  ~RtpPacketSink() override = default;

  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override {


    if (!_is_audio) {
        RTC_LOG(LS_INFO) << "[RtpPacketSink] PRE-FILTER - Received packet with PT: " << (int)packet.PayloadType()
                         << ", Expected PT: " << (int)_payload_type;
    }
    // КЛЮЧОВЕ ВИПРАВЛЕННЯ: Ігноруємо пакети з неправильним типом (напр. RTX, FEC)
    if (packet.PayloadType() != _payload_type) {
      return;
    }

    if (_mode == SinkMode::RtpPacket) {
      // РЕЖИМ 1: ПОВНІ RTP-ПАКЕТИ (тільки відфільтровані)
      const uint8_t* data = packet.data();
      size_t size = packet.size();
      std::vector<uint8_t> full_packet(data, data + size);
      if (_on_data) {
        _on_data(full_packet);
      }
    } else { // _mode == SinkMode::CodecFrame
      // РЕЖИМ 2: ЧИСТІ КАДРИ (PAYLOAD) (тільки з правильних пакетів)
      if (_is_audio) {
        // Для АУДІО: кожен пакет - це кадр
        auto payload = packet.payload();
        std::vector<uint8_t> frame(payload.begin(), payload.end());
        if (_on_data) {
          _on_data(frame);
        }
      } else {
        // Для ВІДЕО: збираємо пакети в кадри
        _frame_buffer.insert(_frame_buffer.end(), packet.payload().begin(), packet.payload().end());
        if (packet.Marker()) {
          if (_on_data) {
            _on_data(_frame_buffer);
          }
          _frame_buffer.clear();
        }
      }
    }
  }

 private:
  OnDataCallback _on_data;
  SinkMode _mode;
  uint8_t _payload_type; // Зберігаємо потрібний Payload Type для фільтрації
  bool _is_audio;
  std::vector<uint8_t> _frame_buffer;
};

#endif  // SRC_RTPSINK_H_
