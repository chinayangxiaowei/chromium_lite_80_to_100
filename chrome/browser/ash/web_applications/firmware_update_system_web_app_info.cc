// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/web_applications/firmware_update_system_web_app_info.h"

#include <memory>

#include "ash/constants/ash_features.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/webui/firmware_update_ui/url_constants.h"
#include "ash/webui/grit/ash_firmware_update_app_resources.h"
#include "chrome/browser/ash/web_applications/system_web_app_install_utils.h"
#include "chrome/browser/web_applications/system_web_apps/system_web_app_types.h"
#include "chrome/browser/web_applications/web_app_install_info.h"
#include "third_party/blink/public/mojom/manifest/display_mode.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/ui_base_features.h"
#include "ui/display/screen.h"

namespace {
// The Firmware Update SWA window will be a fixed 600px * 640px portal per
// the specification.
constexpr int kFirmwareUpdateAppDefaultWidth = 600;
constexpr int kFirmwareUpdateAppDefaultHeight = 640;
}  // namespace

// TODO(michaelcheco): Update to correct icon.
std::unique_ptr<WebAppInstallInfo>
CreateWebAppInfoForFirmwareUpdateSystemWebApp() {
  auto info = std::make_unique<WebAppInstallInfo>();
  info->start_url = GURL(ash::kChromeUIFirmwareUpdateAppURL);
  info->scope = GURL(ash::kChromeUIFirmwareUpdateAppURL);
  info->title = l10n_util::GetStringUTF16(IDS_ASH_FIRMWARE_UPDATE_APP_TITLE);
  web_app::CreateIconInfoForSystemWebApp(
      info->start_url,
      {{"app_icon_192.png", 192, IDR_ASH_FIRMWARE_UPDATE_APP_APP_ICON_192_PNG}},
      *info);
  info->display_mode = blink::mojom::DisplayMode::kStandalone;
  info->user_display_mode = blink::mojom::DisplayMode::kStandalone;

  return info;
}

gfx::Rect GetDefaultBoundsForFirmwareUpdateApp(Browser*) {
  gfx::Rect bounds =
      display::Screen::GetScreen()->GetDisplayForNewWindows().work_area();
  bounds.ClampToCenteredSize(
      {kFirmwareUpdateAppDefaultWidth, kFirmwareUpdateAppDefaultHeight});
  return bounds;
}

FirmwareUpdateSystemAppDelegate::FirmwareUpdateSystemAppDelegate(
    Profile* profile)
    : web_app::SystemWebAppDelegate(web_app::SystemAppType::FIRMWARE_UPDATE,
                                    "FirmwareUpdate",
                                    GURL(ash::kChromeUIFirmwareUpdateAppURL),
                                    profile) {}

std::unique_ptr<WebAppInstallInfo>
FirmwareUpdateSystemAppDelegate::GetWebAppInfo() const {
  return CreateWebAppInfoForFirmwareUpdateSystemWebApp();
}

bool FirmwareUpdateSystemAppDelegate::IsAppEnabled() const {
  return ash::features::IsFirmwareUpdaterAppEnabled();
}

bool FirmwareUpdateSystemAppDelegate::ShouldAllowMaximize() const {
  return false;
}

bool FirmwareUpdateSystemAppDelegate::ShouldAllowResize() const {
  return false;
}

bool FirmwareUpdateSystemAppDelegate::ShouldShowInLauncher() const {
  return false;
}

bool FirmwareUpdateSystemAppDelegate::ShouldShowInSearch() const {
  return false;
}

gfx::Rect FirmwareUpdateSystemAppDelegate::GetDefaultBounds(
    Browser* browser) const {
  return GetDefaultBoundsForFirmwareUpdateApp(browser);
}
