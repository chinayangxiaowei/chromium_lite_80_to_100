// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/chromeos/device_stylus_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ash/public/cpp/stylus_utils.h"
#include "base/bind.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/ash/arc/arc_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"

namespace chromeos {
namespace settings {

namespace {

// Keys in objects passed to onNoteTakingAppsUpdated.
constexpr char kAppNameKey[] = "name";
constexpr char kAppIdKey[] = "value";
constexpr char kAppPreferredKey[] = "preferred";
constexpr char kAppLockScreenSupportKey[] = "lockScreenSupport";

}  // namespace

StylusHandler::StylusHandler() = default;
StylusHandler::~StylusHandler() = default;

void StylusHandler::RegisterMessages() {
  DCHECK(web_ui());

  // Note: initializeStylusSettings must be called before observers will be
  // added.
  web_ui()->RegisterMessageCallback(
      "initializeStylusSettings",
      base::BindRepeating(&StylusHandler::HandleInitialize,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "requestNoteTakingApps",
      base::BindRepeating(&StylusHandler::HandleRequestApps,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setPreferredNoteTakingApp",
      base::BindRepeating(&StylusHandler::HandleSetPreferredNoteTakingApp,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setPreferredNoteTakingAppEnabledOnLockScreen",
      base::BindRepeating(
          &StylusHandler::HandleSetPreferredNoteTakingAppEnabledOnLockScreen,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "showPlayStoreApps",
      base::BindRepeating(&StylusHandler::HandleShowPlayStoreApps,
                          base::Unretained(this)));
}

void StylusHandler::OnJavascriptAllowed() {
  note_observation_.Observe(NoteTakingHelper::Get());
  input_observation_.Observe(ui::DeviceDataManager::GetInstance());
}

void StylusHandler::OnJavascriptDisallowed() {
  note_observation_.Reset();
  input_observation_.Reset();
}

void StylusHandler::OnAvailableNoteTakingAppsUpdated() {
  UpdateNoteTakingApps();
}

void StylusHandler::OnPreferredNoteTakingAppUpdated(Profile* profile) {
  if (Profile::FromWebUI(web_ui()) == profile)
    UpdateNoteTakingApps();
}

void StylusHandler::OnDeviceListsComplete() {
  SendHasStylus();
}

void StylusHandler::UpdateNoteTakingApps() {
  bool waiting_for_android = false;
  note_taking_app_ids_.clear();
  base::ListValue apps_list;

  NoteTakingHelper* helper = NoteTakingHelper::Get();
  if (helper->play_store_enabled() && !helper->android_apps_received()) {
    // If Play Store is enabled but not ready yet, let the JS know so it can
    // disable the menu and display an explanatory message.
    waiting_for_android = true;
  } else {
    std::vector<NoteTakingAppInfo> available_apps =
        helper->GetAvailableApps(Profile::FromWebUI(web_ui()));
    for (const NoteTakingAppInfo& info : available_apps) {
      auto dict = std::make_unique<base::DictionaryValue>();
      dict->SetStringKey(kAppNameKey, info.name);
      dict->SetStringKey(kAppIdKey, info.app_id);
      dict->SetBoolKey(kAppPreferredKey, info.preferred);
      dict->SetIntKey(kAppLockScreenSupportKey,
                      static_cast<int>(info.lock_screen_support));
      apps_list.Append(std::move(dict));

      note_taking_app_ids_.insert(info.app_id);
    }
  }

  FireWebUIListener("onNoteTakingAppsUpdated", apps_list,
                    base::Value(waiting_for_android));
}

void StylusHandler::HandleRequestApps(base::Value::ConstListView unused_args) {
  AllowJavascript();
  UpdateNoteTakingApps();
}

void StylusHandler::HandleSetPreferredNoteTakingApp(
    base::Value::ConstListView args) {
  const std::string& app_id = args[0].GetString();

  // Sanity check: make sure that the ID we got back from WebUI is in the
  // currently-available set.
  if (!note_taking_app_ids_.count(app_id)) {
    LOG(ERROR) << "Got unknown note-taking-app ID \"" << app_id << "\"";
    return;
  }

  NoteTakingHelper::Get()->SetPreferredApp(Profile::FromWebUI(web_ui()),
                                           app_id);
}

void StylusHandler::HandleSetPreferredNoteTakingAppEnabledOnLockScreen(
    base::Value::ConstListView args) {
  bool enabled = false;
  CHECK(args[0].is_bool());
  enabled = args[0].GetBool();

  NoteTakingHelper::Get()->SetPreferredAppEnabledOnLockScreen(
      Profile::FromWebUI(web_ui()), enabled);
}

void StylusHandler::HandleInitialize(base::Value::ConstListView args) {
  AllowJavascript();
  if (ui::DeviceDataManager::GetInstance()->AreDeviceListsComplete())
    SendHasStylus();
}

void StylusHandler::SendHasStylus() {
  DCHECK(ui::DeviceDataManager::GetInstance()->AreDeviceListsComplete());
  FireWebUIListener("has-stylus-changed",
                    base::Value(ash::stylus_utils::HasStylusInput()));
}

void StylusHandler::HandleShowPlayStoreApps(base::Value::ConstListView args) {
  const std::string& apps_url = !args.empty() ? args[0].GetString() : "";
  Profile* profile = Profile::FromWebUI(web_ui());
  if (!arc::IsArcAllowedForProfile(profile)) {
    VLOG(1) << "ARC is not enabled for this profile";
    return;
  }

  DCHECK(
      apps::AppServiceProxyFactory::IsAppServiceAvailableForProfile(profile));
  apps::AppServiceProxyFactory::GetForProfile(profile)->LaunchAppWithUrl(
      arc::kPlayStoreAppId, ui::EF_NONE, GURL(apps_url),
      apps::mojom::LaunchSource::kFromChromeInternal);
}

}  // namespace settings
}  // namespace chromeos
