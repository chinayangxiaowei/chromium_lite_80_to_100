// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/metrics/per_user_state_manager_chromeos.h"

#include "base/callback_list.h"
#include "base/guid.h"
#include "chrome/browser/ash/policy/core/browser_policy_connector_ash.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/ash/settings/device_settings_service.h"
#include "chrome/browser/ash/settings/stats_reporting_controller.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/metrics/profile_pref_names.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/pref_names.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/metrics/metrics_service_client.h"
#include "components/metrics/unsent_log_store_metrics_impl.h"
#include "components/user_manager/user.h"

namespace metrics {

namespace {

absl::optional<bool> g_is_managed_for_testing;

std::string GenerateUserId() {
  return base::GenerateGUID();
}

}  // namespace

PerUserStateManagerChromeOS::PerUserStateManagerChromeOS(
    MetricsServiceClient* metrics_service_client,
    user_manager::UserManager* user_manager,
    PrefService* local_state,
    const MetricsLogStore::StorageLimits& storage_limits,
    const std::string& signing_key)
    : metrics_service_client_(metrics_service_client),
      user_manager_(user_manager),
      local_state_(local_state),
      storage_limits_(storage_limits),
      signing_key_(signing_key) {
  user_manager_->AddSessionStateObserver(this);
  user_manager_->AddObserver(this);
}

PerUserStateManagerChromeOS::PerUserStateManagerChromeOS(
    MetricsServiceClient* metrics_service_client,
    PrefService* local_state)
    : PerUserStateManagerChromeOS(
          metrics_service_client,
          user_manager::UserManager::Get(),
          local_state,
          metrics_service_client->GetStorageLimits(),
          metrics_service_client->GetUploadSigningKey()) {
  // Ensure that user_manager has been initialized.
  DCHECK(user_manager::UserManager::IsInitialized());
}

PerUserStateManagerChromeOS::~PerUserStateManagerChromeOS() {
  user_manager_->RemoveObserver(this);
  user_manager_->RemoveSessionStateObserver(this);
}

// static
void PerUserStateManagerChromeOS::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(prefs::kMetricsCurrentUserId, "");
}

// static
void PerUserStateManagerChromeOS::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(prefs::kMetricsUserMetricLogs);
  registry->RegisterDictionaryPref(prefs::kMetricsUserMetricLogsMetadata);
  registry->RegisterStringPref(prefs::kMetricsUserId, "");
  registry->RegisterBooleanPref(prefs::kMetricsUserConsent, false);
  registry->RegisterBooleanPref(prefs::kMetricsRequiresClientIdResetOnConsent,
                                false);
}

absl::optional<std::string> PerUserStateManagerChromeOS::GetCurrentUserId()
    const {
  if (state_ != State::USER_LOG_STORE_HANDLED)
    return absl::nullopt;
  auto user_id = GetCurrentUserPrefs()->GetString(prefs::kMetricsUserId);
  if (user_id.empty())
    return absl::nullopt;
  return user_id;
}

absl::optional<bool>
PerUserStateManagerChromeOS::GetCurrentUserReportingConsentIfApplicable()
    const {
  if (state_ != State::USER_LOG_STORE_HANDLED)
    return absl::nullopt;

  // Guest sessions with no device owner should use the guest's metrics
  // consent set during guest OOBE flow with no device owner.
  bool is_guest_with_no_owner =
      current_user_->GetType() == user_manager::USER_TYPE_GUEST &&
      !IsDeviceOwned();

  // Cases in which user permissions should be applied to metrics reporting.
  if (IsUserAllowedToChangeConsent(current_user_) || is_guest_with_no_owner)
    return GetCurrentUserPrefs()->GetBoolean(prefs::kMetricsUserConsent);

  return absl::nullopt;
}

