// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/components/proximity_auth/proximity_auth_local_state_pref_manager.h"

#include <memory>
#include <string>
#include <vector>

#include "ash/components/proximity_auth/proximity_auth_pref_names.h"
#include "base/json/json_reader.h"
#include "chromeos/services/multidevice_setup/public/cpp/prefs.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace proximity_auth {
namespace {

const char kUser1[] = "songttim@gmail.com";
const bool kIsChromeOSLoginEnabled1 = true;
const bool kIsEasyUnlockAllowed1 = true;
const bool kIsEasyUnlockEnabled1 = true;
const bool kIsSmartLockEligible1 = true;

const char kUser2[] = "tengs@google.com";
const bool kIsChromeOSLoginEnabled2 = false;
const bool kIsEasyUnlockAllowed2 = false;
const bool kIsEasyUnlockEnabled2 = false;
const bool kIsSmartLockEligible2 = false;

const char kUnknownUser[] = "tengs@chromium.org";
const bool kIsChromeOSLoginEnabledDefault = false;
const bool kIsEasyUnlockAllowedDefault = true;
const bool kIsEasyUnlockEnabledDefault = false;
const bool kIsSmartLockEligibleDefault = true;

}  //  namespace

class ProximityAuthLocalStatePrefManagerTest : public testing::Test {
 public:
  ProximityAuthLocalStatePrefManagerTest(
      const ProximityAuthLocalStatePrefManagerTest&) = delete;
  ProximityAuthLocalStatePrefManagerTest& operator=(
      const ProximityAuthLocalStatePrefManagerTest&) = delete;

 protected:
  ProximityAuthLocalStatePrefManagerTest()
      : user1_(AccountId::FromUserEmail(kUser1)),
        user2_(AccountId::FromUserEmail(kUser2)),
        unknown_user_(AccountId::FromUserEmail(kUnknownUser)) {}

  ~ProximityAuthLocalStatePrefManagerTest() override {}

  void SetUp() override {
    ProximityAuthLocalStatePrefManager::RegisterPrefs(local_state_.registry());

    // Note: in normal circumstances, these prefs are synced to local state in
    // ProximityAuthProfilePrefService.
    base::Value user1_prefs(base::Value::Type::DICTIONARY);
    user1_prefs.SetBoolKey(
        proximity_auth::prefs::kProximityAuthIsChromeOSLoginEnabled,
        kIsChromeOSLoginEnabled1);
    user1_prefs.SetBoolKey(
        chromeos::multidevice_setup::kSmartLockAllowedPrefName,
        kIsEasyUnlockAllowed1);
    user1_prefs.SetBoolKey(
        chromeos::multidevice_setup::kSmartLockEnabledPrefName,
        kIsEasyUnlockEnabled1);
    user1_prefs.SetBoolKey(prefs::kSmartLockEligiblePrefName,
                           kIsSmartLockEligible1);
    DictionaryPrefUpdate update1(&local_state_,
                                 prefs::kEasyUnlockLocalStateUserPrefs);
    update1->SetKey(user1_.GetUserEmail(), std::move(user1_prefs));

    base::Value user2_prefs(base::Value::Type::DICTIONARY);
    user2_prefs.SetBoolKey(
        proximity_auth::prefs::kProximityAuthIsChromeOSLoginEnabled,
        kIsChromeOSLoginEnabled2);
    user2_prefs.SetBoolKey(
        chromeos::multidevice_setup::kSmartLockAllowedPrefName,
        kIsEasyUnlockAllowed2);
    user2_prefs.SetBoolKey(
        chromeos::multidevice_setup::kSmartLockEnabledPrefName,
        kIsEasyUnlockEnabled2);
    user2_prefs.SetBoolKey(prefs::kSmartLockEligiblePrefName,
                           kIsSmartLockEligible2);
    DictionaryPrefUpdate update2(&local_state_,
                                 prefs::kEasyUnlockLocalStateUserPrefs);
    update2->SetKey(user2_.GetUserEmail(), std::move(user2_prefs));
  }

