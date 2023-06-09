// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_PRIVACY_SANDBOX_PRIVACY_SANDBOX_DIALOG_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_PRIVACY_SANDBOX_PRIVACY_SANDBOX_DIALOG_HANDLER_H_

#include "chrome/browser/privacy_sandbox/privacy_sandbox_service.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_message_handler.h"

class PrivacySandboxDialogHandler : public content::WebUIMessageHandler {
 public:
  PrivacySandboxDialogHandler(base::OnceClosure close_callback,
                              base::OnceCallback<void(int)> resize_callback,
                              base::OnceClosure open_settings_callback,
                              PrivacySandboxService::DialogType dialog_type);
  ~PrivacySandboxDialogHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

 protected:
  friend class PrivacySandboxDialogHandlerTest;
  FRIEND_TEST_ALL_PREFIXES(PrivacySandboxConsentDialogHandlerTest,
                           HandleResizeDialog);
  FRIEND_TEST_ALL_PREFIXES(PrivacySandboxConsentDialogHandlerTest,
                           HandleClickLearnMore);
  FRIEND_TEST_ALL_PREFIXES(PrivacySandboxConsentDialogHandlerTest,
                           HandleConsentAccepted);
  FRIEND_TEST_ALL_PREFIXES(PrivacySandboxConsentDialogHandlerTest,
                           HandleConsentDeclined);
  FRIEND_TEST_ALL_PREFIXES(PrivacySandboxNoticeDialogHandlerTest,
                           HandleResizeDialog);
  FRIEND_TEST_ALL_PREFIXES(PrivacySandboxNoticeDialogHandlerTest,
                           HandleOpenSettings);
  FRIEND_TEST_ALL_PREFIXES(PrivacySandboxNoticeDialogHandlerTest,
                           HandleNoticeAcknowledge);

  void HandleDialogActionOccurred(base::Value::ConstListView args);
  void HandleResizeDialog(base::Value::ConstListView args);
  void NotifyServiceAboutDialogAction(
      PrivacySandboxService::DialogAction action);

  base::OnceClosure close_callback_;
  base::OnceCallback<void(int)> resize_callback_;
  base::OnceClosure open_settings_callback_;
  PrivacySandboxService::DialogType dialog_type_;

  raw_ptr<PrivacySandboxService> privacy_sandbox_service_;

  // Whether the user has clicked on one of the buttons: accept consent, decline
  // consent, acknowledge notice or open settings.
  bool did_user_make_decision_ = false;
};

#endif  // CHROME_BROWSER_UI_WEBUI_PRIVACY_SANDBOX_PRIVACY_SANDBOX_DIALOG_HANDLER_H_
