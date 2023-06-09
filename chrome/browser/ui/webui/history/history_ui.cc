// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/history/history_ui.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/memory/ref_counted_memory.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/history_clusters/history_clusters_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/browser/ui/webui/history/browsing_history_handler.h"
#include "chrome/browser/ui/webui/history/foreign_session_handler.h"
#include "chrome/browser/ui/webui/history/history_login_handler.h"
#include "chrome/browser/ui/webui/history/navigation_handler.h"
#include "chrome/browser/ui/webui/history_clusters/history_clusters_handler.h"
#include "chrome/browser/ui/webui/managed_ui_handler.h"
#include "chrome/browser/ui/webui/metrics_handler.h"
#include "chrome/browser/ui/webui/webui_util.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/history_resources.h"
#include "chrome/grit/history_resources_map.h"
#include "chrome/grit/locale_settings.h"
#include "components/favicon_base/favicon_url_parser.h"
#include "components/grit/components_scaled_resources.h"
#include "components/history_clusters/core/features.h"
#include "components/history_clusters/core/history_clusters_prefs.h"
#include "components/history_clusters/core/history_clusters_service.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/webui/web_ui_util.h"

namespace {

constexpr char kIsHistoryClustersVisibleKey[] = "isHistoryClustersVisible";
constexpr char kIsHistoryClustersVisibleManagedByPolicyKey[] =
    "isHistoryClustersVisibleManagedByPolicy";

constexpr char kIsUserSignedInKey[] = "isUserSignedIn";

bool IsUserSignedIn(Profile* profile) {
  signin::IdentityManager* identity_manager =
      IdentityManagerFactory::GetForProfile(profile);
  return identity_manager &&
         identity_manager->HasPrimaryAccount(signin::ConsentLevel::kSync);
}

content::WebUIDataSource* CreateHistoryUIHTMLSource(Profile* profile) {
  content::WebUIDataSource* source =
      content::WebUIDataSource::Create(chrome::kChromeUIHistoryHost);

  static constexpr webui::LocalizedString kStrings[] = {
      // Localized strings (alphabetical order).
      {"actionMenuDescription", IDS_HISTORY_ACTION_MENU_DESCRIPTION},
      {"ariaRoleDescription", IDS_HISTORY_ARIA_ROLE_DESCRIPTION},
      {"bookmarked", IDS_HISTORY_ENTRY_BOOKMARKED},
      {"cancel", IDS_CANCEL},
      {"clearBrowsingData", IDS_CLEAR_BROWSING_DATA_TITLE},
      {"clearSearch", IDS_CLEAR_SEARCH},
      {"collapseSessionButton", IDS_HISTORY_OTHER_SESSIONS_COLLAPSE_SESSION},
      {"delete", IDS_HISTORY_DELETE},
      {"deleteSuccess", IDS_HISTORY_REMOVE_PAGE_SUCCESS},
      {"deleteConfirm", IDS_HISTORY_DELETE_PRIOR_VISITS_CONFIRM_BUTTON},
      {"deleteSession", IDS_HISTORY_OTHER_SESSIONS_HIDE_FOR_NOW},
      {"deleteWarning", IDS_HISTORY_DELETE_PRIOR_VISITS_WARNING},
      {"entrySummary", IDS_HISTORY_ENTRY_SUMMARY},
      {"expandSessionButton", IDS_HISTORY_OTHER_SESSIONS_EXPAND_SESSION},
      {"foundSearchResults", IDS_HISTORY_FOUND_SEARCH_RESULTS},
      {"historyMenuButton", IDS_HISTORY_HISTORY_MENU_DESCRIPTION},
      {"historyMenuItem", IDS_HISTORY_HISTORY_MENU_ITEM},
      {"itemsSelected", IDS_HISTORY_ITEMS_SELECTED},
      {"itemsUnselected", IDS_HISTORY_ITEMS_UNSELECTED},
      {"loading", IDS_HISTORY_LOADING},
      {"menu", IDS_MENU},
      {"moreFromSite", IDS_HISTORY_MORE_FROM_SITE},
      {"openAll", IDS_HISTORY_OTHER_SESSIONS_OPEN_ALL},
      {"openTabsMenuItem", IDS_HISTORY_OPEN_TABS_MENU_ITEM},
      {"noResults", IDS_HISTORY_NO_RESULTS},
      {"noSearchResults", IDS_HISTORY_NO_SEARCH_RESULTS},
      {"noSyncedResults", IDS_HISTORY_NO_SYNCED_RESULTS},
      {"removeBookmark", IDS_HISTORY_REMOVE_BOOKMARK},
      {"removeFromHistory", IDS_HISTORY_REMOVE_PAGE},
      {"removeSelected", IDS_HISTORY_REMOVE_SELECTED_ITEMS},
      {"searchPrompt", IDS_HISTORY_SEARCH_PROMPT},
      {"searchResult", IDS_HISTORY_SEARCH_RESULT},
      {"searchResults", IDS_HISTORY_SEARCH_RESULTS},
      {"signInButton", IDS_HISTORY_SIGN_IN_BUTTON},
      {"signInPromo", IDS_HISTORY_SIGN_IN_PROMO},
      {"signInPromoDesc", IDS_HISTORY_SIGN_IN_PROMO_DESC},
      {"title", IDS_HISTORY_TITLE},
  };
  source->AddLocalizedStrings(kStrings);

  source->AddString(
      "sidebarFooter",
      l10n_util::GetStringFUTF16(
          IDS_HISTORY_OTHER_FORMS_OF_HISTORY,
          l10n_util::GetStringUTF16(
              IDS_SETTINGS_CLEAR_DATA_MYACTIVITY_URL_IN_HISTORY)));

  PrefService* prefs = profile->GetPrefs();
  bool allow_deleting_history =
      prefs->GetBoolean(prefs::kAllowDeletingBrowserHistory);
  source->AddBoolean("allowDeletingHistory", allow_deleting_history);

  source->AddBoolean("isGuestSession", profile->IsGuestSession());
  source->AddBoolean("isSignInAllowed",
                     prefs->GetBoolean(prefs::kSigninAllowed));

  source->AddBoolean(kIsUserSignedInKey, IsUserSignedIn(profile));

  // TODO(crbug.com/1286649): Remove after CSS has been updated to no longer
  // need this attribute.
  source->AddString("enableBrandingUpdateAttribute", "enable-branding-update");

  // History clusters
  auto* history_clusters_service =
      HistoryClustersServiceFactory::GetForBrowserContext(profile);
  source->AddBoolean("isHistoryClustersEnabled",
                     history_clusters_service &&
                         history_clusters_service->IsJourneysEnabled());
  source->AddBoolean(
      kIsHistoryClustersVisibleKey,
      profile->GetPrefs()->GetBoolean(history_clusters::prefs::kVisible));
  source->AddBoolean(kIsHistoryClustersVisibleManagedByPolicyKey,
                     profile->GetPrefs()->IsManagedPreference(
                         history_clusters::prefs::kVisible));
  source->AddBoolean(
      "isHistoryClustersDebug",
      base::FeatureList::IsEnabled(history_clusters::kUserVisibleDebug));

  static constexpr webui::LocalizedString kHistoryClustersStrings[] = {
      {"disableHistoryClusters", IDS_HISTORY_CLUSTERS_DISABLE_MENU_ITEM_LABEL},
      {"enableHistoryClusters", IDS_HISTORY_CLUSTERS_ENABLE_MENU_ITEM_LABEL},
      {"headerText", IDS_HISTORY_CLUSTERS_HEADER_TEXT},
      {"historyClustersTabLabel", IDS_HISTORY_CLUSTERS_JOURNEYS_TAB_LABEL},
      {"historyListTabLabel", IDS_HISTORY_CLUSTERS_LIST_TAB_LABEL},
      {"loadMoreButtonLabel", IDS_HISTORY_CLUSTERS_LOAD_MORE_BUTTON_LABEL},
      {"openAllInTabGroup", IDS_HISTORY_CLUSTERS_OPEN_ALL_IN_TABGROUP},
      {"relatedSearchesHeader", IDS_HISTORY_CLUSTERS_RELATED_SEARCHES_HEADER},
      {"removeAllFromHistory", IDS_HISTORY_CLUSTERS_REMOVE_ALL_ITEMS},
      {"removeFromHistoryToast", IDS_HISTORY_CLUSTERS_REMOVE_ITEM_TOAST},
      {"savedInTabGroup", IDS_HISTORY_CLUSTERS_SAVED_IN_TABGROUP_LABEL},
      {"toggleButtonLabelLess", IDS_HISTORY_CLUSTERS_SHOW_LESS_BUTTON_LABEL},
      {"toggleButtonLabelMore", IDS_HISTORY_CLUSTERS_SHOW_MORE_BUTTON_LABEL},
  };
  source->AddLocalizedStrings(kHistoryClustersStrings);

  webui::SetupWebUIDataSource(
      source, base::make_span(kHistoryResources, kHistoryResourcesSize),
      IDR_HISTORY_HISTORY_HTML);

  content::URLDataSource::Add(
      profile, std::make_unique<FaviconSource>(
                   profile, chrome::FaviconUrlFormat::kFavicon2));

  return source;
}

}  // namespace