  AccountId user1_;
  AccountId user2_;
  AccountId unknown_user_;
  TestingPrefServiceSimple local_state_;
};

TEST_F(ProximityAuthLocalStatePrefManagerTest, RegisterPrefs) {
  EXPECT_TRUE(
      local_state_.FindPreference(prefs::kEasyUnlockLocalStateUserPrefs));
}

TEST_F(ProximityAuthLocalStatePrefManagerTest, IsEasyUnlockAllowed) {
  ProximityAuthLocalStatePrefManager pref_manager(&local_state_);

  // If no active user is set, return the default value.
  EXPECT_EQ(kIsEasyUnlockAllowedDefault, pref_manager.IsEasyUnlockAllowed());

  // Unknown users should return the default value.
  pref_manager.SetActiveUser(unknown_user_);
  EXPECT_EQ(kIsEasyUnlockAllowedDefault, pref_manager.IsEasyUnlockAllowed());

  // Test users with set values.
  pref_manager.SetActiveUser(user1_);
  EXPECT_EQ(kIsEasyUnlockAllowed1, pref_manager.IsEasyUnlockAllowed());
  pref_manager.SetActiveUser(user2_);
  EXPECT_EQ(kIsEasyUnlockAllowed2, pref_manager.IsEasyUnlockAllowed());
}

TEST_F(ProximityAuthLocalStatePrefManagerTest, IsEasyUnlockEnabled) {
  ProximityAuthLocalStatePrefManager pref_manager(&local_state_);

  // If no active user is set, return the default value.
  EXPECT_EQ(kIsEasyUnlockEnabledDefault, pref_manager.IsEasyUnlockEnabled());

  // Unknown users should return the default value.
  pref_manager.SetActiveUser(unknown_user_);
  EXPECT_EQ(kIsEasyUnlockEnabledDefault, pref_manager.IsEasyUnlockEnabled());

  // Test users with set values.
  pref_manager.SetActiveUser(user1_);
  EXPECT_EQ(kIsEasyUnlockEnabled1, pref_manager.IsEasyUnlockEnabled());
  pref_manager.SetActiveUser(user2_);
  EXPECT_EQ(kIsEasyUnlockEnabled2, pref_manager.IsEasyUnlockEnabled());
}

TEST_F(ProximityAuthLocalStatePrefManagerTest, IsSmartLockEligible) {
  ProximityAuthLocalStatePrefManager pref_manager(&local_state_);

  // If no active user is set, return the default value.
  EXPECT_EQ(kIsSmartLockEligibleDefault, pref_manager.IsSmartLockEligible());

  // Unknown users should return the default value.
  pref_manager.SetActiveUser(unknown_user_);
  EXPECT_EQ(kIsSmartLockEligibleDefault, pref_manager.IsSmartLockEligible());

  // Test users with set values.
  pref_manager.SetActiveUser(user1_);
  EXPECT_EQ(kIsSmartLockEligible1, pref_manager.IsSmartLockEligible());
  pref_manager.SetActiveUser(user2_);
  EXPECT_EQ(kIsSmartLockEligible2, pref_manager.IsSmartLockEligible());
}

TEST_F(ProximityAuthLocalStatePrefManagerTest, IsChromeOSLoginEnabled) {
  ProximityAuthLocalStatePrefManager pref_manager(&local_state_);

  // If no active user is set, return the default value.
  EXPECT_EQ(kIsChromeOSLoginEnabledDefault,
            pref_manager.IsChromeOSLoginEnabled());

  // Unknown users should return the default value.
  pref_manager.SetActiveUser(unknown_user_);
  EXPECT_EQ(kIsChromeOSLoginEnabledDefault,
            pref_manager.IsChromeOSLoginEnabled());

  // Test users with set values.
  pref_manager.SetActiveUser(user1_);
  EXPECT_EQ(kIsChromeOSLoginEnabled1, pref_manager.IsChromeOSLoginEnabled());
  pref_manager.SetActiveUser(user2_);
  EXPECT_EQ(kIsChromeOSLoginEnabled2, pref_manager.IsChromeOSLoginEnabled());
}

}  // namespace proximity_auth
