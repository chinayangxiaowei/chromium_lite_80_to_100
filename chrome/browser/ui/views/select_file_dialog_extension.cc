// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/select_file_dialog_extension.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "ash/constants/ash_features.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/public/cpp/style/color_mode_observer.h"
#include "ash/public/cpp/style/color_provider.h"
#include "ash/public/cpp/style/scoped_light_mode_as_default.h"
#include "ash/public/cpp/tablet_mode.h"
#include "base/feature_list.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/no_destructor.h"
#include "base/strings/stringprintf.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/app_mode/app_mode_utils.h"
#include "chrome/browser/apps/platform_apps/app_window_registry_util.h"
#include "chrome/browser/ash/file_manager/app_id.h"
#include "chrome/browser/ash/file_manager/fileapi_util.h"
#include "chrome/browser/ash/file_manager/select_file_dialog_util.h"
#include "chrome/browser/ash/file_manager/url_util.h"
#include "chrome/browser/ash/login/ui/login_display_host.h"
#include "chrome/browser/ash/login/ui/login_web_dialog.h"
#include "chrome/browser/ash/login/ui/webui_login_view.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/chromeos/extensions/file_manager/select_file_dialog_extension_user_data.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/extensions/extension_dialog.h"
#include "chrome/browser/ui/webui/chromeos/system_web_dialog_delegate.h"
#include "chromeos/ui/base/window_properties.h"
#include "extensions/browser/app_window/app_window.h"
#include "extensions/browser/app_window/native_app_window.h"
#include "ui/aura/window.h"
#include "ui/base/base_window.h"
#include "ui/gfx/color_palette.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/views/widget/widget.h"

using extensions::AppWindow;

namespace {

const int kFileManagerWidth = 972;  // pixels
const int kFileManagerHeight = 640;  // pixels
const int kFileManagerMinimumWidth = 640;  // pixels
const int kFileManagerMinimumHeight = 240;  // pixels

// Holds references to file manager dialogs that have callbacks pending
// to their listeners.
class PendingDialog {
 public:
  static PendingDialog* GetInstance();
  void Add(SelectFileDialogExtension::RoutingID id,
           scoped_refptr<SelectFileDialogExtension> dialog);
  void Remove(SelectFileDialogExtension::RoutingID id);
  scoped_refptr<SelectFileDialogExtension> Find(
      SelectFileDialogExtension::RoutingID id);

 private:
  friend struct base::DefaultSingletonTraits<PendingDialog>;
  using Map = std::map<SelectFileDialogExtension::RoutingID,
                       scoped_refptr<SelectFileDialogExtension>>;
  Map map_;
};

// static
PendingDialog* PendingDialog::GetInstance() {
  static base::NoDestructor<PendingDialog> instance;
  return instance.get();
}

void PendingDialog::Add(SelectFileDialogExtension::RoutingID id,
                        scoped_refptr<SelectFileDialogExtension> dialog) {
  DCHECK(dialog.get());
  if (map_.find(id) == map_.end())
    map_.insert(std::make_pair(id, dialog));
  else
    DLOG(WARNING) << "Duplicate pending dialog " << id;
}

void PendingDialog::Remove(SelectFileDialogExtension::RoutingID id) {
  map_.erase(id);
}

scoped_refptr<SelectFileDialogExtension> PendingDialog::Find(
    SelectFileDialogExtension::RoutingID id) {
  Map::const_iterator it = map_.find(id);
  if (it == map_.end())
    return nullptr;
  return it->second;
}

// Return the Chrome OS WebUI login WebContents, if applicable.
content::WebContents* GetLoginWebContents() {
  auto* host = ash::LoginDisplayHost::default_host();
  return host ? host->GetOobeWebContents() : nullptr;
}

// Given |owner_window| finds corresponding |base_window|, it's associated
// |web_contents| and |profile|.
void FindRuntimeContext(gfx::NativeWindow owner_window,
                        ui::BaseWindow** base_window,
                        content::WebContents** web_contents) {
  *base_window = nullptr;
  *web_contents = nullptr;
  // To get the base_window and web contents, either a Browser or AppWindow is
  // needed.
  Browser* owner_browser = nullptr;
  AppWindow* app_window = nullptr;

  // If owner_window is supplied, use that to find a browser or a app window.
  if (owner_window) {
    owner_browser = chrome::FindBrowserWithWindow(owner_window);
    if (!owner_browser) {
      // If an owner_window was supplied but we couldn't find a browser, this
      // could be for a app window.
      app_window =
          AppWindowRegistryUtil::GetAppWindowForNativeWindowAnyProfile(
              owner_window);
    }
  }

  if (app_window) {
    *base_window = app_window->GetBaseWindow();
    *web_contents = app_window->web_contents();
  } else {
    // If the owning window is still unknown, this could be a background page or
    // and extension popup. Use the last active browser.
    if (!owner_browser)
      owner_browser = chrome::FindLastActive();
    if (owner_browser) {
      *base_window = owner_browser->window();
      *web_contents = owner_browser->tab_strip_model()->GetActiveWebContents();
    }
  }

  // In ChromeOS kiosk launch mode, we can still show file picker for
  // certificate manager dialog. There are no browser or webapp window
  // instances present in this case.
  if (chrome::IsRunningInForcedAppMode() && !(*web_contents))
    *web_contents = ash::LoginWebDialog::GetCurrentWebContents();

  // Check for a WebContents used for the Chrome OS WebUI login flow.
  if (!*web_contents)
    *web_contents = GetLoginWebContents();
}

SelectFileDialogExtension::RoutingID GetRoutingID(
    content::WebContents* web_contents,
    const SelectFileDialogExtension::Owner& owner) {
  if (owner.android_task_id.has_value())
    return base::StringPrintf("android.%d", *owner.android_task_id);

  // Lacros ids are already prefixed with "lacros".
  if (owner.lacros_window_id.has_value())
    return *owner.lacros_window_id;

  if (web_contents) {
    return base::StringPrintf(
        "web.%d", web_contents->GetMainFrame()->GetFrameTreeNodeId());
  }
  LOG(ERROR) << "Unable to generate a RoutingID";
  return "";
}

}  // namespace

