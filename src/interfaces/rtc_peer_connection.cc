/* Copyright (c) 2019 The node-webrtc project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE.md file in the root of the source tree. All contributing
 * project authors may be found in the AUTHORS file in the root of the source
 * tree.
 */
#include "src/interfaces/rtc_peer_connection.h"

#include <iosfwd>
#include <memory>
#include <utility>
#include <vector>

// VVV ДОДАНО ВСІ НЕОБХІДНІ ЗАГОЛОВКИ VVV
#include <iostream>
#include <regex>      // <--- ДОДАЙТЕ ЦЕЙ
#include <algorithm>  // <--- І ЦЕЙ

#include <p2p/base/port_allocator.h>
#include <api/sctp_transport_interface.h>

#include "src/rtp_packet_sink.h"
#include <pc/peer_connection.h>
#include <pc/channel_manager.h>
#include <pc/channel.h>
#include <pc/rtp_transceiver.h>
#include <media/base/media_channel.h>
// ^^^ КІНЕЦЬ ДОДАНИХ ЗАГОЛОВКІВ ^^^

#include <api/media_types.h>
#include <api/peer_connection_interface.h>
#include <api/rtc_error.h>
#include <api/rtp_transceiver_interface.h>
#include <api/scoped_refptr.h>
#include <p2p/client/basic_port_allocator.h>

#include "src/converters.h"
#include "src/converters/arguments.h"
#include "src/converters/interfaces.h"
#include "src/converters/napi.h"
#include "src/dictionaries/macros/napi.h"
#include "src/dictionaries/node_webrtc/rtc_answer_options.h"
#include "src/dictionaries/node_webrtc/rtc_offer_options.h"
#include "src/dictionaries/node_webrtc/rtc_session_description_init.h"
#include "src/dictionaries/node_webrtc/some_error.h"
#include "src/dictionaries/webrtc/data_channel_init.h"
#include "src/dictionaries/webrtc/ice_candidate_interface.h"
#include "src/dictionaries/webrtc/rtc_configuration.h"
#include "src/dictionaries/webrtc/rtc_error.h"
#include "src/dictionaries/webrtc/rtp_transceiver_init.h"
#include "src/enums/webrtc/ice_connection_state.h"
#include "src/enums/webrtc/ice_gathering_state.h"
#include "src/enums/webrtc/media_type.h"
#include "src/enums/webrtc/peer_connection_state.h"
#include "src/enums/webrtc/signaling_state.h"
#include "src/functional/either.h"
#include "src/functional/maybe.h"
#include "src/interfaces/media_stream.h"
#include "src/interfaces/media_stream_track.h"
#include "src/interfaces/rtc_data_channel.h"
#include "src/interfaces/rtc_peer_connection/create_session_description_observer.h"
#include "src/interfaces/rtc_peer_connection/peer_connection_factory.h"
#include "src/interfaces/rtc_peer_connection/rtc_stats_collector.h"
#include "src/interfaces/rtc_peer_connection/set_session_description_observer.h"
#include "src/interfaces/rtc_peer_connection/stats_observer.h"
#include "src/interfaces/rtc_rtp_receiver.h"
#include "src/interfaces/rtc_rtp_sender.h"
#include "src/interfaces/rtc_rtp_transceiver.h"
#include "src/interfaces/rtc_sctp_transport.h"
#include "src/node/error_factory.h"
#include "src/node/events.h"
#include "src/node/promise.h"
#include "src/node/utility.h"

namespace node_webrtc {

Napi::FunctionReference& RTCPeerConnection::constructor() {
  static Napi::FunctionReference constructor;
  return constructor;
}

//
// PeerConnection
//

RTCPeerConnection::RTCPeerConnection(const Napi::CallbackInfo& info)
  : AsyncObjectWrapWithLoop<RTCPeerConnection>("RTCPeerConnection", *this, info) {
  auto env = info.Env();

  if (!info.IsConstructCall()) {
    Napi::TypeError::New(env, "Use the new operator to construct the RTCPeerConnection.").ThrowAsJavaScriptException();
    return;
  }

  CONVERT_ARGS_OR_THROW_AND_RETURN_VOID_NAPI(info, maybeConfiguration, Maybe<ExtendedRTCConfiguration>)

  auto configuration = maybeConfiguration.FromMaybe(ExtendedRTCConfiguration());

  // TODO(mroberts): Read `factory` (non-standard) from RTCConfiguration?
  _factory = PeerConnectionFactory::GetOrCreateDefault();
  _shouldReleaseFactory = true;

  auto portAllocator = std::unique_ptr<cricket::PortAllocator>(new cricket::BasicPortAllocator(
              _factory->getNetworkManager(),
              _factory->getSocketFactory()));
  _port_range = configuration.portRange;
  portAllocator->SetPortRange(
      _port_range.min.FromMaybe(0),
      _port_range.max.FromMaybe(65535));

  _jinglePeerConnection = _factory->factory()->CreatePeerConnection(
          configuration.configuration,
          std::move(portAllocator),
          nullptr,
          this);
}

RTCPeerConnection::~RTCPeerConnection() {
  _jinglePeerConnection = nullptr;
  _channels.clear();
  if (_factory) {
    if (_shouldReleaseFactory) {
      PeerConnectionFactory::Release();
    }
    _factory = nullptr;
  }
}

void RTCPeerConnection::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState state) {
  Dispatch(CreateCallback<RTCPeerConnection>([this, state]() {
    MakeCallback("onsignalingstatechange", {});
    if (state == webrtc::PeerConnectionInterface::kClosed) {
      Stop();
    }
  }));
}

