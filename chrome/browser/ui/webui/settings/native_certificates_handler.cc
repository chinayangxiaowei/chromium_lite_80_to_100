// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/native_certificates_handler.h"

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/metrics/user_metrics.h"
#include "chrome/browser/ui/webui/settings/settings_utils.h"
#include "content/public/browser/web_ui.h"

namespace settings {

NativeCertificatesHandler::NativeCertificatesHandler() {}

NativeCertificatesHandler::~NativeCertificatesHandler() {}

void NativeCertificatesHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "showManageSSLCertificates",
      base::BindRepeating(
          &NativeCertificatesHandler::HandleShowManageSSLCertificates,
          base::Unretained(this)));
}

void NativeCertificatesHandler::HandleShowManageSSLCertificates(
    base::Value::ConstListView args) {
  base::RecordAction(base::UserMetricsAction("Options_ManageSSLCertificates"));
  settings_utils::ShowManageSSLCertificates(web_ui()->GetWebContents());
}

}  // namespace settings
