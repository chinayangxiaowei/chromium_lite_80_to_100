// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/preinstalled_web_app_manager.h"

#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/containers/cxx20_erase.h"
#include "base/feature_list.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_file_value_serializer.h"
#include "base/json/json_reader.h"
#include "base/memory/scoped_refptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/task/post_task.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/apps/user_type_filter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/extension_status_utils.h"
#include "chrome/browser/web_applications/externally_installed_web_app_prefs.h"
#include "chrome/browser/web_applications/externally_managed_app_manager.h"
#include "chrome/browser/web_applications/file_utils_wrapper.h"
#include "chrome/browser/web_applications/preinstalled_app_install_features.h"
#include "chrome/browser/web_applications/preinstalled_web_app_utils.h"
#include "chrome/browser/web_applications/preinstalled_web_apps/preinstalled_web_apps.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/web_applications/web_app_install_utils.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/browser/web_applications/web_app_ui_manager.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "components/ntp_tiles/most_visited_sites.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/services/app_service/public/cpp/app_registry_cache.h"
#include "components/services/app_service/public/cpp/types_util.h"
#include "components/version_info/version_info.h"
#include "components/webapps/browser/install_result_code.h"
#include "components/webapps/common/constants.h"
#include "content/public/browser/browser_thread.h"
#include "third_party/abseil-cpp/absl/types/variant.h"
#include "ui/events/devices/device_data_manager.h"
#include "ui/events/devices/touchscreen_device.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/components/arc/arc_util.h"
#include "ash/constants/ash_switches.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

namespace web_app {

namespace {

#if BUILDFLAG(IS_CHROMEOS)
// The sub-directory of the extensions directory in which to scan for external
// web apps (as opposed to external extensions or external ARC apps).
const base::FilePath::CharType kWebAppsSubDirectory[] =
    FILE_PATH_LITERAL("web_apps");
#endif  // BUILDFLAG(IS_CHROMEOS)

bool g_skip_startup_for_testing_ = false;
bool g_bypass_offline_manifest_requirement_for_testing_ = false;
const base::FilePath* g_config_dir_for_testing = nullptr;
const std::vector<base::Value>* g_configs_for_testing = nullptr;
FileUtilsWrapper* g_file_utils_for_testing = nullptr;

const char kHistogramMigrationDisabledReason[] =
    "WebApp.Preinstalled.DisabledReason";

// These values are reported to UMA, do not modify them.
enum class DisabledReason {
  kNotDisabled = 0,
  kPreinstalledAppsNotEnabled = 1,
  kUserTypeNotAllowed = 2,
  kGatedFeatureNotEnabled = 3,
  kGatedFeatureNotEnabledAndAppNotInstalled = 4,
  kArcAvailable = 5,
  kTabletFormFactor = 6,
  kNotNewUserAndNotPreviouslyInstalled = 7,
  kNotPreviouslyPreinstalled = 8,
  kReplacingAppBlockedByPolicy = 9,
  kReplacingAppForceInstalled = 10,
  kReplacingAppStillInstalled = 11,
  kDefaultAppAndAppsToReplaceUninstalled = 12,
  kReplacingAppUninstalledByUser = 13,
  kStylusRequired = 14,
  kMaxValue = kStylusRequired
};

struct LoadedConfig {
  base::Value contents;
  base::FilePath file;
};

struct LoadedConfigs {
  std::vector<LoadedConfig> configs;
  std::vector<std::string> errors;
};

LoadedConfigs LoadConfigsBlocking(std::vector<base::FilePath> config_dirs) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  LoadedConfigs result;
  base::FilePath::StringType extension(FILE_PATH_LITERAL(".json"));

