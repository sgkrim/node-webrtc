#ifndef SRC_RTPSINK_H_
#define SRC_RTPSINK_H_

#include "call/rtp_packet_sink_interface.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include <functional>
#include <vector>
#include <cstdint>

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
 public:
  // ОНОВЛЕНО: Колбек тепер приймає і кадр, і його RTP timestamp
  using OnFrameCallback = std::function<void(const std::vector<uint8_t>&, uint32_t)>;

  explicit RtpPacketSink(OnFrameCallback on_frame, uint8_t payload_type)
      : _on_frame(on_frame),
        _payload_type(payload_type),
        _current_timestamp(0),
        _is_assembling_frame(false) {}

  ~RtpPacketSink() override = default;

  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override {
    if (packet.PayloadType() != _payload_type) {
        return;
    }

    // Якщо це перший пакет нового кадру, запам'ятовуємо його timestamp
    if (!_is_assembling_frame) {
      _current_timestamp = packet.Timestamp();
      _is_assembling_frame = true;
    }

    // Додаємо дані з поточного пакета в наш буфер
    auto payload = packet.payload();
    _frame_buffer.insert(_frame_buffer.end(), payload.begin(), payload.end());

    // Маркерний біт в RTP-заголовку вказує на кінець кадру
    if (packet.Marker()) {
      if (_on_frame) {
        // Кадр зібрано, викликаємо колбек з буфером та міткою
        _on_frame(_frame_buffer, _current_timestamp);
      }
      // Очищуємо буфер і скидаємо стан для наступного кадру
      _frame_buffer.clear();
      _is_assembling_frame = false;
    }
  }

 private:
  OnFrameCallback _on_frame;
  uint8_t _payload_type;

  // Поля для збереження стану між викликами
  std::vector<uint8_t> _frame_buffer;
  uint32_t _current_timestamp;
  bool _is_assembling_frame;
};
#endif
