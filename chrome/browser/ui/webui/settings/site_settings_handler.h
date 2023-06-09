// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SETTINGS_SITE_SETTINGS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SETTINGS_SITE_SETTINGS_HANDLER_H_

#include <map>
#include <memory>
#include <set>
#include <string>

#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/values.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/browsing_data/cookies_tree_model.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"
#include "components/content_settings/core/browser/content_settings_observer.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/permissions/object_permission_context_base.h"
#include "components/prefs/pref_store.h"
#include "content/public/browser/host_zoom_map.h"
#include "ppapi/buildflags/buildflags.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

class PrefChangeRegistrar;

namespace settings {

// Chrome "ContentSettings" settings page UI handler.
class SiteSettingsHandler
    : public SettingsPageUIHandler,
      public content_settings::Observer,
      public ProfileObserver,
      public permissions::ObjectPermissionContextBase::PermissionObserver,
      public CookiesTreeModel::Observer {
 public:
  explicit SiteSettingsHandler(Profile* profile);

  SiteSettingsHandler(const SiteSettingsHandler&) = delete;
  SiteSettingsHandler& operator=(const SiteSettingsHandler&) = delete;

  ~SiteSettingsHandler() override;

  // SettingsPageUIHandler:
  void RegisterMessages() override;

  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // Usage info.
  void OnGetUsageInfo();

  // CookiesTreeModel::Observer:
  // TODO(https://crbug.com/835712): Listen for backend data changes and notify
  // WebUI
  void TreeNodesAdded(ui::TreeModel* model,
                      ui::TreeModelNode* parent,
                      size_t start,
                      size_t count) override;
  void TreeNodesRemoved(ui::TreeModel* model,
                        ui::TreeModelNode* parent,
                        size_t start,
                        size_t count) override;
  void TreeNodeChanged(ui::TreeModel* model, ui::TreeModelNode* node) override;
  void TreeModelEndBatch(CookiesTreeModel* model) override;

  // content_settings::Observer:
  void OnContentSettingChanged(const ContentSettingsPattern& primary_pattern,
                               const ContentSettingsPattern& secondary_pattern,
                               ContentSettingsType content_type) override;

  // ProfileObserver:
  void OnOffTheRecordProfileCreated(Profile* off_the_record) override;
  void OnProfileWillBeDestroyed(Profile* profile) override;

  // ObjectPermissionContextBase::PermissionObserver implementation:
  void OnObjectPermissionChanged(
      absl::optional<ContentSettingsType> guard_content_settings_type,
      ContentSettingsType data_content_settings_type) override;

  void OnZoomLevelChanged(const content::HostZoomMap::ZoomLevelChange& change);

 private:
  friend class SiteSettingsHandlerChooserExceptionTest;
  friend class SiteSettingsHandlerInfobarTest;
  friend class SiteSettingsHandlerTest;
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerChooserExceptionTest,
                           HandleGetChooserExceptionListForUsb);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerChooserExceptionTest,
                           HandleResetChooserExceptionForSiteForUsb);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerInfobarTest,
                           SettingPermissionsTriggersInfobar);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest,
                           BlockAutoplay_SendOnRequest);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, BlockAutoplay_Update);
#if BUILDFLAG(ENABLE_PLUGINS)
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest,
                           ChangingFlashSettingForSiteIsRemembered);