HistoryUI::HistoryUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui, /*enable_chrome_send=*/true) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* data_source = CreateHistoryUIHTMLSource(profile);
  ManagedUIHandler::Initialize(web_ui, data_source);
  content::WebUIDataSource::Add(profile, data_source);

  pref_change_registrar_.Init(profile->GetPrefs());
  pref_change_registrar_.Add(history_clusters::prefs::kVisible,
                             base::BindRepeating(&HistoryUI::UpdateDataSource,
                                                 base::Unretained(this)));

  web_ui->AddMessageHandler(std::make_unique<webui::NavigationHandler>());
  auto browsing_history_handler = std::make_unique<BrowsingHistoryHandler>();
  BrowsingHistoryHandler* browsing_history_handler_ptr =
      browsing_history_handler.get();
  web_ui->AddMessageHandler(std::move(browsing_history_handler));
  browsing_history_handler_ptr->StartQueryHistory();
  web_ui->AddMessageHandler(std::make_unique<MetricsHandler>());

  auto foreign_session_handler =
      std::make_unique<browser_sync::ForeignSessionHandler>();
  browser_sync::ForeignSessionHandler* foreign_session_handler_ptr =
      foreign_session_handler.get();
  web_ui->AddMessageHandler(std::move(foreign_session_handler));
  foreign_session_handler_ptr->InitializeForeignSessions();
  web_ui->AddMessageHandler(
      std::make_unique<HistoryLoginHandler>(base::BindRepeating(
          &HistoryUI::UpdateDataSource, base::Unretained(this))));
}

