// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud/user_policy_signin_service_base.h"

#include <utility>

#include "base/bind.h"
#include "base/dcheck_is_on.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/enterprise/util/managed_browser_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/account_id_from_account_info.h"
#include "chrome/common/chrome_content_client.h"
#include "chrome/common/pref_names.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/cloud/device_management_service.h"
#include "components/policy/core/common/cloud/user_cloud_policy_manager.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/storage_partition.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace policy {

UserPolicySigninServiceBase::UserPolicySigninServiceBase(
    Profile* profile,
    PrefService* local_state,
    DeviceManagementService* device_management_service,
    UserCloudPolicyManager* policy_manager,
    signin::IdentityManager* identity_manager,
    scoped_refptr<network::SharedURLLoaderFactory> system_url_loader_factory)
    : profile_(profile),
      policy_manager_(policy_manager),
      identity_manager_(identity_manager),
      local_state_(local_state),
      device_management_service_(device_management_service),
      system_url_loader_factory_(system_url_loader_factory),
      consent_level_(signin::ConsentLevel::kSignin) {
  // Register a listener to be called back once the current profile has finished
  // initializing, so we can startup/shutdown the UserCloudPolicyManager.
  registrar_.Add(this,
                 chrome::NOTIFICATION_PROFILE_ADDED,
                 content::Source<Profile>(profile));
}

UserPolicySigninServiceBase::~UserPolicySigninServiceBase() {}

void UserPolicySigninServiceBase::FetchPolicyForSignedInUser(
    const AccountId& account_id,
    const std::string& dm_token,
    const std::string& client_id,
    scoped_refptr<network::SharedURLLoaderFactory> profile_url_loader_factory,
    PolicyFetchCallback callback) {
  UserCloudPolicyManager* manager = policy_manager();
  DCHECK(manager);

  // Initialize the cloud policy manager there was no prior initialization.
  if (!manager->core()->client()) {
    std::unique_ptr<CloudPolicyClient> client =
        UserCloudPolicyManager::CreateCloudPolicyClient(
            device_management_service_, profile_url_loader_factory);
    client->SetupRegistration(
        dm_token, client_id,
        std::vector<std::string>() /* user_affiliation_ids */);
    DCHECK(client->is_registered());
    DCHECK(!manager->core()->client());
    InitializeUserCloudPolicyManager(account_id, std::move(client));
  }

  DCHECK(manager->IsClientRegistered());

  // Now initiate a policy fetch.
  manager->core()->service()->RefreshPolicy(std::move(callback));
}

void UserPolicySigninServiceBase::OnPrimaryAccountChanged(
    const signin::PrimaryAccountChangeEvent& event) {
  if (event.GetEventTypeFor(signin::ConsentLevel::kSignin) ==
      signin::PrimaryAccountChangeEvent::Type::kCleared) {
    ProfileManager* profile_manager = g_browser_process->profile_manager();
    // Some tests do not have a profile manager.
    if (profile_manager) {
      ProfileAttributesEntry* entry =
          profile_manager->GetProfileAttributesStorage()
              .GetProfileAttributesWithPath(profile_->GetPath());
      if (entry)
        entry->SetUserAcceptedAccountManagement(false);
      ShutdownUserCloudPolicyManager();
    } else if (event.GetEventTypeFor(signin::ConsentLevel::kSync) ==
               signin::PrimaryAccountChangeEvent::Type::kCleared) {
      ShutdownUserCloudPolicyManager();
    }
  }
}

void UserPolicySigninServiceBase::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(chrome::NOTIFICATION_PROFILE_ADDED, type);

  // A new profile has been loaded - if it's signed in, then initialize the
  // UCPM, otherwise shut down the UCPM (which deletes any cached policy
  // data). This must be done here instead of at constructor time because
  // the Profile is not fully initialized when this object is constructed
  // (DoFinalInit() has not yet been called, so ProfileIOData and
  // SSLConfigServiceManager have not been created yet).
  // TODO(atwilson): Switch to using a timer instead, to avoid contention
  // with other services at startup (http://crbug.com/165468).
  InitializeOnProfileReady(content::Source<Profile>(source).ptr());
}

void UserPolicySigninServiceBase::
    OnCloudPolicyServiceInitializationCompleted() {
  // This is meant to be overridden by subclasses. Starting and stopping to
  // observe the CloudPolicyService from this base class avoids the need for
  // more virtuals.
}

void UserPolicySigninServiceBase::OnPolicyFetched(CloudPolicyClient* client) {}

void UserPolicySigninServiceBase::OnRegistrationStateChanged(
    CloudPolicyClient* client) {}

void UserPolicySigninServiceBase::OnClientError(CloudPolicyClient* client) {
  if (client->is_registered()) {
    // If the client is already registered, it means this error must have
    // come from a policy fetch.
    if (client->status() == DM_STATUS_SERVICE_MANAGEMENT_NOT_SUPPORTED) {
      // OK, policy fetch failed with MANAGEMENT_NOT_SUPPORTED - this is our
      // trigger to revert to "unmanaged" mode (we will check for management
      // being re-enabled on the next restart and/or login).
      DVLOG(1) << "DMServer returned NOT_SUPPORTED error - removing policy";

      // Can't shutdown now because we're in the middle of a callback from
      // the CloudPolicyClient, so queue up a task to do the shutdown.
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(
              &UserPolicySigninServiceBase::ShutdownUserCloudPolicyManager,
              weak_factory_.GetWeakPtr()));
    } else {
      DVLOG(1) << "Error fetching policy: " << client->status();
    }
  }
}

