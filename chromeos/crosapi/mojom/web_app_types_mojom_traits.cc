// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/crosapi/mojom/web_app_types_mojom_traits.h"

#include "chromeos/crosapi/mojom/web_app_types.mojom.h"
#include "components/webapps/browser/install_result_code.h"

namespace mojo {

crosapi::mojom::WebAppInstallResultCode EnumTraits<
    crosapi::mojom::WebAppInstallResultCode,
    webapps::InstallResultCode>::ToMojom(webapps::InstallResultCode input) {
  switch (input) {
    case webapps::InstallResultCode::kSuccessNewInstall:
      return crosapi::mojom::WebAppInstallResultCode::kSuccessNewInstall;
    case webapps::InstallResultCode::kSuccessAlreadyInstalled:
      return crosapi::mojom::WebAppInstallResultCode::kSuccessAlreadyInstalled;
    case webapps::InstallResultCode::kGetWebAppInstallInfoFailed:
      return crosapi::mojom::WebAppInstallResultCode::
          kGetWebAppInstallInfoFailed;
    case webapps::InstallResultCode::kPreviouslyUninstalled:
      return crosapi::mojom::WebAppInstallResultCode::kPreviouslyUninstalled;
    case webapps::InstallResultCode::kWebContentsDestroyed:
      return crosapi::mojom::WebAppInstallResultCode::kWebContentsDestroyed;
    case webapps::InstallResultCode::kWriteDataFailed:
      return crosapi::mojom::WebAppInstallResultCode::kWriteDataFailed;
    case webapps::InstallResultCode::kUserInstallDeclined:
      return crosapi::mojom::WebAppInstallResultCode::kUserInstallDeclined;
    case webapps::InstallResultCode::kNotValidManifestForWebApp:
      return crosapi::mojom::WebAppInstallResultCode::
          kNotValidManifestForWebApp;
    case webapps::InstallResultCode::kIntentToPlayStore:
      return crosapi::mojom::WebAppInstallResultCode::kIntentToPlayStore;
    case webapps::InstallResultCode::kWebAppDisabled:
      return crosapi::mojom::WebAppInstallResultCode::kWebAppDisabled;
    case webapps::InstallResultCode::kInstallURLRedirected:
      return crosapi::mojom::WebAppInstallResultCode::kInstallURLRedirected;
    case webapps::InstallResultCode::kInstallURLLoadFailed:
      return crosapi::mojom::WebAppInstallResultCode::kInstallURLLoadFailed;
    case webapps::InstallResultCode::kExpectedAppIdCheckFailed:
      return crosapi::mojom::WebAppInstallResultCode::kExpectedAppIdCheckFailed;
    case webapps::InstallResultCode::kInstallURLLoadTimeOut:
      return crosapi::mojom::WebAppInstallResultCode::kInstallURLLoadTimeOut;
    case webapps::InstallResultCode::kFailedPlaceholderUninstall:
      return crosapi::mojom::WebAppInstallResultCode::
          kFailedPlaceholderUninstall;
    case webapps::InstallResultCode::kNotInstallable:
      return crosapi::mojom::WebAppInstallResultCode::kNotInstallable;
    case webapps::InstallResultCode::kApkWebAppInstallFailed:
      return crosapi::mojom::WebAppInstallResultCode::kApkWebAppInstallFailed;
    case webapps::InstallResultCode::kCancelledOnWebAppProviderShuttingDown:
      return crosapi::mojom::WebAppInstallResultCode::
          kCancelledOnWebAppProviderShuttingDown;
    case webapps::InstallResultCode::kWebAppProviderNotReady:
      return crosapi::mojom::WebAppInstallResultCode::kWebAppProviderNotReady;
    case webapps::InstallResultCode::kSuccessOfflineOnlyInstall:
      return crosapi::mojom::WebAppInstallResultCode::
          kSuccessOfflineOnlyInstall;
    case webapps::InstallResultCode::kSuccessOfflineFallbackInstall:
      return crosapi::mojom::WebAppInstallResultCode::
          kSuccessOfflineFallbackInstall;
    case webapps::InstallResultCode::kInstallTaskDestroyed:
      return crosapi::mojom::WebAppInstallResultCode::kInstallTaskDestroyed;
    case webapps::InstallResultCode::kUpdateTaskFailed:
      return crosapi::mojom::WebAppInstallResultCode::kUpdateTaskFailed;
    case webapps::InstallResultCode::kAppNotInRegistrarAfterCommit:
      return crosapi::mojom::WebAppInstallResultCode::
          kAppNotInRegistrarAfterCommit;
  };
}

bool EnumTraits<crosapi::mojom::WebAppInstallResultCode,
                webapps::InstallResultCode>::
    FromMojom(crosapi::mojom::WebAppInstallResultCode input,
              webapps::InstallResultCode* output) {
  switch (input) {
    case crosapi::mojom::WebAppInstallResultCode::kSuccessNewInstall:
      *output = webapps::InstallResultCode::kSuccessNewInstall;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kSuccessAlreadyInstalled:
      *output = webapps::InstallResultCode::kSuccessAlreadyInstalled;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kGetWebAppInstallInfoFailed:
      *output = webapps::InstallResultCode::kGetWebAppInstallInfoFailed;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kPreviouslyUninstalled:
      *output = webapps::InstallResultCode::kPreviouslyUninstalled;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kWebContentsDestroyed:
      *output = webapps::InstallResultCode::kWebContentsDestroyed;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kWriteDataFailed:
      *output = webapps::InstallResultCode::kWriteDataFailed;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kUserInstallDeclined:
      *output = webapps::InstallResultCode::kUserInstallDeclined;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kNotValidManifestForWebApp:
      *output = webapps::InstallResultCode::kNotValidManifestForWebApp;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kIntentToPlayStore:
      *output = webapps::InstallResultCode::kIntentToPlayStore;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kWebAppDisabled:
      *output = webapps::InstallResultCode::kWebAppDisabled;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kInstallURLRedirected:
      *output = webapps::InstallResultCode::kInstallURLRedirected;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kInstallURLLoadFailed:
      *output = webapps::InstallResultCode::kInstallURLLoadFailed;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kExpectedAppIdCheckFailed:
      *output = webapps::InstallResultCode::kExpectedAppIdCheckFailed;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kInstallURLLoadTimeOut:
      *output = webapps::InstallResultCode::kInstallURLLoadTimeOut;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kFailedPlaceholderUninstall:
      *output = webapps::InstallResultCode::kFailedPlaceholderUninstall;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kNotInstallable:
      *output = webapps::InstallResultCode::kNotInstallable;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kApkWebAppInstallFailed:
      *output = webapps::InstallResultCode::kApkWebAppInstallFailed;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::
        kCancelledOnWebAppProviderShuttingDown:
      *output =
          webapps::InstallResultCode::kCancelledOnWebAppProviderShuttingDown;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kWebAppProviderNotReady:
      *output = webapps::InstallResultCode::kWebAppProviderNotReady;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kSuccessOfflineOnlyInstall:
      *output = webapps::InstallResultCode::kSuccessOfflineOnlyInstall;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::
        kSuccessOfflineFallbackInstall:
      *output = webapps::InstallResultCode::kSuccessOfflineFallbackInstall;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kInstallTaskDestroyed:
      *output = webapps::InstallResultCode::kInstallTaskDestroyed;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kUpdateTaskFailed:
      *output = webapps::InstallResultCode::kUpdateTaskFailed;
      return true;
    case crosapi::mojom::WebAppInstallResultCode::kAppNotInRegistrarAfterCommit:
      *output = webapps::InstallResultCode::kAppNotInRegistrarAfterCommit;
      return true;
  };

  NOTREACHED();
  return false;
}

}  // namespace mojo
