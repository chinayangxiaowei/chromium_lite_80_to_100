// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_INFOBARS_OVERLAYS_PERMISSIONS_OVERLAY_TAB_HELPER_H_
#define IOS_CHROME_BROWSER_INFOBARS_OVERLAYS_PERMISSIONS_OVERLAY_TAB_HELPER_H_

#import <Foundation/Foundation.h>

#include "components/infobars/core/confirm_infobar_delegate.h"
#include "components/infobars/core/infobar.h"
#include "ios/web/public/web_state_observer.h"
#import "ios/web/public/web_state_user_data.h"

class InfobarOverlayRequestInserter;
class OverlayRequestQueue;
enum class Permission : NSUInteger;

namespace base {
class OneShotTimer;
}  // namespace base

// Tab helper that observes changes to web permissions and creates/replaces the
// respective infobar accordingly.
class PermissionsOverlayTabHelper
    : public infobars::InfoBarManager::Observer,
      public web::WebStateObserver,
      public web::WebStateUserData<PermissionsOverlayTabHelper> {
 public:
  explicit PermissionsOverlayTabHelper(web::WebState* web_state);

  PermissionsOverlayTabHelper(const PermissionsOverlayTabHelper&) = delete;
  PermissionsOverlayTabHelper& operator=(const PermissionsOverlayTabHelper&) =
      delete;
  ~PermissionsOverlayTabHelper() override;

  // web::WebStateObserver implementation.
  void PermissionStateChanged(web::WebState* web_state,
                              web::Permission permission) override
      API_AVAILABLE(ios(15.0));
  void WebStateDestroyed(web::WebState* web_state) override;

  // infobars::InfoBarManager::Observer implementation.
  void OnInfoBarRemoved(infobars::InfoBar* infobar, bool animate) override;

 private:
  friend class web::WebStateUserData<PermissionsOverlayTabHelper>;

  // Adds/replaces the infobar and show the banner.
  void ShowInfoBar();

  // Update the acceptance of the infobar.
  void UpdateIsInfoBarAccepted();

  // The WebState that this object is attached to.
  web::WebState* web_state_;

  // The currently displayed infobar.
  infobars::InfoBar* infobar_ = nullptr;

  // Permissions that have changed its state from NotAccessible to Allowed
  // within a given timeout period.
  NSMutableArray<NSNumber*>* recently_accessible_permissions_ =
      [NSMutableArray array];

  // Timer used to for recently_accessible_permissions_.
  base::OneShotTimer timer_;

  // A mapping of current permissions to their states used to detect changes.
  NSMutableDictionary<NSNumber*, NSNumber*>* permissions_to_state_;

  // Banner queue for the TabHelper's WebState;
  OverlayRequestQueue* banner_queue_ = nullptr;

  // Request inserter for the TabHelper's WebState;
  InfobarOverlayRequestInserter* inserter_ = nullptr;

  WEB_STATE_USER_DATA_KEY_DECL();
};

#endif  // IOS_CHROME_BROWSER_INFOBARS_OVERLAYS_PERMISSIONS_OVERLAY_TAB_HELPER_H_
