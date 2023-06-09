// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/printing/print_backend_service_manager.h"

#include <memory>
#include <string>

#include "base/bind.h"
#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/services/printing/public/mojom/print_backend_service.mojom.h"
#include "components/crash/core/common/crash_keys.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/service_process_host.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "printing/backend/print_backend.h"

#if BUILDFLAG(IS_WIN)
#include "printing/printed_page_win.h"
#endif

namespace printing {

namespace {

// Histogram name for capturing if any printer drivers were encountered that
// required fallback to workaround an access-denied error.  Determining if this
// happens in the wild would be the impetus to pursue further efforts to
// identify and possibly better rectify such cases.
constexpr char kPrintBackendRequiresElevatedPrivilegeHistogramName[] =
    "Printing.PrintBackend.DriversRequiringElevatedPrivilegeEncountered";

// For fetching remote IDs when there is no printer name.
constexpr char kEmptyPrinterName[] = "";

// Amount of idle time to wait before resetting the connection to the service.
constexpr base::TimeDelta kNoClientsRegisteredResetOnIdleTimeout =
    base::Seconds(10);
constexpr base::TimeDelta kClientsRegisteredResetOnIdleTimeout =
    base::Seconds(120);

PrintBackendServiceManager* g_print_backend_service_manager_singleton = nullptr;

}  // namespace

PrintBackendServiceManager::PrintBackendServiceManager() = default;

PrintBackendServiceManager::~PrintBackendServiceManager() = default;

// static
void PrintBackendServiceManager::LogCallToRemote(
    base::StringPiece name,
    const CallbackContext& context) {
  DVLOG(1) << "Sending " << name << " on remote `" << context.remote_id
           << "`, saved callback ID of " << context.saved_callback_id;
}

// static
void PrintBackendServiceManager::LogCallbackFromRemote(
    base::StringPiece name,
    const CallbackContext& context) {
  DVLOG(1) << name << "completed for remote `" << context.remote_id
           << "` saved callback ID " << context.saved_callback_id;
}

void PrintBackendServiceManager::SetCrashKeys(const std::string& printer_name) {
  if (sandboxed_service_remote_for_test_)
    return;

  // TODO(crbug.com/1227561)  Remove local call for driver info, don't want
  // any residual accesses left into the printer drivers from the browser
  // process.
  base::ScopedAllowBlocking allow_blocking;
  scoped_refptr<PrintBackend> print_backend =
      PrintBackend::CreateInstance(g_browser_process->GetApplicationLocale());
  crash_keys_ = std::make_unique<crash_keys::ScopedPrinterInfo>(
      print_backend->GetPrinterDriverInfo(printer_name));
}

uint32_t PrintBackendServiceManager::RegisterClient() {
  uint32_t client_id = ++last_client_id_;

  VLOG(1) << "Registering a client with ID " << client_id
          << " for print backend service.";
  clients_.emplace(client_id);

  // A new client registration is a signal of impending activity to a print
  // backend service.  Performance can be improved if we ensure that an initial
  // service is ready for when the first Mojo call should happen shortly after
  // this registration.
  // It is possible that there might have been prior clients registered that
  // persisted for a long time (e.g., a tab with a Print Preview left open
  // indefinitely).  We use a long timeout against idleness for that scenario,
  // so we want to perform this optimization check every time regardless of
  // number of clients registered.
  // We don't know if a particular printer might be needed, so for now just
  // start for the blank `printer_name` which would cover queries like getting
  // the default printer and enumerating the list of printers.
  std::string remote_id = GetRemoteIdForPrinterName(kEmptyPrinterName);
  auto iter = sandboxed_remotes_.find(remote_id);
  if (iter == sandboxed_remotes_.end()) {
    // Service not already available, so launch it now so that it will be
    // ready by the time the client gets to point of invoking a Mojo call.
    bool is_sandboxed;
    GetService(kEmptyPrinterName, &is_sandboxed);
  } else {
    // Service already existed, possibly was recently marked for being reset
    // with a short timeout.  Ensure it has the long timeout to be available
    // across user interactions but to also get reclaimed should the user leave
    // it unused indefinitely.
    // Safe to use base::Unretained(this) since `this` is a global singleton
    // which never goes away.
    DVLOG(1) << "Updating to long idle timeout for print backend service id `"
             << remote_id << "`";
    mojo::Remote<mojom::PrintBackendService>& service = iter->second;
    service.set_idle_handler(
        kClientsRegisteredResetOnIdleTimeout,
        base::BindRepeating(&PrintBackendServiceManager::OnIdleTimeout,
                            base::Unretained(this), /*sandboxed=*/true,
                            remote_id));

    // TODO(crbug.com/1225111)  Maybe need to issue a quick call here to get
    // adjusted timeout to take effect?  Ideally not, since there is supposed
    // to be an expected call "soon" after having registered.
  }

  return client_id;
}

void PrintBackendServiceManager::UnregisterClient(uint32_t id) {
  if (!clients_.erase(id)) {
    DVLOG(1) << "Unknown client ID " << id
             << ", is client being unregistered multiple times?";
    return;
  }
  VLOG(1) << "Unregistering client with ID " << id
          << " from print backend service.";

  if (!clients_.empty())
    return;

  // No more clients means that there is an opportunity to more aggressively
  // reclaim resources by letting service processes terminate.  Register a
  // short idle timeout with services.  This is preferred to just resetting
  // them immediately here, in case a user immediately reopens a Print Preview.
  for (auto& iter : sandboxed_remotes_) {
    const std::string& remote_id = iter.first;
    mojo::Remote<mojom::PrintBackendService>& service = iter.second;
    UpdateServiceToShortIdleTimeout(service, /*sandboxed=*/true, remote_id);
  }
  for (auto& iter : unsandboxed_remotes_) {
    const std::string& remote_id = iter.first;
    mojo::Remote<mojom::PrintBackendService>& service = iter.second;
    UpdateServiceToShortIdleTimeout(service, /*sandboxed=*/false, remote_id);
  }
}

void PrintBackendServiceManager::EnumeratePrinters(
    mojom::PrintBackendService::EnumeratePrintersCallback callback) {
  CallbackContext context;
  auto& service = GetServiceAndCallbackContext(kEmptyPrinterName, context);

  SaveCallback(GetRemoteSavedEnumeratePrintersCallbacks(context.is_sandboxed),
               context.remote_id, context.saved_callback_id,
               std::move(callback));

  LogCallToRemote("EnumeratePrinters", context);
  service->EnumeratePrinters(
      base::BindOnce(&PrintBackendServiceManager::OnDidEnumeratePrinters,
                     base::Unretained(this), context));
}

void PrintBackendServiceManager::FetchCapabilities(
    const std::string& printer_name,
    mojom::PrintBackendService::FetchCapabilitiesCallback callback) {
  CallbackContext context;
  auto& service = GetServiceAndCallbackContext(printer_name, context);

  SaveCallback(GetRemoteSavedFetchCapabilitiesCallbacks(context.is_sandboxed),
               context.remote_id, context.saved_callback_id,
               std::move(callback));

  SetCrashKeys(printer_name);

  LogCallToRemote("FetchCapabilities", context);
  service->FetchCapabilities(
      printer_name,
      base::BindOnce(&PrintBackendServiceManager::OnDidFetchCapabilities,
                     base::Unretained(this), context));
}

void PrintBackendServiceManager::GetDefaultPrinterName(
    mojom::PrintBackendService::GetDefaultPrinterNameCallback callback) {
  CallbackContext context;
  auto& service = GetServiceAndCallbackContext(kEmptyPrinterName, context);

  SaveCallback(
      GetRemoteSavedGetDefaultPrinterNameCallbacks(context.is_sandboxed),
      context.remote_id, context.saved_callback_id, std::move(callback));

  LogCallToRemote("GetDefaultPrinterName", context);
  service->GetDefaultPrinterName(
      base::BindOnce(&PrintBackendServiceManager::OnDidGetDefaultPrinterName,
                     base::Unretained(this), context));
}

void PrintBackendServiceManager::GetPrinterSemanticCapsAndDefaults(
    const std::string& printer_name,
    mojom::PrintBackendService::GetPrinterSemanticCapsAndDefaultsCallback
        callback) {
  CallbackContext context;
  auto& service = GetServiceAndCallbackContext(printer_name, context);

  SaveCallback(GetRemoteSavedGetPrinterSemanticCapsAndDefaultsCallbacks(
                   context.is_sandboxed),
               context.remote_id, context.saved_callback_id,
               std::move(callback));

  SetCrashKeys(printer_name);

  LogCallToRemote("GetPrinterSemanticCapsAndDefaults", context);
  service->GetPrinterSemanticCapsAndDefaults(
      printer_name,
      base::BindOnce(
          &PrintBackendServiceManager::OnDidGetPrinterSemanticCapsAndDefaults,
          base::Unretained(this), context));
}

void PrintBackendServiceManager::UseDefaultSettings(
    const std::string& printer_name,
    mojom::PrintBackendService::UseDefaultSettingsCallback callback) {
  CallbackContext context;
  auto& service = GetServiceAndCallbackContext(printer_name, context);

  SaveCallback(GetRemoteSavedUseDefaultSettingsCallbacks(context.is_sandboxed),
               context.remote_id, context.saved_callback_id,
               std::move(callback));

  SetCrashKeys(printer_name);

  LogCallToRemote("UseDefaultSettings", context);
  service->UseDefaultSettings(
      base::BindOnce(&PrintBackendServiceManager::OnDidUseDefaultSettings,
                     base::Unretained(this), context));
}

void PrintBackendServiceManager::UpdatePrintSettings(
    const std::string& printer_name,
    base::flat_map<std::string, base::Value> job_settings,
    mojom::PrintBackendService::UpdatePrintSettingsCallback callback) {
  CallbackContext context;
  auto& service = GetServiceAndCallbackContext(printer_name, context);

  SaveCallback(GetRemoteSavedUpdatePrintSettingsCallbacks(context.is_sandboxed),
               context.remote_id, context.saved_callback_id,
               std::move(callback));

  SetCrashKeys(printer_name);

  LogCallToRemote("UpdatePrintSettings", context);
  service->UpdatePrintSettings(
      std::move(job_settings),
      base::BindOnce(&PrintBackendServiceManager::OnDidUpdatePrintSettings,
                     base::Unretained(this), context));
}

void PrintBackendServiceManager::StartPrinting(
    const std::string& printer_name,
    int document_cookie,
    const std::u16string& document_name,
    mojom::PrintTargetType target_type,
    const PrintSettings& settings,
    mojom::PrintBackendService::StartPrintingCallback callback) {
  CallbackContext context;
  auto& service = GetServiceAndCallbackContext(printer_name, context);

  SaveCallback(GetRemoteSavedStartPrintingCallbacks(context.is_sandboxed),
               context.remote_id, context.saved_callback_id,
               std::move(callback));

  SetCrashKeys(printer_name);

  LogCallToRemote("StartPrinting", context);
  service->StartPrinting(
      document_cookie, document_name, target_type, settings,
      base::BindOnce(&PrintBackendServiceManager::OnDidStartPrinting,
                     base::Unretained(this), context));
}

#if BUILDFLAG(IS_WIN)
void PrintBackendServiceManager::RenderPrintedPage(
    const std::string& printer_name,
    int document_cookie,
    const PrintedPage& page,
    mojom::MetafileDataType page_data_type,
    base::ReadOnlySharedMemoryRegion serialized_page_data,
    mojom::PrintBackendService::RenderPrintedPageCallback callback) {
  CallbackContext context;
  auto& service = GetServiceAndCallbackContext(printer_name, context);

  SaveCallback(GetRemoteSavedRenderPrintedPageCallbacks(context.is_sandboxed),
               context.remote_id, context.saved_callback_id,
               std::move(callback));

  SetCrashKeys(printer_name);

  // Page numbers are 0-based for the printing context.
  const uint32_t page_index = page.page_number() - 1;

  LogCallToRemote("RenderPrintedPage", context);
  service->RenderPrintedPage(
      document_cookie, page_index, page_data_type,
      std::move(serialized_page_data), page.page_size(),
      page.page_content_rect(), page.shrink_factor(),
      base::BindOnce(&PrintBackendServiceManager::OnDidRenderPrintedPage,
                     base::Unretained(this), context));
}
#endif  // BUILDFLAG(IS_WIN)

void PrintBackendServiceManager::DocumentDone(
    const std::string& printer_name,
    int document_cookie,
    mojom::PrintBackendService::DocumentDoneCallback callback) {
  CallbackContext context;
  auto& service = GetServiceAndCallbackContext(printer_name, context);

  SaveCallback(GetRemoteSavedDocumentDoneCallbacks(context.is_sandboxed),
               context.remote_id, context.saved_callback_id,
               std::move(callback));

  SetCrashKeys(printer_name);

  LogCallToRemote("DocumentDone", context);
  service->DocumentDone(
      document_cookie,
      base::BindOnce(&PrintBackendServiceManager::OnDidDocumentDone,
                     base::Unretained(this), context));
}

bool PrintBackendServiceManager::PrinterDriverRequiresElevatedPrivilege(
    const std::string& printer_name) const {
  return drivers_requiring_elevated_privilege_.contains(printer_name);
}

void PrintBackendServiceManager::SetPrinterDriverRequiresElevatedPrivilege(
    const std::string& printer_name) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  VLOG(1) << "Destination '" << printer_name
          << "' requires elevated privileges.";
  if (drivers_requiring_elevated_privilege_.emplace(printer_name).second &&
      drivers_requiring_elevated_privilege_.size() == 1) {
    // First time we've detected a problem for any driver.
    base::UmaHistogramBoolean(
        kPrintBackendRequiresElevatedPrivilegeHistogramName, /*sample=*/true);
  }
}

