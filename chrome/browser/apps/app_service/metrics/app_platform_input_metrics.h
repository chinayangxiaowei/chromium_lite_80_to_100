// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_APPS_APP_SERVICE_METRICS_APP_PLATFORM_INPUT_METRICS_H_
#define CHROME_BROWSER_APPS_APP_SERVICE_METRICS_APP_PLATFORM_INPUT_METRICS_H_

#include <map>

#include "base/containers/flat_map.h"
#include "chrome/browser/apps/app_service/metrics/app_platform_metrics_utils.h"
#include "chrome/browser/apps/app_service/metrics/browser_to_tab_list.h"
#include "chrome/browser/profiles/profile.h"
#include "components/services/app_service/public/cpp/app_registry_cache.h"
#include "components/services/app_service/public/cpp/instance_registry.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "ui/events/event_handler.h"

namespace apps {

class InstanceUpdate;

// This is used for logging, so do not remove or reorder existing entries.
enum class InputEventSource {
  kUnknown = 0,
  kMouse = 1,
  kStylus = 2,
  kTouch = 3,
  kKeyboard = 4,

  // Add any new values above this one, and update kMaxValue to the highest
  // enumerator value.
  kMaxValue = kKeyboard,
};

// This class is used to record the input events for the app windows.
class AppPlatformInputMetrics : public ui::EventHandler,
                                public AppRegistryCache::Observer,
                                public InstanceRegistry::Observer {
 public:
  AppPlatformInputMetrics(Profile* profile,
                          AppRegistryCache& app_registry_cache,
                          InstanceRegistry& instance_registry);

  AppPlatformInputMetrics(const AppPlatformInputMetrics&) = delete;
  AppPlatformInputMetrics& operator=(const AppPlatformInputMetrics&) = delete;

  ~AppPlatformInputMetrics() override;

  // ui::EventHandler:
  void OnMouseEvent(ui::MouseEvent* event) override;
  void OnKeyEvent(ui::KeyEvent* event) override;
  void OnTouchEvent(ui::TouchEvent* event) override;

  void OnFiveMinutes();

 private:
  struct AppInfo {
    std::string app_id;
    AppTypeName app_type_name;
  };

  // For web apps and Chrome apps, there might be different app type name for
  // opening in tab or window. So record the app type name for the event count.
  using CountPerAppType = base::flat_map<AppTypeName, int>;

  // The map to record the event count for each InputEventSource.
  using EventSourceToCounts = base::flat_map<InputEventSource, CountPerAppType>;

  // InstanceRegistry::Observer:
  void OnInstanceUpdate(const InstanceUpdate& update) override;
  void OnInstanceRegistryWillBeDestroyed(InstanceRegistry* cache) override;

  // AppRegistryCache::Observer:
  void OnAppRegistryCacheWillBeDestroyed(AppRegistryCache* cache) override;
  void OnAppUpdate(const AppUpdate& update) override;

  void SetAppInfoForActivatedWindow(AppType app_type,
                                    const std::string& app_id,
                                    aura::Window* window,
                                    const base::UnguessableToken& instance_id);

  void SetAppInfoForInactivatedWindow(
      const base::UnguessableToken& instance_id);

  void RecordEventCount(InputEventSource event_source,
                        ui::EventTarget* event_target);

  ukm::SourceId GetSourceId(const std::string& app_id);

  void RecordInputEventsUkm(ukm::SourceId source_id,
                            const EventSourceToCounts& event_counts);

  Profile* profile_;

  BrowserToTabList browser_to_tab_list_;

  // The map from the window to the app info.
  base::flat_map<aura::Window*, AppInfo> window_to_app_info_;

  // The map from the app id to the source id to reuse the existing source id
  // for the same app id based on the UKM guidelines. When the app is removed,
  // the record for the app will be removed, and inform UKM service that the
  // source_id is no longer used.
  std::map<std::string, ukm::SourceId> app_id_to_source_id_;

  // Records the input even count for each app id in the past five minutes. Each
  // app id might have multiple events. For web apps and Chrome apps, there
  // might be different app type name, e.g. Chrome browser for apps opening in
  // a tab, or Web app for apps opening in a window. For example:
  // web_app_id1: {
  //   mouse:    { Chrome browser: 5, Web app: 2}
  //   Keyboard: { Chrome browser: 2, Web app: 3}
  // },
  // chrome_app_id2: {
  //   stylus:   { Chrome browser: 2, Chrome app: 12}
  //   Keyboard: { Chrome browser: 3, Chrome app: 30}
  // },
  // Arc_app_id3: {
  //   mouse:   { Arc: 5}
  // },
  std::map<std::string, EventSourceToCounts>
      app_id_to_event_count_per_five_minutes_;
};

}  // namespace apps

#endif  // CHROME_BROWSER_APPS_APP_SERVICE_METRICS_APP_PLATFORM_INPUT_METRICS_H_