void RTCPeerConnection::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState) {
  Dispatch(CreateCallback<RTCPeerConnection>([this]() {
    MakeCallback("oniceconnectionstatechange", {});
    MakeCallback("onconnectionstatechange", {});
  }));
}

void RTCPeerConnection::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) {
  Dispatch(CreateCallback<RTCPeerConnection>([this]() {
    MakeCallback("onicegatheringstatechange", {});
  }));
}

void RTCPeerConnection::OnIceCandidate(const webrtc::IceCandidateInterface* ice_candidate) {
  std::string error;

  std::string sdp;
  if (!ice_candidate->ToString(&sdp)) {
    error = "Failed to print the candidate string. This is pretty weird. File a bug on https://github.com/node-webrtc/node-webrtc";
    return;
  }

  webrtc::SdpParseError parseError;
  auto candidate = std::shared_ptr<webrtc::IceCandidateInterface>(webrtc::CreateIceCandidate(
              ice_candidate->sdp_mid(),
              ice_candidate->sdp_mline_index(),
              sdp,
              &parseError));
  if (!parseError.description.empty()) {
    error = parseError.description;
  } else if (!candidate) {
    error = "Failed to copy RTCIceCandidate";
  }

  Dispatch(CreateCallback<RTCPeerConnection>([this, candidate, error]() {
    if (error.empty()) {
      auto env = Env();
      auto maybeCandidate = From<Napi::Value>(std::make_pair(env, candidate.get()));
      if (maybeCandidate.IsValid()) {
        MakeCallback("onicecandidate", { maybeCandidate.UnsafeFromValid() });
      }
    }
  }));
}

static Validation<Napi::Value> CreateRTCPeerConnectionIceErrorEvent(
    const Napi::Value hostCandidate,
    const Napi::Value url,
    const Napi::Value errorCode,
    const Napi::Value errorText) {
  auto env = hostCandidate.Env();
  Napi::EscapableHandleScope scope(env);
  NODE_WEBRTC_CREATE_OBJECT_OR_RETURN(env, object)
  NODE_WEBRTC_CONVERT_AND_SET_OR_RETURN(env, object, "hostCandidate", hostCandidate)
  NODE_WEBRTC_CONVERT_AND_SET_OR_RETURN(env, object, "url", url)
  NODE_WEBRTC_CONVERT_AND_SET_OR_RETURN(env, object, "errorCode", errorCode)
  NODE_WEBRTC_CONVERT_AND_SET_OR_RETURN(env, object, "errorText", errorText)
  return Pure(scope.Escape(object));
}

void RTCPeerConnection::OnIceCandidateError(const std::string& host_candidate, const std::string& url, int error_code, const std::string& error_text) {
  Dispatch(CreateCallback<RTCPeerConnection>([this, host_candidate, url, error_code, error_text]() {
    auto env = Env();
    auto maybeEvent = Validation<Napi::Value>::Join(curry(CreateRTCPeerConnectionIceErrorEvent)
            % From<Napi::Value>(std::make_pair(env, host_candidate))
            * From<Napi::Value>(std::make_pair(env, url))
            * From<Napi::Value>(std::make_pair(env, error_code))
            * From<Napi::Value>(std::make_pair(env, error_text)));
    if (maybeEvent.IsValid()) {
      MakeCallback("onicecandidateerror", { maybeEvent.UnsafeFromValid() });
    }
  }));
}

void RTCPeerConnection::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> channel) {
  auto observer = new DataChannelObserver(_factory, channel);
  Dispatch(CreateCallback<RTCPeerConnection>([this, observer]() {
    auto channel = RTCDataChannel::wrap()->GetOrCreate(observer, observer->channel());
    MakeCallback("ondatachannel", { channel->Value() });
  }));
}

void RTCPeerConnection::OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface>) {
}

void RTCPeerConnection::OnAddTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
    const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) {
  if (_jinglePeerConnection->GetConfiguration().sdp_semantics != webrtc::SdpSemantics::kPlanB) {
    return;
  }
  Dispatch(CreateCallback<RTCPeerConnection>([this, receiver, streams]() {
    auto mediaStreams = std::vector<MediaStream*>();
    for (auto const& stream : streams) {
      auto mediaStream = MediaStream::wrap()->GetOrCreate(_factory, stream);
      mediaStreams.push_back(mediaStream);
    }
    CONVERT_OR_THROW_AND_RETURN_VOID_NAPI(Env(), mediaStreams, streamArray, Napi::Value)
    MakeCallback("ontrack", {
      RTCRtpReceiver::wrap()->GetOrCreate(_factory, receiver)->Value(),
      streamArray,
      Env().Null()
    });
  }));
}

void RTCPeerConnection::OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
  auto receiver = transceiver->receiver();
  auto streams = receiver->streams();
  Dispatch(CreateCallback<RTCPeerConnection>([this, transceiver, receiver, streams]() {
    auto mediaStreams = std::vector<MediaStream*>();
    for (auto const& stream : streams) {
      auto mediaStream = MediaStream::wrap()->GetOrCreate(_factory, stream);
      mediaStreams.push_back(mediaStream);
    }
    CONVERT_OR_THROW_AND_RETURN_VOID_NAPI(Env(), mediaStreams, streamArray, Napi::Value)
    MakeCallback("ontrack", {
      RTCRtpReceiver::wrap()->GetOrCreate(_factory, receiver)->Value(),
      streamArray,
      RTCRtpTransceiver::wrap()->GetOrCreate(_factory, transceiver)->Value()
    });
  }));
}

void RTCPeerConnection::OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface>) {
}

