#include "audio_opus_sink.h"

#include <api/audio/audio_frame.h>
#include <modules/audio_device/include/audio_device.h>
#include <media/base/audio_adapter.h>
#include <rtc_base/logging.h>
#include <modules/rtp_rtcp/source/rtp_packet_received.h>

class AudioOpusSink::AudioSinkImpl : public webrtc::AudioTrackSinkInterface {
public:
  AudioSinkImpl(AudioOpusSink::OnOpusDataCallback cb) : m_callback(cb) {}

  void OnData(const void* audio_data,
              int bits_per_sample,
              int sample_rate,
              size_t number_of_channels,
              size_t number_of_frames) override {
    // This is the RAW PCM — we IGNORE this in our Opus sink
  }

  void OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
    if (m_callback) {
      auto payload = packet.payload();
      auto timestamp = packet.Timestamp();
      m_callback(payload.data(), payload.size(), timestamp);
    }
  }

private:
  AudioOpusSink::OnOpusDataCallback m_callback;
};

AudioOpusSink::AudioOpusSink(rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track,
                             OnOpusDataCallback onData)
    : m_track(track), m_onData(onData) {
  m_signalingThread = rtc::Thread::Current();
  m_audioSink = std::make_unique<AudioSinkImpl>(onData);

  auto* audioTrack = static_cast<webrtc::AudioTrackInterface*>(track.get());
  audioTrack->AddSink(m_audioSink.get());
}

AudioOpusSink::~AudioOpusSink() {
  if (m_track && m_audioSink) {
    auto* audioTrack = static_cast<webrtc::AudioTrackInterface*>(m_track.get());
    audioTrack->RemoveSink(m_audioSink.get());
  }
}