void PrintBackendServiceManager::SetServiceForTesting(
    mojo::Remote<mojom::PrintBackendService>* remote) {
  sandboxed_service_remote_for_test_ = remote;
  sandboxed_service_remote_for_test_->set_disconnect_handler(base::BindOnce(
      &PrintBackendServiceManager::OnRemoteDisconnected, base::Unretained(this),
      /*sandboxed=*/true, /*remote_id=*/std::string()));
}

void PrintBackendServiceManager::SetServiceForFallbackTesting(
    mojo::Remote<mojom::PrintBackendService>* remote) {
  unsandboxed_service_remote_for_test_ = remote;
  unsandboxed_service_remote_for_test_->set_disconnect_handler(base::BindOnce(
      &PrintBackendServiceManager::OnRemoteDisconnected, base::Unretained(this),
      /*sandboxed=*/false, /*remote_id=*/std::string()));
}

// static
PrintBackendServiceManager& PrintBackendServiceManager::GetInstance() {
  if (!g_print_backend_service_manager_singleton) {
    g_print_backend_service_manager_singleton =
        new PrintBackendServiceManager();
  }
  return *g_print_backend_service_manager_singleton;
}

// static
void PrintBackendServiceManager::ResetForTesting() {
  if (g_print_backend_service_manager_singleton) {
    delete g_print_backend_service_manager_singleton;
    g_print_backend_service_manager_singleton = nullptr;
  }
}