#endif
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, DefaultSettingSource);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, ExceptionHelpers);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, ExtensionDisplayName);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, GetAllSites);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, GetRecentSitePermissions);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, OnStorageFetched);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, GetAndSetDefault);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, GetAndSetForInvalidURLs);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, GetAndSetOriginPermissions);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, Incognito);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, IncognitoExceptions);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest,
                           ResetCategoryPermissionForEmbargoedOrigins);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest,
                           ResetCategoryPermissionForInvalidOrigins);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, Origins);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, Patterns);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, PatternsAndContentType);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, SessionOnlyException);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, ZoomLevels);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest,
                           HandleClearEtldPlus1DataAndCookies);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest,
                           HandleClearUnpartitionedUsage);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, ClearClientHints);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest,
                           HandleClearPartitionedUsage);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, CookieSettingDescription);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, HandleGetFormattedBytes);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest,
                           NotificationPermissionRevokeUkm);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, ExcludeWebUISchemesInLists);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest,
                           IncludeWebUISchemesInGetOriginPermissions);
  FRIEND_TEST_ALL_PREFIXES(SiteSettingsHandlerTest, HandleGetUsageInfo);

  // Creates the CookiesTreeModel if necessary.
  void EnsureCookiesTreeModelCreated();

  // Add or remove this class as an observer for content settings and chooser
  // contexts corresponding to |profile|.
  void ObserveSourcesForProfile(Profile* profile);
  void StopObservingSourcesForProfile(Profile* profile);

  // Calculates the data storage that has been used for each origin, and
  // stores the information in the |all_sites_map| and |origin_size_map|.
  void GetOriginStorage(
      std::map<std::string, std::set<std::pair<std::string, bool>>>*
          all_sites_map,
      std::map<std::string, int64_t>* origin_size_map);

  // Calculates the number of cookies for each etld+1 and each origin, and
  // stores the information in the |all_sites_map| and |origin_cookie_map|.
  void GetOriginCookies(
      std::map<std::string, std::set<std::pair<std::string, bool>>>*
          all_sites_map,
      std::map<std::pair<std::string, absl::optional<std::string>>, int>*
          origin_cookie_map);

  // Asynchronously fetches the usage for a given origin. Replies back with
  // OnGetUsageInfo above.
  void HandleFetchUsageTotal(base::Value::ConstListView args);

  // Deletes the storage being used for a given host.
  void HandleClearUnpartitionedUsage(base::Value::ConstListView args);

  void HandleClearPartitionedUsage(base::Value::ConstListView args);

  // Gets and sets the default value for a particular content settings type.
  void HandleSetDefaultValueForContentType(base::Value::ConstListView args);
  void HandleGetDefaultValueForContentType(base::Value::ConstListView args);

  // Returns a list of sites with permissions settings, grouped by their
  // eTLD+1. Recreates the cookies tree model to fetch the cookie and usage
  // data, which will send the list of sites with cookies or usage data to
  // the front end when fetching finished.
  void HandleGetAllSites(base::Value::ConstListView args);

  // Returns a list of content settings types that are controlled via a standard
  // permissions UI and should be made visible to the user. There is a single
  // nullable string argument, which represents an associated origin. See
  // `SiteSettingsPrefsBrowserProxy#getCategoryList`.
  void HandleGetCategoryList(base::Value::ConstListView args);

  // Returns a string for display describing the current cookie settings.
  void HandleGetCookieSettingDescription(base::Value::ConstListView args);

  // Returns a list containing the most recent permission changes for the
  // content types that are visiblein settings, grouped by origin/profile
  // (incognito, regular) combinations, limited to N origin/profile pairings.
  // This includes permission changes made by embargo, but does not include
  // permissions enforced via policy.
  void HandleGetRecentSitePermissions(base::Value::ConstListView args);

  // Called when the list of origins using storage has been fetched, and sends
  // this list back to the front end.
  void OnStorageFetched();

  // Returns a list of sites, grouped by their effective top level domain plus
  // 1, with their cookies number and data usage information. This method will
  // only be called after HandleGetAllSites is called.
  base::Value PopulateCookiesAndUsageData(Profile* profile);

  // Converts a given number of bytes into a human-readable format, with data
  // units.
  void HandleGetFormattedBytes(base::Value::ConstListView args);

  // Returns the list of site exceptions for a given content settings type.
  void HandleGetExceptionList(base::Value::ConstListView args);

  // Returns the list of chooser exceptions for a given chooser type.
  void HandleGetChooserExceptionList(base::Value::ConstListView args);

  // Gets and sets a list of ContentSettingTypes for an origin.
  // TODO(https://crbug.com/739241): Investigate replacing the
  // '*CategoryPermissionForPattern' equivalents below with these methods.
  void HandleGetOriginPermissions(base::Value::ConstListView args);
  void HandleSetOriginPermissions(base::Value::ConstListView args);

  // Handles setting and resetting an origin permission.
  void HandleResetCategoryPermissionForPattern(base::Value::ConstListView args);
  void HandleSetCategoryPermissionForPattern(base::Value::ConstListView args);

  // TODO(andypaicu, crbug.com/880684): Update to only expect a list of three
  // arguments, replacing the current (requesting,embedding) arguments with
  // simply (origin) and update all call sites.
  // Handles resetting a chooser exception for the given site.
  void HandleResetChooserExceptionForSite(base::Value::ConstListView args);

  // Returns whether a given string is a valid origin.
  void HandleIsOriginValid(base::Value::ConstListView args);

  // Returns whether the pattern is valid given the type.
  void HandleIsPatternValidForType(base::Value::ConstListView args);

  // Looks up whether an incognito session is active.
  void HandleUpdateIncognitoStatus(base::Value::ConstListView args);

  // Notifies the JS side whether incognito is enabled.
  void SendIncognitoStatus(Profile* profile, bool was_destroyed);

  // Handles the request for a list of all zoom levels.
  void HandleFetchZoomLevels(base::Value::ConstListView args);

  // Sends the zoom level list down to the web ui.
  void SendZoomLevels();

  // Removes a particular zoom level for a given host.
  void HandleRemoveZoomLevel(base::Value::ConstListView args);

  // Handles the request to send block autoplay state.
  void HandleFetchBlockAutoplayStatus(base::Value::ConstListView args);

  // Notifies the JS side about the state of the block autoplay toggle.
  void SendBlockAutoplayStatus();

  // Updates the block autoplay enabled pref when the UI is toggled.
  void HandleSetBlockAutoplayEnabled(base::Value::ConstListView args);

  // Clear web storage data and cookies from cookies tree model for an ETLD+1.
  void HandleClearEtldPlus1DataAndCookies(base::Value::ConstListView args);

  // Record metrics for actions on All Sites Page.
  void HandleRecordAction(base::Value::ConstListView args);

  // Provides an opportunity for site data which is not integrated into the
  // tree model to be removed when entries for |origins| are removed.
  // TODO(crbug.com/1271155): This function is a temporary hack while the
  // CookiesTreeModel is deprecated.
  void RemoveNonTreeModelData(const std::vector<url::Origin>& origins);

  void SetCookiesTreeModelForTesting(
      std::unique_ptr<CookiesTreeModel> cookies_tree_model);

  void ClearAllSitesMapForTesting();

  // Notifies the JS side the effective cookies setting has changed and
  // provides the updated description label for display.
  void SendCookieSettingDescription();

  const raw_ptr<Profile> profile_;

  base::ScopedMultiSourceObservation<Profile, ProfileObserver>
      observed_profiles_{this};

  // Keeps track of events related to zooming.
  base::CallbackListSubscription host_zoom_map_subscription_;

  // The host for which to fetch usage.
  std::string usage_host_;

  // The origin for which to clear usage.
  std::string clearing_origin_;

  // Change observer for content settings.
  base::ScopedMultiSourceObservation<HostContentSettingsMap,
                                     content_settings::Observer>
      observations_{this};

  // Change observer for chooser permissions.
  base::ScopedMultiSourceObservation<
      permissions::ObjectPermissionContextBase,
      permissions::ObjectPermissionContextBase::PermissionObserver>
      chooser_observations_{this};

  // Change observer for prefs.
  std::unique_ptr<PrefChangeRegistrar> pref_change_registrar_;

  std::unique_ptr<CookiesTreeModel> cookies_tree_model_;

  // Whether the tree model was set for testing. Allows the handler to avoid
  // resetting the tree model.
  bool tree_model_set_for_testing_ = false;

  // Whether to send all sites list on cookie tree model update.
  bool send_sites_list_ = false;

  // Populated every time the user reloads the All Sites page.
  std::map<std::string, std::set<std::pair<std::string, bool>>> all_sites_map_;

  // Store the origins that has permission settings.
  std::set<std::string> origin_permission_set_;

  // Whether to send site detail data on cookie tree model update.
  bool update_site_details_ = false;
};

}  // namespace settings

#endif  // CHROME_BROWSER_UI_WEBUI_SETTINGS_SITE_SETTINGS_HANDLER_H_
