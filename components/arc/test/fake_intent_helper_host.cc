// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/arc/test/fake_intent_helper_host.h"

namespace arc {

FakeIntentHelperHost::FakeIntentHelperHost(
    ConnectionHolder<arc::mojom::IntentHelperInstance,
                     arc::mojom::IntentHelperHost>*
        intent_helper_connection_holder)
    : intent_helper_connection_holder_(intent_helper_connection_holder) {
  intent_helper_connection_holder_->SetHost(this);
}

FakeIntentHelperHost::~FakeIntentHelperHost() {
  intent_helper_connection_holder_->SetHost(nullptr);
}

void FakeIntentHelperHost::OnIconInvalidated(const std::string& package_name) {}
void FakeIntentHelperHost::OnIntentFiltersUpdated(
    std::vector<IntentFilter> intent_filters) {}
void FakeIntentHelperHost::OnOpenDownloads() {}
void FakeIntentHelperHost::OnOpenUrl(const std::string& url) {}
void FakeIntentHelperHost::OnOpenCustomTab(const std::string& url,
                                           int32_t task_id,
                                           OnOpenCustomTabCallback callback) {}
void FakeIntentHelperHost::OnOpenChromePage(mojom::ChromePage page) {}
void FakeIntentHelperHost::FactoryResetArc() {}
void FakeIntentHelperHost::OpenWallpaperPicker() {}
void FakeIntentHelperHost::OpenVolumeControl() {}
void FakeIntentHelperHost::OnOpenWebApp(const std::string& url) {}
void FakeIntentHelperHost::RecordShareFilesMetricsDeprecated(
    mojom::ShareFiles flag) {}
void FakeIntentHelperHost::LaunchCameraApp(uint32_t intent_id,
                                           arc::mojom::CameraIntentMode mode,
                                           bool should_handle_result,
                                           bool should_down_scale,
                                           bool is_secure,
                                           int32_t task_id) {}
void FakeIntentHelperHost::OnIntentFiltersUpdatedForPackage(
    const std::string& package_name,
    std::vector<IntentFilter> intent_filters) {}
void FakeIntentHelperHost::CloseCameraApp() {}
void FakeIntentHelperHost::IsChromeAppEnabled(
    arc::mojom::ChromeApp app,
    IsChromeAppEnabledCallback callback) {}
void FakeIntentHelperHost::OnSupportedLinksChanged(
    std::vector<arc::mojom::SupportedLinksPtr> added_packages,
    std::vector<arc::mojom::SupportedLinksPtr> removed_packages,
    arc::mojom::SupportedLinkChangeSource source) {}
void FakeIntentHelperHost::OnDownloadAdded(
    const std::string& relative_path,
    const std::string& owner_package_name) {}
void FakeIntentHelperHost::OnOpenAppWithIntent(
    const GURL& start_url,
    arc::mojom::LaunchIntentPtr intent) {}

}  // namespace arc
