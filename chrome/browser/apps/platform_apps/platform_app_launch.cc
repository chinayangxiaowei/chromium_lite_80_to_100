// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/platform_apps/platform_app_launch.h"

#include "build/build_config.h"
#include "chrome/browser/apps/app_service/app_launch_params.h"
#include "chrome/browser/apps/app_service/launch_utils.h"
#include "chrome/browser/extensions/launch_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_metrics.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/web_applications/extension_status_utils.h"
#include "chrome/common/webui_url_constants.h"
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

namespace apps {

namespace {

// Return true if launch for |app_id| is allowed.  Set
// |out_app| to the app to open, and |out_launch_container|
// to the type of window into which the app should be open.
bool GetAppLaunchContainer(Profile* profile,
                           const std::string& app_id,
                           const extensions::Extension** out_app,
                           apps::mojom::LaunchContainer* out_launch_container) {
  const extensions::Extension* app =
      extensions::ExtensionRegistry::Get(profile)->enabled_extensions().GetByID(
          app_id);
  // The app with id |app_id| may have been uninstalled.
  if (!app)
    return false;

  // Don't launch platform apps in incognito mode.
  if (profile->IsOffTheRecord() && app->is_platform_app())
    return false;

  // Look at preferences to find the right launch container. If no
  // preference is set, launch as a window.
  apps::mojom::LaunchContainer launch_container =
      extensions::GetLaunchContainer(extensions::ExtensionPrefs::Get(profile),
                                     app);

  *out_app = app;
  *out_launch_container = launch_container;
  return true;
}

const extensions::Extension* GetPlatformApp(Profile* profile,
                                            const std::string& app_id) {
  const extensions::Extension* app =
      extensions::ExtensionRegistry::Get(profile)->GetExtensionById(
          app_id, extensions::ExtensionRegistry::EVERYTHING);
  return app && app->is_platform_app() ? app : nullptr;
}

void RecordCmdLineAppHistogram(extensions::Manifest::Type app_type) {
  extensions::RecordAppLaunchType(extension_misc::APP_LAUNCH_CMD_LINE_APP,
                                  app_type);
}

}  // namespace

bool OpenExtensionApplicationWindow(Profile* profile,
                                    const std::string& app_id,
                                    const base::CommandLine& command_line,
                                    const base::FilePath& current_directory) {
  apps::mojom::LaunchContainer launch_container;
  const extensions::Extension* app;
  if (!GetAppLaunchContainer(profile, app_id, &app, &launch_container))
    return false;

  if (launch_container == apps::mojom::LaunchContainer::kLaunchContainerTab)
    return false;

  RecordCmdLineAppHistogram(app->GetType());

  apps::AppLaunchParams params(app_id, launch_container,
                               WindowOpenDisposition::NEW_WINDOW,
                               apps::mojom::LaunchSource::kFromCommandLine);
  params.command_line = command_line;
  params.current_directory = current_directory;

  content::WebContents* tab_in_app_window =
      ::OpenApplication(profile, std::move(params));

  // Platform apps fire off a launch event which may or may not open a window.
  return tab_in_app_window != nullptr || ::CanLaunchViaEvent(app);
}

bool OpenExtensionApplicationTab(Profile* profile, const std::string& app_id) {
  apps::mojom::LaunchContainer launch_container;
  const extensions::Extension* app;
  if (!GetAppLaunchContainer(profile, app_id, &app, &launch_container))
    return false;

  // If the user doesn't want to open a tab, fail.
  if (launch_container != apps::mojom::LaunchContainer::kLaunchContainerTab)
    return false;

  RecordCmdLineAppHistogram(app->GetType());

  content::WebContents* app_tab = ::OpenApplication(
      profile, apps::AppLaunchParams(
                   app_id, apps::mojom::LaunchContainer::kLaunchContainerTab,
                   WindowOpenDisposition::NEW_FOREGROUND_TAB,
                   apps::mojom::LaunchSource::kFromCommandLine));
  return app_tab != nullptr;
}

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
bool OpenDeprecatedApplicationPrompt(Profile* profile,
                                     const std::string& app_id) {
  if (!extensions::IsExtensionUnsupportedDeprecatedApp(profile, app_id))
    return false;

  Browser::CreateParams create_params(profile, /*user_gesture=*/false);
  Browser* browser = Browser::Create(create_params);

  NavigateParams params(browser, GURL(chrome::kChromeUIAppsURL),
                        ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  params.tabstrip_add_types = TabStripModel::ADD_ACTIVE;
  Navigate(&params);

  browser->window()->Show();

  // TODO(crbug.com/1225779): Show the deprecated apps dialog.
  return true;
}
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

bool OpenExtensionApplicationWithReenablePrompt(
    Profile* profile,
    const std::string& app_id,
    const base::CommandLine& command_line,
    const base::FilePath& current_directory) {
  if (!GetPlatformApp(profile, app_id))
    return false;

  RecordCmdLineAppHistogram(extensions::Manifest::TYPE_PLATFORM_APP);
  apps::AppLaunchParams params(
      app_id, apps::mojom::LaunchContainer::kLaunchContainerNone,
      WindowOpenDisposition::NEW_WINDOW,
      apps::mojom::LaunchSource::kFromCommandLine);
  params.command_line = command_line;
  params.current_directory = current_directory;
  ::OpenApplicationWithReenablePrompt(profile, std::move(params));
  return true;
}

content::WebContents* OpenExtensionAppShortcutWindow(Profile* profile,
                                                     const GURL& url) {
  const extensions::Extension* app = extensions::ExtensionRegistry::Get(profile)
                                         ->enabled_extensions()
                                         .GetAppByURL(url);
  if (app) {
    RecordCmdLineAppHistogram(app->GetType());
  } else {
    extensions::RecordAppLaunchType(
        extension_misc::APP_LAUNCH_CMD_LINE_APP_LEGACY,
        extensions::Manifest::TYPE_HOSTED_APP);
  }

  return ::OpenAppShortcutWindow(profile, url);
}

void RecordExtensionAppLaunchOnTabRestored(Profile* profile, const GURL& url) {
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(profile)
          ->enabled_extensions()
          .GetAppByURL(url);
  if (!extension)
    return;

  extensions::RecordAppLaunchType(
      extension_misc::APP_LAUNCH_NTP_RECENTLY_CLOSED, extension->GetType());
}

}  // namespace apps
