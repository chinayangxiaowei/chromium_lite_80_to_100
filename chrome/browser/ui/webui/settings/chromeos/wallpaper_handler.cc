// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/chromeos/wallpaper_handler.h"

#include "base/bind.h"
#include "chrome/browser/ui/ash/wallpaper_controller_client_impl.h"
#include "content/public/browser/web_ui.h"

namespace chromeos {
namespace settings {

WallpaperHandler::WallpaperHandler() = default;

WallpaperHandler::~WallpaperHandler() = default;

void WallpaperHandler::OnJavascriptAllowed() {}
void WallpaperHandler::OnJavascriptDisallowed() {}

void WallpaperHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "openWallpaperManager",
      base::BindRepeating(&WallpaperHandler::HandleOpenWallpaperManager,
                          base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "isWallpaperSettingVisible",
      base::BindRepeating(&WallpaperHandler::HandleIsWallpaperSettingVisible,
                          base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "isWallpaperPolicyControlled",
      base::BindRepeating(&WallpaperHandler::HandleIsWallpaperPolicyControlled,
                          base::Unretained(this)));
}

void WallpaperHandler::HandleIsWallpaperSettingVisible(
    base::Value::ConstListView args) {
  CHECK_EQ(args.size(), 1U);
  ResolveCallback(
      args[0],
      WallpaperControllerClientImpl::Get()->ShouldShowWallpaperSetting());
}

void WallpaperHandler::HandleIsWallpaperPolicyControlled(
    base::Value::ConstListView args) {
  CHECK_EQ(args.size(), 1U);
  bool result = WallpaperControllerClientImpl::Get()
                    ->IsActiveUserWallpaperControlledByPolicy();
  ResolveCallback(args[0], result);
}

void WallpaperHandler::HandleOpenWallpaperManager(
    base::Value::ConstListView args) {
  WallpaperControllerClientImpl::Get()->OpenWallpaperPickerIfAllowed();
}

void WallpaperHandler::ResolveCallback(const base::Value& callback_id,
                                       bool result) {
  AllowJavascript();
  ResolveJavascriptCallback(callback_id, base::Value(result));
}

}  // namespace settings
}  // namespace chromeos
