#ifndef SRC_RTPSINK_H_
#define SRC_RTPSINK_H_

#include "call/rtp_packet_sink_interface.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include <functional>
#include <vector>

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
 public:
  // Колбек тепер буде викликатись з повним кадром, а не з пакетом
  using OnFrameCallback = std::function<void(const std::vector<uint8_t>&, uint32_t)>;

  // ЗМІНЕНО: Додаємо прапорець is_audio в конструктор
  explicit RtpPacketSink(OnFrameCallback on_frame, bool is_audio)
      : _on_frame(on_frame), _is_audio(is_audio) {}

  ~RtpPacketSink() override = default;

  // Цей метод тепер має різну логіку для аудіо та відео
  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override {
    if (_is_audio) {
      // Для АУДІО: кожен пакет - це кадр. Віддаємо одразу корисне навантаження.
      auto payload = packet.payload();
      std::vector<uint8_t> frame(payload.begin(), payload.end());
      if (_on_frame) {
        _on_frame(frame, packet.Timestamp());
      }
    } else {
      // Для ВІДЕО: збираємо пакети в кадри за маркерним бітом.
      _frame_buffer.insert(_frame_buffer.end(), packet.payload().begin(), packet.payload().end());
      if (packet.Marker()) {
        if (_on_frame) {
          _on_frame(_frame_buffer, packet.Timestamp());
        }
        _frame_buffer.clear();
      }
    }
  }

 private:
  OnFrameCallback _on_frame;
  bool _is_audio; // Прапорець для визначення типу потоку
  std::vector<uint8_t> _frame_buffer;
};

#endif  // SRC_RTPSINK_H_
