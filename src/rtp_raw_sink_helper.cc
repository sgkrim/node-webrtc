#include <node-addon-api/napi.h>
#include <iostream>

#include "src/interfaces/rtc_peer_connection.h"
#include "src/rtp_packet_sink.h"

#include "pc/peer_connection.h"
#include "pc/channel_manager.h"
#include "pc/channel.h"
#include "media/base/media_channel.h"
#include "api/rtp_transceiver_interface.h"

// Цей клас тепер є внутрішньою деталлю реалізації
class OnPacketWorker : public Napi::AsyncWorker {
 public:
  OnPacketWorker(const Napi::Function& callback, RtpPacketData* data)
    : Napi::AsyncWorker(callback), _data(data) {}
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
    Callback().Call({packet_obj});
    delete _data;
  }
 private:
  RtpPacketData* _data;
};

// Наша єдина, stateless функція
Napi::Value AttachRawSink(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() != 3 || !info[0].IsObject() || !info[1].IsString() || !info[2].IsFunction()) {
        Napi::TypeError::New(env, "attachRawSink expects 3 arguments: (peerConnection, trackId, callback)").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Object pcObject = info[0].As<Napi::Object>();
    std::string trackId = info[1].As<Napi::String>().Utf8Value();
    Napi::Function callback = info[2].As<Napi::Function>();

    // Створюємо постійне посилання на callback, щоб він не був видалений збирачем сміття
    auto persistent_callback = new Napi::FunctionReference();
    *persistent_callback = Napi::Persistent(callback);

    // Створюємо sink
    auto sink = new RtpPacketSink([env, persistent_callback](const webrtc::RtpPacketReceived& packet) {
        auto* packet_data = new RtpPacketData();
        packet_data->length = packet.size();
        packet_data->data = std::unique_ptr<uint8_t[]>(new uint8_t[packet.size()]);
        memcpy(packet_data->data.get(), packet.data(), packet.size());
        packet_data->timestamp = packet.Timestamp();
        (new OnPacketWorker(persistent_callback->Value(), packet_data))->Queue();
    });

    // --- Починаємо логіку отримання каналу ---
    auto* pc_wrapper = Napi::ObjectWrap<node_webrtc::RTCPeerConnection>::Unwrap(pcObject);
    if (!pc_wrapper) {
        delete sink;
        delete persistent_callback;
        Napi::Error::New(env, "attachRawSink Error: Failed to unwrap RTCPeerConnection.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    webrtc::PeerConnectionInterface* pc_interface = pc_wrapper->pc();
    if (!pc_interface) {
        delete sink;
        delete persistent_callback;
        Napi::Error::New(env, "attachRawSink Error: The internal PeerConnectionInterface is null.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    auto* pc_impl = static_cast<webrtc::PeerConnection*>(pc_interface);

    rtc::scoped_refptr<webrtc::RtpTransceiverInterface> target_transceiver;
    for (const auto& transceiver : pc_impl->GetTransceivers()) {
        if (transceiver && transceiver->receiver() && transceiver->receiver()->track()) {
            if (transceiver->receiver()->track()->id() == trackId) {
                target_transceiver = transceiver;
                break;
            }
        }
    }

    if (!target_transceiver) {
        delete sink;
        delete persistent_callback;
        std::string error_msg = "attachRawSink Error: Failed to find a transceiver for track ID: " + trackId;
        Napi::Error::New(env, error_msg).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!target_transceiver->mid()) {
        delete sink;
        delete persistent_callback;
        Napi::Error::New(env, "attachRawSink Error: The corresponding RTCRtpTransceiver has no MID.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    auto transport_name = *target_transceiver->mid();
    auto* channel_manager = pc_impl->channel_manager();
    cricket::MediaChannel* media_channel = nullptr;

    std::string track_kind = target_transceiver->receiver()->track()->kind();
    if (track_kind == webrtc::MediaStreamTrackInterface::kAudioKind) {
        auto* voice_channel = channel_manager->GetVoiceChannel(transport_name);
        if (voice_channel) media_channel = voice_channel->media_channel();
    } else if (track_kind == webrtc::MediaStreamTrackInterface::kVideoKind) {
        auto* video_channel = channel_manager->GetVideoChannel(transport_name);
        if (video_channel) media_channel = video_channel->media_channel();
    }

    if (media_channel) {
        std::cout << "attachRawSink: MediaChannel found for " << trackId << "! Attaching sink." << std::endl;
        // ВАЖЛИВО: libwebrtc перебирає на себе володіння вказівником `sink`
        media_channel->SetRawRtpPacketSink(sink);
    } else {
        // Якщо канал не знайдено, ми повинні видалити sink самі
        delete sink;
        delete persistent_callback;
        Napi::Error::New(env, "attachRawSink Error: Failed to find MediaChannel.").ThrowAsJavaScriptException();
    }

    return env.Undefined();
}
