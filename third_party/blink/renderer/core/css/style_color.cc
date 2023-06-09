// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/style_color.h"

#include "third_party/blink/renderer/core/layout/layout_theme.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"

namespace blink {

Color StyleColor::Resolve(Color current_color,
                          mojom::blink::ColorScheme color_scheme,
                          bool is_forced_color) const {
  if (IsCurrentColor())
    return current_color;
  if (EffectiveColorKeyword() != CSSValueID::kInvalid ||
      (is_forced_color && IsSystemColorIncludingDeprecated()))
    return ColorFromKeyword(color_keyword_, color_scheme);
  return color_;
}

Color StyleColor::ResolveWithAlpha(Color current_color,
                                   mojom::blink::ColorScheme color_scheme,
                                   int alpha,
                                   bool is_forced_color) const {
  Color color = Resolve(current_color, color_scheme, is_forced_color);
  return Color(color.Red(), color.Green(), color.Blue(), alpha);
}

Color StyleColor::ColorFromKeyword(CSSValueID keyword,
                                   mojom::blink::ColorScheme color_scheme) {
  if (const char* value_name = getValueName(keyword)) {
    if (const NamedColor* named_color =
            FindColor(value_name, static_cast<wtf_size_t>(strlen(value_name))))
      return Color(named_color->argb_value);
  }
  return LayoutTheme::GetTheme().SystemColor(keyword, color_scheme);
}

bool StyleColor::IsColorKeyword(CSSValueID id) {
  // Named colors and color keywords:
  //
  // <named-color>
  //   'aqua', 'black', 'blue', ..., 'yellow' (CSS3: "basic color keywords")
  //   'aliceblue', ..., 'yellowgreen'        (CSS3: "extended color keywords")
  //   'transparent'
  //
  // 'currentcolor'
  //
  // <deprecated-system-color>
  //   'ActiveBorder', ..., 'WindowText'
  //
  // WebKit proprietary/internal:
  //   '-webkit-link'
  //   '-webkit-activelink'
  //   '-internal-active-list-box-selection'
  //   '-internal-active-list-box-selection-text'
  //   '-internal-inactive-list-box-selection'
  //   '-internal-inactive-list-box-selection-text'
  //   '-webkit-focus-ring-color'
  //   '-internal-quirk-inherit'
  //
  return (id >= CSSValueID::kAqua && id <= CSSValueID::kInternalQuirkInherit) ||
         (id >= CSSValueID::kAliceblue && id <= CSSValueID::kYellowgreen) ||
         id == CSSValueID::kMenu;
}

bool StyleColor::IsSystemColorIncludingDeprecated(CSSValueID id) {
  return (id >= CSSValueID::kActiveborder && id <= CSSValueID::kWindowtext) ||
         id == CSSValueID::kMenu;
}

bool StyleColor::IsSystemColor(CSSValueID id) {
  switch (id) {
    // ButtonBorder, SelectedItem, SelectedItemText, Mark, MarkText not
    // understood yet.
    case CSSValueID::kCanvas:
    case CSSValueID::kCanvastext:
    case CSSValueID::kLinktext:
    case CSSValueID::kVisitedtext:
    case CSSValueID::kActivetext:
    case CSSValueID::kButtonface:
    case CSSValueID::kButtontext:
    case CSSValueID::kField:
    case CSSValueID::kFieldtext:
    case CSSValueID::kHighlight:
    case CSSValueID::kHighlighttext:
    case CSSValueID::kGraytext:
      return true;
    default:
      return false;
  }
}

CSSValueID StyleColor::EffectiveColorKeyword() const {
  if (!RuntimeEnabledFeatures::CSSSystemColorComputeToSelfEnabled()) {
    return IsSystemColorIncludingDeprecated(color_keyword_)
               ? CSSValueID::kInvalid
               : color_keyword_;
  }
  return color_keyword_;
}

}  // namespace blink