std::string PrintBackendServiceManager::GetRemoteIdForPrinterName(
    const std::string& printer_name) const {
  if (sandboxed_service_remote_for_test_) {
    // Test environment is always just one instance for all printers.
    return std::string();
  }

#if BUILDFLAG(IS_WIN)
  // Windows drivers are not thread safe.  Use a process per driver to prevent
  // bad interactions when interfacing to multiple drivers in parallel.
  // https://crbug.com/957242
  return printer_name;
#else
  return std::string();
#endif
}

const mojo::Remote<mojom::PrintBackendService>&
PrintBackendServiceManager::GetService(const std::string& printer_name,
                                       bool* is_sandboxed) {
  bool should_sandbox = !PrinterDriverRequiresElevatedPrivilege(printer_name);
  *is_sandboxed = should_sandbox;

  if (sandboxed_service_remote_for_test_) {
    // The presence of a sandboxed remote for testing signals a testing
    // environment.  If no unsandboxed test service was provided for fallback
    // processing then use the sandboxed one for that as well.
    if (!should_sandbox && unsandboxed_service_remote_for_test_)
      return *unsandboxed_service_remote_for_test_;

    return *sandboxed_service_remote_for_test_;
  }

  // Performance is improved if a service is launched ahead of the time it will
  // be needed by client callers.
  DCHECK(!clients_.empty());

  RemotesMap& remote =
      should_sandbox ? sandboxed_remotes_ : unsandboxed_remotes_;

  // On the first print make note that so far no drivers have required fallback.
  static bool first_print = true;
  if (first_print) {
    DCHECK(should_sandbox);
    first_print = false;
    base::UmaHistogramBoolean(
        kPrintBackendRequiresElevatedPrivilegeHistogramName, /*sample=*/false);
  }

  std::string remote_id = GetRemoteIdForPrinterName(printer_name);
  auto iter = remote.find(remote_id);
  if (iter == remote.end()) {
    // First time for this `remote_id`.
    auto result = remote.emplace(printer_name,
                                 mojo::Remote<mojom::PrintBackendService>());
    iter = result.first;
  }

  mojo::Remote<mojom::PrintBackendService>& service = iter->second;
  if (!service) {
    VLOG(1) << "Launching print backend "
            << (should_sandbox ? "sandboxed" : "unsandboxed") << " for '"
            << remote_id << "'";
    if (should_sandbox) {
      mojo::Remote<mojom::SandboxedPrintBackendHost> sandboxed;
      content::ServiceProcessHost::Launch(
          sandboxed.BindNewPipeAndPassReceiver(),
          content::ServiceProcessHost::Options()
              .WithDisplayName(IDS_UTILITY_PROCESS_PRINT_BACKEND_SERVICE_NAME)
              .Pass());
      sandboxed->BindBackend(service.BindNewPipeAndPassReceiver());
      sandboxed_hosts_.Add(std::move(sandboxed));
    } else {
      mojo::Remote<mojom::UnsandboxedPrintBackendHost> unsandboxed;
      content::ServiceProcessHost::Launch(
          unsandboxed.BindNewPipeAndPassReceiver(),
          content::ServiceProcessHost::Options()
              .WithDisplayName(IDS_UTILITY_PROCESS_PRINT_BACKEND_SERVICE_NAME)
              .Pass());
      unsandboxed->BindBackend(service.BindNewPipeAndPassReceiver());
      unsandboxed_hosts_.Add(std::move(unsandboxed));
    }

    // Ensure that if the interface is ever disconnected (e.g. the service
    // process crashes) then we will drop our handle to the remote.
    // Safe to use base::Unretained(this) since `this` is a global singleton
    // which never goes away.
    service.set_disconnect_handler(
        base::BindOnce(&PrintBackendServiceManager::OnRemoteDisconnected,
                       base::Unretained(this), should_sandbox, remote_id));

    // Beware of case where a user leaves a tab with a Print Preview open
    // indefinitely.  Use a long timeout against idleness to reclaim the unused
    // resources of an idle print backend service for this case.
    // Safe to use base::Unretained(this) since `this` is a global singleton
    // which never goes away.
    DVLOG(1) << "Updating to long idle timeout for print backend service id `"
             << remote_id << "`";
    service.set_idle_handler(
        kClientsRegisteredResetOnIdleTimeout,
        base::BindRepeating(&PrintBackendServiceManager::OnIdleTimeout,
                            base::Unretained(this), should_sandbox, remote_id));

    // Initialize the new service for the desired locale.
    service->Init(g_browser_process->GetApplicationLocale());
  }

  return service;
}

