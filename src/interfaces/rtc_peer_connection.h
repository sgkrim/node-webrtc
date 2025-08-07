/* Copyright (c) 2019 The node-webrtc project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE.md file in the root of the source tree. All contributing
 * project authors may be found in the AUTHORS file in the root of the source
 * tree.
 */
#pragma once

#include <vector>

#include <node-addon-api/napi.h>
#include <api/peer_connection_interface.h>
#include <api/scoped_refptr.h>

#include "src/node/async_object_wrap_with_loop.h"
#include "src/dictionaries/node_webrtc/extended_rtc_configuration.h"
#include "src/dictionaries/node_webrtc/rtc_session_description_init.h"
#include "src/rtp_packet_sink.h"
#include "src/encoded_frame_sink.h"

namespace webrtc {

class DataChannelInterface;
class IceCandidateInterface;
class MediaStreamInterface;
class RtpReceiverInterface;
class RtpTransceiverInterface;

}  // namespace webrtc

class RtpPacketSinkWrapper;


namespace node_webrtc {

class RTCDataChannel;
class PeerConnectionFactory;

class RTCPeerConnection
  : public AsyncObjectWrapWithLoop<RTCPeerConnection>
  , public webrtc::PeerConnectionObserver {
 public:
  explicit RTCPeerConnection(const Napi::CallbackInfo&);

  ~RTCPeerConnection() override;

  //
  // PeerConnectionObserver implementation.
  //
  void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override;
  void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
  void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override;
  void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
  void OnIceCandidateError(const std::string& host_candidate, const std::string& url, int error_code, const std::string& error_text) override;
  void OnRenegotiationNeeded() override;

  void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) override;

  void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
  void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;

  void OnAddTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
      const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) override;
  void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;

  static void Init(Napi::Env, Napi::Object);

  static Napi::FunctionReference& constructor();

  void SaveLastSdp(const RTCSessionDescriptionInit& lastSdp);

// VVV ДОДАЙТЕ ЦЕЙ НОВИЙ МЕТОД VVV
  webrtc::PeerConnectionInterface* pc() { return _jinglePeerConnection.get(); }

 private:
  friend class RtpPacketSinkWrapper; // <--- ДОДАЙТЕ ЦЕЙ РЯДОК
  Napi::Value AddTrack(const Napi::CallbackInfo&);
  Napi::Value AddTransceiver(const Napi::CallbackInfo&);
  Napi::Value RemoveTrack(const Napi::CallbackInfo&);
  Napi::Value CreateOffer(const Napi::CallbackInfo&);
  Napi::Value CreateAnswer(const Napi::CallbackInfo&);
  Napi::Value SetLocalDescription(const Napi::CallbackInfo&);
  Napi::Value SetRemoteDescription(const Napi::CallbackInfo&);
  Napi::Value UpdateIce(const Napi::CallbackInfo&);
  Napi::Value AddIceCandidate(const Napi::CallbackInfo&);
  Napi::Value CreateDataChannel(const Napi::CallbackInfo&);
  /*
  Napi::Value GetLocalStreams(const Napi::CallbackInfo&);
  Napi::Value GetRemoteStreams(const Napi::CallbackInfo&);
  Napi::Value GetStreamById(const Napi::CallbackInfo&);
  Napi::Value AddStream(const Napi::CallbackInfo&);
  Napi::Value RemoveStream(const Napi::CallbackInfo&);
  */
  Napi::Value GetConfiguration(const Napi::CallbackInfo&);
  Napi::Value SetConfiguration(const Napi::CallbackInfo&);
  Napi::Value GetReceivers(const Napi::CallbackInfo&);
  Napi::Value GetSenders(const Napi::CallbackInfo&);
  Napi::Value GetStats(const Napi::CallbackInfo&);
  Napi::Value LegacyGetStats(const Napi::CallbackInfo&);
  Napi::Value GetTransceivers(const Napi::CallbackInfo&);
  Napi::Value Close(const Napi::CallbackInfo&);
  Napi::Value RestartIce(const Napi::CallbackInfo&);

  Napi::Value AttachRawSink(const Napi::CallbackInfo&);
  Napi::Value GetCustomMethodExists(const Napi::CallbackInfo& info);

  Napi::Value AttachAudioSink(const Napi::CallbackInfo& info);
  Napi::Value AttachEncodedVideoSink(const Napi::CallbackInfo& info);

  Napi::Value GetCanTrickleIceCandidates(const Napi::CallbackInfo&);
  Napi::Value GetConnectionState(const Napi::CallbackInfo&);
  Napi::Value GetCurrentLocalDescription(const Napi::CallbackInfo&);
  Napi::Value GetLocalDescription(const Napi::CallbackInfo&);
  Napi::Value GetPendingLocalDescription(const Napi::CallbackInfo&);
  Napi::Value GetCurrentRemoteDescription(const Napi::CallbackInfo&);
  Napi::Value GetRemoteDescription(const Napi::CallbackInfo&);
  Napi::Value GetPendingRemoteDescription(const Napi::CallbackInfo&);
  Napi::Value GetIceConnectionState(const Napi::CallbackInfo&);
  Napi::Value GetSctp(const Napi::CallbackInfo&);
  Napi::Value GetSignalingState(const Napi::CallbackInfo&);
  Napi::Value GetIceGatheringState(const Napi::CallbackInfo&);

  RTCSessionDescriptionInit _lastSdp;

  // МЕТОД ДЛЯ ОТРИМАННЯ PAYLOAD TYPES
  Napi::Value GetPayloadTypes(const Napi::CallbackInfo&);

  // ЛОГІКА ДЛЯ SDP
  void ParseSdpForPayloadTypes(const std::string& sdp);
  std::map<std::string, uint8_t> _payload_types;

  std::vector<std::unique_ptr<EncodedFrameSink>> _encoded_sinks;
  // Зберігаємо sink-и, щоб вони були живі, поки PeerConnection існує.
  std::vector<std::unique_ptr<RtpPacketSink>> _sinks;
  std::map<std::string, Napi::ThreadSafeFunction> _tsfns;

  UnsignedShortRange _port_range;
  ExtendedRTCConfiguration _cached_configuration;
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> _jinglePeerConnection;

  PeerConnectionFactory* _factory;
  bool _shouldReleaseFactory;

  std::vector<RTCDataChannel*> _channels;
};

}  // namespace node_webrtc