// A customization of SystemWebDialogDelegate that provides notifications
// to SelectFileDialogExtension about web dialog closing events. Must be outside
// anonymous namespace for the friend declaration to work.
class SystemFilesAppDialogDelegate : public chromeos::SystemWebDialogDelegate,
                                     public ash::ColorModeObserver {
 public:
  SystemFilesAppDialogDelegate(base::WeakPtr<SelectFileDialogExtension> parent,
                               const std::string& id,
                               GURL url,
                               std::u16string title)
      : chromeos::SystemWebDialogDelegate(url, title),
        id_(id),
        parent_(std::move(parent)) {
    ash::ColorProvider::Get()->AddObserver(this);
  }
  ~SystemFilesAppDialogDelegate() override {
    ash::ColorProvider::Get()->RemoveObserver(this);
  }

  void SetModal(bool modal) {
    set_modal_type(modal ? ui::MODAL_TYPE_WINDOW : ui::MODAL_TYPE_NONE);
  }

  FrameKind GetWebDialogFrameKind() const override {
    // The default is kDialog, however it doesn't allow to customize the title
    // color and to make the dialog movable and re-sizable.
    return FrameKind::kNonClient;
  }

  void AdjustWidgetInitParams(views::Widget::InitParams* params) override {
    params->shadow_type = views::Widget::InitParams::ShadowType::kDefault;
    auto* color_provider = ash::ColorProvider::Get();
    ash::ScopedLightModeAsDefault scoped_light_mode_as_default;
    params->init_properties_container.SetProperty(
        chromeos::kFrameActiveColorKey,
        color_provider->GetActiveDialogTitleBarColor());
    params->init_properties_container.SetProperty(
        chromeos::kFrameInactiveColorKey,
        color_provider->GetInactiveDialogTitleBarColor());
  }

  void GetMinimumDialogSize(gfx::Size* size) const override {
    size->set_width(kFileManagerMinimumWidth);
    size->set_height(kFileManagerMinimumHeight);
  }

  void GetDialogSize(gfx::Size* size) const override {
    *size = SystemWebDialogDelegate::ComputeDialogSizeForInternalScreen(
        {kFileManagerWidth, kFileManagerHeight});
  }

  void OnDialogShown(content::WebUI* webui) override {
    if (parent_) {
      parent_->OnSystemDialogShown(webui->GetWebContents(), id_);
    }
    chromeos::SystemWebDialogDelegate::OnDialogShown(webui);
  }

  void OnDialogWillClose() override {
    if (parent_) {
      parent_->OnSystemDialogWillClose();
    }
  }

  void OnColorModeChanged(bool dark_mode_enabled) override {
    auto* color_provider = ash::ColorProvider::Get();
    dialog_window()->SetProperty(
        chromeos::kFrameActiveColorKey,
        color_provider->GetActiveDialogTitleBarColor());
    dialog_window()->SetProperty(
        chromeos::kFrameInactiveColorKey,
        color_provider->GetInactiveDialogTitleBarColor());
  }

 private:
  // The routing ID. We store it so that we can call back into the
  // SelectFileDialog to inform it about contents::WebContents and
  // the ID associated with it.
  const std::string id_;

  // The parent of this delegate.
  base::WeakPtr<SelectFileDialogExtension> parent_;
};