  for (auto config_dir : config_dirs) {
    base::FileEnumerator json_files(config_dir,
                                    false,  // Recursive.
                                    base::FileEnumerator::FILES);
    for (base::FilePath file = json_files.Next(); !file.empty();
         file = json_files.Next()) {
      if (!file.MatchesExtension(extension)) {
        continue;
      }

      JSONFileValueDeserializer deserializer(file);
      std::string error_msg;
      std::unique_ptr<base::Value> app_config =
          deserializer.Deserialize(nullptr, &error_msg);
      if (!app_config) {
        result.errors.push_back(base::StrCat(
            {file.AsUTF8Unsafe(), " was not valid JSON: ", error_msg}));
        VLOG(1) << result.errors.back();
        continue;
      }
      result.configs.push_back(
          {.contents = std::move(*app_config), .file = file});
    }
  }
  return result;
}

struct ParsedConfigs {
  std::vector<ExternalInstallOptions> options_list;
  std::vector<std::string> errors;
};

ParsedConfigs ParseConfigsBlocking(LoadedConfigs loaded_configs) {
  ParsedConfigs result;
  result.errors = std::move(loaded_configs.errors);

  scoped_refptr<FileUtilsWrapper> file_utils =
      g_file_utils_for_testing ? base::WrapRefCounted(g_file_utils_for_testing)
                               : base::MakeRefCounted<FileUtilsWrapper>();

  for (const LoadedConfig& loaded_config : loaded_configs.configs) {
    OptionsOrError parse_result =
        ParseConfig(*file_utils, loaded_config.file.DirName(),
                    loaded_config.file, loaded_config.contents);
    if (ExternalInstallOptions* options =
            absl::get_if<ExternalInstallOptions>(&parse_result)) {
      result.options_list.push_back(std::move(*options));
    } else {
      result.errors.push_back(std::move(absl::get<std::string>(parse_result)));
      VLOG(1) << result.errors.back();
    }
  }

  return result;
}

absl::optional<std::string> GetDisableReason(
    const ExternalInstallOptions& options,
    Profile* profile,
    WebAppRegistrar* registrar,
    bool preinstalled_apps_enabled_in_prefs,
    bool is_new_user,
    const std::string& user_type) {
  DCHECK(registrar);

  if (!preinstalled_apps_enabled_in_prefs) {
    base::UmaHistogramEnumeration(kHistogramMigrationDisabledReason,
                                  DisabledReason::kPreinstalledAppsNotEnabled);
    return options.install_url.spec() +
           " disabled by preinstalled_apps pref setting.";
  }

  // Remove if not applicable to current user type.
  DCHECK_GT(options.user_type_allowlist.size(), 0u);
  if (!base::Contains(options.user_type_allowlist, user_type)) {
    base::UmaHistogramEnumeration(kHistogramMigrationDisabledReason,
                                  DisabledReason::kUserTypeNotAllowed);
    return options.install_url.spec() + " disabled for user type: " + user_type;
  }

  // Remove if gated on a disabled feature.
  if (options.gate_on_feature && !IsPreinstalledAppInstallFeatureEnabled(
                                     *options.gate_on_feature, *profile)) {
    base::UmaHistogramEnumeration(kHistogramMigrationDisabledReason,
                                  DisabledReason::kGatedFeatureNotEnabled);
    return options.install_url.spec() +
           " disabled because feature is disabled: " + *options.gate_on_feature;
  }

  // Remove if gated on a disabled feature, and the app hasn't been preinstalled
  // before.
  if (options.gate_on_feature_or_installed &&
      !IsPreinstalledAppInstallFeatureEnabled(
          *options.gate_on_feature_or_installed, *profile)) {
    absl::optional<AppId> app_id =
        ExternallyInstalledWebAppPrefs(profile->GetPrefs())
            .LookupAppId(options.install_url);
    if (!app_id.has_value()) {
      base::UmaHistogramEnumeration(
          kHistogramMigrationDisabledReason,
          DisabledReason::kGatedFeatureNotEnabledAndAppNotInstalled);
      return options.install_url.spec() + " disabled because the feature " +
             *options.gate_on_feature_or_installed +
             " is disabled and the app is not already installed";
    }
  }

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // Remove if ARC is supported and app should be disabled.
  if (options.disable_if_arc_supported && arc::IsArcAvailable()) {
    base::UmaHistogramEnumeration(kHistogramMigrationDisabledReason,
                                  DisabledReason::kArcAvailable);
    return options.install_url.spec() + " disabled because ARC is available.";
  }

  // Remove if device is tablet and app should be disabled.
  if (options.disable_if_tablet_form_factor &&
      ash::switches::IsTabletFormFactor()) {
    base::UmaHistogramEnumeration(kHistogramMigrationDisabledReason,
                                  DisabledReason::kTabletFormFactor);
    return options.install_url.spec() + " disabled because device is tablet.";
  }
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  // Remove if only for new users, user isn't new and app was not
  // installed previously.
  if (options.only_for_new_users && !is_new_user) {
    bool was_previously_installed =
        ExternallyInstalledWebAppPrefs(profile->GetPrefs())
            .LookupAppId(options.install_url)
            .has_value();
    if (!was_previously_installed) {
      base::UmaHistogramEnumeration(
          kHistogramMigrationDisabledReason,
          DisabledReason::kNotNewUserAndNotPreviouslyInstalled);
      return options.install_url.spec() +
             " disabled because user was not new when config was added.";
    }
  }

  // Remove if was not previously preinstalled.
  if (options.only_if_previously_preinstalled) {
    absl::optional<AppId> app_id =
        ExternallyInstalledWebAppPrefs(profile->GetPrefs())
            .LookupAppId(options.install_url);

    bool was_previously_preinstalled = false;
    if (app_id.has_value()) {
      const WebApp* web_app = registrar->GetAppById(app_id.value());
      if (web_app && web_app->IsPreinstalledApp())
        was_previously_preinstalled = true;
    }

    if (!was_previously_preinstalled) {
      base::UmaHistogramEnumeration(kHistogramMigrationDisabledReason,
                                    DisabledReason::kNotPreviouslyPreinstalled);
      return options.install_url.spec() +
             " disabled because was not previously preinstalled.";
    }
  }

  // Remove if any apps to replace are blocked or force installed by admin
  // policy.
  for (const AppId& app_id : options.uninstall_and_replace) {
    if (extensions::IsExtensionBlockedByPolicy(profile, app_id)) {
      base::UmaHistogramEnumeration(
          kHistogramMigrationDisabledReason,
          DisabledReason::kReplacingAppBlockedByPolicy);
      return options.install_url.spec() +
             " disabled due to admin policy blocking replacement "
             "Extension.";
    }
    std::u16string reason;
    if (extensions::IsExtensionForceInstalled(profile, app_id, &reason)) {
      base::UmaHistogramEnumeration(
          kHistogramMigrationDisabledReason,
          DisabledReason::kReplacingAppForceInstalled);
      return options.install_url.spec() +
             " disabled due to admin policy force installing replacement "
             "Extension: " +
             base::UTF16ToUTF8(reason);
    }
  }

  // Keep if any apps to replace are installed.
  for (const AppId& app_id : options.uninstall_and_replace) {
    if (extensions::IsExtensionInstalled(profile, app_id)) {
      base::UmaHistogramEnumeration(
          kHistogramMigrationDisabledReason,
          DisabledReason::kReplacingAppStillInstalled);
      return absl::nullopt;
    }
  }

#if !BUILDFLAG(IS_CHROMEOS)
  // Remove if it's a default app and the apps to replace are not installed and
  // default extension apps are not performing new installation.
  if (options.gate_on_feature && !options.uninstall_and_replace.empty() &&
      !extensions::DidPreinstalledAppsPerformNewInstallation(profile)) {
    for (const AppId& app_id : options.uninstall_and_replace) {
      // First time migration and the app to replace is uninstalled as it passed
      // the last code block. Save the information that the app was
      // uninstalled by user.
      if (!WasMigrationRun(profile, *options.gate_on_feature)) {
        if (extensions::IsPreinstalledAppId(app_id)) {
          MarkPreinstalledAppAsUninstalled(profile, app_id);
          base::UmaHistogramEnumeration(
              kHistogramMigrationDisabledReason,
              DisabledReason::kDefaultAppAndAppsToReplaceUninstalled);
          return options.install_url.spec() +
                 "disabled because it's default app and apps to replace were "
                 "uninstalled.";
        }
      } else {
        // Not first time migration, can't determine if the app to replace is
        // uninstalled by user as the migration is already run, use the pref
        // saved in first migration.
        if (WasPreinstalledAppUninstalled(profile, app_id)) {
          base::UmaHistogramEnumeration(
              kHistogramMigrationDisabledReason,
              DisabledReason::kDefaultAppAndAppsToReplaceUninstalled);
          return options.install_url.spec() +
                 "disabled because it's default app and apps to replace were "
                 "uninstalled.";
        }
      }
    }
  }
#endif  // !BUILDFLAG(IS_CHROMEOS)

  // Remove if any apps to replace were previously uninstalled.
  for (const AppId& app_id : options.uninstall_and_replace) {
    if (extensions::IsExternalExtensionUninstalled(profile, app_id)) {
      base::UmaHistogramEnumeration(
          kHistogramMigrationDisabledReason,
          DisabledReason::kReplacingAppUninstalledByUser);
      return options.install_url.spec() +
             " disabled because apps to replace were uninstalled.";
    }
  }

  // Only install if device has a built-in touch screen with stylus support.
  if (options.disable_if_touchscreen_with_stylus_not_supported) {
    DCHECK(ui::DeviceDataManager::GetInstance()->AreDeviceListsComplete());
    bool have_touchscreen_with_stylus = false;
    for (const ui::TouchscreenDevice& device :
         ui::DeviceDataManager::GetInstance()->GetTouchscreenDevices()) {
      if (device.has_stylus &&
          device.type == ui::InputDeviceType::INPUT_DEVICE_INTERNAL) {
        have_touchscreen_with_stylus = true;
        break;
      }
    }
    if (!have_touchscreen_with_stylus) {
      base::UmaHistogramEnumeration(kHistogramMigrationDisabledReason,
                                    DisabledReason::kStylusRequired);
      return options.install_url.spec() +
             " disabled because the device does not have a built-in "
             "touchscreen with stylus support.";
    }
  }
  base::UmaHistogramEnumeration(kHistogramMigrationDisabledReason,
                                DisabledReason::kNotDisabled);

  return absl::nullopt;
}

std::string GetConfigDirectoryFromCommandLine() {
  return base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      switches::kPreinstalledWebAppsDir);
}

