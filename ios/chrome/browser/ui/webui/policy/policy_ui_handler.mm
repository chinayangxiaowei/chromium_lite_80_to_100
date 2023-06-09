// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/ui/webui/policy/policy_ui_handler.h"

#import <UIKit/UIKit.h>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#import "base/strings/sys_string_conversions.h"
#include "base/values.h"
#include "components/enterprise/browser/controller/browser_dm_token_storage.h"
#include "components/enterprise/browser/controller/chrome_browser_cloud_management_controller.h"
#include "components/enterprise/browser/reporting/common_pref_names.h"
#include "components/policy/core/browser/policy_conversions.h"
#include "components/policy/core/browser/webui/json_generation.h"
#include "components/policy/core/browser/webui/machine_level_user_cloud_policy_status_provider.h"
#include "components/policy/core/common/cloud/machine_level_user_cloud_policy_manager.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/core/common/policy_types.h"
#include "components/policy/core/common/schema.h"
#include "components/policy/core/common/schema_map.h"
#include "components/policy/policy_constants.h"
#include "components/prefs/pref_service.h"
#include "components/strings/grit/components_strings.h"
#include "components/version_info/version_info.h"
#include "ios/chrome/browser/application_context.h"
#include "ios/chrome/browser/browser_state/chrome_browser_state.h"
#include "ios/chrome/browser/policy/browser_policy_connector_ios.h"
#include "ios/chrome/browser/policy/browser_state_policy_connector.h"
#include "ios/chrome/browser/policy/policy_conversions_client_ios.h"
#import "ios/chrome/common/channel_info.h"
#include "ios/chrome/grit/ios_chromium_strings.h"
#include "ios/chrome/grit/ios_strings.h"
#import "ui/base/l10n/l10n_util.h"
#include "ui/base/webui/web_ui_util.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

PolicyUIHandler::PolicyUIHandler() = default;

PolicyUIHandler::~PolicyUIHandler() {
  GetPolicyService()->RemoveObserver(policy::POLICY_DOMAIN_CHROME, this);
  policy::SchemaRegistry* registry = ChromeBrowserState::FromWebUIIOS(web_ui())
                                         ->GetPolicyConnector()
                                         ->GetSchemaRegistry();
  registry->RemoveObserver(this);
}

void PolicyUIHandler::AddCommonLocalizedStringsToSource(
    web::WebUIIOSDataSource* source) {
  static constexpr webui::LocalizedString kStrings[] = {
      {"conflict", IDS_POLICY_LABEL_CONFLICT},
      {"superseding", IDS_POLICY_LABEL_SUPERSEDING},
      {"conflictValue", IDS_POLICY_LABEL_CONFLICT_VALUE},
      {"supersededValue", IDS_POLICY_LABEL_SUPERSEDED_VALUE},
      {"headerLevel", IDS_POLICY_HEADER_LEVEL},
      {"headerName", IDS_POLICY_HEADER_NAME},
      {"headerScope", IDS_POLICY_HEADER_SCOPE},
      {"headerSource", IDS_POLICY_HEADER_SOURCE},
      {"headerStatus", IDS_POLICY_HEADER_STATUS},
      {"headerValue", IDS_POLICY_HEADER_VALUE},
      {"warning", IDS_POLICY_HEADER_WARNING},
      {"levelMandatory", IDS_POLICY_LEVEL_MANDATORY},
      {"levelRecommended", IDS_POLICY_LEVEL_RECOMMENDED},
      {"error", IDS_POLICY_LABEL_ERROR},
      {"deprecated", IDS_POLICY_LABEL_DEPRECATED},
      {"future", IDS_POLICY_LABEL_FUTURE},
      {"info", IDS_POLICY_LABEL_INFO},
      {"ignored", IDS_POLICY_LABEL_IGNORED},
      {"notSpecified", IDS_POLICY_NOT_SPECIFIED},
      {"ok", IDS_POLICY_OK},
      {"scopeDevice", IDS_POLICY_SCOPE_DEVICE},
      {"scopeUser", IDS_POLICY_SCOPE_USER},
      {"title", IDS_POLICY_TITLE},
      {"unknown", IDS_POLICY_UNKNOWN},
      {"unset", IDS_POLICY_UNSET},
      {"value", IDS_POLICY_LABEL_VALUE},
      {"sourceDefault", IDS_POLICY_SOURCE_DEFAULT},
      {"loadPoliciesDone", IDS_POLICY_LOAD_POLICIES_DONE},
      {"loadingPolicies", IDS_POLICY_LOADING_POLICIES},
  };
  source->AddLocalizedStrings(kStrings);
  source->AddLocalizedStrings(policy::kPolicySources);
  source->UseStringsJs();
}

