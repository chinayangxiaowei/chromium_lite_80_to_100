// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_QUICK_UNLOCK_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_QUICK_UNLOCK_HANDLER_H_

#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"
#include "components/prefs/pref_change_registrar.h"

class PrefService;
class Profile;

namespace chromeos {
namespace settings {

// Settings WebUI handler for quick unlock settings.
class QuickUnlockHandler : public ::settings::SettingsPageUIHandler {
 public:
  QuickUnlockHandler(Profile* profile, PrefService* pref_service);
  QuickUnlockHandler(const QuickUnlockHandler&) = delete;
  QuickUnlockHandler& operator=(const QuickUnlockHandler&) = delete;
  ~QuickUnlockHandler() override;

  // SettingsPageUIHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

 private:
  void HandleRequestPinLoginState(base::Value::ConstListView args);
  void OnPinLoginAvailable(bool is_available);

  void HandleQuickUnlockDisabledByPolicy(base::Value::ConstListView args);
  void UpdateQuickUnlockDisabledByPolicy();

  Profile* profile_;
  PrefService* pref_service_;
  PrefChangeRegistrar pref_change_registrar_;

  base::WeakPtrFactory<QuickUnlockHandler> weak_ptr_factory_{this};
};

}  // namespace settings
}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_QUICK_UNLOCK_HANDLER_H_
