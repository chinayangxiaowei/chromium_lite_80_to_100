// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/login/session/chrome_session_manager.h"

#include <memory>

#include "ash/components/cryptohome/cryptohome_parameters.h"
#include "ash/constants/ash_features.h"
#include "ash/constants/ash_switches.h"
#include "ash/webui/shimless_rma/shimless_rma.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "chrome/browser/ash/app_mode/app_launch_utils.h"
#include "chrome/browser/ash/app_mode/kiosk_cryptohome_remover.h"
#include "chrome/browser/ash/arc/session/arc_service_launcher.h"
#include "chrome/browser/ash/boot_times_recorder.h"
#include "chrome/browser/ash/child_accounts/child_status_reporting_service_factory.h"
#include "chrome/browser/ash/child_accounts/child_user_service_factory.h"
#include "chrome/browser/ash/child_accounts/screen_time_controller_factory.h"
#include "chrome/browser/ash/crostini/crostini_manager.h"
#include "chrome/browser/ash/lock_screen_apps/state_controller.h"
#include "chrome/browser/ash/login/chrome_restart_request.h"
#include "chrome/browser/ash/login/demo_mode/demo_resources.h"
#include "chrome/browser/ash/login/demo_mode/demo_session.h"
#include "chrome/browser/ash/login/existing_user_controller.h"
#include "chrome/browser/ash/login/login_wizard.h"
#include "chrome/browser/ash/login/screens/arc_terms_of_service_screen.h"
#include "chrome/browser/ash/login/screens/sync_consent_screen.h"
#include "chrome/browser/ash/login/session/user_session_initializer.h"
#include "chrome/browser/ash/login/session/user_session_manager.h"
#include "chrome/browser/ash/login/wizard_controller.h"
#include "chrome/browser/ash/policy/core/browser_policy_connector_ash.h"
#include "chrome/browser/ash/policy/handlers/tpm_auto_update_mode_policy_handler.h"
#include "chrome/browser/ash/policy/reporting/app_install_event_log_manager_wrapper.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/ash/tether/tether_service.h"
#include "chrome/browser/ash/tpm_firmware_update_notification.h"
#include "chrome/browser/ash/u2f_notification.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part_chromeos.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/ui/app_list/app_list_client_impl.h"
#include "chrome/browser/ui/webui/chromeos/login/app_launch_splash_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/lacros_data_migration_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/shimless_rma_dialog.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chromeos/dbus/rmad/rmad_client.h"
#include "chromeos/dbus/session_manager/session_manager_client.h"
#include "components/account_id/account_id.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/signin/public/identity_manager/primary_account_mutator.h"
#include "components/user_manager/user_manager.h"
#include "components/user_manager/user_names.h"
#include "components/user_manager/user_type.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/notification_service.h"
#include "content/public/common/content_switches.h"
#include "services/service_manager/public/cpp/connector.h"