void PerUserStateManagerChromeOS::SetCurrentUserMetricsConsent(
    bool metrics_consent) {
  DCHECK(state_ >= State::WAITING_ON_FIRST_CONSENT);

  // If waiting on the first consent, trigger initialization now that first
  // consent has happened.
  if (state_ == State::WAITING_ON_FIRST_CONSENT) {
    OnFirstConsent(metrics_consent);
    return;
  }

  DCHECK(current_user_);

  // No-op if user should not be able to change metrics consent.
  if (!IsUserAllowedToChangeConsent(current_user_))
    return;

  DCHECK_EQ(state_, State::USER_LOG_STORE_HANDLED);

  auto* user_prefs = GetCurrentUserPrefs();

  // Value of |metrics_consent| is the same as the current consent. Terminate
  // early.
  if (user_prefs->GetBoolean(prefs::kMetricsUserConsent) == metrics_consent)
    return;

  // |new_user_id| = "" for on->off.
  std::string new_user_id;

  // off -> on state.
  if (metrics_consent) {
    new_user_id = GenerateUserId();
  }

  UpdateCurrentUserId(new_user_id);
  SetReportingState(metrics_consent);
  UpdateLocalStatePrefs(metrics_consent);
}

bool PerUserStateManagerChromeOS::ShouldUseUserLogStore() const {
  DCHECK(state_ > State::CONSTRUCTED);

  if (user_manager_->IsCurrentUserCryptohomeDataEphemeral()) {
    // Sessions using temporary cryptohome should have logs be persisted in
    // the cryptohome partition if metrics reporting managed by device is
    // disabled so recorded logs are deleted when the session ends. For
    // metrics reporting enabled, logs should be persisted in local store with
    // no associated user_id.
    return !GetDeviceMetricsConsent();
  }

  // Users with persistent cryptohome data should store logs in the user
  // cryptohome.
  return true;
}

bool PerUserStateManagerChromeOS::IsUserAllowedToChangeConsent(
    user_manager::User* user) const {
  // Devices with managed policy should not have control to toggle metrics
  // consent.
  if (IsReportingPolicyManaged())
    return false;

  // Owner should not use per-user. Owner should use the device local pref.
  if (user->GetAccountId() == user_manager_->GetOwnerAccountId())
    return false;

  auto user_type = user->GetType();

  // Guest sessions for non-owned devices should be allowed to modify metrics
  // consent during the lifetime of the session.
  if (user_type == user_manager::USER_TYPE_GUEST) {
    return ash::DeviceSettingsService::Get()->GetOwnershipStatus() ==
           ash::DeviceSettingsService::OWNERSHIP_NONE;
  }

  // Non-managed devices only have control if owner has enabled metrics
  // reporting.
  if (!GetDeviceMetricsConsent())
    return false;

  return user_type == user_manager::USER_TYPE_REGULAR ||
         user_type == user_manager::USER_TYPE_ACTIVE_DIRECTORY;
}

base::CallbackListSubscription PerUserStateManagerChromeOS::AddObserver(
    const MetricsConsentHandler& callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return callback_list_.Add(callback);
}

// static
void PerUserStateManagerChromeOS::SetIsManagedForTesting(bool is_managed) {
  g_is_managed_for_testing = is_managed;
}

void PerUserStateManagerChromeOS::SetUserLogStore(
    std::unique_ptr<UnsentLogStore> log_store) {
  DCHECK(state_ == State::USER_PROFILE_READY ||
         state_ == State::WAITING_ON_FIRST_CONSENT);

  metrics_service_client_->GetMetricsService()->SetUserLogStore(
      std::move(log_store));
}

void PerUserStateManagerChromeOS::UnsetUserLogStore() {
  DCHECK_EQ(state_, State::USER_LOG_STORE_HANDLED);
  metrics_service_client_->GetMetricsService()->UnsetUserLogStore();
}

void PerUserStateManagerChromeOS::ForceClientIdReset() {
  metrics_service_client_->GetMetricsService()->ResetClientId();
}

