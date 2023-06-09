// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/safe_browsing/content/renderer/phishing_classifier/scorer.h"

#include <math.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "base/memory/read_only_shared_memory_region.h"
#include "base/memory/shared_memory_mapping.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/trace_event/trace_event.h"
#include "components/safe_browsing/content/renderer/phishing_classifier/features.h"
#include "components/safe_browsing/core/common/proto/client_model.pb.h"
#include "components/safe_browsing/core/common/proto/csd.pb.h"
#include "components/safe_browsing/core/common/visual_utils.h"
#include "content/public/renderer/render_thread.h"
#include "crypto/sha2.h"
#include "third_party/skia/include/core/SkBitmap.h"

#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
#include "third_party/tflite/src/tensorflow/lite/kernels/builtin_op_kernels.h"
#include "third_party/tflite/src/tensorflow/lite/op_resolver.h"
#include "third_party/tflite_support/src/tensorflow_lite_support/cc/task/core/task_api_factory.h"
#include "third_party/tflite_support/src/tensorflow_lite_support/cc/task/vision/image_classifier.h"
#endif

namespace safe_browsing {

namespace {

#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
std::unique_ptr<tflite::MutableOpResolver> CreateOpResolver() {
  tflite::MutableOpResolver resolver;
  // The minimal set of OPs required to run the visual model.
  resolver.AddBuiltin(tflite::BuiltinOperator_ADD,
                      tflite::ops::builtin::Register_ADD());
  resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,
                      tflite::ops::builtin::Register_CONV_2D());
  resolver.AddBuiltin(tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
                      tflite::ops::builtin::Register_DEPTHWISE_CONV_2D());
  resolver.AddBuiltin(tflite::BuiltinOperator_FULLY_CONNECTED,
                      tflite::ops::builtin::Register_FULLY_CONNECTED());
  resolver.AddBuiltin(tflite::BuiltinOperator_MEAN,
                      tflite::ops::builtin::Register_MEAN());
  resolver.AddBuiltin(tflite::BuiltinOperator_SOFTMAX,
                      tflite::ops::builtin::Register_SOFTMAX());
  resolver.AddBuiltin(tflite::BuiltinOperator_DEQUANTIZE,
                      tflite::ops::builtin::Register_DEQUANTIZE());
  resolver.AddBuiltin(tflite::BuiltinOperator_QUANTIZE,
                      tflite::ops::builtin::Register_QUANTIZE());
  return std::make_unique<tflite::MutableOpResolver>(resolver);
}

std::unique_ptr<tflite::task::vision::ImageClassifier> CreateClassifier(
    std::string model_data) {
  TRACE_EVENT0("safe_browsing", "CreateTfLiteClassifier");
  tflite::task::vision::ImageClassifierOptions options;
  options.mutable_model_file_with_metadata()->set_file_content(
      std::move(model_data));
  auto statusor_classifier =
      tflite::task::vision::ImageClassifier::CreateFromOptions(
          options, CreateOpResolver());
  if (!statusor_classifier.ok()) {
    VLOG(1) << statusor_classifier.status().ToString();
    return nullptr;
  }

  return std::move(*statusor_classifier);
}

std::string GetModelInput(const SkBitmap& bitmap, int width, int height) {
  TRACE_EVENT0("safe_browsing", "GetTfLiteModelInput");
  // Use the Rec. 2020 color space, in case the user input is wide-gamut.
  sk_sp<SkColorSpace> rec2020 = SkColorSpace::MakeRGB(
      {2.22222f, 0.909672f, 0.0903276f, 0.222222f, 0.0812429f, 0, 0},
      SkNamedGamut::kRec2020);

  SkImageInfo downsampled_info = SkImageInfo::MakeN32(
      width, height, SkAlphaType::kUnpremul_SkAlphaType, rec2020);
  SkBitmap downsampled;
  if (!downsampled.tryAllocPixels(downsampled_info))
    return std::string();
  bitmap.pixmap().scalePixels(
      downsampled.pixmap(),
      SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNearest));