void PrintBackendServiceManager::UpdateServiceToShortIdleTimeout(
    mojo::Remote<mojom::PrintBackendService>& service,
    bool sandboxed,
    const std::string& remote_id) {
  DVLOG(1) << "Updating to short idle timeout for "
           << (sandboxed ? "sandboxed" : "unsandboxed")
           << " print backend service id `" << remote_id << "`";
  service.set_idle_handler(
      kNoClientsRegisteredResetOnIdleTimeout,
      base::BindRepeating(&PrintBackendServiceManager::OnIdleTimeout,
                          base::Unretained(this), sandboxed, remote_id));

  // TODO(crbug.com/1225111)  Make a superfluous call to the service, just to
  // cause an IPC that will in turn make the adjusted timeout value actually
  // take effect.
  service->Poke();
}

void PrintBackendServiceManager::OnIdleTimeout(bool sandboxed,
                                               const std::string& remote_id) {
  DVLOG(1) << "Print Backend service idle timeout for "
           << (sandboxed ? "sandboxed" : "unsandboxed") << " remote id "
           << remote_id;
  if (sandboxed) {
    sandboxed_remotes_.erase(remote_id);
  } else {
    unsandboxed_remotes_.erase(remote_id);
  }
}

void PrintBackendServiceManager::OnRemoteDisconnected(
    bool sandboxed,
    const std::string& remote_id) {
  DVLOG(1) << "Print Backend service disconnected for "
           << (sandboxed ? "sandboxed" : "unsandboxed") << " remote id "
           << remote_id;
  if (sandboxed) {
    sandboxed_remotes_.erase(remote_id);
  } else {
    unsandboxed_remotes_.erase(remote_id);
  }
  RunSavedCallbacksStructResult(
      GetRemoteSavedEnumeratePrintersCallbacks(sandboxed), remote_id,
      mojom::PrinterListResult::NewResultCode(mojom::ResultCode::kFailed));
  RunSavedCallbacksStructResult(
      GetRemoteSavedFetchCapabilitiesCallbacks(sandboxed), remote_id,
      mojom::PrinterCapsAndInfoResult::NewResultCode(
          mojom::ResultCode::kFailed));
  RunSavedCallbacksStructResult(
      GetRemoteSavedGetDefaultPrinterNameCallbacks(sandboxed), remote_id,
      mojom::DefaultPrinterNameResult::NewResultCode(
          mojom::ResultCode::kFailed));
  RunSavedCallbacksStructResult(
      GetRemoteSavedGetPrinterSemanticCapsAndDefaultsCallbacks(sandboxed),
      remote_id,
      mojom::PrinterSemanticCapsAndDefaultsResult::NewResultCode(
          mojom::ResultCode::kFailed));
  RunSavedCallbacksStructResult(
      GetRemoteSavedUseDefaultSettingsCallbacks(sandboxed), remote_id,
      mojom::PrintSettingsResult::NewResultCode(mojom::ResultCode::kFailed));
  RunSavedCallbacksStructResult(
      GetRemoteSavedUpdatePrintSettingsCallbacks(sandboxed), remote_id,
      mojom::PrintSettingsResult::NewResultCode(mojom::ResultCode::kFailed));
  RunSavedCallbacksResult(GetRemoteSavedStartPrintingCallbacks(sandboxed),
                          remote_id, mojom::ResultCode::kFailed);
#if BUILDFLAG(IS_WIN)
  RunSavedCallbacksResult(GetRemoteSavedRenderPrintedPageCallbacks(sandboxed),
                          remote_id, mojom::ResultCode::kFailed);
#endif
  RunSavedCallbacksResult(GetRemoteSavedDocumentDoneCallbacks(sandboxed),
                          remote_id, mojom::ResultCode::kFailed);
}

