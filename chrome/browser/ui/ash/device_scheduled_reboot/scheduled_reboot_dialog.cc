// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/device_scheduled_reboot/scheduled_reboot_dialog.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/i18n/time_formatting.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/strings/grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/dialog_model.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"

ScheduledRebootDialog::ScheduledRebootDialog(base::Time reboot_time)
    : reboot_time_(reboot_time),
      title_refresh_timer_(
          reboot_time,
          base::BindRepeating(&ScheduledRebootDialog::UpdateWindowTitle,
                              base::Unretained(this))) {}

ScheduledRebootDialog::~ScheduledRebootDialog() {
  if (dialog_delegate_) {
    dialog_delegate_->GetWidget()->CloseNow();
    dialog_delegate_ = nullptr;
  }
}

void ScheduledRebootDialog::SetRebootTime(base::Time reboot_time) {
  reboot_time_ = reboot_time;
  title_refresh_timer_.SetDeadline(reboot_time);
}

void ScheduledRebootDialog::ShowBubble(gfx::NativeView native_view,
                                       base::OnceClosure reboot_callback) {
  auto dialog_model =
      ui::DialogModel::Builder(std::make_unique<ui::DialogModelDelegate>())
          .SetTitle(BuildTitle())
          .AddOkButton(base::OnceClosure())
          .AddCancelButton(std::move(reboot_callback),
                           l10n_util::GetStringUTF16(IDS_POLICY_REBOOT_BUTTON))
          .AddBodyText(
              ui::DialogModelLabel(
                  l10n_util::GetStringFUTF16(
                      IDS_POLICY_DEVICE_SCHEDULED_REBOOT_DIALOG_MESSAGE,
                      base::TimeFormatTimeOfDay(reboot_time_),
                      base::TimeFormatShortDate(reboot_time_)))
                  .set_is_secondary())
          .OverrideShowCloseButton(false)
          .Build();

  auto bubble = views::BubbleDialogModelHost::CreateModal(
      std::move(dialog_model), ui::MODAL_TYPE_SYSTEM);
  dialog_delegate_ = bubble.get();
  bubble->SetOwnedByWidget(true);
  constrained_window::CreateBrowserModalDialogViews(std::move(bubble),
                                                    native_view)
      ->Show();
  dialog_delegate_->GetWidget()->AddObserver(this);
}

void ScheduledRebootDialog::OnWidgetClosing(views::Widget* widget) {
  widget->RemoveObserver(this);
  dialog_delegate_ = nullptr;
}

views::DialogDelegate* ScheduledRebootDialog::GetDialogDelegate() {
  return dialog_delegate_;
}

void ScheduledRebootDialog::UpdateWindowTitle() {
  if (dialog_delegate_)
    dialog_delegate_->SetTitle(BuildTitle());
}

std::u16string ScheduledRebootDialog::BuildTitle() {
  const base::TimeDelta rounded_offset =
      title_refresh_timer_.GetRoundedDeadlineDelta();
  int amount = rounded_offset.InSeconds();
  int message_id = IDS_REBOOT_SCHEDULED_TITLE_SECONDS;
  if (rounded_offset.InMinutes() >= 1) {
    amount = rounded_offset.InMinutes();
    message_id = IDS_REBOOT_SCHEDULED_TITLE_MINUTES;
  }
  return l10n_util::GetPluralStringFUTF16(message_id, amount);
}