  // Format as an RGB buffer for input into the model
  std::string data;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      SkColor color = downsampled.getColor(x, y);
      data += static_cast<char>(SkColorGetR(color));
      data += static_cast<char>(SkColorGetG(color));
      data += static_cast<char>(SkColorGetB(color));
    }
  }

  return data;
}
#endif

}  // namespace

#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
Scorer::VisualTfliteModelHelperResult Scorer::ApplyVisualTfLiteModelHelper(
    const SkBitmap& bitmap,
    int input_width,
    int input_height,
    std::unique_ptr<base::MemoryMappedFile> visual_tflite_model) {
  VisualTfliteModelHelperResult result;
  result.visual_tflite_model = std::move(visual_tflite_model);

  TRACE_EVENT0("safe_browsing", "ApplyVisualTfLiteModel");
  base::Time before_operation = base::Time::Now();
  std::string model_data = std::string(
      reinterpret_cast<const char*>(result.visual_tflite_model->data()),
      result.visual_tflite_model->length());
  base::UmaHistogramTimes("SBClientPhishing.ApplyTfliteTime.ModelCopy",
                          base::Time::Now() - before_operation);
  before_operation = base::Time::Now();
  std::unique_ptr<tflite::task::vision::ImageClassifier> classifier =
      CreateClassifier(std::move(model_data));
  base::UmaHistogramTimes("SBClientPhishing.ApplyTfliteTime.CreateClassifier",
                          base::Time::Now() - before_operation);
  if (!classifier)
    return result;

  before_operation = base::Time::Now();
  std::string model_input = GetModelInput(bitmap, input_width, input_height);
  if (model_input.empty())
    return result;
  base::UmaHistogramTimes("SBClientPhishing.ApplyTfliteTime.GetModelInput",
                          base::Time::Now() - before_operation);

  before_operation = base::Time::Now();
  tflite::task::vision::FrameBuffer::Plane plane{
      reinterpret_cast<const tflite::uint8*>(model_input.data()),
      {3 * input_width, 3}};
  auto frame_buffer = tflite::task::vision::FrameBuffer::Create(
      {plane}, {input_width, input_height},
      tflite::task::vision::FrameBuffer::Format::kRGB,
      tflite::task::vision::FrameBuffer::Orientation::kTopLeft);
  auto statusor_result = classifier->Classify(*frame_buffer);
  base::UmaHistogramTimes("SBClientPhishing.ApplyTfliteTime.Classify",
                          base::Time::Now() - before_operation);
  if (!statusor_result.ok()) {
    VLOG(1) << statusor_result.status().ToString();
    return result;
  } else {
    std::vector<double> scores(
        statusor_result->classifications(0).classes().size());
    for (const tflite::task::vision::Class& clas :
         statusor_result->classifications(0).classes()) {
      scores[clas.index()] = clas.score();
    }
    result.scores = std::move(scores);
    return result;
  }
}

void Scorer::OnVisualTfLiteModelComplete(
    base::OnceCallback<void(std::vector<double>)> callback,
    VisualTfliteModelHelperResult result) {
  visual_tflite_model_ = std::move(result.visual_tflite_model);
  std::move(callback).Run(result.scores);
}
#endif

double Scorer::LogOdds2Prob(double log_odds) {
  // 709 = floor(1023*ln(2)).  2**1023 is the largest finite double.
  // Small log odds aren't a problem.  as the odds will be 0.  It's only
  // when we get +infinity for the odds, that odds/(odds+1) would be NaN.
  if (log_odds >= 709) {
    return 1.0;
  }
  double odds = exp(log_odds);
  return odds / (odds + 1.0);
}

Scorer::Scorer() = default;
Scorer::~Scorer() = default;

Scorer::VisualTfliteModelHelperResult::VisualTfliteModelHelperResult() =
    default;
Scorer::VisualTfliteModelHelperResult::~VisualTfliteModelHelperResult() =
    default;
Scorer::VisualTfliteModelHelperResult::VisualTfliteModelHelperResult(
    VisualTfliteModelHelperResult&&) = default;
Scorer::VisualTfliteModelHelperResult&
Scorer::VisualTfliteModelHelperResult::operator=(
    VisualTfliteModelHelperResult&&) = default;

}  // namespace safe_browsing