std::string GetExtraConfigSubdirectory() {
#if BUILDFLAG(IS_CHROMEOS_ASH)
  return base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      ash::switches::kExtraWebAppsDir);
#else
  return std::string();
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
}

}  // namespace

const char* PreinstalledWebAppManager::kHistogramEnabledCount =
    "WebApp.Preinstalled.EnabledCount";
const char* PreinstalledWebAppManager::kHistogramDisabledCount =
    "WebApp.Preinstalled.DisabledCount";
const char* PreinstalledWebAppManager::kHistogramConfigErrorCount =
    "WebApp.Preinstalled.ConfigErrorCount";
const char* PreinstalledWebAppManager::kHistogramInstallResult =
    "Webapp.InstallResult.Default";
const char* PreinstalledWebAppManager::kHistogramUninstallAndReplaceCount =
    "WebApp.Preinstalled.UninstallAndReplaceCount";
const char*
    PreinstalledWebAppManager::kHistogramAppToReplaceStillInstalledCount =
        "WebApp.Preinstalled.AppToReplaceStillInstalledCount";
const char* PreinstalledWebAppManager::
    kHistogramAppToReplaceStillDefaultInstalledCount =
        "WebApp.Preinstalled.AppToReplaceStillDefaultInstalledCount";
const char* PreinstalledWebAppManager::
    kHistogramAppToReplaceStillInstalledInShelfCount =
        "WebApp.Preinstalled.AppToReplaceStillInstalledInShelfCount";