/////////////////////////////////////////////////////////////////////////////

SelectFileDialogExtension::Owner::Owner() = default;
SelectFileDialogExtension::Owner::~Owner() = default;

// static
SelectFileDialogExtension* SelectFileDialogExtension::Create(
    Listener* listener,
    std::unique_ptr<ui::SelectFilePolicy> policy) {
  return new SelectFileDialogExtension(listener, std::move(policy));
}

SelectFileDialogExtension::SelectFileDialogExtension(
    Listener* listener,
    std::unique_ptr<ui::SelectFilePolicy> policy)
    : SelectFileDialog(listener, std::move(policy)),
      system_files_app_web_contents_(nullptr) {}

SelectFileDialogExtension::~SelectFileDialogExtension() {
  if (extension_dialog_.get())
    extension_dialog_->ObserverDestroyed();
}

bool SelectFileDialogExtension::IsRunning(
    gfx::NativeWindow owner_window) const {
  return owner_window_ == owner_window;
}

void SelectFileDialogExtension::ListenerDestroyed() {
  listener_ = nullptr;
  params_ = nullptr;
  PendingDialog::GetInstance()->Remove(routing_id_);
}

void SelectFileDialogExtension::ExtensionDialogClosing(
    ExtensionDialog* /*dialog*/) {
  if (!ash::features::IsFileManagerSwaEnabled() && ash::ColorProvider::Get())
    ash::ColorProvider::Get()->RemoveObserver(this);
  profile_ = nullptr;
  owner_window_ = nullptr;
  // Release our reference to the underlying dialog to allow it to close.
  extension_dialog_ = nullptr;
  system_files_app_web_contents_ = nullptr;
  PendingDialog::GetInstance()->Remove(routing_id_);
  // Actually invoke the appropriate callback on our listener.
  NotifyListener();
}

void SelectFileDialogExtension::ExtensionTerminated(
    ExtensionDialog* dialog) {
  // The extension crashed (or the process was killed). Close the dialog.
  dialog->GetWidget()->Close();
}

void SelectFileDialogExtension::OnColorModeChanged(bool dark_mode_enabled) {
  if (!ash::features::IsFileManagerSwaEnabled() && extension_dialog_.get()) {
    auto* color_provider = ash::ColorProvider::Get();
    gfx::NativeWindow native_view =
        extension_dialog_->GetWidget()->GetNativeView();
    native_view->SetProperty(chromeos::kFrameActiveColorKey,
                             color_provider->GetActiveDialogTitleBarColor());
    native_view->SetProperty(chromeos::kFrameInactiveColorKey,
                             color_provider->GetInactiveDialogTitleBarColor());
  }
}

void SelectFileDialogExtension::OnSystemDialogShown(
    content::WebContents* web_contents,
    const std::string& id) {
  system_files_app_web_contents_ = web_contents;
  SelectFileDialogExtensionUserData::SetRoutingIdForWebContents(web_contents,
                                                                id);
}

void SelectFileDialogExtension::OnSystemDialogWillClose() {
  ExtensionDialogClosing(nullptr);
}

// static
void SelectFileDialogExtension::OnFileSelected(
    RoutingID routing_id,
    const ui::SelectedFileInfo& file,
    int index) {
  scoped_refptr<SelectFileDialogExtension> dialog =
      PendingDialog::GetInstance()->Find(routing_id);
  if (!dialog.get())
    return;
  dialog->selection_type_ = SINGLE_FILE;
  dialog->selection_files_.clear();
  dialog->selection_files_.push_back(file);
  dialog->selection_index_ = index;
}

// static
void SelectFileDialogExtension::OnMultiFilesSelected(
    RoutingID routing_id,
    const std::vector<ui::SelectedFileInfo>& files) {
  scoped_refptr<SelectFileDialogExtension> dialog =
      PendingDialog::GetInstance()->Find(routing_id);
  if (!dialog.get())
    return;
  dialog->selection_type_ = MULTIPLE_FILES;
  dialog->selection_files_ = files;
  dialog->selection_index_ = 0;
}

