/* Copyright (c) 2019 The node-webrtc project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE.md file in the root of the source tree. All contributing
 * project authors may be found in the AUTHORS file in the root of the source
 * tree.
 */
#include "src/interfaces/rtc_video_sink.h"

#include <type_traits>
#include <utility>

#include <api/video/video_source_interface.h>
#include <api/video/encoded_image.h> // Потрібно для EncodedImageBufferInterface

#include "src/converters.h"
#include "src/converters/arguments.h"
#include "src/converters/napi.h"
#include "src/dictionaries/webrtc/video_frame.h"
#include "src/functional/validation.h"
#include "src/interfaces/media_stream_track.h"
#include "src/node/events.h"

namespace node_webrtc {

Napi::FunctionReference& RTCVideoSink::constructor() {
  static Napi::FunctionReference constructor;
  return constructor;
}

RTCVideoSink::RTCVideoSink(const Napi::CallbackInfo& info)
  : AsyncObjectWrapWithLoop<RTCVideoSink>("RTCVideoSink", *this, info) {
  if (!info.IsConstructCall()) {
    Napi::TypeError::New(info.Env(), "Use the new operator to construct an RTCVideoSink.").ThrowAsJavaScriptException();
    return;
  }
  CONVERT_ARGS_OR_THROW_AND_RETURN_VOID_NAPI(info, track, rtc::scoped_refptr<webrtc::VideoTrackInterface>)

  _track = std::move(track);

  // 1. Реєструємося як слухач ДЕКОДОВАНИХ кадрів (як і раніше)
  rtc::VideoSinkWants wants;
  // Використовуємо static_cast, щоб компілятор точно знав, яку версію інтерфейсу ми передаємо
  _track->AddOrUpdateSink(static_cast<rtc::VideoSinkInterface<webrtc::VideoFrame>*>(this), wants);

  // 2. ДОДАНО: Реєструємося як слухач ЗАКОДОВАНИХ кадрів
  if (auto* source = _track->GetSource()) {
    // Також використовуємо static_cast для уникнення неоднозначності
    source->AddEncodedSink(static_cast<rtc::VideoSinkInterface<webrtc::RecordableEncodedFrame>*>(this));
  }
}

Napi::Value RTCVideoSink::GetStopped(const Napi::CallbackInfo& info) {
  CONVERT_OR_THROW_AND_RETURN_NAPI(info.Env(), _stopped, result, Napi::Value)
  return result;
}

void RTCVideoSink::Stop() {
  if (_track) {
    _stopped = true;

    // 1. Відписуємося від ДЕКОДОВАНИХ кадрів
    _track->RemoveSink(static_cast<rtc::VideoSinkInterface<webrtc::VideoFrame>*>(this));

    // 2. ДОДАНО: Відписуємося від ЗАКОДОВАНИХ кадрів
    if (auto* source = _track->GetSource()) {
      source->RemoveEncodedSink(static_cast<rtc::VideoSinkInterface<webrtc::RecordableEncodedFrame>*>(this));
    }
    _track = nullptr;
  }
  AsyncObjectWrapWithLoop<RTCVideoSink>::Stop();
}

Napi::Value RTCVideoSink::JsStop(const Napi::CallbackInfo& info) {
  Stop();
  return info.Env().Undefined();
}

// Цей метод для ДЕКОДОВАНИХ кадрів залишається без змін
void RTCVideoSink::OnFrame(const webrtc::VideoFrame& frame) {
  Dispatch(CreateCallback<RTCVideoSink>([this, frame]() {
    auto env = Env();
    Napi::HandleScope scope(env);
    auto maybeValue = From<Napi::Value>(std::make_pair(env, frame));
    if (maybeValue.IsInvalid()) {
      // TODO(mroberts): Should raise an error; although this really shouldn't happen.
      return;
    }
    auto object = Napi::Object::New(env);
    object.Set("type", Napi::String::New(env, "frame"));
    object.Set("frame", maybeValue.UnsafeFromValid());
    MakeCallback("dispatchEvent", { object });
  }));
}

// ДОДАНО: Новий метод для ЗАКОДОВАНИХ кадрів
void RTCVideoSink::OnFrame(const webrtc::RecordableEncodedFrame& frame) {
  auto buffer = frame.encoded_buffer();
  if (!buffer || buffer->size() == 0) {
    return;
  }

  // Копіюємо дані для безпечної передачі
  auto data_copy = new uint8_t[buffer->size()];
  memcpy(data_copy, buffer->data(), buffer->size());

  // Зберігаємо метадані для передачі
  auto size = buffer->size();
  bool is_key_frame = frame.is_key_frame();
  auto resolution = frame.resolution();

  Dispatch(CreateCallback<RTCVideoSink>([this, data_copy, size, is_key_frame, resolution]() {
    auto env = Env();
    Napi::HandleScope scope(env);

    // Створюємо Napi::Buffer
    auto napi_buffer = Napi::Buffer<uint8_t>::New(env, data_copy, size, [](Napi::Env, uint8_t* data) {
        delete[] data;
    });

    // Створюємо об'єкт з метаданими
    auto event_data = Napi::Object::New(env);
    event_data.Set("frame", napi_buffer);
    event_data.Set("isKeyFrame", Napi::Boolean::New(env, is_key_frame));

    auto res_obj = Napi::Object::New(env);
    res_obj.Set("width", Napi::Number::New(env, resolution.width));
    res_obj.Set("height", Napi::Number::New(env, resolution.height));
    event_data.Set("resolution", res_obj);

    // Створюємо фінальний об'єкт події
    auto event_object = Napi::Object::New(env);
    event_object.Set("type", Napi::String::New(env, "encodedframe"));
    event_object.Set("data", event_data); // Вкладаємо наші дані в поле "data"

    MakeCallback("dispatchEvent", { event_object });
  }));
}

void RTCVideoSink::Init(Napi::Env env, Napi::Object exports) {
  auto func = DefineClass(env, "RTCVideoSink", {
    InstanceAccessor("stopped", &RTCVideoSink::GetStopped, nullptr),
    InstanceMethod("stop", &RTCVideoSink::JsStop)
  });

  constructor() = Napi::Persistent(func);
  constructor().SuppressDestruct();

  exports.Set("RTCVideoSink", func);
}

}  // namespace node_webrtc