void PreinstalledWebAppManager::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(prefs::kWebAppsLastPreinstallSynchronizeVersion,
                               "");
  registry->RegisterListPref(webapps::kWebAppsMigratedPreinstalledApps);
  registry->RegisterListPref(prefs::kWebAppsDidMigrateDefaultChromeApps);
  registry->RegisterListPref(prefs::kWebAppsUninstalledDefaultChromeApps);
}

void PreinstalledWebAppManager::SkipStartupForTesting() {
  g_skip_startup_for_testing_ = true;
}

void PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting() {
  g_bypass_offline_manifest_requirement_for_testing_ = true;
}

void PreinstalledWebAppManager::SetConfigDirForTesting(
    const base::FilePath* config_dir) {
  g_config_dir_for_testing = config_dir;
}

void PreinstalledWebAppManager::SetConfigsForTesting(
    const std::vector<base::Value>* configs) {
  g_configs_for_testing = configs;
}

void PreinstalledWebAppManager::SetFileUtilsForTesting(
    FileUtilsWrapper* file_utils) {
  g_file_utils_for_testing = file_utils;
}

PreinstalledWebAppManager::PreinstalledWebAppManager(Profile* profile)
    : profile_(profile) {
  if (base::FeatureList::IsEnabled(features::kRecordWebAppDebugInfo)) {
    debug_info_ = std::make_unique<DebugInfo>();
  }
}