PrintBackendServiceManager::RemoteSavedEnumeratePrintersCallbacks&
PrintBackendServiceManager::GetRemoteSavedEnumeratePrintersCallbacks(
    bool sandboxed) {
  return sandboxed ? sandboxed_saved_enumerate_printers_callbacks_
                   : unsandboxed_saved_enumerate_printers_callbacks_;
}

PrintBackendServiceManager::RemoteSavedFetchCapabilitiesCallbacks&
PrintBackendServiceManager::GetRemoteSavedFetchCapabilitiesCallbacks(
    bool sandboxed) {
  return sandboxed ? sandboxed_saved_fetch_capabilities_callbacks_
                   : unsandboxed_saved_fetch_capabilities_callbacks_;
}

PrintBackendServiceManager::RemoteSavedGetDefaultPrinterNameCallbacks&
PrintBackendServiceManager::GetRemoteSavedGetDefaultPrinterNameCallbacks(
    bool sandboxed) {
  return sandboxed ? sandboxed_saved_get_default_printer_name_callbacks_
                   : unsandboxed_saved_get_default_printer_name_callbacks_;
}

PrintBackendServiceManager::
    RemoteSavedGetPrinterSemanticCapsAndDefaultsCallbacks&
    PrintBackendServiceManager::
        GetRemoteSavedGetPrinterSemanticCapsAndDefaultsCallbacks(
            bool sandboxed) {
  return sandboxed
             ? sandboxed_saved_get_printer_semantic_caps_and_defaults_callbacks_
             : unsandboxed_saved_get_printer_semantic_caps_and_defaults_callbacks_;
}

