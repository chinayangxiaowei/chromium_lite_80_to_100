// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/web_app_translation_manager.h"

#include "base/strings/utf_string_conversions.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/web_applications/proto/web_app_translations.pb.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "third_party/blink/public/common/features.h"

namespace web_app {

namespace {

constexpr base::TaskTraits kTaskTraits = {
    base::MayBlock(), base::TaskPriority::USER_VISIBLE,
    base::TaskShutdownBehavior::BLOCK_SHUTDOWN};

base::FilePath GetDirectory(const base::FilePath& web_apps_directory) {
  return web_apps_directory.AppendASCII("Translations");
}

LocaleOverrides ConvertTranslationItemToLocaleOverrides(
    blink::Manifest::TranslationItem translation) {
  LocaleOverrides locale_overrides;
  if (translation.name) {
    locale_overrides.set_name(translation.name.value());
  }
  if (translation.short_name) {
    locale_overrides.set_short_name(translation.short_name.value());
  }
  if (translation.description) {
    locale_overrides.set_description(translation.description.value());
  }
  return locale_overrides;
}

blink::Manifest::TranslationItem ConvertLocaleOverridesToTranslationItem(
    LocaleOverrides locale_overrides) {
  blink::Manifest::TranslationItem translation_item;

  if (locale_overrides.has_name()) {
    translation_item.name = locale_overrides.name();
  }

  if (locale_overrides.has_short_name()) {
    translation_item.short_name = locale_overrides.short_name();
  }

  if (locale_overrides.has_description()) {
    translation_item.description = locale_overrides.description();
  }

  return translation_item;
}

AllTranslations ReadProtoBlocking(scoped_refptr<FileUtilsWrapper> utils,
                                  const base::FilePath& web_apps_directory) {
  base::FilePath translations_dir = GetDirectory(web_apps_directory);
  std::string value;
  if (!utils->ReadFileToString(translations_dir, &value)) {
    return AllTranslations();
  }
  AllTranslations proto;
  if (!proto.ParseFromString(value)) {
    return AllTranslations();
  }
  return proto;
}

bool WriteProtoBlocking(scoped_refptr<FileUtilsWrapper> utils,
                        const base::FilePath& web_apps_directory,
                        AllTranslations proto) {
  base::FilePath translations_dir = GetDirectory(web_apps_directory);
  std::string proto_as_string = proto.SerializeAsString();
  int size = base::checked_cast<int>(proto_as_string.size());
  if (utils->WriteFile(translations_dir, proto_as_string.c_str(), size) !=
      size) {
    return false;
  }
  return true;
}

bool DeleteTranslationsBlocking(scoped_refptr<FileUtilsWrapper> utils,
                                const base::FilePath& web_apps_directory,
                                const AppId& app_id) {
  AllTranslations proto = ReadProtoBlocking(utils, web_apps_directory);

  proto.mutable_id_to_translations_map()->erase(app_id);

  return WriteProtoBlocking(utils, web_apps_directory, std::move(proto));
}

bool WriteTranslationsBlocking(
    scoped_refptr<FileUtilsWrapper> utils,
    const base::FilePath& web_apps_directory,
    const AppId& app_id,
    base::flat_map<Locale, blink::Manifest::TranslationItem> translations) {
  if (!utils->CreateDirectory(web_apps_directory)) {
    return false;
  }

  AllTranslations proto = ReadProtoBlocking(utils, web_apps_directory);

  auto* mutable_id_to_translations_map = proto.mutable_id_to_translations_map();
  mutable_id_to_translations_map->erase(app_id);

  WebAppTranslations locale_to_overrides_map;

  for (const auto& translation : translations) {
    (*locale_to_overrides_map
          .mutable_locale_to_overrides_map())[base::UTF16ToUTF8(
        translation.first)] =
        ConvertTranslationItemToLocaleOverrides(translation.second);
  }
  (*proto.mutable_id_to_translations_map())[app_id] = locale_to_overrides_map;

  return WriteProtoBlocking(utils, web_apps_directory, proto);
}

}  // namespace

WebAppTranslationManager::WebAppTranslationManager(
    Profile* profile,
    base::raw_ptr<WebAppInstallManager> install_manager,
    scoped_refptr<FileUtilsWrapper> utils)
    : install_manager_(install_manager), utils_(std::move(utils)) {
  web_apps_directory_ = GetWebAppsRootDirectory(profile);
}

WebAppTranslationManager::~WebAppTranslationManager() = default;

void WebAppTranslationManager::Start() {
  if (base::FeatureList::IsEnabled(
          blink::features::kWebAppEnableTranslations)) {
    ReadTranslations(base::DoNothing());
    install_manager_observation_.Observe(install_manager_.get());
  }
}

// TODO(crbug.com/1259777): Consider adding to cache when writing a translation
// to avoid reading everything again here.
void WebAppTranslationManager::OnWebAppInstalled(const AppId& app_id) {
  ReadTranslations(base::DoNothing());
}

void WebAppTranslationManager::OnWebAppUninstalled(const AppId& app_id) {
  DeleteTranslations(app_id, base::DoNothing());
}

void WebAppTranslationManager::OnWebAppInstallManagerDestroyed() {
  install_manager_observation_.Reset();
}

void WebAppTranslationManager::WriteTranslations(
    const AppId& app_id,
    const base::flat_map<Locale, blink::Manifest::TranslationItem>&
        translations,
    WriteCallback callback) {
  if (base::FeatureList::IsEnabled(
          blink::features::kWebAppEnableTranslations)) {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, kTaskTraits,
        base::BindOnce(WriteTranslationsBlocking, utils_, web_apps_directory_,
                       std::move(app_id), std::move(translations)),
        std::move(callback));
  } else {
    std::move(callback).Run(true);
  }
}

void WebAppTranslationManager::DeleteTranslations(const AppId& app_id,
                                                  WriteCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kTaskTraits,
      base::BindOnce(DeleteTranslationsBlocking, utils_, web_apps_directory_,
                     std::move(app_id)),
      std::move(callback));
}

void WebAppTranslationManager::ReadTranslations(ReadCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kTaskTraits,
      base::BindOnce(ReadProtoBlocking, utils_, web_apps_directory_),
      base::BindOnce(&WebAppTranslationManager::OnTranslationsRead,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void WebAppTranslationManager::OnTranslationsRead(
    ReadCallback callback,
    const AllTranslations& proto) {
  translation_cache_.clear();
  std::string locale = g_browser_process->GetApplicationLocale();

  for (const auto& id_to_translations : proto.id_to_translations_map()) {
    const AppId& app_id = id_to_translations.first;

    for (const auto& locale_to_overrides :
         id_to_translations.second.locale_to_overrides_map()) {
      // TODO(crbug.com/1259777): Check other matching locales. Eg if no name
      // defined in en-US, check en.
      if (locale_to_overrides.first == locale) {
        translation_cache_[app_id] =
            ConvertLocaleOverridesToTranslationItem(locale_to_overrides.second);
      }
    }
  }
  std::move(callback).Run(translation_cache_);
}

}  // namespace web_app