PreinstalledWebAppManager::~PreinstalledWebAppManager() {
  for (auto& observer : observers_) {
    observer.OnDestroyed();
  }
}

void PreinstalledWebAppManager::SetSubsystems(
    WebAppRegistrar* registrar,
    const WebAppUiManager* ui_manager,
    ExternallyManagedAppManager* externally_managed_app_manager) {
  registrar_ = registrar;
  ui_manager_ = ui_manager;
  externally_managed_app_manager_ = externally_managed_app_manager;
}

void PreinstalledWebAppManager::Start() {
  if (!g_skip_startup_for_testing_) {
    LoadAndSynchronize(
        base::BindOnce(&PreinstalledWebAppManager::OnStartUpTaskCompleted,
                       weak_ptr_factory_.GetWeakPtr()));
  }
}

void PreinstalledWebAppManager::LoadForTesting(ConsumeInstallOptions callback) {
  Load(std::move(callback));
}

void PreinstalledWebAppManager::AddObserver(
    PreinstalledWebAppManager::Observer* observer) {
  observers_.AddObserver(observer);
}

void PreinstalledWebAppManager::RemoveObserver(
    PreinstalledWebAppManager::Observer* observer) {
  observers_.RemoveObserver(observer);
}

void PreinstalledWebAppManager::LoadAndSynchronizeForTesting(
    SynchronizeCallback callback) {
  LoadAndSynchronize(std::move(callback));
}

void PreinstalledWebAppManager::LoadAndSynchronize(
    SynchronizeCallback callback) {
  // Make sure ExtensionSystem is ready to know if default apps new installation
  // will be performed.
  extensions::OnExtensionSystemReady(
      profile_,
      base::BindOnce(
          &PreinstalledWebAppManager::Load, weak_ptr_factory_.GetWeakPtr(),
          base::BindOnce(&PreinstalledWebAppManager::Synchronize,
                         weak_ptr_factory_.GetWeakPtr(), std::move(callback))));
}

void PreinstalledWebAppManager::Load(ConsumeInstallOptions callback) {
  bool preinstalling_enabled =
      base::FeatureList::IsEnabled(features::kPreinstalledWebAppInstallation);

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // With Lacros, web apps are not installed using the Ash browser.
  if (IsWebAppsCrosapiEnabled())
    preinstalling_enabled = false;
#endif

  if (!preinstalling_enabled) {
    std::move(callback).Run({});
    return;
  }

  LoadConfigs(base::BindOnce(
      &PreinstalledWebAppManager::ParseConfigs, weak_ptr_factory_.GetWeakPtr(),
      base::BindOnce(&PreinstalledWebAppManager::PostProcessConfigs,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback))));
}