void RTCPeerConnection::OnRenegotiationNeeded() {
  Dispatch(CreateCallback<RTCPeerConnection>([this]() {
    MakeCallback("onnegotiationneeded", {});
  }));
}

Napi::Value RTCPeerConnection::AddTrack(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  if (!_jinglePeerConnection) {
    Napi::Error(env, ErrorFactory::CreateInvalidStateError(env, "Cannot addTrack; RTCPeerConnection is closed")).ThrowAsJavaScriptException();
    return env.Undefined();
  }
  CONVERT_ARGS_OR_THROW_AND_RETURN_NAPI(info, pair, std::tuple<MediaStreamTrack* COMMA Maybe<std::vector<MediaStream*>>>)
  auto mediaStreamTrack = std::get<0>(pair);
  Maybe<std::vector<MediaStream*>> mediaStreams = std::get<1>(pair);
  std::vector<std::string> streamIds;
  if (mediaStreams.IsJust()) {
    streamIds.reserve(mediaStreams.UnsafeFromJust().size());
    for (auto const& stream : mediaStreams.UnsafeFromJust()) {
      streamIds.emplace_back(stream->stream()->id());
    }
  }
  auto result = _jinglePeerConnection->AddTrack(mediaStreamTrack->track(), streamIds);
  if (!result.ok()) {
    CONVERT_OR_THROW_AND_RETURN_NAPI(env, &result.error(), error, Napi::Value)
    Napi::Error(env, error).ThrowAsJavaScriptException();
    return env.Undefined();
  }
  auto rtpSender = result.value();
  return RTCRtpSender::wrap()->GetOrCreate(_factory, rtpSender)->Value();
}

Napi::Value RTCPeerConnection::AddTransceiver(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  if (!_jinglePeerConnection) {
    Napi::Error::New(env, "Cannot addTransceiver; RTCPeerConnection is closed").ThrowAsJavaScriptException();
    return env.Undefined();
  } else if (_jinglePeerConnection->GetConfiguration().sdp_semantics != webrtc::SdpSemantics::kUnifiedPlan) {
    Napi::Error::New(env, "AddTransceiver is only available with Unified Plan SdpSemanticsAbort").ThrowAsJavaScriptException();
    return env.Undefined();
  }
  CONVERT_ARGS_OR_THROW_AND_RETURN_NAPI(info, args, std::tuple<Either<cricket::MediaType COMMA MediaStreamTrack*> COMMA Maybe<webrtc::RtpTransceiverInit>>)
  Either<cricket::MediaType, MediaStreamTrack*> kindOrTrack = std::get<0>(args);
  Maybe<webrtc::RtpTransceiverInit> maybeInit = std::get<1>(args);
  auto result = kindOrTrack.IsLeft()
      ? maybeInit.IsNothing()
      ? _jinglePeerConnection->AddTransceiver(kindOrTrack.UnsafeFromLeft())
      : _jinglePeerConnection->AddTransceiver(kindOrTrack.UnsafeFromLeft(), maybeInit.UnsafeFromJust())
      : maybeInit.IsNothing()
      ? _jinglePeerConnection->AddTransceiver(kindOrTrack.UnsafeFromRight()->track())
      : _jinglePeerConnection->AddTransceiver(kindOrTrack.UnsafeFromRight()->track(), maybeInit.UnsafeFromJust());
  if (!result.ok()) {
    CONVERT_OR_THROW_AND_RETURN_NAPI(env, &result.error(), error, Napi::Value)
    Napi::Error(env, error).ThrowAsJavaScriptException();
    return env.Undefined();
  }
  auto rtpTransceiver = result.value();
  return RTCRtpTransceiver::wrap()->GetOrCreate(_factory, rtpTransceiver)->Value();
}

Napi::Value RTCPeerConnection::RemoveTrack(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  if (!_jinglePeerConnection) {
    Napi::Error(env, ErrorFactory::CreateInvalidStateError(env, "Cannot removeTrack; RTCPeerConnection is closed")).ThrowAsJavaScriptException();
    return env.Undefined();
  }
  CONVERT_ARGS_OR_THROW_AND_RETURN_NAPI(info, sender, RTCRtpSender*)
  auto senders = _jinglePeerConnection->GetSenders();
  if (std::find(senders.begin(), senders.end(), sender->sender()) == senders.end()) {
    Napi::Error(env, ErrorFactory::CreateInvalidAccessError(env, "Cannot removeTrack")).ThrowAsJavaScriptException();
    return env.Undefined();
  }
  if (!_jinglePeerConnection->RemoveTrack(sender->sender())) {
    Napi::Error(env, ErrorFactory::CreateInvalidAccessError(env, "Cannot removeTrack")).ThrowAsJavaScriptException();
    return env.Undefined();
  }
  return env.Undefined();
}

Napi::Value RTCPeerConnection::CreateOffer(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  CREATE_DEFERRED(env, deferred)

  auto maybeOptions = From<Maybe<RTCOfferOptions>>(Arguments(info)).Map([](auto maybeOptions) {
    return maybeOptions.FromMaybe(RTCOfferOptions());
  });
  if (maybeOptions.IsInvalid()) {
    Reject(deferred, SomeError(maybeOptions.ToErrors()[0]));
    return deferred.Promise();
  }

  if (!_jinglePeerConnection || _jinglePeerConnection->signaling_state() == webrtc::PeerConnectionInterface::SignalingState::kClosed) {
    Reject(deferred, ErrorFactory::CreateInvalidStateError(env,
            "Failed to execute 'createOffer' on 'RTCPeerConnection': "
            "The RTCPeerConnection's signalingState is 'closed'."));
    return deferred.Promise();
  }

  auto observer = new rtc::RefCountedObject<CreateSessionDescriptionObserver>(this, deferred);
  _jinglePeerConnection->CreateOffer(observer, maybeOptions.UnsafeFromValid().options);

  return deferred.Promise();
}

