// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/display/util/display_util.h"

#include <stddef.h>

#include "base/containers/contains.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "ui/display/util/edid_parser.h"

namespace display {

namespace {

// A list of bogus sizes in mm that should be ignored.
// See crbug.com/136533. The first element maintains the minimum
// size required to be valid size.
const int kInvalidDisplaySizeList[][2] = {
    {40, 30},
    {50, 40},
    {160, 90},
    {160, 100},
};

// Used in the GetColorSpaceFromEdid function to collect data on whether the
// color space extracted from an EDID blob passed the sanity checks.
void EmitEdidColorSpaceChecksOutcomeUma(EdidColorSpaceChecksOutcome outcome) {
  base::UmaHistogramEnumeration("DrmUtil.GetColorSpaceFromEdid.ChecksOutcome",
                                outcome);
}

// Returns true if each and all matrix values are within |epsilon| distance.
bool NearlyEqual(const skcms_Matrix3x3& lhs,
                 const skcms_Matrix3x3& rhs,
                 float epsilon) {
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      if (std::abs(lhs.vals[r][c] - rhs.vals[r][c]) > epsilon)
        return false;
    }
  }
  return true;
}

}  // namespace

bool IsDisplaySizeValid(const gfx::Size& physical_size) {
  // Ignore if the reported display is smaller than minimum size.
  if (physical_size.width() <= kInvalidDisplaySizeList[0][0] ||
      physical_size.height() <= kInvalidDisplaySizeList[0][1]) {
    VLOG(1) << "Smaller than minimum display size";
    return false;
  }
  for (size_t i = 1; i < base::size(kInvalidDisplaySizeList); ++i) {
    const gfx::Size size(kInvalidDisplaySizeList[i][0],
                         kInvalidDisplaySizeList[i][1]);
    if (physical_size == size) {
      VLOG(1) << "Invalid display size detected:" << size.ToString();
      return false;
    }
  }
  return true;
}

int64_t GenerateDisplayID(uint16_t manufacturer_id,
                          uint32_t product_code_hash,
                          uint8_t output_index) {
  return ((static_cast<int64_t>(manufacturer_id) << 40) |
          (static_cast<int64_t>(product_code_hash) << 8) | output_index);
}

