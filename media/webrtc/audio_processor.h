// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_WEBRTC_AUDIO_PROCESSOR_H_
#define MEDIA_WEBRTC_AUDIO_PROCESSOR_H_

#include <memory>

#include "base/component_export.h"
#include "base/files/file.h"
#include "base/memory/scoped_refptr.h"
#include "base/sequence_checker.h"
#include "base/thread_annotations.h"
#include "base/time/time.h"
#include "media/base/audio_parameters.h"
#include "media/base/audio_processing.h"
#include "media/webrtc/audio_delay_stats_reporter.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/webrtc/modules/audio_processing/include/audio_processing.h"
#include "third_party/webrtc/modules/audio_processing/include/audio_processing_statistics.h"
#include "third_party/webrtc/rtc_base/task_queue.h"

namespace media {
class AudioBus;
class AudioProcessorCaptureBus;
class AudioProcessorCaptureFifo;

// This class applies audio processing effects such as echo cancellation and
// noise suppression to input capture audio (such as a microphone signal).
// Effects are applied based on configuration from AudioProcessingSettings, and
// mainly rely on an instance of the webrtc::AudioProcessing module (APM) owned
// by the AudioProcessor.
//
// The AudioProcessor can handle up to three threads (in practice, sequences):
// - An owning sequence, which performs construction, destruction, diagnostic
// recordings, and similar signals.
// - A capture thread, which calls ProcessCapturedAudio().
// - A playout thread, which calls OnPlayoutData().
//
// All member functions must be called on the owning sequence unless
// specifically documented otherwise.
//
// Thread-safe exceptions to this scheme are explicitly documented as such.
class COMPONENT_EXPORT(MEDIA_WEBRTC) AudioProcessor {
 public:
  // Callback for consuming processed capture audio.
  // |audio_bus| contains the most recent processed capture audio.
  // |new_volume| specifies a new microphone volume from the AGC. The new
  // microphone volume range is [0.0, 1.0], and is only set if the microphone
  // volume should be adjusted.
  // Called on the capture thread.
  using DeliverProcessedAudioCallback =
      base::RepeatingCallback<void(const media::AudioBus& audio_bus,
                                   base::TimeTicks audio_capture_time,
                                   absl::optional<double> new_volume)>;

  using LogCallback = base::RepeatingCallback<void(base::StringPiece)>;

  // |deliver_processed_audio_callback| is used to deliver frames of processed
  // capture audio, from ProcessCapturedAudio(), and has to be valid for as long
  // as ProcessCapturedAudio() may be called.
  // |log_callback| is used for logging messages on the owning sequence.
  // |input_format| specifies the format of the incoming capture data.
  // |output_format| specifies the output format. If
  // |settings.NeedWebrtcAudioProcessing()| is true, then the output must be in
  // 10 ms chunks.
  AudioProcessor(DeliverProcessedAudioCallback deliver_processed_audio_callback,
                 LogCallback log_callback,
                 const AudioProcessingSettings& settings,
                 const media::AudioParameters& input_format,
                 const media::AudioParameters& output_format);

  ~AudioProcessor();

  AudioProcessor(const AudioProcessor&) = delete;
  AudioProcessor& operator=(const AudioProcessor&) = delete;

  // Processes and delivers capture audio in chunks of <= 10 ms to
  // |deliver_processed_audio_callback_|: Each call to ProcessCapturedAudio()
  // method triggers zero or more calls to |deliver_processed_audio_callback_|,
  // depending on internal FIFO size and content. |num_preferred_channels| is
  // the highest number of channels that any sink is interested in. This can be
  // different from the number of channels in the output format. A value of -1
  // means an unknown number. If |settings_.multi_channel_capture_processing| is
  // true, the number of channels of the output of the Audio Processing Module
  // (APM) will be equal to the highest observed value of num_preferred_channels
  // as long as it does not exceed the number of channels of the output format.
  // |volume| specifies the current microphone volume, in the range [0.0, 1.0].
  // Must be called on the capture thread.
  void ProcessCapturedAudio(const media::AudioBus& audio_source,
                            base::TimeTicks audio_capture_time,
                            int num_preferred_channels,
                            double volume,
                            bool key_pressed);

  // Processes playout audio. |audio_bus| must contain |sample_rate/100| samples
  // per channel.
  // Must be called on the playout thread.
  void OnPlayoutData(const media::AudioBus& audio_bus,
                     int sample_rate,
                     base::TimeDelta audio_delay);

  // The format of the processed capture output audio from the processor;
  // constant throughout AudioProcessor lifetime.
  const media::AudioParameters& OutputFormat() const;

  // Accessor to check if WebRTC audio processing is enabled or not.
  bool has_webrtc_audio_processing() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
    return !!webrtc_audio_processing_;
  }

