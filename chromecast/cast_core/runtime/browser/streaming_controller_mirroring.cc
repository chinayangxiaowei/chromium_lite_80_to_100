// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/cast_core/runtime/browser/streaming_controller_mirroring.h"

#include "components/cast/message_port/message_port.h"
#include "components/cast_streaming/browser/public/receiver_session.h"

namespace chromecast {

StreamingControllerMirroring::StreamingControllerMirroring(
    std::unique_ptr<cast_api_bindings::MessagePort> message_port,
    CastWebContents* cast_web_contents)
    : StreamingControllerBase(std::move(message_port), cast_web_contents) {}

StreamingControllerMirroring::~StreamingControllerMirroring() = default;

void StreamingControllerMirroring::StartPlayback(
    cast_streaming::ReceiverSession* receiver_session,
    mojo::AssociatedRemote<cast_streaming::mojom::CastStreamingReceiver>
        cast_streaming_receiver,
    mojo::AssociatedRemote<cast_streaming::mojom::RendererController>
        renderer_connection) {
  receiver_session->SetCastStreamingReceiver(
      std::move(cast_streaming_receiver));

  renderer_connection_ = std::move(renderer_connection);
  renderer_connection_->SetPlaybackController(
      renderer_controls_.BindNewPipeAndPassReceiver());
  renderer_controls_->StartPlayingFrom(base::Seconds(0));
  renderer_controls_->SetPlaybackRate(1.0);
}

void StreamingControllerMirroring::ProcessAVConstraints(
    cast_streaming::ReceiverSession::AVConstraints* constraints) {
  DCHECK(constraints);

  // Ensure remoting is disabled for this streaming session.
  DLOG_IF(INFO, constraints->remoting)
      << "Remoting configuration removed from received AVConstraints";
  constraints->remoting.reset();
}

}  // namespace chromecast
