// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_DEVICE_STYLUS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_DEVICE_STYLUS_HANDLER_H_

#include <set>
#include <string>

#include "base/scoped_observation.h"
#include "chrome/browser/ash/note_taking_helper.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"
#include "ui/events/devices/device_data_manager.h"
#include "ui/events/devices/input_device_event_observer.h"

namespace chromeos {
namespace settings {

// Chrome OS stylus settings handler.
class StylusHandler : public ::settings::SettingsPageUIHandler,
                      public NoteTakingHelper::Observer,
                      public ui::InputDeviceEventObserver {
 public:
  StylusHandler();

  StylusHandler(const StylusHandler&) = delete;
  StylusHandler& operator=(const StylusHandler&) = delete;

  ~StylusHandler() override;

  // SettingsPageUIHandler implementation.
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // NoteTakingHelper::Observer implementation.
  void OnAvailableNoteTakingAppsUpdated() override;
  void OnPreferredNoteTakingAppUpdated(Profile* profile) override;

  // ui::InputDeviceObserver:
  void OnDeviceListsComplete() override;

 private:
  void UpdateNoteTakingApps();
  void HandleRequestApps(base::Value::ConstListView unused_args);
  void HandleSetPreferredNoteTakingApp(base::Value::ConstListView args);
  void HandleSetPreferredNoteTakingAppEnabledOnLockScreen(
      base::Value::ConstListView args);
  void HandleInitialize(base::Value::ConstListView args);

  // Enables or disables the stylus UI section.
  void SendHasStylus();

  // Called by JS to show the Play Store Android app.
  void HandleShowPlayStoreApps(base::Value::ConstListView args);

  // IDs of available note-taking apps.
  std::set<std::string> note_taking_app_ids_;

  // Observer registration.
  base::ScopedObservation<NoteTakingHelper, NoteTakingHelper::Observer>
      note_observation_{this};
  base::ScopedObservation<ui::DeviceDataManager, ui::InputDeviceEventObserver>
      input_observation_{this};
};

}  // namespace settings
}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_DEVICE_STYLUS_HANDLER_H_