void PolicyUIHandler::RegisterMessages() {
  policy::MachineLevelUserCloudPolicyManager* manager =
      GetApplicationContext()
          ->GetBrowserPolicyConnector()
          ->machine_level_user_cloud_policy_manager();

  if (manager) {
    policy::BrowserDMTokenStorage* dmTokenStorage =
        policy::BrowserDMTokenStorage::Get();

    base::Time lastCloudReportSent;
    PrefService* prefService = GetApplicationContext()->GetLocalState();

    if (prefService->HasPrefPath(
            enterprise_reporting::kLastUploadSucceededTimestamp)) {
      lastCloudReportSent = prefService->GetTime(
          enterprise_reporting::kLastUploadSucceededTimestamp);
    }

    machine_status_provider_ =
        std::make_unique<policy::MachineLevelUserCloudPolicyStatusProvider>(
            manager->core(),
            new policy::MachineLevelUserCloudPolicyContext(
                {dmTokenStorage->RetrieveEnrollmentToken(),
                 dmTokenStorage->RetrieveClientId(), lastCloudReportSent}));
  }

  if (!machine_status_provider_)
    machine_status_provider_ = std::make_unique<policy::PolicyStatusProvider>();

  machine_status_provider_->SetStatusChangeCallback(base::BindRepeating(
      &PolicyUIHandler::SendStatus, base::Unretained(this)));

  GetPolicyService()->AddObserver(policy::POLICY_DOMAIN_CHROME, this);

  ChromeBrowserState* browser_state =
      ChromeBrowserState::FromWebUIIOS(web_ui());
  browser_state->GetPolicyConnector()->GetSchemaRegistry()->AddObserver(this);

  web_ui()->RegisterMessageCallback(
      "listenPoliciesUpdates",
      base::BindRepeating(&PolicyUIHandler::HandleListenPoliciesUpdates,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "reloadPolicies",
      base::BindRepeating(&PolicyUIHandler::HandleReloadPolicies,
                          base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "copyPoliciesJSON",
      base::BindRepeating(&PolicyUIHandler::HandleCopyPoliciesJson,
                          base::Unretained(this)));
}

void PolicyUIHandler::HandleCopyPoliciesJson(base::Value::ConstListView args) {
  NSString* jsonString = base::SysUTF8ToNSString(GetPoliciesAsJson());
  [UIPasteboard generalPasteboard].string = jsonString;
}

std::string PolicyUIHandler::GetPoliciesAsJson() {
  auto client = std::make_unique<PolicyConversionsClientIOS>(
      ChromeBrowserState::FromWebUIIOS(web_ui()));

  return policy::GenerateJson(
      std::move(client), GetStatusValue(/*include_box_legend_key=*/false),
      policy::JsonGenerationParams()
          .with_application_name(l10n_util::GetStringUTF8(IDS_IOS_PRODUCT_NAME))
          .with_channel_name(GetChannelString(GetChannel()))
          .with_processor_variation(l10n_util::GetStringUTF8(
              sizeof(void*) == 8 ? IDS_VERSION_UI_64BIT : IDS_VERSION_UI_32BIT))
          .with_os_name(version_info::GetOSType()));
}

void PolicyUIHandler::OnSchemaRegistryUpdated(bool has_new_schemas) {
  // Update UI when new schema is added.
  if (has_new_schemas) {
    SendPolicies();
  }
}

void PolicyUIHandler::OnPolicyUpdated(const policy::PolicyNamespace& ns,
                                      const policy::PolicyMap& previous,
                                      const policy::PolicyMap& current) {
  SendPolicies();
}

base::Value PolicyUIHandler::GetPolicyNames() const {
  ChromeBrowserState* browser_state =
      ChromeBrowserState::FromWebUIIOS(web_ui());
  policy::SchemaRegistry* registry =
      browser_state->GetPolicyConnector()->GetSchemaRegistry();
  scoped_refptr<policy::SchemaMap> schema_map = registry->schema_map();

  // Add Chrome policy names.
  base::Value chrome_policy_names(base::Value::Type::LIST);
  policy::PolicyNamespace chrome_namespace(policy::POLICY_DOMAIN_CHROME, "");
  const policy::Schema* chrome_schema = schema_map->GetSchema(chrome_namespace);
  for (auto it = chrome_schema->GetPropertiesIterator(); !it.IsAtEnd();
       it.Advance()) {
    chrome_policy_names.Append(base::Value(it.key()));
  }

  base::Value chrome_values(base::Value::Type::DICTIONARY);
  chrome_values.SetStringKey("name", "Chrome Policies");
  chrome_values.SetKey("policyNames", std::move(chrome_policy_names));

  base::Value names(base::Value::Type::DICTIONARY);
  names.SetKey("chrome", std::move(chrome_values));
  return names;
}

base::Value PolicyUIHandler::GetPolicyValues() const {
  auto client = std::make_unique<PolicyConversionsClientIOS>(
      ChromeBrowserState::FromWebUIIOS(web_ui()));
  return policy::ArrayPolicyConversions(std::move(client))
      .EnableConvertValues(true)
      .ToValue();
}

void PolicyUIHandler::HandleListenPoliciesUpdates(
    base::Value::ConstListView args) {
  OnRefreshPoliciesDone();
}

void PolicyUIHandler::HandleReloadPolicies(base::Value::ConstListView args) {
  GetPolicyService()->RefreshPolicies(base::BindOnce(
      &PolicyUIHandler::OnRefreshPoliciesDone, weak_factory_.GetWeakPtr()));
}

void PolicyUIHandler::SendPolicies() {
  base::Value names = GetPolicyNames();
  base::Value values = GetPolicyValues();
  std::vector<const base::Value*> args;
  args.push_back(&names);
  args.push_back(&values);
  web_ui()->FireWebUIListener("policies-updated", args);
}

base::Value PolicyUIHandler::GetStatusValue(bool include_box_legend_key) const {
  std::unique_ptr<base::DictionaryValue> machine_status(
      new base::DictionaryValue);
  machine_status_provider_->GetStatus(machine_status.get());

  // Given that it's usual for users to bring their own devices and the fact
  // that device names could expose personal information. We do not show
  // this field in Device Policy Box
  if (machine_status->HasKey("machine"))
    machine_status->RemoveKey("machine");

  base::DictionaryValue status;
  if (!machine_status->DictEmpty()) {
    if (include_box_legend_key) {
      machine_status->SetString("boxLegendKey", "statusDevice");
    }
    status.Set("machine", std::move(machine_status));
  }
  return status;
}

void PolicyUIHandler::SendStatus() {
  base::Value status = GetStatusValue(/*include_box_legend_key=*/true);
  std::vector<const base::Value*> args = {&status};
  web_ui()->FireWebUIListener("status-updated", args);
}

void PolicyUIHandler::OnRefreshPoliciesDone() {
  SendPolicies();
  SendStatus();
}

policy::PolicyService* PolicyUIHandler::GetPolicyService() const {
  ChromeBrowserState* browser_state =
      ChromeBrowserState::FromWebUIIOS(web_ui());
  return browser_state->GetPolicyConnector()->GetPolicyService();
}
