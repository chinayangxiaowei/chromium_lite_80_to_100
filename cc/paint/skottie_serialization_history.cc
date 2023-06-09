// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/paint/skottie_serialization_history.h"

#include <utility>

#include "base/bind.h"
#include "base/check.h"
#include "base/location.h"
#include "base/logging.h"
#include "cc/paint/skottie_wrapper.h"

namespace cc {

SkottieSerializationHistory::SkottieFrameDataId::SkottieFrameDataId(
    const SkottieFrameData& frame_data)
    : paint_image_id(frame_data.image.stable_id()),
      quality(frame_data.quality) {
  DCHECK_NE(paint_image_id, PaintImage::kInvalidId);
}

bool SkottieSerializationHistory::SkottieFrameDataId::operator==(
    const SkottieFrameDataId& other) const {
  return paint_image_id == other.paint_image_id && quality == other.quality;
}

bool SkottieSerializationHistory::SkottieFrameDataId::operator!=(
    const SkottieFrameDataId& other) const {
  return !(*this == other);
}

SkottieSerializationHistory::SkottieWrapperHistory::SkottieWrapperHistory(
    const SkottieFrameDataMap& initial_images) {
  for (const auto& image_asset_pair : initial_images) {
    DVLOG(1) << "Received initial image for asset " << image_asset_pair.first;
    last_frame_data_per_asset_.emplace(
        /*asset id*/ image_asset_pair.first,
        SkottieFrameDataId(image_asset_pair.second));
  }
}

SkottieSerializationHistory::SkottieWrapperHistory::SkottieWrapperHistory(
    const SkottieWrapperHistory& other) = default;

SkottieSerializationHistory::SkottieWrapperHistory&
SkottieSerializationHistory::SkottieWrapperHistory::operator=(
    const SkottieWrapperHistory& other) = default;

SkottieSerializationHistory::SkottieWrapperHistory::~SkottieWrapperHistory() =
    default;

void SkottieSerializationHistory::SkottieWrapperHistory::FilterNewFrameImages(
    SkottieFrameDataMap& images) {
  ++current_sequence_id_;
  auto images_iter = images.begin();
  while (images_iter != images.end()) {
    const SkottieResourceIdHash& asset_id = images_iter->first;
    const SkottieFrameData& frame_data = images_iter->second;
    SkottieFrameDataId new_frame_data_id(frame_data);
    auto [result_iterator, is_new_insertion] =
        last_frame_data_per_asset_.emplace(asset_id, new_frame_data_id);
    SkottieFrameDataId& existing_frame_data_id = result_iterator->second;

    bool asset_has_updated_frame_data =
        is_new_insertion || existing_frame_data_id != new_frame_data_id;
    if (asset_has_updated_frame_data) {
      DVLOG(1) << "New image available for asset " << asset_id;
      existing_frame_data_id = std::move(new_frame_data_id);
      ++images_iter;
    } else {
      DVLOG(4) << "No update to image for asset" << asset_id;
      images_iter = images.erase(images_iter);
    }
  }
}

SkottieSerializationHistory::SkottieSerializationHistory(int purge_period)
    : purge_period_(purge_period) {}

SkottieSerializationHistory::~SkottieSerializationHistory() = default;

void SkottieSerializationHistory::FilterNewSkottieFrameImages(
    const SkottieWrapper& skottie,
    SkottieFrameDataMap& images) {
  DCHECK(skottie.is_valid());
  base::AutoLock lock(mutex_);
  auto [result_iterator, is_new_insertion] =
      history_per_animation_.try_emplace(skottie.id(), images);
  if (is_new_insertion) {
    DVLOG(1) << "Encountered new SkottieWrapper with id " << skottie.id()
             << " and " << images.size() << " images";
  } else {
    SkottieWrapperHistory& skottie_history_found = result_iterator->second;
    skottie_history_found.FilterNewFrameImages(images);
  }
}

void SkottieSerializationHistory::RequestInactiveAnimationsPurge() {
  base::AutoLock lock(mutex_);
  // Since RequestInactiveAnimationsPurge() is called frequently in a
  // time-sensitive part of the code and purging stale history is not an urgent
  // operation, only do a purge check once in a while. (Even then, a purge check
  // actually isn't that expensive)
  //
  // If there is some odd corner case where a Skottie animation's history gets
  // purged while it is still active somehow, user functionality will not be
  // broken. The animation's history will just be recreated with a clean slate
  // the next time its state is registered with this class.
  ++purge_period_counter_;
  if (purge_period_counter_ < purge_period_)
    return;

  purge_period_counter_ = 0;
  auto animation_history_iter = history_per_animation_.begin();
  while (animation_history_iter != history_per_animation_.end()) {
    SkottieWrapperHistory& skottie_wrapper_history =
        animation_history_iter->second;
    if (skottie_wrapper_history.current_sequence_id() ==
        skottie_wrapper_history.sequence_id_at_last_purge_check()) {
      DVLOG(1) << "Purging Skottie animation with id "
               << animation_history_iter->first
               << ". No update to animation's state since last purge check.";
      animation_history_iter =
          history_per_animation_.erase(animation_history_iter);
    } else {
      skottie_wrapper_history.update_sequence_id_at_last_purge_check();
      ++animation_history_iter;
    }
  }
}

}  // namespace cc