Napi::Value RTCPeerConnection::CreateAnswer(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  CREATE_DEFERRED(env, deferred)

  auto maybeOptions = From<Maybe<RTCAnswerOptions>>(Arguments(info)).Map([](auto maybeOptions) {
    return maybeOptions.FromMaybe(RTCAnswerOptions());
  });
  if (maybeOptions.IsInvalid()) {
    Reject(deferred, SomeError(maybeOptions.ToErrors()[0]));
    return deferred.Promise();
  }

  if (!_jinglePeerConnection || _jinglePeerConnection->signaling_state() == webrtc::PeerConnectionInterface::SignalingState::kClosed) {
    Reject(deferred, ErrorFactory::CreateInvalidStateError(env,
            "Failed to execute 'createAnswer' on 'RTCPeerConnection': "
            "The RTCPeerConnection's signalingState is 'closed'."));
    return deferred.Promise();
  }

  auto observer = new rtc::RefCountedObject<CreateSessionDescriptionObserver>(this, deferred);
  _jinglePeerConnection->CreateAnswer(observer, maybeOptions.UnsafeFromValid().options);

  return deferred.Promise();
}

Napi::Value RTCPeerConnection::SetLocalDescription(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  CREATE_DEFERRED(env, deferred)

  CONVERT_ARGS_OR_REJECT_AND_RETURN_NAPI(deferred, info, descriptionInit, RTCSessionDescriptionInit)
  if (descriptionInit.sdp.empty()) {
    descriptionInit.sdp = _lastSdp.sdp;
  }

  auto maybeRawDescription = From<webrtc::SessionDescriptionInterface*>(descriptionInit);
  if (maybeRawDescription.IsInvalid()) {
    Reject(deferred, maybeRawDescription.ToErrors()[0]);
    return deferred.Promise();
  }
  auto rawDescription = maybeRawDescription.UnsafeFromValid();
  std::unique_ptr<webrtc::SessionDescriptionInterface> description(rawDescription);

  if (!_jinglePeerConnection || _jinglePeerConnection->signaling_state() == webrtc::PeerConnectionInterface::SignalingState::kClosed) {
    Reject(deferred, ErrorFactory::CreateInvalidStateError(env,
            "Failed to execute 'setLocalDescription' on 'RTCPeerConnection': "
            "The RTCPeerConnection's signalingState is 'closed'."));
    return deferred.Promise();
  }

  auto observer = new rtc::RefCountedObject<SetSessionDescriptionObserver>(this, deferred);

    // Parse Sdp for gets payload types
   ParseSdpForPayloadTypes(descriptionInit.sdp);

  _jinglePeerConnection->SetLocalDescription(observer, description.release());

  return deferred.Promise();
}

Napi::Value RTCPeerConnection::SetRemoteDescription(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  CREATE_DEFERRED(env, deferred)

  CONVERT_ARGS_OR_REJECT_AND_RETURN_NAPI(deferred, info, rawDescription, webrtc::SessionDescriptionInterface*)
  std::unique_ptr<webrtc::SessionDescriptionInterface> description(rawDescription);

  if (!_jinglePeerConnection || _jinglePeerConnection->signaling_state() == webrtc::PeerConnectionInterface::SignalingState::kClosed) {
    Reject(deferred, ErrorFactory::CreateInvalidStateError(env,
            "Failed to execute 'setRemoteDescription' on 'RTCPeerConnection': "
            "The RTCPeerConnection's signalingState is 'closed'."));
    return deferred.Promise();
  }

  auto observer = new rtc::RefCountedObject<SetSessionDescriptionObserver>(this, deferred);

  // Parse sdp payload types
  std::string sdp_str;
  description->ToString(&sdp_str);
  ParseSdpForPayloadTypes(sdp_str);

  _jinglePeerConnection->SetRemoteDescription(observer, description.release());

  return deferred.Promise();  // NOLINT
}

Napi::Value RTCPeerConnection::AddIceCandidate(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  CREATE_DEFERRED(env, deferred)

  CONVERT_ARGS_OR_REJECT_AND_RETURN_NAPI(deferred, info, candidate, std::shared_ptr<webrtc::IceCandidateInterface>)

  Dispatch(CreatePromise<RTCPeerConnection>(deferred, [this, candidate](auto deferred) {
    if (_jinglePeerConnection
        && _jinglePeerConnection->signaling_state() != webrtc::PeerConnectionInterface::SignalingState::kClosed
        && _jinglePeerConnection->AddIceCandidate(candidate.get())) {
      Resolve(deferred, this->Env().Undefined());
    } else {
      std::string error = std::string("Failed to set ICE candidate");
      if (!_jinglePeerConnection
          || _jinglePeerConnection->signaling_state() == webrtc::PeerConnectionInterface::SignalingState::kClosed) {
        error += "; RTCPeerConnection is closed";
      }
      error += ".";
      Reject(deferred, SomeError(error));
    }
  }));

  return deferred.Promise();
}