void PerUserStateManagerChromeOS::SetReportingState(bool metrics_consent) {
  DCHECK_EQ(state_, State::USER_LOG_STORE_HANDLED);
  DCHECK(IsUserAllowedToChangeConsent(current_user_));

  GetCurrentUserPrefs()->SetBoolean(prefs::kMetricsUserConsent,
                                    metrics_consent);
  NotifyObservers(metrics_consent);
}

bool PerUserStateManagerChromeOS::IsReportingPolicyManaged() const {
  if (g_is_managed_for_testing.has_value())
    return g_is_managed_for_testing.value();

  return metrics_service_client_->IsReportingPolicyManaged();
}

bool PerUserStateManagerChromeOS::GetDeviceMetricsConsent() const {
  DCHECK_NE(ash::DeviceSettingsService::Get()->GetOwnershipStatus(),
            ash::DeviceSettingsService::OWNERSHIP_UNKNOWN);

  return ash::DeviceSettingsService::Get()->GetOwnershipStatus() ==
             ash::DeviceSettingsService::OWNERSHIP_TAKEN &&
         ash::StatsReportingController::Get()->IsEnabled();
}

bool PerUserStateManagerChromeOS::HasUserLogStore() const {
  return metrics_service_client_->GetMetricsService()->HasUserLogStore();
}

bool PerUserStateManagerChromeOS::IsDeviceOwned() const {
  return ash::DeviceSettingsService::Get()->GetOwnershipStatus() ==
         ash::DeviceSettingsService::OwnershipStatus::OWNERSHIP_TAKEN;
}

void PerUserStateManagerChromeOS::ActiveUserChanged(user_manager::User* user) {
  DCHECK_EQ(state_, State::CONSTRUCTED) << "User already detected.";

  state_ = State::USER_LOGIN;

  current_user_ = user;
  user->AddProfileCreatedObserver(base::BindOnce(
      &PerUserStateManagerChromeOS::InitializeProfileMetricsState,
      weak_ptr_factory_.GetWeakPtr()));
}

void PerUserStateManagerChromeOS::OnUserToBeRemoved(
    const AccountId& account_id) {
  auto* user = user_manager_->FindUser(account_id);

  // Resets the state of |this| if user to be removed is the current logged in
  // user.
  if (user && current_user_ == user) {
    ResetState();
  }
}

void PerUserStateManagerChromeOS::InitializeProfileMetricsState() {
  DCHECK_EQ(state_, State::USER_LOGIN);

  // Defer initialization if we should wait for first consent. Initialization
  // will occur when SetCurrentUserMetricsConsent() is called for the first time
  // for the user.
  //
  // Logs created in this state will be stored in local state and will be
  // treated as pre-login logs.
  if (ShouldWaitForFirstConsent()) {
    state_ = State::WAITING_ON_FIRST_CONSENT;
    return;
  }

  state_ = State::USER_PROFILE_READY;

  if (ShouldUseUserLogStore()) {
    // Sets the metrics log store to one in the user cryptohome. Before this
    // happens, all pending logs recorded before the user login will be
    // flushed to local state.
    AssignUserLogStore();
  } else {
    DCHECK(!HasUserLogStore());
  }
  state_ = State::USER_LOG_STORE_HANDLED;

  // Sets the current logged-in user ID to handle crashes. Assigns a user ID if
  // current user has one. Otherwise, clear the pref to reflect that current
  // user does not have a user id.
  auto user_id = GetCurrentUserId();
  if (user_id.has_value())
    local_state_->SetString(prefs::kMetricsCurrentUserId, user_id.value());
  else
    local_state_->ClearPref(prefs::kMetricsCurrentUserId);

  // Only initialize metrics consent to user metrics consent if user is
  // allowed to change the metrics consent.
  if (IsUserAllowedToChangeConsent(current_user_)) {
    SetReportingState(
        GetCurrentUserPrefs()->GetBoolean(prefs::kMetricsUserConsent));
  }
}

