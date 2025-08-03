#ifndef SRC_RTPSINK_H_
#define SRC_RTPSINK_H_

#include "call/rtp_packet_sink_interface.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include <functional>
#include <vector>

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
 public:
  // Змінено: Колбек тепер буде викликатись з повним кадром, а не з пакетом
  using OnFrameCallback = std::function<void(const std::vector<uint8_t>&, uint32_t)>;

  explicit RtpPacketSink(OnFrameCallback on_frame)
      : _on_frame(on_frame) {}

  ~RtpPacketSink() override = default;

  // Цей метод тепер збирає пакети в кадри
  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) override {
    // Додаємо корисне навантаження (payload) в буфер
    _frame_buffer.insert(_frame_buffer.end(), packet.payload().begin(), packet.payload().end());

    // Якщо це останній пакет кадру (є маркерний біт)
    if (packet.Marker()) {
      if (_on_frame) {
        // Викликаємо колбек з повним кадром і часовою міткою
        _on_frame(_frame_buffer, packet.Timestamp());
      }
      // Очищуємо буфер для наступного кадру
      _frame_buffer.clear();
    }
  }

 private:
  OnFrameCallback _on_frame;
  std::vector<uint8_t> _frame_buffer;
};

#endif  // SRC_RTPSINK_H_
