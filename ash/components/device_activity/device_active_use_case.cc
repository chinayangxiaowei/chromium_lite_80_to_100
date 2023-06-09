// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/components/device_activity/device_active_use_case.h"

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "chromeos/system/statistics_provider.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "crypto/hmac.h"
#include "third_party/private_membership/src/private_membership_rlwe_client.h"

namespace ash {
namespace device_activity {

namespace psm_rlwe = private_membership::rlwe;

namespace {

// Default value for devices that are missing the hardware class.
const char kHardwareClassKeyNotFound[] = "HARDWARE_CLASS_KEY_NOT_FOUND";

}  // namespace

DeviceActiveUseCase::DeviceActiveUseCase(
    PrefService* local_state,
    const std::string& psm_device_active_secret,
    const std::string& use_case_pref_key,
    psm_rlwe::RlweUseCase psm_use_case)
    : local_state_(local_state),
      psm_device_active_secret_(psm_device_active_secret),
      use_case_pref_key_(use_case_pref_key),
      psm_use_case_(psm_use_case),
      statistics_provider_(
          chromeos::system::StatisticsProvider::GetInstance()) {}

DeviceActiveUseCase::~DeviceActiveUseCase() = default;

PrefService* DeviceActiveUseCase::GetLocalState() const {
  return local_state_;
}

// Return the last known daily ping timestamp from local state pref.
base::Time DeviceActiveUseCase::GetLastKnownPingTimestamp() const {
  return GetLocalState()->GetTime(use_case_pref_key_);
}

// Set the last known daily ping timestamp in local state pref.
void DeviceActiveUseCase::SetLastKnownPingTimestamp(base::Time new_ts) {
  GetLocalState()->SetTime(use_case_pref_key_, new_ts);
}

psm_rlwe::RlweUseCase DeviceActiveUseCase::GetPsmUseCase() const {
  return psm_use_case_;
}

absl::optional<std::string> DeviceActiveUseCase::GetWindowIdentifier() const {
  return window_id_;
}

void DeviceActiveUseCase::SetWindowIdentifier(
    absl::optional<std::string> window_id) {
  window_id_ = window_id;

  // nullopt the psm_id_ if a new window_id gets assigned.
  psm_id_ = absl::nullopt;
}

std::string DeviceActiveUseCase::GetDigestString(
    const std::string& key,
    const std::string& message) const {
  crypto::HMAC hmac(crypto::HMAC::SHA256);
  std::vector<uint8_t> digest(hmac.DigestLength());
  if (!hmac.Init(key) || !hmac.Sign(message, &digest[0], digest.size())) {
    return std::string();
  }
  return base::HexEncode(&digest[0], digest.size());
}

absl::optional<psm_rlwe::RlwePlaintextId>
DeviceActiveUseCase::GetPsmIdentifier() {
  if (!psm_id_.has_value()) {
    psm_id_ = GeneratePsmIdentifier();
  }
  return psm_id_;
}

void DeviceActiveUseCase::SetPsmIdentifier(psm_rlwe::RlwePlaintextId psm_id) {
  psm_id_ = psm_id;
}

psm_rlwe::PrivateMembershipRlweClient* DeviceActiveUseCase::GetPsmRlweClient() {
  return psm_rlwe_client_.get();
}

void DeviceActiveUseCase::SetPsmRlweClient(
    std::unique_ptr<psm_rlwe::PrivateMembershipRlweClient> psm_rlwe_client) {
  if (psm_rlwe_client) {
    VLOG(1) << "Empty psm_rlwe_client passed to setter.";
    return;
  }

  psm_rlwe_client_ = std::move(psm_rlwe_client);
}

bool DeviceActiveUseCase::IsDevicePingRequired(base::Time prev_ping_ts,
                                               base::Time new_ping_ts) const {
  std::string prev_ping_window_id = GenerateUTCWindowIdentifier(prev_ping_ts);
  std::string new_ping_window_id = GenerateUTCWindowIdentifier(new_ping_ts);

  // Safety check to avoid against clock drift, or unexpected timestamps.
  // Check should make sure that we are not reporting window id's for
  // day's previous to one that we reported already.
  return prev_ping_ts < new_ping_ts &&
         prev_ping_window_id != new_ping_window_id;
}

std::string DeviceActiveUseCase::GetFullHardwareClass() const {
  // Retrieve full hardware class from machine statistics object.
  // Default |full_hardware_class| to kHardwareClassKeyNotFound if retrieval
  // from machine statistics fails.
  std::string full_hardware_class = kHardwareClassKeyNotFound;
  statistics_provider_->GetMachineStatistic(chromeos::system::kHardwareClassKey,
                                            &full_hardware_class);
  return full_hardware_class;
}

std::string DeviceActiveUseCase::GetChromeOSVersion() const {
  return version_info::GetMajorVersionNumber();
}

absl::optional<psm_rlwe::RlwePlaintextId>
DeviceActiveUseCase::GeneratePsmIdentifier() const {
  const std::string psm_use_case = psm_rlwe::RlweUseCase_Name(GetPsmUseCase());
  absl::optional<std::string> window_id = GetWindowIdentifier();
  if (psm_device_active_secret_.empty() || psm_use_case.empty() ||
      !window_id.has_value()) {
    VLOG(1) << "Can not generate PSM id without the psm device secret, use "
               "case, and window id being defined.";
    return absl::nullopt;
  }

  std::string unhashed_psm_id =
      base::JoinString({psm_use_case, window_id.value()}, "|");

  // Convert bytes to hex to avoid encoding/decoding proto issues across
  // client/server.
  std::string psm_id_hex =
      GetDigestString(psm_device_active_secret_, unhashed_psm_id);

  if (!psm_id_hex.empty()) {
    psm_rlwe::RlwePlaintextId psm_rlwe_id;
    psm_rlwe_id.set_sensitive_id(psm_id_hex);
    return psm_rlwe_id;
  }

  // Failed HMAC-SHA256 hash on PSM id.
  VLOG(1) << "Failed to calculate HMAC-256 has on PSM id.";
  return absl::nullopt;
}

}  // namespace device_activity
}  // namespace ash
