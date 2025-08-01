#ifndef SRC_RTPSINK_H_
#define SRC_RTPSINK_H_

#include "call/rtp_packet_sink_interface.h"
#include "modules/rtp_rtcp/include/rtp_packet_received.h"

#include <functional>
#include <node-addon-api/napi.h>

class RtpPacketSink : public webrtc::RtpPacketSinkInterface {
 public:
  using OnRtpPacketCallback = std::function<void(const webrtc::RtpPacketReceived&)>;

  // НАШ НОВИЙ КОНСТРУКТОР
  explicit RtpPacketSink(OnRtpPacketCallback on_packet, Napi::FunctionReference* persistent_callback)
      : _on_packet(on_packet), _persistent_callback(persistent_callback) {}

  // ДЕСТРУКТОР ДЛЯ ОЧИЩЕННЯ ПАМ'ЯТІ
  ~RtpPacketSink() override {
    if (_persistent_callback) {
      _persistent_callback->Reset();
      delete _persistent_callback;
    }
  }

  void OnPacket(const webrtc::RtpPacketReceived& packet) override {
    if (_on_packet) {
      _on_packet(packet);
    }
  }

 private:
  OnRtpPacketCallback _on_packet;
  Napi::FunctionReference* _persistent_callback; // Поле для зберігання
};

#endif  // SRC_RTPSINK_H_