gfx::ColorSpace GetColorSpaceFromEdid(const display::EdidParser& edid_parser) {
  const SkColorSpacePrimaries primaries = edid_parser.primaries();

  // Sanity check: primaries should verify By <= Ry <= Gy, Bx <= Rx and Gx <=
  // Rx, to guarantee that the R, G and B colors are each in the correct region.
  if (!(primaries.fBX <= primaries.fRX && primaries.fGX <= primaries.fRX &&
        primaries.fBY <= primaries.fRY && primaries.fRY <= primaries.fGY)) {
    EmitEdidColorSpaceChecksOutcomeUma(
        EdidColorSpaceChecksOutcome::kErrorBadCoordinates);
    return gfx::ColorSpace();
  }

  // Sanity check: the area spawned by the primaries' triangle is too small,
  // i.e. less than half the surface of the triangle spawned by sRGB/BT.709.
  constexpr double kBT709PrimariesArea = 0.0954;
  const float primaries_area_twice =
      (primaries.fRX * primaries.fGY) + (primaries.fBX * primaries.fRY) +
      (primaries.fGX * primaries.fBY) - (primaries.fBX * primaries.fGY) -
      (primaries.fGX * primaries.fRY) - (primaries.fRX * primaries.fBY);
  if (primaries_area_twice < kBT709PrimariesArea) {
    EmitEdidColorSpaceChecksOutcomeUma(
        EdidColorSpaceChecksOutcome::kErrorPrimariesAreaTooSmall);
    return gfx::ColorSpace();
  }

  // Sanity check: https://crbug.com/809909, the blue primary coordinates should
  // not be too far left/upwards of the expected location (namely [0.15, 0.06]
  // for sRGB/ BT.709/ Adobe RGB/ DCI-P3, and [0.131, 0.046] for BT.2020).
  constexpr float kExpectedBluePrimaryX = 0.15f;
  constexpr float kBluePrimaryXDelta = 0.02f;
  constexpr float kExpectedBluePrimaryY = 0.06f;
  constexpr float kBluePrimaryYDelta = 0.031f;
  const bool is_blue_primary_broken =
      (std::abs(primaries.fBX - kExpectedBluePrimaryX) > kBluePrimaryXDelta) ||
      (std::abs(primaries.fBY - kExpectedBluePrimaryY) > kBluePrimaryYDelta);
  if (is_blue_primary_broken) {
    EmitEdidColorSpaceChecksOutcomeUma(
        EdidColorSpaceChecksOutcome::kErrorBluePrimaryIsBroken);
    return gfx::ColorSpace();
  }

  skcms_Matrix3x3 primaries_matrix;
  if (!primaries.toXYZD50(&primaries_matrix)) {
    EmitEdidColorSpaceChecksOutcomeUma(
        EdidColorSpaceChecksOutcome::kErrorCannotExtractToXYZD50);
    return gfx::ColorSpace();
  }

  // Snap the primaries to those of BT.709/sRGB for performance purposes, see
  // crbug.com/1073467. kPrimariesTolerance is an educated guess from various
  // ChromeOS panels observations.
  auto color_space_primaries = gfx::ColorSpace::PrimaryID::INVALID;
  constexpr float kPrimariesTolerance = 0.025;
  if (NearlyEqual(primaries_matrix, SkNamedGamut::kSRGB, kPrimariesTolerance))
    color_space_primaries = gfx::ColorSpace::PrimaryID::BT709;

  const float gamma = edid_parser.gamma();
  if (gamma < 1.0f) {
    EmitEdidColorSpaceChecksOutcomeUma(
        EdidColorSpaceChecksOutcome::kErrorBadGamma);
    return gfx::ColorSpace();
  }
  EmitEdidColorSpaceChecksOutcomeUma(EdidColorSpaceChecksOutcome::kSuccess);

  auto transfer_id = gfx::ColorSpace::TransferID::INVALID;
  if (base::Contains(edid_parser.supported_color_primary_ids(),
                     gfx::ColorSpace::PrimaryID::BT2020)) {
    if (base::Contains(edid_parser.supported_color_transfer_ids(),
                       gfx::ColorSpace::TransferID::PQ)) {
      transfer_id = gfx::ColorSpace::TransferID::PQ;
    } else if (base::Contains(edid_parser.supported_color_transfer_ids(),
                              gfx::ColorSpace::TransferID::HLG)) {
      transfer_id = gfx::ColorSpace::TransferID::HLG;
    }
  } else if (gamma == 2.2f) {
    transfer_id = gfx::ColorSpace::TransferID::GAMMA22;
  } else if (gamma == 2.4f) {
    transfer_id = gfx::ColorSpace::TransferID::GAMMA24;
  }

  // Prefer to return a name-based ColorSpace to ease subsequent calculations.
  if (transfer_id != gfx::ColorSpace::TransferID::INVALID) {
    if (color_space_primaries != gfx::ColorSpace::PrimaryID::INVALID)
      return gfx::ColorSpace(color_space_primaries, transfer_id);
    return gfx::ColorSpace::CreateCustom(primaries_matrix, transfer_id);
  }

  skcms_TransferFunction transfer = {gamma, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f};
  if (color_space_primaries == gfx::ColorSpace::PrimaryID::INVALID)
    return gfx::ColorSpace::CreateCustom(primaries_matrix, transfer);
  return gfx::ColorSpace(
      color_space_primaries, gfx::ColorSpace::TransferID::CUSTOM,
      gfx::ColorSpace::MatrixID::RGB, gfx::ColorSpace::RangeID::FULL,
      /*custom_primary_matrix=*/nullptr, &transfer);
}

}  // namespace display