// static
void SelectFileDialogExtension::OnFileSelectionCanceled(RoutingID routing_id) {
  scoped_refptr<SelectFileDialogExtension> dialog =
      PendingDialog::GetInstance()->Find(routing_id);
  if (!dialog.get())
    return;
  dialog->selection_type_ = CANCEL;
  dialog->selection_files_.clear();
  dialog->selection_index_ = 0;
}

content::RenderFrameHost* SelectFileDialogExtension::GetMainFrame() {
  if (extension_dialog_)
    return extension_dialog_->host()->main_frame_host();
  else if (system_files_app_web_contents_)
    return system_files_app_web_contents_->GetMainFrame();
  return nullptr;
}

GURL SelectFileDialogExtension::MakeDialogURL(
    Type type,
    const std::u16string& title,
    const base::FilePath& default_path,
    const FileTypeInfo* file_types,
    int file_type_index,
    const std::string& search_query,
    bool show_android_picker_apps,
    Profile* profile) {
  base::FilePath download_default_path(
      DownloadPrefs::FromBrowserContext(profile)->DownloadPath());
  base::FilePath selection_path =
      default_path.IsAbsolute()
          ? default_path
          : download_default_path.Append(default_path.BaseName());
  base::FilePath fallback_path = profile->last_selected_directory().empty()
                                     ? download_default_path
                                     : profile->last_selected_directory();

  // Convert the above absolute paths to file system URLs.
  GURL selection_url;
  if (!file_manager::util::ConvertAbsoluteFilePathToFileSystemUrl(
          profile, selection_path, file_manager::util::GetFileManagerURL(),
          &selection_url)) {
    // Due to the current design, an invalid temporal cache file path may passed
    // as |default_path| (crbug.com/178013 #9-#11). In such a case, we use the
    // last selected directory as a workaround. Real fix is tracked at
    // crbug.com/110119.
    if (!file_manager::util::ConvertAbsoluteFilePathToFileSystemUrl(
            profile, fallback_path.Append(default_path.BaseName()),
            file_manager::util::GetFileManagerURL(), &selection_url)) {
      DVLOG(1) << "Unable to resolve the selection URL.";
    }
  }

  GURL current_directory_url;
  base::FilePath current_directory_path = selection_path.DirName();
  if (!file_manager::util::ConvertAbsoluteFilePathToFileSystemUrl(
          profile, current_directory_path,
          file_manager::util::GetFileManagerURL(), &current_directory_url)) {
    // Fallback if necessary, see the comment above.
    if (!file_manager::util::ConvertAbsoluteFilePathToFileSystemUrl(
            profile, fallback_path, file_manager::util::GetFileManagerURL(),
            &current_directory_url)) {
      DVLOG(1) << "Unable to resolve the current directory URL for: "
               << fallback_path.value();
    }
  }

  return file_manager::util::GetFileManagerMainPageUrlWithParams(
      type, title, current_directory_url, selection_url,
      default_path.BaseName().value(), file_types, file_type_index,
      search_query, show_android_picker_apps);
}

