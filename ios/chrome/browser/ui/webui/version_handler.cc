// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/ui/webui/version_handler.h"

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "components/version_ui/version_handler_helper.h"
#include "components/version_ui/version_ui_constants.h"
#include "ios/web/public/webui/web_ui_ios.h"
#include "ui/base/l10n/l10n_util.h"

VersionHandler::VersionHandler() {}

VersionHandler::~VersionHandler() {}

void VersionHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      version_ui::kRequestVariationInfo,
      base::BindRepeating(&VersionHandler::HandleRequestVariationInfo,
                          base::Unretained(this)));
}

void VersionHandler::HandleRequestVariationInfo(
    base::Value::ConstListView args) {
  // Respond with the variations info immediately.
  CHECK_EQ(2U, args.size());
  std::string callback_id = args[0].GetString();

  base::Value response(base::Value::Type::DICTIONARY);
  response.SetKey(version_ui::kKeyVariationsList,
                  version_ui::GetVariationsList());
  web_ui()->ResolveJavascriptCallback(base::Value(callback_id), response);
}