Napi::Value RTCPeerConnection::CreateDataChannel(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  if (_jinglePeerConnection == nullptr) {
    Napi::Error(env, ErrorFactory::CreateInvalidStateError(env,
            "Failed to execute 'createDataChannel' on 'RTCPeerConnection': "
            "The RTCPeerConnection's signalingState is 'closed'.")).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  CONVERT_ARGS_OR_THROW_AND_RETURN_NAPI(info, args, std::tuple<std::string COMMA Maybe<webrtc::DataChannelInit>>)

  auto label = std::get<0>(args);
  auto dataChannelInit = std::get<1>(args).FromMaybe(webrtc::DataChannelInit());

  rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel_interface =
      _jinglePeerConnection->CreateDataChannel(label, &dataChannelInit);

  if (!data_channel_interface) {
    Napi::Error(env, ErrorFactory::CreateInvalidStateError(env, "'createDataChannel' failed")).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto observer = new DataChannelObserver(_factory, data_channel_interface);
  auto channel = RTCDataChannel::wrap()->GetOrCreate(observer, observer->channel());
  _channels.push_back(channel);

  return channel->Value();
}

Napi::Value RTCPeerConnection::GetConfiguration(const Napi::CallbackInfo& info) {
  auto configuration = _jinglePeerConnection
      ? ExtendedRTCConfiguration(_jinglePeerConnection->GetConfiguration(), _port_range)
      : _cached_configuration;
  CONVERT_OR_THROW_AND_RETURN_NAPI(info.Env(), configuration, result, Napi::Value)
  return result;
}

Napi::Value RTCPeerConnection::SetConfiguration(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  CONVERT_ARGS_OR_THROW_AND_RETURN_NAPI(info, configuration, webrtc::PeerConnectionInterface::RTCConfiguration)

  if (!_jinglePeerConnection) {
    Napi::Error(env, ErrorFactory::CreateInvalidStateError(env, "RTCPeerConnection is closed")).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto rtcError = _jinglePeerConnection->SetConfiguration(configuration);
  if (!rtcError.ok()) {
    CONVERT_OR_THROW_AND_RETURN_NAPI(env, &rtcError, error, Napi::Value)
    Napi::Error(env, error).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  return env.Undefined();
}

Napi::Value RTCPeerConnection::GetReceivers(const Napi::CallbackInfo& info) {
  std::vector<RTCRtpReceiver*> receivers;
  if (_jinglePeerConnection) {
    for (const auto& receiver : _jinglePeerConnection->GetReceivers()) {
      receivers.emplace_back(RTCRtpReceiver::wrap()->GetOrCreate(_factory, receiver));
    }
  }
  CONVERT_OR_THROW_AND_RETURN_NAPI(info.Env(), receivers, result, Napi::Value)
  return result;
}

Napi::Value RTCPeerConnection::GetSenders(const Napi::CallbackInfo& info) {
  std::vector<RTCRtpSender*> senders;
  if (_jinglePeerConnection) {
    for (const auto& sender : _jinglePeerConnection->GetSenders()) {
      senders.emplace_back(RTCRtpSender::wrap()->GetOrCreate(_factory, sender));
    }
  }
  CONVERT_OR_THROW_AND_RETURN_NAPI(info.Env(), senders, result, Napi::Value)
  return result;
}

Napi::Value RTCPeerConnection::GetStats(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  CREATE_DEFERRED(env, deferred)

  if (!_jinglePeerConnection) {
    Reject(deferred, ErrorFactory::CreateError(env, "RTCPeerConnection is closed"));
    return deferred.Promise();
  }

  auto callback = new rtc::RefCountedObject<RTCStatsCollector>(this, deferred);
  _jinglePeerConnection->GetStats(callback);

  return deferred.Promise();  // NOLINT
}

Napi::Value RTCPeerConnection::LegacyGetStats(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  CREATE_DEFERRED(env, deferred)

  if (!_jinglePeerConnection) {
    Reject(deferred, Napi::Error::New(env, "RTCPeerConnection is closed"));
    return deferred.Promise();
  }

  auto statsObserver = new rtc::RefCountedObject<StatsObserver>(this, deferred);
  if (!_jinglePeerConnection->GetStats(statsObserver, nullptr,
          webrtc::PeerConnectionInterface::kStatsOutputLevelStandard)) {
    Reject(deferred, Napi::Error::New(env, "Failed to execute getStats"));
    return deferred.Promise();
  }

  return deferred.Promise();
}

Napi::Value RTCPeerConnection::GetTransceivers(const Napi::CallbackInfo& info) {
  std::vector<RTCRtpTransceiver*> transceivers;
  if (_jinglePeerConnection
      && _jinglePeerConnection->GetConfiguration().sdp_semantics == webrtc::SdpSemantics::kUnifiedPlan) {
    for (const auto& transceiver : _jinglePeerConnection->GetTransceivers()) {
      transceivers.emplace_back(RTCRtpTransceiver::wrap()->GetOrCreate(_factory, transceiver));
    }
  }
  CONVERT_OR_THROW_AND_RETURN_NAPI(info.Env(), transceivers, result, Napi::Value)
  return result;
}

Napi::Value RTCPeerConnection::UpdateIce(const Napi::CallbackInfo& info) {
  return info.Env().Undefined();
}

Napi::Value RTCPeerConnection::Close(const Napi::CallbackInfo& info) {
  if (_jinglePeerConnection) {
    _cached_configuration = ExtendedRTCConfiguration(
            _jinglePeerConnection->GetConfiguration(),
            _port_range);
    _jinglePeerConnection->Close();
    // NOTE(mroberts): Perhaps another way to do this is to just register all remote MediaStreamTracks against this
    // RTCPeerConnection, not unlike what we do with RTCDataChannels.
    if (_jinglePeerConnection->GetConfiguration().sdp_semantics == webrtc::SdpSemantics::kUnifiedPlan) {
      for (const auto& transceiver : _jinglePeerConnection->GetTransceivers()) {
        auto track = MediaStreamTrack::wrap()->GetOrCreate(_factory, transceiver->receiver()->track());
        track->OnPeerConnectionClosed();
      }
    }
    for (auto channel : _channels) {
      channel->OnPeerConnectionClosed();
    }
  }

  _jinglePeerConnection = nullptr;

  if (_factory) {
    if (_shouldReleaseFactory) {
      PeerConnectionFactory::Release();
    }
    _factory = nullptr;
  }

for (auto& pair : _tsfns) {
    pair.second.Release();
  }
  _tsfns.clear();
  _sinks.clear();

  return info.Env().Undefined();
}

Napi::Value RTCPeerConnection::RestartIce(const Napi::CallbackInfo& info) {
  (void) info;
  if (_jinglePeerConnection) {
    _jinglePeerConnection->RestartIce();
  }
  return info.Env().Undefined();
}

struct RtpPacketData {
  size_t length;
  uint32_t timestamp;
  std::unique_ptr<uint8_t[]> data;
};

class OnPacketWorker : public Napi::AsyncWorker {
 public:
  // Змінено: Конструктор приймає Env і FunctionReference,
  // але викликає базовий конструктор ЛИШЕ з Env.
  OnPacketWorker(Napi::Env env, Napi::FunctionReference& callback, RtpPacketData* data)
    : Napi::AsyncWorker(env), _callback(callback), _data(data) {}

  ~OnPacketWorker() override = default;

  void Execute() override {}

  void OnOK() override {
    Napi::HandleScope scope(Env());
    Napi::Object packet_obj = Napi::Object::New(Env());
    packet_obj.Set("payload", Napi::Buffer<uint8_t>::New(
      Env(),
      _data->data.release(),
      _data->length,
      [](Napi::Env, uint8_t* d) { delete[] d; }
    ));
    packet_obj.Set("timestamp", Napi::Number::New(Env(), _data->timestamp));

    // Змінено: Викликаємо колбек, який зберегли самі, а не через базовий клас.
    _callback.Value().Call({packet_obj});
    delete _data;
  }
 private:
  // Зберігаємо посилання на колбек самі.
  Napi::FunctionReference& _callback;
  RtpPacketData* _data;
};

// Реалізація методу AttachRawSink

Napi::Value RTCPeerConnection::AttachRtpSink(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() != 3 || !info[0].IsString() || !info[1].IsNumber() || !info[2].IsFunction()) {
        Napi::TypeError::New(env, "attachRtpSink expects 3 arguments: (trackId, payloadType, callback)").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string trackId = info[0].As<Napi::String>().Utf8Value();
    uint8_t payloadType = info[1].As<Napi::Number>().Uint32Value();
    Napi::Function callback = info[2].As<Napi::Function>();

    Napi::ThreadSafeFunction tsfn = Napi::ThreadSafeFunction::New(env, callback, "RtpPacketCallback", 0, 1, [this, trackId](Napi::Env) { this->_tsfns.erase(trackId); });
    _tsfns[trackId] = tsfn;

    auto on_packet_callback = [tsfn](const std::vector<uint8_t>& packet) {
        auto* packet_copy = new std::vector<uint8_t>(packet);
        tsfn.NonBlockingCall(packet_copy, [](Napi::Env env, Napi::Function jsCallback, std::vector<uint8_t>* pkt) {
            jsCallback.Call({Napi::Buffer<uint8_t>::Copy(env, pkt->data(), pkt->size())});
            delete pkt;
        });
    };

    Dispatch(CreateCallback<RTCPeerConnection>([this, trackId, payloadType, on_packet_callback]() {
        auto sink = std::make_unique<RtpPacketSink>(on_packet_callback, SinkMode::RtpPacket, payloadType);
        RtpPacketSink* sink_ptr = sink.get();
        this->_sinks.push_back(std::move(sink));

        auto* pc_impl = static_cast<webrtc::PeerConnection*>(_jinglePeerConnection.get());
        cricket::ChannelInterface* channel_iface = pc_impl->GetChannelByTrackId(trackId);
        if (channel_iface) {
            channel_iface->SetRawRtpPacketSink(sink_ptr);
        }
    }));
    return env.Undefined();
}

// МЕТОД 2: ОТРИМАННЯ ЧИСТИХ КАДРІВ (сам знаходить PT)
Napi::Value RTCPeerConnection::AttachFrameSink(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() != 2 || !info[0].IsString() || !info[1].IsFunction()) {
        Napi::TypeError::New(env, "attachFrameSink expects 2 arguments: (trackId, callback)").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::cout << "Run frame getter. Version 2.0" << std::endl;
    std::string trackId = info[0].As<Napi::String>().Utf8Value();
    Napi::Function callback = info[1].As<Napi::Function>();

    Napi::ThreadSafeFunction tsfn = Napi::ThreadSafeFunction::New(env, callback, "FrameCallback", 0, 1, [this, trackId](Napi::Env) { this->_tsfns.erase(trackId); });
    _tsfns[trackId] = tsfn;

    auto on_frame_callback = [tsfn](const std::vector<uint8_t>& frame) {
        auto* frame_copy = new std::vector<uint8_t>(frame);
        tsfn.NonBlockingCall(frame_copy, [](Napi::Env env, Napi::Function jsCallback, std::vector<uint8_t>* frm) {
            jsCallback.Call({Napi::Buffer<uint8_t>::Copy(env, frm->data(), frm->size())});
            delete frm;
        });
    };

    Dispatch(CreateCallback<RTCPeerConnection>([this, trackId, on_frame_callback]() {
        bool is_audio = false;
        std::string codec_name;
        uint8_t payload_type = 0;

        for (const auto& transceiver : _jinglePeerConnection->GetTransceivers()) {
            if (transceiver && transceiver->receiver() && transceiver->receiver()->track() && transceiver->receiver()->track()->id() == trackId) {
                is_audio = (transceiver->receiver()->track()->kind() == webrtc::MediaStreamTrackInterface::kAudioKind);
                codec_name = is_audio ? "opus" : "vp8";
                if (_payload_types.count(codec_name)) {
                    payload_type = _payload_types[codec_name];
                }
                break;
            }
        }

        if (payload_type == 0) {
            RTC_LOG(LS_WARNING) << "PayloadType for " << codec_name << " not found for track " << trackId;
            return;
        }

        auto sink = std::make_unique<RtpPacketSink>(on_frame_callback, SinkMode::CodecFrame, payload_type, is_audio);
        RtpPacketSink* sink_ptr = sink.get();
        this->_sinks.push_back(std::move(sink));

        auto* pc_impl = static_cast<webrtc::PeerConnection*>(_jinglePeerConnection.get());
        cricket::ChannelInterface* channel_iface = pc_impl->GetChannelByTrackId(trackId);
        if (channel_iface) {
            channel_iface->SetRawRtpPacketSink(sink_ptr);
        }
    }));
    return env.Undefined();
}

// End AttachRawSink

// Start ParseSdp and GetPayloadTypes

void RTCPeerConnection::ParseSdpForPayloadTypes(const std::string& sdp) {
    _payload_types.clear();
    // Регулярний вираз для пошуку рядків типу "a=rtpmap:111 opus/48000/2"
    // Він знаходить числа, за якими йде "opus" або "vp8", ігноруючи регістр
    std::regex rtpmap_regex("a=rtpmap:(\\d+)\\s+(opus|VP8)\\/", std::regex_constants::icase);

    std::istringstream sdp_stream(sdp);
    std::string line;
    while (std::getline(sdp_stream, line)) {
        // Обрізаємо '\r' в кінці, якщо є
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::smatch match;
        if (std::regex_search(line, match, rtpmap_regex) && match.size() == 3) {
            uint8_t pt = static_cast<uint8_t>(std::stoi(match[1].str()));
            std::string codec_name = match[2].str();
            // Приводимо до нижнього регістру для уніфікації
            std::transform(codec_name.begin(), codec_name.end(), codec_name.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            _payload_types[codec_name] = pt;
        }
    }
}

Napi::Value RTCPeerConnection::GetPayloadTypes(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Object result = Napi::Object::New(env);
    for (const auto& pair : _payload_types) {
        result.Set(pair.first, Napi::Number::New(env, pair.second));
    }
    return result;
}

// End ParseSdp and GetPayloadTypes


Napi::Value RTCPeerConnection::GetCanTrickleIceCandidates(const Napi::CallbackInfo& info) {
  return info.Env().Null();
}

Napi::Value RTCPeerConnection::GetConnectionState(const Napi::CallbackInfo& info) {
  auto env = info.Env();

  auto connectionState = _jinglePeerConnection
      ? _jinglePeerConnection->peer_connection_state()
      : webrtc::PeerConnectionInterface::PeerConnectionState::kClosed;

  CONVERT_OR_THROW_AND_RETURN_NAPI(env, connectionState, result, Napi::Value)
  return result;
}

Napi::Value RTCPeerConnection::GetCurrentLocalDescription(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  Napi::Value result = env.Null();
  if (_jinglePeerConnection && _jinglePeerConnection->current_local_description()) {
    CONVERT_OR_THROW_AND_RETURN_NAPI(env, _jinglePeerConnection->current_local_description(), description, Napi::Value)
    result = description;
  }
  return result;
}

Napi::Value RTCPeerConnection::GetLocalDescription(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  Napi::Value result = env.Null();
  if (_jinglePeerConnection && _jinglePeerConnection->local_description()) {
    CONVERT_OR_THROW_AND_RETURN_NAPI(env, _jinglePeerConnection->local_description(), description, Napi::Value)
    result = description;
  }
  return result;
}

Napi::Value RTCPeerConnection::GetPendingLocalDescription(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  Napi::Value result = env.Null();
  if (_jinglePeerConnection && _jinglePeerConnection->pending_local_description()) {
    CONVERT_OR_THROW_AND_RETURN_NAPI(env, _jinglePeerConnection->pending_local_description(), description, Napi::Value)
    result = description;
  }
  return result;
}

Napi::Value RTCPeerConnection::GetCurrentRemoteDescription(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  Napi::Value result = env.Null();
  if (_jinglePeerConnection && _jinglePeerConnection->current_remote_description()) {
    CONVERT_OR_THROW_AND_RETURN_NAPI(env, _jinglePeerConnection->current_remote_description(), description, Napi::Value)
    result = description;
  }
  return result;
}

Napi::Value RTCPeerConnection::GetRemoteDescription(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  Napi::Value result = env.Null();
  if (_jinglePeerConnection && _jinglePeerConnection->remote_description()) {
    CONVERT_OR_THROW_AND_RETURN_NAPI(env, _jinglePeerConnection->remote_description(), description, Napi::Value)
    result = description;
  }
  return result;
}

Napi::Value RTCPeerConnection::GetPendingRemoteDescription(const Napi::CallbackInfo& info) {
  auto env = info.Env();
  Napi::Value result = env.Null();
  if (_jinglePeerConnection && _jinglePeerConnection->pending_remote_description()) {
    CONVERT_OR_THROW_AND_RETURN_NAPI(env, _jinglePeerConnection->pending_remote_description(), description, Napi::Value)
    result = description;
  }
  return result;
}

Napi::Value RTCPeerConnection::GetSctp(const Napi::CallbackInfo& info) {
  return _jinglePeerConnection && _jinglePeerConnection->GetSctpTransport()
      ? RTCSctpTransport::wrap()->GetOrCreate(_factory, _jinglePeerConnection->GetSctpTransport())->Value()
      : info.Env().Null();
}

Napi::Value RTCPeerConnection::GetSignalingState(const Napi::CallbackInfo& info) {
  auto signalingState = _jinglePeerConnection
      ? _jinglePeerConnection->signaling_state()
      : webrtc::PeerConnectionInterface::SignalingState ::kClosed;
  CONVERT_OR_THROW_AND_RETURN_NAPI(info.Env(), signalingState, result, Napi::Value)
  return result;
}

Napi::Value RTCPeerConnection::GetIceConnectionState(const Napi::CallbackInfo& info) {
  auto iceConnectionState = _jinglePeerConnection
      ? _jinglePeerConnection->standardized_ice_connection_state()
      : webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionClosed;
  CONVERT_OR_THROW_AND_RETURN_NAPI(info.Env(), iceConnectionState, result, Napi::Value)
  return result;
}

Napi::Value RTCPeerConnection::GetIceGatheringState(const Napi::CallbackInfo& info) {
  auto iceGatheringState = _jinglePeerConnection
      ? _jinglePeerConnection->ice_gathering_state()
      : webrtc::PeerConnectionInterface::IceGatheringState::kIceGatheringComplete;
  CONVERT_OR_THROW_AND_RETURN_NAPI(info.Env(), iceGatheringState, result, Napi::Value)
  return result;
}

Napi::Value RTCPeerConnection::GetCustomMethodExists(const Napi::CallbackInfo& info) {
  return Napi::Boolean::New(info.Env(), true);
}


void RTCPeerConnection::SaveLastSdp(const RTCSessionDescriptionInit& lastSdp) {
  this->_lastSdp = lastSdp;
}

void RTCPeerConnection::Init(Napi::Env env, Napi::Object exports) {
  auto func = DefineClass(env, "RTCPeerConnection", {
    InstanceMethod("addTrack", &RTCPeerConnection::AddTrack),
    InstanceMethod("addTransceiver", &RTCPeerConnection::AddTransceiver),
    InstanceMethod("removeTrack", &RTCPeerConnection::RemoveTrack),
    InstanceMethod("createOffer", &RTCPeerConnection::CreateOffer),
    InstanceMethod("createAnswer", &RTCPeerConnection::CreateAnswer),
    InstanceMethod("setLocalDescription", &RTCPeerConnection::SetLocalDescription),
    InstanceMethod("setRemoteDescription", &RTCPeerConnection::SetRemoteDescription),
    InstanceMethod("getConfiguration", &RTCPeerConnection::GetConfiguration),
    InstanceMethod("setConfiguration", &RTCPeerConnection::SetConfiguration),
    InstanceMethod("restartIce", &RTCPeerConnection::RestartIce),
    InstanceMethod("getReceivers", &RTCPeerConnection::GetReceivers),
    InstanceMethod("getSenders", &RTCPeerConnection::GetSenders),
    InstanceMethod("getStats", &RTCPeerConnection::GetStats),
    InstanceMethod("legacyGetStats", &RTCPeerConnection::LegacyGetStats),
    InstanceMethod("getTransceivers", &RTCPeerConnection::GetTransceivers),
    InstanceMethod("updateIce", &RTCPeerConnection::UpdateIce),
    InstanceMethod("addIceCandidate", &RTCPeerConnection::AddIceCandidate),
    InstanceMethod("createDataChannel", &RTCPeerConnection::CreateDataChannel),
    InstanceMethod("close", &RTCPeerConnection::Close),
    InstanceMethod("attachRtpSink", &RTCPeerConnection::AttachRtpSink),
    InstanceMethod("attachFrameSink", &RTCPeerConnection::AttachFrameSink),
    InstanceMethod("getPayloadTypes", &RTCPeerConnection::GetPayloadTypes),
    InstanceAccessor("customMethodExists", &RTCPeerConnection::GetCustomMethodExists, nullptr),
    InstanceAccessor("canTrickleIceCandidates", &RTCPeerConnection::GetCanTrickleIceCandidates, nullptr),
    InstanceAccessor("connectionState", &RTCPeerConnection::GetConnectionState, nullptr),
    InstanceAccessor("currentLocalDescription", &RTCPeerConnection::GetCurrentLocalDescription, nullptr),
    InstanceAccessor("localDescription", &RTCPeerConnection::GetLocalDescription, nullptr),
    InstanceAccessor("pendingLocalDescription", &RTCPeerConnection::GetPendingLocalDescription, nullptr),
    InstanceAccessor("currentRemoteDescription", &RTCPeerConnection::GetCurrentRemoteDescription, nullptr),
    InstanceAccessor("remoteDescription", &RTCPeerConnection::GetRemoteDescription, nullptr),
    InstanceAccessor("pendingRemoteDescription", &RTCPeerConnection::GetPendingRemoteDescription, nullptr),
    InstanceAccessor("sctp", &RTCPeerConnection::GetSctp, nullptr),
    InstanceAccessor("signalingState", &RTCPeerConnection::GetSignalingState, nullptr),
    InstanceAccessor("iceConnectionState", &RTCPeerConnection::GetIceConnectionState, nullptr),
    InstanceAccessor("iceGatheringState", &RTCPeerConnection::GetIceGatheringState, nullptr)
  });

  constructor() = Napi::Persistent(func);
  constructor().SuppressDestruct();

  exports.Set("RTCPeerConnection", func);
}

}  // namespace node_webrtc
