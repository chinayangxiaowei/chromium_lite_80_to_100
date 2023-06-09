// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/user_education/tutorial/tutorial_description.h"

#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/interaction/interaction_sequence.h"

TutorialDescription::TutorialDescription() = default;
TutorialDescription::~TutorialDescription() = default;
TutorialDescription::TutorialDescription(TutorialDescription&&) = default;
TutorialDescription& TutorialDescription::operator=(TutorialDescription&&) =
    default;

TutorialDescription::Step::Step()
    : step_type(ui::InteractionSequence::StepType::kShown),
      arrow(HelpBubbleArrow::kNone) {}
TutorialDescription::Step::~Step() = default;

TutorialDescription::Step::Step(
    absl::optional<std::u16string> title_text_,
    std::u16string body_text_,
    ui::InteractionSequence::StepType step_type_,
    ui::ElementIdentifier element_id_,
    std::string element_name_,
    HelpBubbleArrow arrow_,
    ui::CustomElementEventType event_type_,
    absl::optional<bool> must_remain_visible_,
    bool transition_only_on_event_,
    TutorialDescription::NameElementsCallback name_elements_callback_)
    : title_text(title_text_),
      body_text(body_text_),
      step_type(step_type_),
      event_type(event_type_),
      element_id(element_id_),
      element_name(element_name_),
      arrow(arrow_),
      must_remain_visible(must_remain_visible_),
      transition_only_on_event(transition_only_on_event_),
      name_elements_callback(name_elements_callback_) {
  DCHECK(!title_text.has_value() || !title_text->empty())
      << "If title is specified it must be non-empty.";
  DCHECK(!title_text.has_value() || !body_text.empty())
      << "Tutorial bubble should not have a title without body text.";
}

TutorialDescription::Step::Step(const TutorialDescription::Step&) = default;
TutorialDescription::Step& TutorialDescription::Step::operator=(
    const TutorialDescription::Step&) = default;

bool TutorialDescription::Step::Step::ShouldShowBubble() const {
  // Hide steps and steps with no body text are "hidden" steps.
  return !body_text.empty() &&
         step_type != ui::InteractionSequence::StepType::kHidden;
}