void PreinstalledWebAppManager::LoadConfigs(ConsumeLoadedConfigs callback) {
  if (g_configs_for_testing) {
    LoadedConfigs loaded_configs;
    for (const base::Value& config : *g_configs_for_testing) {
      auto file = base::FilePath(FILE_PATH_LITERAL("test.json"));
      if (g_config_dir_for_testing) {
        file = g_config_dir_for_testing->Append(file);
      }

      loaded_configs.configs.push_back(
          {.contents = config.Clone(), .file = file});
    }
    std::move(callback).Run(std::move(loaded_configs));
    return;
  }

  base::FilePath config_dir = GetConfigDir();
  if (config_dir.empty()) {
    std::move(callback).Run({});
    return;
  }

  std::vector<base::FilePath> config_dirs = {config_dir};
  std::string extra_config_subdir = GetExtraConfigSubdirectory();
  if (!extra_config_subdir.empty()) {
    config_dirs.push_back(config_dir.AppendASCII(extra_config_subdir));
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&LoadConfigsBlocking, std::move(config_dirs)),
      std::move(callback));
}

void PreinstalledWebAppManager::ParseConfigs(ConsumeParsedConfigs callback,
                                             LoadedConfigs loaded_configs) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&ParseConfigsBlocking, std::move(loaded_configs)),
      std::move(callback));
}

void PreinstalledWebAppManager::PostProcessConfigs(
    ConsumeInstallOptions callback,
    ParsedConfigs parsed_configs) {
  // Add hard coded configs.
  for (ExternalInstallOptions& options : GetPreinstalledWebApps())
    parsed_configs.options_list.push_back(std::move(options));

  // Set common install options.
  for (ExternalInstallOptions& options : parsed_configs.options_list) {
    DCHECK_EQ(options.install_source, ExternalInstallSource::kExternalDefault);

    options.require_manifest = true;

#if BUILDFLAG(IS_CHROMEOS)
    // On Chrome OS the "quick launch bar" is the shelf pinned apps.
    // This is configured in `GetDefaultPinnedAppsForFormFactor()` instead of
    // here to ensure a specific order is deployed.
    options.add_to_quick_launch_bar = false;
#else   // BUILDFLAG(IS_CHROMEOS)
    if (!g_bypass_offline_manifest_requirement_for_testing_) {
      // Non-Chrome OS platforms are not permitted to fetch the web app install
      // URLs during start up.
      DCHECK(options.app_info_factory);
      options.only_use_app_info_factory = true;
    }

    // Preinstalled web apps should not have OS shortcuts of any kind outside of
    // Chrome OS.
    options.add_to_applications_menu = false;
    options.add_to_search = false;
    options.add_to_management = false;
    options.add_to_desktop = false;
    options.add_to_quick_launch_bar = false;
#endif  // BUILDFLAG(IS_CHROMEOS)
  }

  // TODO(crbug.com/1175196): Move this constant into some shared constants.h
  // file.
  bool preinstalled_apps_enabled_in_prefs =
      profile_->GetPrefs()->GetString(prefs::kPreinstalledApps) == "install";
  bool is_new_user = IsNewUser();
  std::string user_type = apps::DetermineUserType(profile_);
  size_t disabled_count = 0;
  base::EraseIf(
      parsed_configs.options_list, [&](const ExternalInstallOptions& options) {
        absl::optional<std::string> disable_reason = GetDisableReason(
            options, profile_, registrar_, preinstalled_apps_enabled_in_prefs,
            is_new_user, user_type);
        if (disable_reason) {
          VLOG(1) << *disable_reason;
          ++disabled_count;
          if (debug_info_) {
            debug_info_->disabled_configs.emplace_back(
                std::move(options), std::move(*disable_reason));
          }
          return true;
        }
        return false;
      });

  if (debug_info_) {
    debug_info_->parse_errors = parsed_configs.errors;
    debug_info_->enabled_configs = parsed_configs.options_list;
  }

  // Triggers |force_reinstall| in ExternallyManagedAppManager if milestone
  // increments across |force_reinstall_for_milestone|.
  for (ExternalInstallOptions& options : parsed_configs.options_list) {
    if (options.force_reinstall_for_milestone &&
        IsReinstallPastMilestoneNeededSinceLastSync(
            options.force_reinstall_for_milestone.value())) {
      options.force_reinstall = true;
    }
  }

  base::UmaHistogramCounts100(kHistogramEnabledCount,
                              parsed_configs.options_list.size());
  base::UmaHistogramCounts100(kHistogramDisabledCount, disabled_count);
  base::UmaHistogramCounts100(kHistogramConfigErrorCount,
                              parsed_configs.errors.size());

  std::move(callback).Run(parsed_configs.options_list);
}

