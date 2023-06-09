// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/chromeos/fingerprint_handler.h"

#include <algorithm>
#include <memory>

#include "ash/constants/ash_pref_names.h"
#include "base/bind.h"
#include "base/containers/contains.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/ash/login/quick_unlock/auth_token.h"
#include "chrome/browser/ash/login/quick_unlock/quick_unlock_factory.h"
#include "chrome/browser/ash/login/quick_unlock/quick_unlock_storage.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/device_service.h"
#include "ui/base/l10n/l10n_util.h"

using session_manager::SessionManager;
using session_manager::SessionState;

namespace chromeos {
namespace settings {
namespace {

// The max number of fingerprints that can be stored.
constexpr int kMaxAllowedFingerprints = 3;

std::unique_ptr<base::DictionaryValue> GetFingerprintsInfo(
    const std::vector<std::string>& fingerprints_list) {
  auto response = std::make_unique<base::DictionaryValue>();
  base::ListValue fingerprints;

  DCHECK_LE(static_cast<int>(fingerprints_list.size()),
            kMaxAllowedFingerprints);
  for (auto& fingerprint_name: fingerprints_list) {
    base::Value str(fingerprint_name);
    fingerprints.Append(std::move(str));
  }

  response->SetKey("fingerprintsList", std::move(fingerprints));
  response->SetBoolKey("isMaxed", static_cast<int>(fingerprints_list.size()) >=
                                      kMaxAllowedFingerprints);
  return response;
}

}  // namespace

FingerprintHandler::FingerprintHandler(Profile* profile) : profile_(profile) {
  content::GetDeviceService().BindFingerprint(
      fp_service_.BindNewPipeAndPassReceiver());
  user_id_ = ProfileHelper::Get()->GetUserIdHashFromProfile(profile);
}

FingerprintHandler::~FingerprintHandler() {
}

void FingerprintHandler::RegisterMessages() {
  // Note: getFingerprintsList must be called before observers will be added.
  web_ui()->RegisterMessageCallback(
      "getFingerprintsList",
      base::BindRepeating(&FingerprintHandler::HandleGetFingerprintsList,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getNumFingerprints",
      base::BindRepeating(&FingerprintHandler::HandleGetNumFingerprints,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "startEnroll", base::BindRepeating(&FingerprintHandler::HandleStartEnroll,
                                         base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "cancelCurrentEnroll",
      base::BindRepeating(&FingerprintHandler::HandleCancelCurrentEnroll,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getEnrollmentLabel",
      base::BindRepeating(&FingerprintHandler::HandleGetEnrollmentLabel,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "removeEnrollment",
      base::BindRepeating(&FingerprintHandler::HandleRemoveEnrollment,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "changeEnrollmentLabel",
      base::BindRepeating(&FingerprintHandler::HandleChangeEnrollmentLabel,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "startAuthentication",
      base::BindRepeating(&FingerprintHandler::HandleStartAuthentication,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "endCurrentAuthentication",
      base::BindRepeating(&FingerprintHandler::HandleEndCurrentAuthentication,
                          base::Unretained(this)));
}

void FingerprintHandler::OnJavascriptAllowed() {
  // SessionManager may not exist in some tests.
  if (SessionManager::Get())
    session_observation_.Observe(SessionManager::Get());

  fp_service_->AddFingerprintObserver(receiver_.BindNewPipeAndPassRemote());
}

void FingerprintHandler::OnJavascriptDisallowed() {
  session_observation_.Reset();
  receiver_.reset();
}

void FingerprintHandler::OnRestarted() {}

void FingerprintHandler::OnEnrollScanDone(device::mojom::ScanResult scan_result,
                                          bool enroll_session_complete,
                                          int percent_complete) {
  VLOG(1) << "Receive fingerprint enroll scan result. scan_result="
          << scan_result
          << ", enroll_session_complete=" << enroll_session_complete
          << ", percent_complete=" << percent_complete;
  auto scan_attempt = std::make_unique<base::DictionaryValue>();
  scan_attempt->SetIntKey("result", static_cast<int>(scan_result));
  scan_attempt->SetBoolKey("isComplete", enroll_session_complete);
  scan_attempt->SetIntKey("percentComplete", percent_complete);

  FireWebUIListener("on-fingerprint-scan-received", *scan_attempt);
}

void FingerprintHandler::OnAuthScanDone(
    const device::mojom::FingerprintMessagePtr msg,
    const base::flat_map<std::string, std::vector<std::string>>& matches) {
  switch (msg->which()) {
    case device::mojom::FingerprintMessage::Tag::kScanResult:
      VLOG(1) << "Receive fingerprint auth scan result. scan_result="
              << msg->get_scan_result();
      return;
    case device::mojom::FingerprintMessage::Tag::kFingerprintError:
      VLOG(1) << "Receive fingerprint auth error. error="
              << msg->get_fingerprint_error();
      return;
  }
  NOTREACHED();
}

void FingerprintHandler::OnSessionFailed() {
  LOG(ERROR) << "Fingerprint session failed.";
}

void FingerprintHandler::OnSessionStateChanged() {
  SessionState state = SessionManager::Get()->session_state();

  FireWebUIListener("on-screen-locked",
                    base::Value(state == SessionState::LOCKED));
}

void FingerprintHandler::HandleGetFingerprintsList(
    base::Value::ConstListView args) {
  CHECK_EQ(1U, args.size());
  const std::string& callback_id = args[0].GetString();

  AllowJavascript();
  fp_service_->GetRecordsForUser(
      user_id_, base::BindOnce(&FingerprintHandler::OnGetFingerprintsList,
                               weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void FingerprintHandler::OnGetFingerprintsList(
    const std::string& callback_id,
    const base::flat_map<std::string, std::string>& fingerprints_list_mapping) {
  fingerprints_labels_.clear();
  fingerprints_paths_.clear();
  for (auto it = fingerprints_list_mapping.begin();
       it != fingerprints_list_mapping.end(); ++it) {
    fingerprints_paths_.push_back(it->first);
    fingerprints_labels_.push_back(it->second);
  }

  profile_->GetPrefs()->SetInteger(prefs::kQuickUnlockFingerprintRecord,
                                   fingerprints_list_mapping.size());

  std::unique_ptr<base::DictionaryValue> fingerprint_info =
      GetFingerprintsInfo(fingerprints_labels_);
  ResolveJavascriptCallback(base::Value(callback_id), *fingerprint_info);
}

void FingerprintHandler::HandleGetNumFingerprints(
    base::Value::ConstListView args) {
  CHECK_EQ(1U, args.size());
  const std::string& callback_id = args[0].GetString();

  int fingerprints_num =
      profile_->GetPrefs()->GetInteger(prefs::kQuickUnlockFingerprintRecord);

  AllowJavascript();
  ResolveJavascriptCallback(base::Value(callback_id),
                            base::Value(fingerprints_num));
}

void FingerprintHandler::HandleStartEnroll(base::Value::ConstListView args) {
  AllowJavascript();

  const std::string& auth_token = args[0].GetString();

  // Auth token expiration will trigger password prompt.
  // Silently fail if auth token is incorrect.
  quick_unlock::QuickUnlockStorage* quick_unlock_storage =
      quick_unlock::QuickUnlockFactory::GetForProfile(profile_);
  if (!quick_unlock_storage->GetAuthToken())
    return;
  if (auth_token != quick_unlock_storage->GetAuthToken()->Identifier())
    return;

  // Determines what the newly added fingerprint's name should be.
  for (int i = 1; i <= kMaxAllowedFingerprints; ++i) {
    std::string fingerprint_name = l10n_util::GetStringFUTF8(
        IDS_SETTINGS_PEOPLE_LOCK_SCREEN_NEW_FINGERPRINT_DEFAULT_NAME,
        base::NumberToString16(i));
    if (!base::Contains(fingerprints_labels_, fingerprint_name)) {
      fp_service_->StartEnrollSession(user_id_, fingerprint_name);
      break;
    }
  }
}

void FingerprintHandler::HandleCancelCurrentEnroll(
    base::Value::ConstListView args) {
  AllowJavascript();
  fp_service_->CancelCurrentEnrollSession(
      base::BindOnce(&FingerprintHandler::OnCancelCurrentEnrollSession,
                     weak_ptr_factory_.GetWeakPtr()));
}

void FingerprintHandler::OnCancelCurrentEnrollSession(bool success) {
  if (!success)
    LOG(ERROR) << "Failed to cancel current fingerprint enroll session.";
}

void FingerprintHandler::HandleGetEnrollmentLabel(
    base::Value::ConstListView args) {
  const auto& list = args;
  CHECK_EQ(2U, list.size());
  std::string callback_id = list[0].GetString();
  int index = list[1].GetInt();
  CHECK_GE(index, 0);
  CHECK_LT(index, static_cast<int>(fingerprints_paths_.size()));

  AllowJavascript();
  fp_service_->RequestRecordLabel(
      fingerprints_paths_[index],
      base::BindOnce(&FingerprintHandler::OnRequestRecordLabel,
                     weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void FingerprintHandler::OnRequestRecordLabel(const std::string& callback_id,
                                              const std::string& label) {
  ResolveJavascriptCallback(base::Value(callback_id), base::Value(label));
}

void FingerprintHandler::HandleRemoveEnrollment(
    base::Value::ConstListView args) {
  const auto& list = args;
  CHECK_EQ(2U, list.size());
  std::string callback_id = list[0].GetString();
  int index = list[1].GetInt();
  CHECK_GE(index, 0);
  CHECK_LT(index, static_cast<int>(fingerprints_paths_.size()));

  AllowJavascript();
  fp_service_->RemoveRecord(
      fingerprints_paths_[index],
      base::BindOnce(&FingerprintHandler::OnRemoveRecord,
                     weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void FingerprintHandler::OnRemoveRecord(const std::string& callback_id,
                                        bool success) {
  if (!success)
    LOG(ERROR) << "Failed to remove fingerprint record.";
  ResolveJavascriptCallback(base::Value(callback_id), base::Value(success));
}

void FingerprintHandler::HandleChangeEnrollmentLabel(
    base::Value::ConstListView args) {
  const auto& list = args;
  CHECK_EQ(3U, list.size());

  std::string callback_id = list[0].GetString();
  int index = list[1].GetInt();
  CHECK_GE(index, 0);
  CHECK_LT(index, static_cast<int>(fingerprints_paths_.size()));

  std::string new_label = list[2].GetString();

  AllowJavascript();
  fp_service_->SetRecordLabel(
      new_label, fingerprints_paths_[index],
      base::BindOnce(&FingerprintHandler::OnSetRecordLabel,
                     weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void FingerprintHandler::OnSetRecordLabel(const std::string& callback_id,
                                          bool success) {
  if (!success)
    LOG(ERROR) << "Failed to set fingerprint record label.";
  ResolveJavascriptCallback(base::Value(callback_id), base::Value(success));
}

void FingerprintHandler::HandleStartAuthentication(
    base::Value::ConstListView args) {
  AllowJavascript();
  fp_service_->StartAuthSession();
}

void FingerprintHandler::HandleEndCurrentAuthentication(
    base::Value::ConstListView args) {
  AllowJavascript();
  fp_service_->EndCurrentAuthSession(
      base::BindOnce(&FingerprintHandler::OnEndCurrentAuthSession,
                     weak_ptr_factory_.GetWeakPtr()));
}

void FingerprintHandler::OnEndCurrentAuthSession(bool success) {
  if (!success)
    LOG(ERROR) << "Failed to end current fingerprint authentication session.";
}

}  // namespace settings
}  // namespace chromeos
