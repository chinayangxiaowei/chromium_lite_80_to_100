// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_PARENTAL_CONTROLS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_PARENTAL_CONTROLS_HANDLER_H_

#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"

class Profile;

namespace chromeos {
namespace settings {

// Chrome "Parental Controls" settings page UI handler.
class ParentalControlsHandler : public ::settings::SettingsPageUIHandler {
 public:
  explicit ParentalControlsHandler(Profile* profile);

  ParentalControlsHandler(const ParentalControlsHandler&) = delete;
  ParentalControlsHandler& operator=(const ParentalControlsHandler&) = delete;

  ~ParentalControlsHandler() override;

 protected:
  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  // ::settings::SettingsPageUIHandler:
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // Callbacks for handling chrome.send() events.
  void HandleShowAddSupervisionDialog(base::Value::ConstListView args);
  void HandleLaunchFamilyLinkSettings(base::Value::ConstListView args);

  Profile* profile_;
};

}  // namespace settings
}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_PARENTAL_CONTROLS_HANDLER_H_
