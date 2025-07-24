#ifndef AUDIO_OPUS_SINK_H_
#define AUDIO_OPUS_SINK_H_

#include <functional>
#include <api/media_stream_interface.h>
#include <modules/audio_coding/codecs/opus/audio_decoder_opus.h>
#include <modules/rtp_rtcp/source/rtp_packet_received.h>
#include <rtc_base/thread.h>

class AudioOpusSink {
public:
  using OnOpusDataCallback = std::function<void(const uint8_t* data, size_t length, uint32_t timestamp)>;

  AudioOpusSink(rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track, OnOpusDataCallback onData);
  ~AudioOpusSink();

private:
  rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> m_track;
  OnOpusDataCallback m_onData;
  rtc::Thread* m_signalingThread;

  class AudioSinkImpl;
  std::unique_ptr<AudioSinkImpl> m_audioSink;
};

#endif  // AUDIO_OPUS_SINK_H_