HistoryUI::~HistoryUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(HistoryUI)

// static
base::RefCountedMemory* HistoryUI::GetFaviconResourceBytes(
    ui::ResourceScaleFactor scale_factor) {
  return static_cast<base::RefCountedMemory*>(
      ui::ResourceBundle::GetSharedInstance().LoadDataResourceBytesForScale(
          IDR_HISTORY_FAVICON, scale_factor));
}

void HistoryUI::BindInterface(
    mojo::PendingReceiver<history_clusters::mojom::PageHandler>
        pending_page_handler) {
  history_clusters_handler_ =
      std::make_unique<history_clusters::HistoryClustersHandler>(
          std::move(pending_page_handler), Profile::FromWebUI(web_ui()),
          web_ui()->GetWebContents());
}

void HistoryUI::UpdateDataSource() {
  CHECK(web_ui());

  Profile* profile = Profile::FromWebUI(web_ui());

  std::unique_ptr<base::DictionaryValue> update =
      std::make_unique<base::DictionaryValue>();
  update->SetBoolKey(kIsUserSignedInKey, IsUserSignedIn(profile));
  update->SetBoolKey(
      kIsHistoryClustersVisibleKey,
      profile->GetPrefs()->GetBoolean(history_clusters::prefs::kVisible));
  update->SetBoolKey(kIsHistoryClustersVisibleManagedByPolicyKey,
                     profile->GetPrefs()->IsManagedPreference(
                         history_clusters::prefs::kVisible));

  content::WebUIDataSource::Update(profile, chrome::kChromeUIHistoryHost,
                                   std::move(update));
}
