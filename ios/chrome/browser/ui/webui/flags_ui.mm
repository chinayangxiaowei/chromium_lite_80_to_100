// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/ui/webui/flags_ui.h"

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "build/branding_buildflags.h"
#include "components/flags_ui/flags_ui_constants.h"
#include "components/flags_ui/flags_ui_pref_names.h"
#include "components/flags_ui/pref_service_flags_storage.h"
#include "components/grit/components_resources.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/strings/grit/components_chromium_strings.h"
#include "components/strings/grit/components_strings.h"
#include "components/version_info/version_info.h"
#include "ios/chrome/browser/application_context.h"
#include "ios/chrome/browser/browser_state/chrome_browser_state.h"
#include "ios/chrome/browser/chrome_url_constants.h"
#include "ios/chrome/browser/flags/about_flags.h"
#include "ios/web/public/webui/web_ui_ios.h"
#include "ios/web/public/webui/web_ui_ios_data_source.h"
#include "ios/web/public/webui/web_ui_ios_message_handler.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

web::WebUIIOSDataSource* CreateFlagsUIHTMLSource() {
  web::WebUIIOSDataSource* source =
      web::WebUIIOSDataSource::Create(kChromeUIFlagsHost);
  source->AddString(flags_ui::kVersion, version_info::GetVersionNumber());

  source->UseStringsJs();
  FlagsUI::AddFlagsIOSStrings(source);
  source->AddResourcePath(flags_ui::kFlagsJS, IDR_FLAGS_UI_FLAGS_JS);
  source->AddResourcePath(flags_ui::kFlagsCSS, IDR_FLAGS_UI_FLAGS_CSS);
  source->SetDefaultResource(IDR_FLAGS_UI_FLAGS_HTML);
  source->UseStringsJs();
  return source;
}

////////////////////////////////////////////////////////////////////////////////
//
// FlagsDOMHandler
//
////////////////////////////////////////////////////////////////////////////////

// The handler for Javascript messages for the about:flags page.
class FlagsDOMHandler : public web::WebUIIOSMessageHandler {
 public:
  FlagsDOMHandler() : access_(flags_ui::kGeneralAccessFlagsOnly) {}

  FlagsDOMHandler(const FlagsDOMHandler&) = delete;
  FlagsDOMHandler& operator=(const FlagsDOMHandler&) = delete;

  ~FlagsDOMHandler() override {}

  // Initializes the DOM handler with the provided flags storage and flags
  // access. If there were flags experiments requested from javascript before
  // this was called, it calls |HandleRequestExperimentalFeatures| again.
  void Init(std::unique_ptr<flags_ui::FlagsStorage> flags_storage,
            flags_ui::FlagAccess access);

  // WebUIMessageHandler implementation.
  void RegisterMessages() override;

  // Callback for the "requestExperimentFeatures" message.
  void HandleRequestExperimentalFeatures(base::Value::ConstListView args);

  // Callback for the "enableExperimentalFeature" message.
  void HandleEnableExperimentalFeatureMessage(base::Value::ConstListView args);

  // Callback for the "restartBrowser" message. Restores all tabs on restart.
  void HandleRestartBrowser(base::Value::ConstListView args);

  // Callback for the "resetAllFlags" message.
  void HandleResetAllFlags(base::Value::ConstListView args);

 private:
  std::unique_ptr<flags_ui::FlagsStorage> flags_storage_;
  flags_ui::FlagAccess access_;
};

void FlagsDOMHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      flags_ui::kRequestExperimentalFeatures,
      base::BindRepeating(&FlagsDOMHandler::HandleRequestExperimentalFeatures,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      flags_ui::kEnableExperimentalFeature,
      base::BindRepeating(
          &FlagsDOMHandler::HandleEnableExperimentalFeatureMessage,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      flags_ui::kRestartBrowser,
      base::BindRepeating(&FlagsDOMHandler::HandleRestartBrowser,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      flags_ui::kResetAllFlags,
      base::BindRepeating(&FlagsDOMHandler::HandleResetAllFlags,
                          base::Unretained(this)));
}

void FlagsDOMHandler::Init(
    std::unique_ptr<flags_ui::FlagsStorage> flags_storage,
    flags_ui::FlagAccess access) {
  flags_storage_ = std::move(flags_storage);
  access_ = access;
}

void FlagsDOMHandler::HandleRequestExperimentalFeatures(
    base::Value::ConstListView args) {
  DCHECK(flags_storage_);
  DCHECK(!args.empty());
  const base::Value& callback_id = args[0];

  std::vector<base::Value> supported_features;
  std::vector<base::Value> unsupported_features;
  GetFlagFeatureEntries(flags_storage_.get(), access_, supported_features,
                        unsupported_features);

  base::Value results(base::Value::Type::DICTIONARY);
  results.SetKey(flags_ui::kSupportedFeatures,
                 base::Value(std::move(supported_features)));
  results.SetKey(flags_ui::kUnsupportedFeatures,
                 base::Value(std::move(unsupported_features)));

  // Cannot restart the browser on iOS.
  results.SetBoolKey(flags_ui::kNeedsRestart, false);
  results.SetBoolKey(flags_ui::kShowOwnerWarning,
                     access_ == flags_ui::kGeneralAccessFlagsOnly);

  results.SetBoolKey(flags_ui::kShowBetaChannelPromotion, false);
  results.SetBoolKey(flags_ui::kShowDevChannelPromotion, false);

  web_ui()->ResolveJavascriptCallback(callback_id, results);
}

void FlagsDOMHandler::HandleEnableExperimentalFeatureMessage(
    base::Value::ConstListView args) {
  DCHECK(flags_storage_);
  DCHECK_EQ(2u, args.size());
  if (args.size() != 2)
    return;

  const std::string* entry_internal_name = args[0].GetIfString();
  const std::string* enable_str = args[1].GetIfString();
  if (!entry_internal_name || !enable_str)
    return;

  SetFeatureEntryEnabled(flags_storage_.get(), *entry_internal_name,
                         *enable_str == "true");
  flags_storage_->CommitPendingWrites();
}

void FlagsDOMHandler::HandleRestartBrowser(base::Value::ConstListView args) {
#if BUILDFLAG(CHROMIUM_BRANDING)
  CHECK(false);
#endif  // BUILDFLAG(CHROMIUM_BRANDING)
}

void FlagsDOMHandler::HandleResetAllFlags(base::Value::ConstListView args) {
  DCHECK(flags_storage_);
  ResetAllFlags(flags_storage_.get());
  flags_storage_->CommitPendingWrites();
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
//
// FlagsUI
//
///////////////////////////////////////////////////////////////////////////////

FlagsUI::FlagsUI(web::WebUIIOS* web_ui, const std::string& host)
    : web::WebUIIOSController(web_ui, host), weak_factory_(this) {
  FlagsDOMHandler* handler = new FlagsDOMHandler();
  web_ui->AddMessageHandler(base::WrapUnique(handler));

  flags_ui::FlagAccess flag_access = flags_ui::kOwnerAccessToFlags;
  handler->Init(std::make_unique<flags_ui::PrefServiceFlagsStorage>(
                    GetApplicationContext()->GetLocalState()),
                flag_access);

  // Set up the about:flags source.
  web::WebUIIOSDataSource::Add(ChromeBrowserState::FromWebUIIOS(web_ui),
                               CreateFlagsUIHTMLSource());
}

FlagsUI::~FlagsUI() {}

// static
void FlagsUI::AddFlagsIOSStrings(web::WebUIIOSDataSource* source) {
  // Strings added here are all marked a non-translatable, so they are not
  // actually localized.
  source->AddLocalizedString(flags_ui::kFlagsRestartNotice,
                             IDS_FLAGS_UI_RELAUNCH_NOTICE);
  source->AddLocalizedString("available", IDS_FLAGS_UI_AVAILABLE_FEATURE);
  source->AddLocalizedString("clear-search", IDS_FLAGS_UI_CLEAR_SEARCH);
  source->AddLocalizedString("disabled", IDS_FLAGS_UI_DISABLED_FEATURE);
  source->AddLocalizedString("enabled", IDS_FLAGS_UI_ENABLED_FEATURE);
  source->AddLocalizedString("experiment-enabled",
                             IDS_FLAGS_UI_EXPERIMENT_ENABLED);
  source->AddLocalizedString("heading", IDS_FLAGS_UI_TITLE);
  source->AddLocalizedString("no-results", IDS_FLAGS_UI_NO_RESULTS);
  source->AddLocalizedString("not-available-platform",
                             IDS_FLAGS_UI_NOT_AVAILABLE_ON_PLATFORM);
  source->AddLocalizedString("page-warning", IDS_FLAGS_UI_PAGE_WARNING);
  source->AddLocalizedString("page-warning-explanation",
                             IDS_FLAGS_UI_PAGE_WARNING_EXPLANATION);
  source->AddLocalizedString("relaunch", IDS_FLAGS_UI_RELAUNCH);
  source->AddLocalizedString("reset", IDS_FLAGS_UI_PAGE_RESET);
  source->AddLocalizedString("reset-acknowledged",
                             IDS_FLAGS_UI_RESET_ACKNOWLEDGED);
  source->AddLocalizedString("search-label", IDS_FLAGS_UI_SEARCH_LABEL);
  source->AddLocalizedString("search-placeholder",
                             IDS_FLAGS_UI_SEARCH_PLACEHOLDER);
  source->AddLocalizedString("title", IDS_FLAGS_UI_TITLE);
  source->AddLocalizedString("unavailable", IDS_FLAGS_UI_UNAVAILABLE_FEATURE);
  source->AddLocalizedString("searchResultsSingular",
                             IDS_FLAGS_UI_SEARCH_RESULTS_SINGULAR);
  source->AddLocalizedString("searchResultsPlural",
                             IDS_FLAGS_UI_SEARCH_RESULTS_PLURAL);
}