void PreinstalledWebAppManager::Synchronize(
    ExternallyManagedAppManager::SynchronizeCallback callback,
    std::vector<ExternalInstallOptions> desired_apps_install_options) {
  DCHECK(externally_managed_app_manager_);

  std::map<InstallUrl, std::vector<AppId>> desired_uninstalls;
  for (const auto& entry : desired_apps_install_options) {
    if (!entry.uninstall_and_replace.empty())
      desired_uninstalls.emplace(entry.install_url,
                                 entry.uninstall_and_replace);
  }
  externally_managed_app_manager_->SynchronizeInstalledApps(
      std::move(desired_apps_install_options),
      ExternalInstallSource::kExternalDefault,
      base::BindOnce(&PreinstalledWebAppManager::OnExternalWebAppsSynchronized,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(desired_uninstalls)));
}

void PreinstalledWebAppManager::OnExternalWebAppsSynchronized(
    ExternallyManagedAppManager::SynchronizeCallback callback,
    std::map<InstallUrl, std::vector<AppId>> desired_uninstalls,
    std::map<InstallUrl, ExternallyManagedAppManager::InstallResult>
        install_results,
    std::map<InstallUrl, bool> uninstall_results) {
  // Note that we are storing the Chrome version (milestone number) instead of a
  // "has synchronised" bool in order to do version update specific logic.
  profile_->GetPrefs()->SetString(
      prefs::kWebAppsLastPreinstallSynchronizeVersion,
      version_info::GetMajorVersionNumber());

  DCHECK(
      apps::AppServiceProxyFactory::IsAppServiceAvailableForProfile(profile_));
  auto* proxy = apps::AppServiceProxyFactory::GetForProfile(profile_);

  size_t uninstall_and_replace_count = 0;
  size_t app_to_replace_still_installed_count = 0;
  size_t app_to_replace_still_default_installed_count = 0;
  size_t app_to_replace_still_installed_in_shelf_count = 0;

  for (const auto& url_and_result : install_results) {
    const ExternallyManagedAppManager::InstallResult& result =
        url_and_result.second;
    base::UmaHistogramEnumeration(kHistogramInstallResult, result.code);
    if (result.did_uninstall_and_replace) {
      ++uninstall_and_replace_count;
    }

    if (!IsSuccess(result.code))
      continue;

    DCHECK(result.app_id.has_value());

    auto iter = desired_uninstalls.find(url_and_result.first);
    if (iter == desired_uninstalls.end())
      continue;

    for (const AppId& replace_id : iter->second) {
      // We mark the app as migrated to a web app as long as the
      // installation was successful, even if the previous app was not
      // installed. This ensures we properly re-install apps if the
      // migration feature is rolled back.
      MarkAppAsMigratedToWebApp(profile_, replace_id, /*was_migrated=*/true);

      // Track whether the app to replace is still present. This is
      // possibly due to getting reinstalled by the user or by Chrome app
      // sync. See https://crbug.com/1266234 for context.
      if (proxy &&
          result.code == webapps::InstallResultCode::kSuccessAlreadyInstalled) {
        bool is_installed = false;
        proxy->AppRegistryCache().ForOneApp(
            replace_id, [&is_installed](const apps::AppUpdate& app) {
              is_installed = apps_util::IsInstalled(app.Readiness());
            });

        if (!is_installed)
          continue;

        ++app_to_replace_still_installed_count;

        if (extensions::IsExtensionDefaultInstalled(profile_, replace_id))
          ++app_to_replace_still_default_installed_count;

        if (ui_manager_->CanAddAppToQuickLaunchBar()) {
          if (ui_manager_->IsAppInQuickLaunchBar(result.app_id.value()))
            ++app_to_replace_still_installed_in_shelf_count;
        }
      }
    }
  }
  base::UmaHistogramCounts100(kHistogramUninstallAndReplaceCount,
                              uninstall_and_replace_count);

  base::UmaHistogramCounts100(kHistogramAppToReplaceStillInstalledCount,
                              app_to_replace_still_installed_count);
  base::UmaHistogramCounts100(kHistogramAppToReplaceStillDefaultInstalledCount,
                              app_to_replace_still_default_installed_count);
  base::UmaHistogramCounts100(kHistogramAppToReplaceStillInstalledInShelfCount,
                              app_to_replace_still_installed_in_shelf_count);

  SetMigrationRun(profile_, kMigrateDefaultChromeAppToWebAppsGSuite.name,
                  IsPreinstalledAppInstallFeatureEnabled(
                      kMigrateDefaultChromeAppToWebAppsGSuite.name, *profile_));
  SetMigrationRun(
      profile_, kMigrateDefaultChromeAppToWebAppsNonGSuite.name,
      IsPreinstalledAppInstallFeatureEnabled(
          kMigrateDefaultChromeAppToWebAppsNonGSuite.name, *profile_));
  if (uninstall_and_replace_count > 0) {
    for (auto& observer : observers_) {
      observer.OnMigrationRun();
    }
  }

  if (callback) {
    std::move(callback).Run(std::move(install_results),
                            std::move(uninstall_results));
  }
}