void SelectFileDialogExtension::SelectFileWithFileManagerParams(
    Type type,
    const std::u16string& title,
    const base::FilePath& default_path,
    const FileTypeInfo* file_types,
    int file_type_index,
    void* params,
    const Owner& owner,
    const std::string& search_query,
    bool show_android_picker_apps) {
  if (owner_window_) {
    LOG(ERROR) << "File dialog already in use!";
    return;
  }

  // The base window to associate the dialog with.
  ui::BaseWindow* base_window = nullptr;

  // The web contents to associate the dialog with.
  content::WebContents* web_contents = nullptr;

  // The folder selection dialog created for capture mode should never be
  // parented to a browser window (if one exists). https://crbug.com/1258842.
  const bool is_for_capture_mode =
      owner.window &&
      owner.window->GetId() ==
          ash::kShellWindowId_CaptureModeFolderSelectionDialogOwner;

  const bool skip_finding_browser = is_for_capture_mode ||
                                    owner.android_task_id.has_value() ||
                                    owner.lacros_window_id.has_value();

  can_resize_ = !ash::TabletMode::IsInTabletMode() && !is_for_capture_mode;

  // Obtain BaseWindow and WebContents if the owner window is browser.
  if (!skip_finding_browser)
    FindRuntimeContext(owner.window, &base_window, &web_contents);

  if (web_contents)
    profile_ = Profile::FromBrowserContext(web_contents->GetBrowserContext());

  // Handle the cases where |web_contents| is not available or |web_contents| is
  // associated with Default profile.
  if (!web_contents || ash::ProfileHelper::IsSigninProfile(profile_))
    profile_ = ProfileManager::GetActiveUserProfile();

  DCHECK(profile_);

  // Check if we have another dialog opened for the contents. It's unlikely, but
  // possible. In such situation, discard this request.
  RoutingID routing_id = GetRoutingID(web_contents, owner);
  if (PendingExists(routing_id))
    return;

  GURL file_manager_url = SelectFileDialogExtension::MakeDialogURL(
      type, title, default_path, file_types, file_type_index, search_query,
      show_android_picker_apps, profile_);

  has_multiple_file_type_choices_ =
      !file_types || (file_types->extensions.size() > 1);

  std::u16string dialog_title =
      !title.empty() ? title
                     : file_manager::util::GetSelectFileDialogTitle(type);
  gfx::NativeWindow parent_window =
      base_window ? base_window->GetNativeWindow() : owner.window;

  if (ash::features::IsFileManagerSwaEnabled()) {
    auto* dialog_delegate = new SystemFilesAppDialogDelegate(
        weak_factory_.GetWeakPtr(), routing_id, file_manager_url, dialog_title);
    dialog_delegate->SetModal(owner.window != nullptr);
    dialog_delegate->set_can_resize(can_resize_);
    dialog_delegate->ShowSystemDialogForBrowserContext(profile_, parent_window);
  } else {
    ExtensionDialog::InitParams dialog_params(
        {kFileManagerWidth, kFileManagerHeight});
    dialog_params.is_modal = (owner.window != nullptr);
    dialog_params.min_size = {kFileManagerMinimumWidth,
                              kFileManagerMinimumHeight};
    dialog_params.title = dialog_title;
    auto* color_provider = ash::ColorProvider::Get();
    ash::ScopedLightModeAsDefault scoped_light_mode_as_default;
    dialog_params.title_color = color_provider->GetActiveDialogTitleBarColor();
    dialog_params.title_inactive_color =
        color_provider->GetInactiveDialogTitleBarColor();

    ash::ColorProvider::Get()->AddObserver(this);

    ExtensionDialog* dialog = ExtensionDialog::Show(
        file_manager_url, parent_window, profile_, web_contents,
        /*ExtensionDialogObserver=*/this, dialog_params);
    if (!dialog) {
      LOG(ERROR) << "Unable to create extension dialog";
      return;
    }

    dialog->SetCanResize(can_resize_);
    SelectFileDialogExtensionUserData::SetRoutingIdForWebContents(
        dialog->host()->host_contents(), routing_id);

    extension_dialog_ = dialog;
  }

  // Connect our listener to FileDialogFunction's per-tab callbacks.
  AddPending(routing_id);

  params_ = params;
  routing_id_ = routing_id;
  owner_window_ = owner.window;
}

void SelectFileDialogExtension::SelectFileImpl(
    Type type,
    const std::u16string& title,
    const base::FilePath& default_path,
    const FileTypeInfo* file_types,
    int file_type_index,
    const base::FilePath::StringType& default_extension,
    gfx::NativeWindow owner_window,
    void* params) {
  // |default_extension| is ignored.
  Owner owner;
  owner.window = owner_window;
  SelectFileWithFileManagerParams(type, title, default_path, file_types,
                                  file_type_index, params, owner,
                                  /*search_query=*/"",
                                  /*show_android_picker_apps=*/false);
}

bool SelectFileDialogExtension::HasMultipleFileTypeChoicesImpl() {
  return has_multiple_file_type_choices_;
}

bool SelectFileDialogExtension::IsResizeable() const {
  return can_resize_;
}

void SelectFileDialogExtension::NotifyListener() {
  if (!listener_)
    return;

  // The selected files are passed by reference to the listener. Ensure they
  // outlive the dialog if it is immediately deleted by the listener.
  std::vector<ui::SelectedFileInfo> selection_files =
      std::move(selection_files_);
  selection_files_.clear();

  switch (selection_type_) {
    case CANCEL:
      listener_->FileSelectionCanceled(params_);
      break;
    case SINGLE_FILE:
      listener_->FileSelectedWithExtraInfo(selection_files[0], selection_index_,
                                           params_);
      break;
    case MULTIPLE_FILES:
      listener_->MultiFilesSelectedWithExtraInfo(selection_files, params_);
      break;
    default:
      NOTREACHED();
      break;
  }
}

void SelectFileDialogExtension::AddPending(RoutingID routing_id) {
  PendingDialog::GetInstance()->Add(routing_id, this);
}

// static
bool SelectFileDialogExtension::PendingExists(RoutingID routing_id) {
  return PendingDialog::GetInstance()->Find(routing_id).get() != nullptr;
}
