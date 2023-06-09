// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/chromeos/google_assistant_handler.h"

#include <utility>

#include "ash/components/arc/arc_prefs.h"
#include "ash/components/arc/session/arc_service_manager.h"
#include "ash/components/audio/cras_audio_handler.h"
#include "ash/public/cpp/assistant/assistant_setup.h"
#include "ash/public/cpp/assistant/controller/assistant_controller.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/values.h"
#include "chrome/browser/ui/webui/chromeos/assistant_optin/assistant_optin_ui.h"
#include "chromeos/services/assistant/public/cpp/assistant_service.h"
#include "content/public/browser/browser_context.h"
#include "ui/gfx/geometry/rect.h"

namespace chromeos {
namespace settings {

GoogleAssistantHandler::GoogleAssistantHandler() {
  chromeos::CrasAudioHandler::Get()->AddAudioObserver(this);
}

GoogleAssistantHandler::~GoogleAssistantHandler() {
  chromeos::CrasAudioHandler::Get()->RemoveAudioObserver(this);
}

void GoogleAssistantHandler::OnJavascriptAllowed() {
  if (pending_hotword_update_) {
    OnAudioNodesChanged();
  }
}

void GoogleAssistantHandler::OnJavascriptDisallowed() {}

void GoogleAssistantHandler::OnAudioNodesChanged() {
  if (!IsJavascriptAllowed()) {
    pending_hotword_update_ = true;
    return;
  }

  pending_hotword_update_ = false;
  FireWebUIListener(
      "hotwordDeviceUpdated",
      base::Value(chromeos::CrasAudioHandler::Get()->HasHotwordDevice()));
}

void GoogleAssistantHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "showGoogleAssistantSettings",
      base::BindRepeating(
          &GoogleAssistantHandler::HandleShowGoogleAssistantSettings,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "retrainAssistantVoiceModel",
      base::BindRepeating(&GoogleAssistantHandler::HandleRetrainVoiceModel,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "syncVoiceModelStatus",
      base::BindRepeating(&GoogleAssistantHandler::HandleSyncVoiceModelStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "initializeGoogleAssistantPage",
      base::BindRepeating(&GoogleAssistantHandler::HandleInitialized,
                          base::Unretained(this)));
}

void GoogleAssistantHandler::HandleShowGoogleAssistantSettings(
    base::Value::ConstListView args) {
  CHECK_EQ(0U, args.size());
  ash::AssistantController::Get()->OpenAssistantSettings();
}

void GoogleAssistantHandler::HandleRetrainVoiceModel(
    base::Value::ConstListView args) {
  CHECK_EQ(0U, args.size());
  chromeos::AssistantOptInDialog::Show(ash::FlowType::kSpeakerIdRetrain,
                                       base::DoNothing());
}

void GoogleAssistantHandler::HandleSyncVoiceModelStatus(
    base::Value::ConstListView args) {
  CHECK_EQ(0U, args.size());

  auto* settings = assistant::AssistantSettings::Get();
  if (settings)
    settings->SyncSpeakerIdEnrollmentStatus();
}

void GoogleAssistantHandler::HandleInitialized(
    base::Value::ConstListView args) {
  CHECK_EQ(0U, args.size());
  AllowJavascript();
}

}  // namespace settings
}  // namespace chromeos