namespace ash {
namespace {

// Starts kiosk app auto launch and shows the splash screen.
void StartKioskSession() {
  // Kiosk app launcher starts with login state.
  session_manager::SessionManager::Get()->SetSessionState(
      session_manager::SessionState::LOGIN_PRIMARY);

  ShowLoginWizard(chromeos::AppLaunchSplashScreenView::kScreenId);

  // Login screen is skipped but 'login-prompt-visible' signal is still needed.
  VLOG(1) << "Kiosk app auto launch >> login-prompt-visible";
  SessionManagerClient::Get()->EmitLoginPromptVisible();
}

// Starts the login/oobe screen.
void StartLoginOobeSession() {
  // State will be defined once out-of-box/login branching is complete.
  ShowLoginWizard(OobeScreen::SCREEN_UNKNOWN);

  // Reset reboot after update flag when login screen is shown.
  policy::BrowserPolicyConnectorAsh* connector =
      g_browser_process->platform_part()->browser_policy_connector_ash();
  if (!connector->IsDeviceEnterpriseManaged()) {
    PrefService* local_state = g_browser_process->local_state();
    local_state->ClearPref(prefs::kRebootAfterUpdate);
  }
}

// Starts Chrome with an existing user session. Possible cases:
// 1. Chrome is restarted after crash.
// 2. Chrome is restarted for Guest session.
// 3. Chrome is started in browser_tests skipping the login flow.
// 4. Chrome is started on dev machine i.e. not on Chrome OS device w/o
//    login flow. In that case --login-user=[user_manager::kStubUserEmail] is
//    added. See PreEarlyInitialization().
void StartUserSession(Profile* user_profile, const std::string& login_user_id) {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

  if (command_line->HasSwitch(switches::kLoginUser)) {
    // TODO(https://crbug.com/977489): There's a lot of code duplication with
    // UserSessionManager::FinalizePrepareProfile, which is (only!) run for
    // regular session starts. This needs to be refactored.

    // This is done in SessionManager::OnProfileCreated during normal login.
    UserSessionManager* user_session_mgr = UserSessionManager::GetInstance();
    user_manager::UserManager* user_manager = user_manager::UserManager::Get();
    const user_manager::User* user = user_manager->GetActiveUser();
    if (!user) {
      // This is possible if crash occured after profile removal
      // (see crbug.com/178290 for some more info).
      LOG(ERROR) << "Could not get active user after crash.";
      return;
    }

    auto* demo_session = DemoSession::Get();
    // In demo session, delay starting user session until the demo
    // session resources have been loaded.
    if (demo_session && demo_session->started() &&
        !demo_session->resources()->loaded()) {
      demo_session->EnsureResourcesLoaded(
          base::BindOnce(&StartUserSession, user_profile, login_user_id));
      LOG(WARNING) << "Delay demo user session start until demo "
                   << "resources are loaded";
      return;
    }

    ProfileHelper::Get()->ProfileStartup(user_profile);

    user_session_mgr->NotifyUserProfileLoaded(user_profile, user);

    // This call will set session state to SESSION_STATE_ACTIVE (same one).
    session_manager::SessionManager::Get()->SessionStarted();

    // Now is the good time to retrieve other logged in users for this session.
    // First user has been already marked as logged in and active in
    // PreProfileInit(). Restore sessions for other users in the background.
    user_session_mgr->RestoreActiveSessions();
  }

  bool is_running_test = command_line->HasSwitch(::switches::kTestName) ||
                         command_line->HasSwitch(::switches::kTestType);
  if (!is_running_test) {
    // We did not log in (we crashed or are debugging), so we need to
    // restore Sync.
    UserSessionManager::GetInstance()->RestoreAuthenticationSession(
        user_profile);

    UserSessionManager::GetInstance()->StartTetherServiceIfPossible(
        user_profile);

    // Associates AppListClient with the current active profile.
    AppListClientImpl::GetInstance()->UpdateProfile();
  }

  if (base::FeatureList::IsEnabled(features::kEolWarningNotifications) &&
      !user_profile->GetProfilePolicyConnector()->IsManaged())
    UserSessionManager::GetInstance()->CheckEolInfo(user_profile);

  UserSessionManager::GetInstance()->ShowNotificationsIfNeeded(user_profile);
  UserSessionManager::GetInstance()->MaybeLaunchSettings(user_profile);
}

void LaunchShimlessRma() {
  session_manager::SessionManager::Get()->SetSessionState(
      session_manager::SessionState::RMA);

  chromeos::ShimlessRmaDialog::ShowDialog();
  // Login screen is skipped but 'login-prompt-visible' signal is still
  // needed.
  VLOG(1) << "Shimless RMA app auto launch >> login-prompt-visible";
  SessionManagerClient::Get()->EmitLoginPromptVisible();
}

// The callback invoked when RmadClient determines that RMA is required.
void OnRmaIsRequiredResponse() {
  switch (session_manager::SessionManager::Get()->session_state()) {
    case session_manager::SessionState::UNKNOWN:
      LOG(ERROR) << "OnRmaIsRequiredResponse callback triggered unexpectedly";
      break;
    case session_manager::SessionState::RMA:
      // Already in RMA, do nothing.
      break;
    case session_manager::SessionState::OOBE:
    case session_manager::SessionState::LOGIN_PRIMARY: {
      auto* existing_user_controller =
          ash::ExistingUserController::current_controller();
      if (!existing_user_controller ||
          !existing_user_controller->IsSigninInProgress()) {
        if (existing_user_controller) {
          existing_user_controller->StopAutoLoginTimer();
        }
        // Append the kLaunchRma flag and restart Chrome to force launch RMA.
        const base::CommandLine& browser_command_line =
            *base::CommandLine::ForCurrentProcess();
        base::CommandLine command_line(browser_command_line);
        command_line.AppendSwitch(::ash::switches::kLaunchRma);
        ash::RestartChrome(command_line, ash::RestartChromeReason::kUserless);
        break;
      }

      // If signin in progress, don't launch RMA and fall through to the
      // logged in state cases.
      ABSL_FALLTHROUGH_INTENDED;
    }
    case session_manager::SessionState::LOGGED_IN_NOT_ACTIVE:
    case session_manager::SessionState::ACTIVE:
    case session_manager::SessionState::LOCKED:
    case session_manager::SessionState::LOGIN_SECONDARY:
      // TODO(gavinwill): Trigger a notification to open RMA app.
      break;
  }
}

}  // namespace

ChromeSessionManager::ChromeSessionManager()
    : oobe_configuration_(std::make_unique<OobeConfiguration>()),
      user_session_initializer_(std::make_unique<UserSessionInitializer>()) {
  AddObserver(user_session_initializer_.get());
}

ChromeSessionManager::~ChromeSessionManager() {
  RemoveObserver(user_session_initializer_.get());
}

void ChromeSessionManager::Initialize(
    const base::CommandLine& parsed_command_line,
    Profile* profile,
    bool is_running_test) {
  // If a forced powerwash was triggered and no confirmation from the user is
  // necessary, we trigger the device wipe here before the user can log in again
  // and return immediately because there is no need to show the login screen.
  if (g_browser_process->local_state()->GetBoolean(prefs::kForceFactoryReset)) {
    SessionManagerClient::Get()->StartDeviceWipe();
    return;
  }

  if (ash::shimless_rma::IsShimlessRmaAllowed()) {
    // If the RMA state is detected later, OnRmaIsRequiredResponse() is invoked
    // to reboot the device in RMA mode.
    const bool was_rma_state_detected_now =
        chromeos::RmadClient::Get()->WasRmaStateDetectedForSessionManager(
            base::BindOnce(&OnRmaIsRequiredResponse));
    const bool has_launch_rma_switch =
        ash::shimless_rma::HasLaunchRmaSwitchAndIsAllowed();

    // If we should be in Shimless RMA, start it and skip the rest of
    // initialization.
    if (has_launch_rma_switch || was_rma_state_detected_now) {
      LaunchShimlessRma();
      return;
    }
  }

  // Tests should be able to tune login manager before showing it. Thus only
  // show login UI (login and out-of-box) in normal (non-testing) mode with
  // --login-manager switch and if test passed --force-login-manager-in-tests.
  bool force_login_screen_in_test =
      parsed_command_line.HasSwitch(switches::kForceLoginManagerInTests);

  const std::string cryptohome_id =
      parsed_command_line.GetSwitchValueASCII(switches::kLoginUser);
  const AccountId login_account_id(
      cryptohome::Identification::FromString(cryptohome_id).GetAccountId());

  KioskCryptohomeRemover::RemoveObsoleteCryptohomes();

  if (ShouldAutoLaunchKioskApp(parsed_command_line,
                               g_browser_process->local_state())) {
    VLOG(1) << "Starting Chrome with kiosk auto launch.";
    StartKioskSession();
    return;
  }

  if (parsed_command_line.HasSwitch(switches::kBrowserDataMigrationForUser)) {
    VLOG(1) << "Ash is running to do browser data migration.";
    // Show UI for browser data migration. The migration itself will be started
    // in `LacrosDataMigrationScreen::ShowImpl`.
    ShowLoginWizard(LacrosDataMigrationScreenView::kScreenId);
    return;
  }

  if (parsed_command_line.HasSwitch(switches::kLoginManager) &&
      (!is_running_test || force_login_screen_in_test)) {
    VLOG(1) << "Starting Chrome with login/oobe screen.";
    oobe_configuration_->CheckConfiguration();
    StartLoginOobeSession();
    return;
  } else if (is_running_test) {
    oobe_configuration_->CheckConfiguration();
  }

  VLOG(1) << "Starting Chrome with a user session.";
  StartUserSession(profile, login_account_id.GetUserEmail());
}

void ChromeSessionManager::SessionStarted() {
  session_manager::SessionManager::SessionStarted();
  SetSessionState(session_manager::SessionState::ACTIVE);

  // Notifies UserManager so that it can update login state.
  user_manager::UserManager* user_manager = user_manager::UserManager::Get();
  if (user_manager)
    user_manager->OnSessionStarted();
}

void ChromeSessionManager::NotifyUserLoggedIn(const AccountId& user_account_id,
                                              const std::string& user_id_hash,
                                              bool browser_restart,
                                              bool is_child) {
  BootTimesRecorder* btl = BootTimesRecorder::Get();
  btl->AddLoginTimeMarker("UserLoggedIn-Start", false);
  session_manager::SessionManager::NotifyUserLoggedIn(
      user_account_id, user_id_hash, browser_restart, is_child);
  btl->AddLoginTimeMarker("UserLoggedIn-End", false);
}

}  // namespace ash