PrintBackendServiceManager::RemoteSavedUseDefaultSettingsCallbacks&
PrintBackendServiceManager::GetRemoteSavedUseDefaultSettingsCallbacks(
    bool sandboxed) {
  return sandboxed ? sandboxed_saved_use_default_settings_callbacks_
                   : unsandboxed_saved_use_default_settings_callbacks_;
}

PrintBackendServiceManager::RemoteSavedUpdatePrintSettingsCallbacks&
PrintBackendServiceManager::GetRemoteSavedUpdatePrintSettingsCallbacks(
    bool sandboxed) {
  return sandboxed ? sandboxed_saved_update_print_settings_callbacks_
                   : unsandboxed_saved_update_print_settings_callbacks_;
}

PrintBackendServiceManager::RemoteSavedStartPrintingCallbacks&
PrintBackendServiceManager::GetRemoteSavedStartPrintingCallbacks(
    bool sandboxed) {
  return sandboxed ? sandboxed_saved_start_printing_callbacks_
                   : unsandboxed_saved_start_printing_callbacks_;
}

#if BUILDFLAG(IS_WIN)
PrintBackendServiceManager::RemoteSavedRenderPrintedPageCallbacks&
PrintBackendServiceManager::GetRemoteSavedRenderPrintedPageCallbacks(
    bool sandboxed) {
  return sandboxed ? sandboxed_saved_render_printed_page_callbacks_
                   : unsandboxed_saved_render_printed_page_callbacks_;
}
#endif