void PerUserStateManagerChromeOS::OnFirstConsent(bool metrics_consent) {
  // See ShouldWaitForFirstConsent() documentation for when this is called.
  DCHECK_EQ(state_, State::WAITING_ON_FIRST_CONSENT);

  // If metrics collection is disabled, then the ephemeral partition should be
  // used to log metrics. That way, all data collected at the end of the guest
  // session will be deleted when the guest session is over.
  //
  // If metrics collection is enabled, local state should continue to be used
  // to persist logs so that they are persistent.
  if (!metrics_consent)
    AssignUserLogStore();
  else
    DCHECK(!HasUserLogStore());

  state_ = State::USER_LOG_STORE_HANDLED;

  // Sessions only need to keep track of the consent since no user ID will be
  // assigned.
  GetCurrentUserPrefs()->SetBoolean(prefs::kMetricsUserConsent,
                                    metrics_consent);

  // User should not be associated with a user ID.
  local_state_->ClearPref(prefs::kMetricsCurrentUserId);

  // Only change the reporting state if the reporting policy is not managed.
  if (!IsReportingPolicyManaged())
    SetReportingState(metrics_consent);
}

void PerUserStateManagerChromeOS::UpdateCurrentUserId(
    const std::string& new_user_id) {
  DCHECK_EQ(state_, State::USER_LOG_STORE_HANDLED);

  // Updates both the user profile as well as the user ID stored in local state
  // to handle crashes appropriately.
  GetCurrentUserPrefs()->SetString(prefs::kMetricsUserId, new_user_id);
  local_state_->SetString(prefs::kMetricsCurrentUserId, new_user_id);
}

void PerUserStateManagerChromeOS::ResetState() {
  UnsetUserLogStore();

  current_user_ = nullptr;
  local_state_->ClearPref(prefs::kMetricsCurrentUserId);
  state_ = State::CONSTRUCTED;
}

PrefService* PerUserStateManagerChromeOS::GetCurrentUserPrefs() const {
  DCHECK(state_ >= State::USER_PROFILE_READY);
  return ash::ProfileHelper::Get()->GetProfileByUser(current_user_)->GetPrefs();
}

void PerUserStateManagerChromeOS::AssignUserLogStore() {
  SetUserLogStore(std::make_unique<UnsentLogStore>(
      std::make_unique<UnsentLogStoreMetricsImpl>(), GetCurrentUserPrefs(),
      prefs::kMetricsUserMetricLogs, prefs::kMetricsUserMetricLogsMetadata,
      storage_limits_.min_ongoing_log_queue_count,
      storage_limits_.min_ongoing_log_queue_size,
      storage_limits_.max_ongoing_log_size, signing_key_));
}

bool PerUserStateManagerChromeOS::ShouldWaitForFirstConsent() const {
  DCHECK_EQ(state_, State::USER_LOGIN);

  // Managed devices consent should be controlled by
  // ash::StatsReportingController.
  if (IsReportingPolicyManaged())
    return false;

  // If the device owner has not been set and a guest session starts, the
  // decision which log store to use must be deferred until the first consent
  // (done in ToS) is established.
  return current_user_->GetType() == user_manager::USER_TYPE_GUEST &&
         !IsDeviceOwned();
}

void PerUserStateManagerChromeOS::NotifyObservers(bool metrics_consent) {
  callback_list_.Notify(metrics_consent);
}

void PerUserStateManagerChromeOS::UpdateLocalStatePrefs(bool metrics_enabled) {
  // If user consent is turned off, client ID does not need to be reset. Only
  // profile prefs (ie user_id) should be reset.
  if (!metrics_enabled)
    return;

  auto* user_prefs = GetCurrentUserPrefs();

  // TODO(crbug/1266086): In the case that multiple users toggle consent off
  // to on, this will cause the client ID to be reset each time, which is not
  // necessary. Look for a way to allow resetting client id less eager.
  if (user_prefs->GetBoolean(prefs::kMetricsRequiresClientIdResetOnConsent)) {
    ForceClientIdReset();
  } else {
    user_prefs->SetBoolean(prefs::kMetricsRequiresClientIdResetOnConsent, true);
  }
}

}  // namespace metrics