void PreinstalledWebAppManager::OnStartUpTaskCompleted(
    std::map<InstallUrl, ExternallyManagedAppManager::InstallResult>
        install_results,
    std::map<InstallUrl, bool> uninstall_results) {
  if (debug_info_) {
    debug_info_->is_start_up_task_complete = true;
    debug_info_->install_results = std::move(install_results);
    debug_info_->uninstall_results = std::move(uninstall_results);
  }
}

base::FilePath PreinstalledWebAppManager::GetConfigDir() {
  std::string command_line_directory = GetConfigDirectoryFromCommandLine();
  if (!command_line_directory.empty())
    return base::FilePath::FromUTF8Unsafe(command_line_directory);

#if BUILDFLAG(IS_CHROMEOS)
    // As of mid 2018, only Chrome OS has default/external web apps, and
    // chrome::DIR_STANDALONE_EXTERNAL_EXTENSIONS is only defined for OS_LINUX,
    // which includes OS_CHROMEOS.

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // Exclude sign-in and lock screen profiles.
  if (!ash::ProfileHelper::IsRegularProfile(profile_)) {
    return {};
  }
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  if (g_config_dir_for_testing) {
    return *g_config_dir_for_testing;
  }

  // For manual testing, you can change s/STANDALONE/USER/, as writing to
  // "$HOME/.config/chromium/test-user/.config/chromium/External
  // Extensions/web_apps" does not require root ACLs, unlike
  // "/usr/share/chromium/extensions/web_apps".
  base::FilePath dir;
  if (base::PathService::Get(chrome::DIR_STANDALONE_EXTERNAL_EXTENSIONS,
                             &dir)) {
    return dir.Append(kWebAppsSubDirectory);
  }

  LOG(ERROR) << "base::PathService::Get failed";
#endif  // BUILDFLAG(IS_CHROMEOS)

  return {};
}

bool PreinstalledWebAppManager::IsNewUser() {
  PrefService* prefs = profile_->GetPrefs();
  return prefs->GetString(prefs::kWebAppsLastPreinstallSynchronizeVersion)
      .empty();
}

bool PreinstalledWebAppManager::IsReinstallPastMilestoneNeededSinceLastSync(
    int force_reinstall_for_milestone) {
  PrefService* prefs = profile_->GetPrefs();
  std::string last_preinstall_synchronize_milestone =
      prefs->GetString(prefs::kWebAppsLastPreinstallSynchronizeVersion);

  return IsReinstallPastMilestoneNeeded(last_preinstall_synchronize_milestone,
                                        version_info::GetMajorVersionNumber(),
                                        force_reinstall_for_milestone);
}

PreinstalledWebAppManager::DebugInfo::DebugInfo() = default;

PreinstalledWebAppManager::DebugInfo::~DebugInfo() = default;

}  //  namespace web_app