  // Instructs the Audio Processing Module (APM) to reduce its complexity when
  // |muted| is true. This mode is triggered when all audio tracks are disabled.
  // The default APM complexity mode is restored by |muted| set to false.
  void SetOutputWillBeMuted(bool muted);

  // Starts a new diagnostic audio recording (aecdump). If an aecdump recording
  // is already ongoing, it is stopped before starting the new one.
  void OnStartDump(base::File dump_file);

  // Stops any ongoing aecdump.
  void OnStopDump();

  // Returns statistics from the WebRTC audio processing module. Requires that
  // WebRTC audio processing is enabled.
  // May be called on any thread.
  webrtc::AudioProcessingStats GetStats();

  absl::optional<webrtc::AudioProcessing::Config>
  GetAudioProcessingModuleConfigForTesting() const {
    if (webrtc_audio_processing_) {
      return webrtc_audio_processing_->GetConfig();
    }
    return absl::nullopt;
  }

  // Format of input to ProcessCapturedAudio().
  const media::AudioParameters& GetInputFormatForTesting() const {
    return input_format_;
  }

  // Returns an output format that minimizes delay and resampling for a given
  // input format.
  static AudioParameters GetDefaultOutputFormat(
      const AudioParameters& input_format,
      const AudioProcessingSettings& settings);

 private:
  friend class AudioProcessorTest;

  // Called by ProcessCapturedAudio().
  // Returns the new microphone volume in the range of |0.0, 1.0], or unset if
  // the volume should not be updated.
  // |num_preferred_channels| is the highest number of channels that any sink is
  // interested in. This can be different from the number of channels in the
  // output format. A value of -1 means an unknown number. If
  // |settings_.multi_channel_capture_processing| is true, the number of
  // channels of the output of the Audio Processing Module (APM) will be equal
  // to the highest observed value of num_preferred_channels as long as it does
  // not exceed the number of channels of the output format.
  // Called on the capture thread.
  absl::optional<double> ProcessData(const float* const* process_ptrs,
                                     int process_frames,
                                     base::TimeDelta capture_delay,
                                     double volume,
                                     bool key_pressed,
                                     int num_preferred_channels,
                                     float* const* output_ptrs);

  void SendLogMessage(const std::string& message)
      VALID_CONTEXT_REQUIRED(owning_sequence_);

  SEQUENCE_CHECKER(owning_sequence_);

  const AudioProcessingSettings settings_;

  // The WebRTC audio processing module (APM). Performs the bulk of the audio
  // processing and resampling algorithms.
  const rtc::scoped_refptr<webrtc::AudioProcessing> webrtc_audio_processing_;

  // Members accessed only by the owning sequence:

  // Used by SendLogMessage.
  const LogCallback log_callback_ GUARDED_BY_CONTEXT(owning_sequence_);

  // Low-priority task queue for doing AEC dump recordings. It has to
  // created/destroyed on the same sequence and it must outlive
  // any aecdump recording in |webrtc_audio_processing_|.
  std::unique_ptr<rtc::TaskQueue> worker_queue_
      GUARDED_BY_CONTEXT(owning_sequence_);

  // Cached value for the playout delay latency. Accessed on both capture and
  // playout threads.
  std::atomic<base::TimeDelta> playout_delay_{base::TimeDelta()};

  // Members configured on the owning sequence in the constructor and
  // used on the capture thread:

  // FIFO to provide capture audio in chunks of up to 10 ms.
  std::unique_ptr<AudioProcessorCaptureFifo> capture_fifo_;

  // Receives APM processing output.
  std::unique_ptr<AudioProcessorCaptureBus> output_bus_;

  // Input and output formats for capture processing.
  const media::AudioParameters input_format_;
  const media::AudioParameters output_format_;

  // Members accessed only on the capture thread:

  // Consumer of processed capture audio in ProcessCapturedAudio().
  const DeliverProcessedAudioCallback deliver_processed_audio_callback_;

  // Observed maximum number of preferred output channels. Used for not
  // performing audio processing on more channels than the sinks are interested
  // in. The value is a maximum over time and can increase but never decrease.
  // If |settings_.multi_channel_capture_processing| is true, Audio Processing
  // Module (APM) will output max_num_preferred_output_channels_ channels as
  // long as it does not exceed the number of channels of the output format.
  int max_num_preferred_output_channels_ = 1;

  // For reporting audio delay stats.
  media::AudioDelayStatsReporter audio_delay_stats_reporter_;

  // Members accessed only on the playout thread:

  // Indicates whether the audio processor playout signal has ever had
  // asymmetric left and right channel content.
  bool assume_upmixed_mono_playout_ = true;

  // Counters to avoid excessively logging errors on a real-time thread.
  size_t unsupported_buffer_size_log_count_ = 0;
  size_t apm_playout_error_code_log_count_ = 0;
  size_t large_delay_log_count_ = 0;
};

}  // namespace media

#endif  // MEDIA_WEBRTC_AUDIO_PROCESSOR_H_
