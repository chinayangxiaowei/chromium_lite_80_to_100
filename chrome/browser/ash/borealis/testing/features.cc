// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/borealis/testing/features.h"

#include "chrome/browser/ash/borealis/borealis_prefs.h"
#include "chrome/browser/ash/login/users/fake_chrome_user_manager.h"
#include "chrome/common/chrome_features.h"
#include "chrome/test/base/testing_profile.h"

namespace borealis {

void AllowBorealis(TestingProfile* profile,
                   base::test::ScopedFeatureList* features,
                   ash::FakeChromeUserManager* user_manager,
                   bool also_enable) {
  features->InitAndEnableFeature(features::kBorealis);
  AccountId account_id =
      AccountId::FromUserEmail(profile->GetProfileUserName());
  user_manager->AddUserWithAffiliation(account_id, /*is_affiliated=*/false);
  user_manager->LoginUser(account_id);
  profile->GetPrefs()->SetBoolean(prefs::kBorealisInstalledOnDevice,
                                  also_enable);
}

ScopedAllowBorealis::ScopedAllowBorealis(TestingProfile* profile,
                                         bool also_enable)
    : profile_(profile),
      user_manager_(std::make_unique<ash::FakeChromeUserManager>()) {
  AllowBorealis(profile_, &features_,
                static_cast<ash::FakeChromeUserManager*>(
                    user_manager::UserManager::Get()),
                also_enable);
}

ScopedAllowBorealis::~ScopedAllowBorealis() {
  profile_->GetPrefs()->SetBoolean(prefs::kBorealisInstalledOnDevice, false);
}

}  // namespace borealis
