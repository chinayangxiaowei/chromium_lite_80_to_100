// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_USER_EDUCATION_HELP_BUBBLE_PARAMS_H_
#define CHROME_BROWSER_UI_USER_EDUCATION_HELP_BUBBLE_PARAMS_H_

#include <string>
#include <utility>
#include <vector>

#include "base/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/gfx/vector_icon_types.h"

// Mirrors most values of views::BubbleBorder::Arrow.
// All values except kNone show a visible arrow between the bubble and the
// anchor element.
enum class HelpBubbleArrow {
  kNone,  // Positions the bubble directly beneath the anchor with no arrow.
  kTopLeft,
  kTopRight,
  kBottomLeft,
  kBottomRight,
  kLeftTop,
  kRightTop,
  kLeftBottom,
  kRightBottom,
  kTopCenter,
  kBottomCenter,
  kLeftCenter,
  kRightCenter,
};

struct HelpBubbleButtonParams {
  HelpBubbleButtonParams();
  HelpBubbleButtonParams(HelpBubbleButtonParams&&);
  ~HelpBubbleButtonParams();
  HelpBubbleButtonParams& operator=(HelpBubbleButtonParams&&);

  std::u16string text;
  bool is_default = false;
  base::OnceClosure callback = base::DoNothing();
};

struct HelpBubbleParams {
  HelpBubbleParams();
  HelpBubbleParams(HelpBubbleParams&&);
  ~HelpBubbleParams();
  HelpBubbleParams& operator=(HelpBubbleParams&&);

  HelpBubbleArrow arrow = HelpBubbleArrow::kTopRight;

  std::u16string title_text;
  raw_ptr<const gfx::VectorIcon> body_icon = nullptr;
  std::u16string body_text;
  std::u16string screenreader_text;

  // Additional message to be read to screen reader users to aid in
  // navigation.
  std::u16string keyboard_navigation_hint;

  std::vector<HelpBubbleButtonParams> buttons;

  // If set to true, a close button will always be shown.
  bool force_close_button = false;

  // Determines whether a progress indicator will be displayed; if set the
  // first value is current progress and the second is max progress.
  absl::optional<std::pair<int, int>> tutorial_progress;

  // Sets the bubble timeout. If a timeout is not provided a default will
  // be used. If the timeout is 0, the bubble never times out.
  absl::optional<base::TimeDelta> timeout;

  // Called when the bubble is actively dismissed by the user, using the close
  // button or the ESC key.
  base::OnceClosure dismiss_callback = base::DoNothing();

  // Called when the bubble times out.
  base::OnceClosure timeout_callback = base::DoNothing();
};

#endif  // CHROME_BROWSER_UI_USER_EDUCATION_HELP_BUBBLE_PARAMS_H_
