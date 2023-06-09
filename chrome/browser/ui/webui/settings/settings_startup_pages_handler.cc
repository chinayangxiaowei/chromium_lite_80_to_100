// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/settings_startup_pages_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/settings/settings_utils.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/web_ui.h"
#include "url/gurl.h"

namespace settings {

StartupPagesHandler::StartupPagesHandler(content::WebUI* webui)
    : startup_custom_pages_table_model_(Profile::FromWebUI(webui)) {
}

StartupPagesHandler::~StartupPagesHandler() {
}

void StartupPagesHandler::RegisterMessages() {
  if (Profile::FromWebUI(web_ui())->IsOffTheRecord())
    return;

  web_ui()->RegisterMessageCallback(
      "addStartupPage",
      base::BindRepeating(&StartupPagesHandler::HandleAddStartupPage,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "editStartupPage",
      base::BindRepeating(&StartupPagesHandler::HandleEditStartupPage,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "onStartupPrefsPageLoad",
      base::BindRepeating(&StartupPagesHandler::HandleOnStartupPrefsPageLoad,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "removeStartupPage",
      base::BindRepeating(&StartupPagesHandler::HandleRemoveStartupPage,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setStartupPagesToCurrentPages",
      base::BindRepeating(
          &StartupPagesHandler::HandleSetStartupPagesToCurrentPages,
          base::Unretained(this)));
}

void StartupPagesHandler::OnJavascriptAllowed() {
  startup_custom_pages_table_model_.SetObserver(this);

  PrefService* prefService = Profile::FromWebUI(web_ui())->GetPrefs();
  SessionStartupPref pref = SessionStartupPref::GetStartupPref(prefService);
  startup_custom_pages_table_model_.SetURLs(pref.urls);

  if (pref.urls.empty())
    pref.type = SessionStartupPref::DEFAULT;

  pref_change_registrar_.Init(prefService);
  pref_change_registrar_.Add(
      prefs::kURLsToRestoreOnStartup,
      base::BindRepeating(&StartupPagesHandler::UpdateStartupPages,
                          base::Unretained(this)));
}

void StartupPagesHandler::OnJavascriptDisallowed() {
  startup_custom_pages_table_model_.SetObserver(nullptr);
  pref_change_registrar_.RemoveAll();
}

void StartupPagesHandler::OnModelChanged() {
  base::ListValue startup_pages;
  int page_count = startup_custom_pages_table_model_.RowCount();
  std::vector<GURL> urls = startup_custom_pages_table_model_.GetURLs();
  for (int i = 0; i < page_count; ++i) {
    std::unique_ptr<base::DictionaryValue> entry(new base::DictionaryValue());
    entry->SetStringKey("title",
                        startup_custom_pages_table_model_.GetText(i, 0));
    entry->SetStringKey("url", urls[i].spec());
    entry->SetStringKey("tooltip",
                        startup_custom_pages_table_model_.GetTooltip(i));
    entry->SetIntKey("modelIndex", i);
    startup_pages.Append(std::move(entry));
  }

  FireWebUIListener("update-startup-pages", startup_pages);
}

void StartupPagesHandler::OnItemsChanged(int start, int length) {
  OnModelChanged();
}

void StartupPagesHandler::OnItemsAdded(int start, int length) {
  OnModelChanged();
}

void StartupPagesHandler::OnItemsRemoved(int start, int length) {
  OnModelChanged();
}

void StartupPagesHandler::HandleAddStartupPage(
    base::Value::ConstListView args) {
  CHECK_EQ(2U, args.size());

  const base::Value& callback_id = args[0];
  std::string url_string = args[1].GetString();

  GURL url;
  if (!settings_utils::FixupAndValidateStartupPage(url_string, &url)) {
    ResolveJavascriptCallback(callback_id, base::Value(false));
    return;
  }

  int row_count = startup_custom_pages_table_model_.RowCount();
  int index = row_count;
  if (args[1].is_int() && args[1].GetInt() <= row_count)
    index = args[1].GetInt();

  startup_custom_pages_table_model_.Add(index, url);
  SaveStartupPagesPref();
  ResolveJavascriptCallback(callback_id, base::Value(true));
}

void StartupPagesHandler::HandleEditStartupPage(
    base::Value::ConstListView args) {
  CHECK_EQ(args.size(), 3U);
  const base::Value& callback_id = args[0];
  int index = args[1].GetInt();

  if (index < 0 || index >= startup_custom_pages_table_model_.RowCount()) {
    RejectJavascriptCallback(callback_id, base::Value());
    NOTREACHED();
    return;
  }

  std::string url_string = args[2].GetString();

  GURL fixed_url;
  if (settings_utils::FixupAndValidateStartupPage(url_string, &fixed_url)) {
    std::vector<GURL> urls = startup_custom_pages_table_model_.GetURLs();
    urls[index] = fixed_url;
    startup_custom_pages_table_model_.SetURLs(urls);
    SaveStartupPagesPref();
    ResolveJavascriptCallback(callback_id, base::Value(true));
  } else {
    ResolveJavascriptCallback(callback_id, base::Value(false));
  }
}

void StartupPagesHandler::HandleOnStartupPrefsPageLoad(
    base::Value::ConstListView args) {
  AllowJavascript();
}

void StartupPagesHandler::HandleRemoveStartupPage(
    base::Value::ConstListView args) {
  DCHECK_GE(args.size(), 1u);
  if (!args[0].is_int()) {
    NOTREACHED();
    return;
  }
  int selected_index = args[0].GetInt();

  if (selected_index < 0 ||
      selected_index >= startup_custom_pages_table_model_.RowCount()) {
    NOTREACHED();
    return;
  }

  startup_custom_pages_table_model_.Remove(selected_index);
  SaveStartupPagesPref();
}

void StartupPagesHandler::HandleSetStartupPagesToCurrentPages(
    base::Value::ConstListView args) {
  startup_custom_pages_table_model_.SetToCurrentlyOpenPages(
      web_ui()->GetWebContents());
  SaveStartupPagesPref();
}

void StartupPagesHandler::SaveStartupPagesPref() {
  PrefService* prefs = Profile::FromWebUI(web_ui())->GetPrefs();

  SessionStartupPref pref = SessionStartupPref::GetStartupPref(prefs);
  pref.urls = startup_custom_pages_table_model_.GetURLs();

  if (pref.urls.empty())
    pref.type = SessionStartupPref::DEFAULT;

  SessionStartupPref::SetStartupPref(prefs, pref);
}

void StartupPagesHandler::UpdateStartupPages() {
  const SessionStartupPref startup_pref = SessionStartupPref::GetStartupPref(
      Profile::FromWebUI(web_ui())->GetPrefs());
  startup_custom_pages_table_model_.SetURLs(startup_pref.urls);
  // The change will go to the JS code in the
  // StartupPagesHandler::OnModelChanged() method.
}

}  // namespace settings
