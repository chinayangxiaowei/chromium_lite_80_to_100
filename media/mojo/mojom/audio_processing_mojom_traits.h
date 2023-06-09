// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef MEDIA_MOJO_MOJOM_AUDIO_PROCESSING_MOJOM_TRAITS_H_
#define MEDIA_MOJO_MOJOM_AUDIO_PROCESSING_MOJOM_TRAITS_H_

#include "media/base/audio_processing.h"
#include "media/mojo/mojom/audio_processing.mojom.h"
#include "mojo/public/cpp/bindings/struct_traits.h"

namespace mojo {

template <>
struct StructTraits<media::mojom::AudioProcessingSettingsDataView,
                    media::AudioProcessingSettings> {
 public:
  static bool echo_cancellation(const media::AudioProcessingSettings& s) {
    return s.echo_cancellation;
  }
  static bool noise_suppression(const media::AudioProcessingSettings& s) {
    return s.noise_suppression;
  }
  static bool transient_noise_suppression(
      const media::AudioProcessingSettings& s) {
    return s.transient_noise_suppression;
  }
  static bool automatic_gain_control(const media::AudioProcessingSettings& s) {
    return s.automatic_gain_control;
  }
  static bool experimental_automatic_gain_control(
      const media::AudioProcessingSettings& s) {
    return s.experimental_automatic_gain_control;
  }
  static bool high_pass_filter(const media::AudioProcessingSettings& s) {
    return s.high_pass_filter;
  }
  static bool multi_channel_capture_processing(
      const media::AudioProcessingSettings& s) {
    return s.multi_channel_capture_processing;
  }
  static bool stereo_mirroring(const media::AudioProcessingSettings& s) {
    return s.stereo_mirroring;
  }
  static bool force_apm_creation(const media::AudioProcessingSettings& s) {
    return s.force_apm_creation;
  }
  static bool Read(media::mojom::AudioProcessingSettingsDataView input,
                   media::AudioProcessingSettings* out_settings);
};

}  // namespace mojo

#endif  // MEDIA_MOJO_MOJOM_AUDIO_PROCESSING_MOJOM_TRAITS_H_