PrintBackendServiceManager::RemoteSavedDocumentDoneCallbacks&
PrintBackendServiceManager::GetRemoteSavedDocumentDoneCallbacks(
    bool sandboxed) {
  return sandboxed ? sandboxed_saved_document_done_callbacks_
                   : unsandboxed_saved_document_done_callbacks_;
}

const mojo::Remote<mojom::PrintBackendService>&
PrintBackendServiceManager::GetServiceAndCallbackContext(
    const std::string& printer_name,
    CallbackContext& context) {
  context.remote_id = GetRemoteIdForPrinterName(printer_name);
  context.saved_callback_id = base::UnguessableToken::Create();
  return GetService(printer_name, &context.is_sandboxed);
}

template <class T, class X>
void PrintBackendServiceManager::SaveCallback(
    RemoteSavedCallbacks<T>& saved_callbacks,
    const std::string& remote_id,
    const base::UnguessableToken& saved_callback_id,
    base::OnceCallback<void(X)> callback) {
  saved_callbacks[remote_id].emplace(saved_callback_id, std::move(callback));
}

template <class T, class X>
void PrintBackendServiceManager::ServiceCallbackDone(
    RemoteSavedCallbacks<T>& saved_callbacks,
    const std::string& remote_id,
    const base::UnguessableToken& saved_callback_id,
    X data) {
  auto found_callback_map = saved_callbacks.find(remote_id);
  DCHECK(found_callback_map != saved_callbacks.end());

  SavedCallbacks<T>& callback_map = found_callback_map->second;

  auto callback_entry = callback_map.find(saved_callback_id);
  DCHECK(callback_entry != callback_map.end());
  base::OnceCallback<void(X)> callback = std::move(callback_entry->second);
  callback_map.erase(callback_entry);

  // Done disconnect wrapper management, propagate the callback.
  std::move(callback).Run(std::move(data));
}

void PrintBackendServiceManager::OnDidEnumeratePrinters(
    const CallbackContext& context,
    mojom::PrinterListResultPtr printer_list) {
  LogCallbackFromRemote("EnumeratePrinters", context);
  ServiceCallbackDone(
      GetRemoteSavedEnumeratePrintersCallbacks(context.is_sandboxed),
      context.remote_id, context.saved_callback_id, std::move(printer_list));
}

void PrintBackendServiceManager::OnDidFetchCapabilities(
    const CallbackContext& context,
    mojom::PrinterCapsAndInfoResultPtr printer_caps_and_info) {
  LogCallbackFromRemote("FetchCapabilities", context);
  ServiceCallbackDone(
      GetRemoteSavedFetchCapabilitiesCallbacks(context.is_sandboxed),
      context.remote_id, context.saved_callback_id,
      std::move(printer_caps_and_info));
}

void PrintBackendServiceManager::OnDidGetDefaultPrinterName(
    const CallbackContext& context,
    mojom::DefaultPrinterNameResultPtr printer_name) {
  LogCallbackFromRemote("GetDefaultPrinterName", context);
  ServiceCallbackDone(
      GetRemoteSavedGetDefaultPrinterNameCallbacks(context.is_sandboxed),
      context.remote_id, context.saved_callback_id, std::move(printer_name));
}

void PrintBackendServiceManager::OnDidGetPrinterSemanticCapsAndDefaults(
    const CallbackContext& context,
    mojom::PrinterSemanticCapsAndDefaultsResultPtr printer_caps) {
  LogCallbackFromRemote("GetPrinterSemanticCapsAndDefaults", context);
  ServiceCallbackDone(GetRemoteSavedGetPrinterSemanticCapsAndDefaultsCallbacks(
                          context.is_sandboxed),
                      context.remote_id, context.saved_callback_id,
                      std::move(printer_caps));
}