void UserPolicySigninServiceBase::Shutdown() {
  if (identity_manager())
    identity_manager()->RemoveObserver(this);
  PrepareForUserCloudPolicyManagerShutdown();
}

void UserPolicySigninServiceBase::PrepareForUserCloudPolicyManagerShutdown() {
  UserCloudPolicyManager* manager = policy_manager();
  if (manager && manager->core()->client())
    manager->core()->client()->RemoveObserver(this);
  if (manager && manager->core()->service())
    manager->core()->service()->RemoveObserver(this);
}

std::unique_ptr<CloudPolicyClient>
UserPolicySigninServiceBase::CreateClientForRegistrationOnly(
    const std::string& username) {
  DCHECK(!username.empty());
  // We should not be called with a client already initialized.
  DCHECK(!policy_manager() || !policy_manager()->core()->client());

  // If the user should not get policy, just bail out.
  if (!policy_manager() || !ShouldLoadPolicyForUser(username)) {
    DVLOG(1) << "Signed in user is not in the allowlist";
    return nullptr;
  }

  // If the DeviceManagementService is not yet initialized, start it up now.
  device_management_service_->ScheduleInitialization(0);

  // Create a new CloudPolicyClient for fetching the DMToken.
  return UserCloudPolicyManager::CreateCloudPolicyClient(
      device_management_service_, system_url_loader_factory_);
}

bool UserPolicySigninServiceBase::ShouldLoadPolicyForUser(
    const std::string& username) {
  if (username.empty())
    return false;  // Not signed in.

  return !BrowserPolicyConnector::IsNonEnterpriseUser(username);
}

void UserPolicySigninServiceBase::InitializeOnProfileReady(Profile* profile) {
  DCHECK_EQ(profile, profile_);
  // If using a TestingProfile with no IdentityManager or
  // UserCloudPolicyManager, skip initialization.
  if (!policy_manager() || !identity_manager()) {
    DVLOG(1) << "Skipping initialization for tests due to missing components.";
    return;
  }

  // Shutdown the UserCloudPolicyManager when the user signs out. We start
  // observing the IdentityManager here because we don't want to get signout
  // notifications until after the profile has started initializing
  // (http://crbug.com/316229).
  identity_manager()->AddObserver(this);

  AccountId account_id = AccountIdFromAccountInfo(
      identity_manager()->GetPrimaryAccountInfo(consent_level()));
  if (!CanApplyPoliciesForSignedInUser(/*check_for_refresh_token=*/false)) {
    ShutdownUserCloudPolicyManager();
  } else {
    InitializeForSignedInUser(account_id,
                              profile->GetDefaultStoragePartition()
                                  ->GetURLLoaderFactoryForBrowserProcess());
  }
}

void UserPolicySigninServiceBase::InitializeForSignedInUser(
    const AccountId& account_id,
    scoped_refptr<network::SharedURLLoaderFactory> profile_url_loader_factory) {
  DCHECK(account_id.is_valid());
  UserCloudPolicyManager* manager = policy_manager();
  if (!ShouldLoadPolicyForUser(account_id.GetUserEmail())) {
    manager->SetPoliciesRequired(false);
    DVLOG(1) << "Policy load not enabled for user: "
             << account_id.GetUserEmail();
    return;
  }

  // Initialize the UCPM if it is not already initialized.
  if (!manager->core()->service()) {
    // If there is no cached DMToken then we can detect this when the
    // OnCloudPolicyServiceInitializationCompleted() callback is invoked and
    // this will initiate a policy fetch.
    InitializeUserCloudPolicyManager(
        account_id,
        UserCloudPolicyManager::CreateCloudPolicyClient(
            device_management_service_, profile_url_loader_factory));
  } else {
    manager->SetSigninAccountId(account_id);
  }

  // If the CloudPolicyService is initialized, kick off registration.
  // Otherwise OnCloudPolicyServiceInitializationCompleted is invoked as soon as
  // the service finishes its initialization.
  if (manager->core()->service()->IsInitializationComplete())
    OnCloudPolicyServiceInitializationCompleted();
}

void UserPolicySigninServiceBase::InitializeUserCloudPolicyManager(
    const AccountId& account_id,
    std::unique_ptr<CloudPolicyClient> client) {
  DCHECK(client);
  UserCloudPolicyManager* manager = policy_manager();
  manager->SetSigninAccountId(account_id);
  DCHECK(!manager->core()->client());
  manager->Connect(local_state_, std::move(client));
  DCHECK(manager->core()->service());

  // Observe the client to detect errors fetching policy.
  manager->core()->client()->AddObserver(this);
  // Observe the service to determine when it's initialized.
  manager->core()->service()->AddObserver(this);
}

void UserPolicySigninServiceBase::ShutdownUserCloudPolicyManager() {
  PrepareForUserCloudPolicyManagerShutdown();
  UserCloudPolicyManager* manager = policy_manager();
  if (manager)
    manager->DisconnectAndRemovePolicy();
}

bool UserPolicySigninServiceBase::CanApplyPoliciesForSignedInUser(
    bool check_for_refresh_token) {
  return (check_for_refresh_token
              ? identity_manager()->HasPrimaryAccountWithRefreshToken(
                    signin::ConsentLevel::kSignin)
              : identity_manager()->HasPrimaryAccount(
                    signin::ConsentLevel::kSignin)) &&
         (profile_can_be_managed_for_testing_ ||
          chrome::enterprise_util::ProfileCanBeManaged(profile()));
}

}  // namespace policy