void PrintBackendServiceManager::OnDidUseDefaultSettings(
    const CallbackContext& context,
    mojom::PrintSettingsResultPtr settings) {
  LogCallbackFromRemote("UseDefaultSettings", context);
  ServiceCallbackDone(
      GetRemoteSavedUseDefaultSettingsCallbacks(context.is_sandboxed),
      context.remote_id, context.saved_callback_id, std::move(settings));
}

void PrintBackendServiceManager::OnDidUpdatePrintSettings(
    const CallbackContext& context,
    mojom::PrintSettingsResultPtr settings) {
  LogCallbackFromRemote("UpdatePrintSettings", context);
  ServiceCallbackDone(
      GetRemoteSavedUpdatePrintSettingsCallbacks(context.is_sandboxed),
      context.remote_id, context.saved_callback_id, std::move(settings));
}

void PrintBackendServiceManager::OnDidStartPrinting(
    const CallbackContext& context,
    mojom::ResultCode result) {
  LogCallbackFromRemote("StartPrinting", context);
  ServiceCallbackDone(
      GetRemoteSavedStartPrintingCallbacks(context.is_sandboxed),
      context.remote_id, context.saved_callback_id, result);
}

#if BUILDFLAG(IS_WIN)
void PrintBackendServiceManager::OnDidRenderPrintedPage(
    const CallbackContext& context,
    mojom::ResultCode result) {
  LogCallbackFromRemote("RenderPrintedPage", context);
  ServiceCallbackDone(
      GetRemoteSavedRenderPrintedPageCallbacks(context.is_sandboxed),
      context.remote_id, context.saved_callback_id, result);
}
#endif

void PrintBackendServiceManager::OnDidDocumentDone(
    const CallbackContext& context,
    mojom::ResultCode result) {
  LogCallbackFromRemote("DocumentDone", context);
  ServiceCallbackDone(GetRemoteSavedDocumentDoneCallbacks(context.is_sandboxed),
                      context.remote_id, context.saved_callback_id, result);
}

template <class T>
void PrintBackendServiceManager::RunSavedCallbacksStructResult(
    RemoteSavedStructCallbacks<T>& saved_callbacks,
    const std::string& remote_id,
    mojo::StructPtr<T> result_to_clone) {
  auto found_callbacks_map = saved_callbacks.find(remote_id);
  if (found_callbacks_map == saved_callbacks.end())
    return;  // No callbacks to run.

  SavedCallbacks<mojo::StructPtr<T>>& callbacks_map =
      found_callbacks_map->second;
  for (auto& iter : callbacks_map) {
    const base::UnguessableToken& saved_callback_id = iter.first;
    DVLOG(1) << "Propagating print backend callback, saved callback ID "
             << saved_callback_id << " for remote `" << remote_id << "`";

    // Don't remove entries from the map while we are iterating through it,
    // just run the callbacks.
    base::OnceCallback<void(mojo::StructPtr<T>)>& callback = iter.second;
    std::move(callback).Run(result_to_clone.Clone());
  }

  // Now that we're done iterating we can safely delete all of the callbacks.
  callbacks_map.clear();
}

template <class T>
void PrintBackendServiceManager::RunSavedCallbacksResult(
    RemoteSavedCallbacks<T>& saved_callbacks,
    const std::string& remote_id,
    T result) {
  auto found_callbacks_map = saved_callbacks.find(remote_id);
  if (found_callbacks_map == saved_callbacks.end())
    return;  // No callbacks to run.

  SavedCallbacks<T>& callbacks_map = found_callbacks_map->second;
  for (auto& iter : callbacks_map) {
    const base::UnguessableToken& saved_callback_id = iter.first;
    DVLOG(1) << "Propagating print backend callback, saved callback ID "
             << saved_callback_id << " for remote `" << remote_id << "`";

    // Don't remove entries from the map while we are iterating through it,
    // just run the callbacks.
    base::OnceCallback<void(T)>& callback = iter.second;
    std::move(callback).Run(result);
  }

  // Now that we're done iterating we can safely delete all of the callbacks.
  callbacks_map.clear();
}

}  // namespace printing
