// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/sct_auditing/sct_auditing_handler.h"

#include <algorithm>

#include "base/base64.h"
#include "base/containers/cxx20_erase.h"
#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/metrics/histogram_functions.h"
#include "base/rand_util.h"
#include "base/task/bind_post_task.h"
#include "base/task/post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_runner_util.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/base/backoff_entry.h"
#include "net/base/backoff_entry_serializer.h"
#include "net/base/hash_value.h"
#include "net/cert/merkle_tree_leaf.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/network_context.h"
#include "services/network/network_service.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/proto/sct_audit_report.pb.h"
#include "services/network/sct_auditing/sct_auditing_cache.h"
#include "services/network/sct_auditing/sct_auditing_reporter.h"

namespace network {

namespace {

std::string LoadReports(const base::FilePath& path) {
  std::string result;
  if (!base::ReadFileToString(path, &result)) {
    return "";
  }
  return result;
}

// Keys in dictionary for each serialized pending reporter entry in the
// top-level list of serialized entries.
const char kReporterKeyKey[] = "reporter_key";
const char kBackoffEntryKey[] = "backoff_entry";
const char kReportKey[] = "report";
const char kSCTHashdanceMetadataKey[] = "sct_metadata";

void RecordPopularSCTSkippedMetrics(bool popular_sct_skipped) {
  base::UmaHistogramBoolean("Security.SCTAuditing.OptOut.PopularSCTSkipped",
                            popular_sct_skipped);
}

}  // namespace

SCTAuditingHandler::SCTAuditingHandler(NetworkContext* context,
                                       const base::FilePath& persistence_path,
                                       size_t cache_size)
    : owner_network_context_(context),
      pending_reporters_(cache_size),
      persistence_path_(persistence_path),
      foreground_runner_(base::SequencedTaskRunnerHandle::Get()) {
  if (base::FeatureList::IsEnabled(features::kSCTAuditingRetryReports) &&
      base::FeatureList::IsEnabled(features::kSCTAuditingPersistReports)) {
    // If no persistence path is set, only store pending reporters in memory.
    if (persistence_path_.empty()) {
      return;
    }

    // Persisting reports uses a low priority task runner as it should not block
    // anything user-visible, but it should block shutdown to ensure updates are
    // persisted to disk (particularly clearing entries or the entire
    // persisted state).
    background_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
         base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
    writer_ = std::make_unique<base::ImportantFileWriter>(persistence_path_,
                                                          background_runner_);

    // Post a task to load persisted state after startup has finished.
    foreground_runner_->PostTask(
        FROM_HERE, base::BindOnce(&SCTAuditingHandler::OnStartupFinished,
                                  weak_factory_.GetWeakPtr()));
  }
}

SCTAuditingHandler::~SCTAuditingHandler() {
  DCHECK(foreground_runner_->RunsTasksInCurrentSequence());
  if (writer_ && writer_->HasPendingWrite()) {
    writer_->DoScheduledWrite();
  }
}

void SCTAuditingHandler::MaybeEnqueueReport(
    const net::HostPortPair& host_port_pair,
    const net::X509Certificate* validated_certificate_chain,
    const net::SignedCertificateTimestampAndStatusList&
        signed_certificate_timestamps) {
  if (mode_ == mojom::SCTAuditingMode::kDisabled) {
    return;
  }

  // Only audit valid SCTs. This ensures that they come from a known log, have
  // a valid signature, and thus are expected to be public certificates. If
  // there are no valid SCTs, there's no need to report anything.
  net::SignedCertificateTimestampAndStatusList validated_scts;
  std::copy_if(
      signed_certificate_timestamps.begin(),
      signed_certificate_timestamps.end(), std::back_inserter(validated_scts),
      [](const auto& sct) { return sct.status == net::ct::SCT_STATUS_OK; });
  if (validated_scts.empty()) {
    return;
  }

  absl::optional<SCTAuditingReporter::SCTHashdanceMetadata> sct_metadata;
  if (mode_ == mojom::SCTAuditingMode::kHashdance) {
    // Randomly select a single entry and calculate its leaf hash for the
    // hashdance lookup query.
    sct_metadata.emplace();
    const net::ct::SignedCertificateTimestamp* sct =
        validated_scts.at(base::RandInt(0, validated_scts.size() - 1))
            .sct.get();
    sct_metadata->issued = sct->timestamp;
    net::ct::MerkleTreeLeaf tree_leaf;
    bool result = net::ct::GetMerkleTreeLeaf(validated_certificate_chain, sct,
                                             &tree_leaf);
    DCHECK(result);
    result = net::ct::HashMerkleTreeLeaf(tree_leaf, &sct_metadata->leaf_hash);
    DCHECK(result);

    // Do not report if this is a known popular SCT.
    if (owner_network_context_->network_service()
            ->sct_auditing_cache()
            ->IsPopularSCT(
                base::as_bytes(base::make_span(sct_metadata->leaf_hash)))) {
      RecordPopularSCTSkippedMetrics(true);
      return;
    }
    RecordPopularSCTSkippedMetrics(false);

    // Find the corresponding log entry metadata.
    const std::vector<mojom::CTLogInfoPtr>& logs =
        owner_network_context_->network_service()->log_list();
    auto log = std::find_if(logs.begin(), logs.end(), [&sct](const auto& log) {
      return log->id == sct->log_id;
    });
    CHECK(log != logs.end());
    sct_metadata->log_id = log->get()->id;
    sct_metadata->log_mmd = log->get()->mmd;
    sct_metadata->certificate_expiry =
        validated_certificate_chain->valid_expiry();
  }
  absl::optional<SCTAuditingCache::ReportEntry> report =
      owner_network_context_->network_service()
          ->sct_auditing_cache()
          ->MaybeGenerateReportEntry(
              host_port_pair, validated_certificate_chain, validated_scts);
  if (!report) {
    return;
  }
  AddReporter(std::move(report->key), std::move(report->report),
              std::move(sct_metadata));
}

bool SCTAuditingHandler::SerializeData(std::string* output) {
  DCHECK(foreground_runner_->RunsTasksInCurrentSequence());

  base::Value reports(base::Value::Type::LIST);
  for (const auto& kv : pending_reporters_) {
    auto reporter_key = kv.first;
    auto* reporter = kv.second.get();

    base::Value report_entry(base::Value::Type::DICTIONARY);

    report_entry.SetStringKey(kReporterKeyKey, reporter_key.ToString());

    if (reporter->sct_hashdance_metadata()) {
      report_entry.SetKey(kSCTHashdanceMetadataKey,
                          reporter->sct_hashdance_metadata()->ToValue());
    }

    base::Value backoff_entry_value =
        net::BackoffEntrySerializer::SerializeToValue(
            *reporter->backoff_entry(), base::Time::Now());
    report_entry.SetKey(kBackoffEntryKey, std::move(backoff_entry_value));

    std::string serialized_report;
    reporter->report()->SerializeToString(&serialized_report);
    base::Base64Encode(serialized_report, &serialized_report);
    report_entry.SetStringKey(kReportKey, serialized_report);

    reports.Append(std::move(report_entry));
  }
  return base::JSONWriter::Write(reports, output);
}

void SCTAuditingHandler::DeserializeData(const std::string& serialized) {
  DCHECK(foreground_runner_->RunsTasksInCurrentSequence());

  // Parse the serialized reports.
  absl::optional<base::Value> value = base::JSONReader::Read(serialized);
  if (!value || !value->is_list()) {
    return;
  }

  size_t num_reporters_deserialized = 0u;
  for (base::Value& sct_entry : value->GetListDeprecated()) {
    if (!sct_entry.is_dict()) {
      continue;
    }

    const std::string* reporter_key_string =
        sct_entry.FindStringKey(kReporterKeyKey);
    const std::string* report_string = sct_entry.FindStringKey(kReportKey);
    const absl::optional<base::Value> sct_metadata_value =
        sct_entry.ExtractKey(kSCTHashdanceMetadataKey);
    const base::Value* backoff_entry_value =
        sct_entry.FindKey(kBackoffEntryKey);

    if (!reporter_key_string || !report_string || !backoff_entry_value) {
      continue;
    }

    // Try to read the reporter_key from the entry and convert back to a
    // HashValue. If it fails, continue to the next entry.
    net::HashValue cache_key(net::HASH_VALUE_SHA256);
    if (!cache_key.FromString(*reporter_key_string)) {
      continue;
    }

    // Check if cache_key already exists. If it's already in the pending set,
    // skip re-adding it.
    auto it = pending_reporters_.Get(cache_key);
    if (it != pending_reporters_.end()) {
      continue;
    }

    // Try to recreate the BackoffEntry from the serialized value.
    std::unique_ptr<net::BackoffEntry> backoff_entry =
        net::BackoffEntrySerializer::DeserializeFromValue(
            *backoff_entry_value, &SCTAuditingReporter::kDefaultBackoffPolicy,
            nullptr, base::Time::Now());
    if (!backoff_entry) {
      continue;
    }

    // Try parsing the serialized protobuf. If it fails, continue to next entry.
    std::string decoded_report_string;
    if (!base::Base64Decode(*report_string, &decoded_report_string)) {
      continue;
    }
    auto audit_report = std::make_unique<sct_auditing::SCTClientReport>();
    if (!audit_report->ParseFromString(decoded_report_string)) {
      continue;
    }

    absl::optional<SCTAuditingReporter::SCTHashdanceMetadata> sct_metadata;
    if (sct_metadata_value) {
      sct_metadata = SCTAuditingReporter::SCTHashdanceMetadata::FromValue(
          *sct_metadata_value);
      if (!sct_metadata) {
        continue;
      }
    }

    AddReporter(cache_key, std::move(audit_report), std::move(sct_metadata),
                std::move(backoff_entry));
    ++num_reporters_deserialized;
  }
  // TODO(crbug.com/1144205): Add metrics for number of reporters deserialized.
}

void SCTAuditingHandler::OnStartupFinished() {
  DCHECK(foreground_runner_->RunsTasksInCurrentSequence());
  is_after_startup_ = true;
  // Load the persisted pending reports from disk on a background sequence, and
  // then process them.
  background_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&LoadReports, persistence_path_),
      base::BindOnce(&SCTAuditingHandler::OnReportsLoadedFromDisk,
                     weak_factory_.GetWeakPtr()));
}

void SCTAuditingHandler::AddReporter(
    net::HashValue reporter_key,
    std::unique_ptr<sct_auditing::SCTClientReport> report,
    absl::optional<SCTAuditingReporter::SCTHashdanceMetadata> sct_metadata,
    std::unique_ptr<net::BackoffEntry> backoff_entry) {
  DCHECK(foreground_runner_->RunsTasksInCurrentSequence());
  if (mode_ == mojom::SCTAuditingMode::kDisabled) {
    return;
  }

  // Get the URLs, traffic annotations, and timing parameters as configured on
  // the SCTAuditingCache.
  auto* sct_auditing_cache =
      owner_network_context_->network_service()->sct_auditing_cache();
  auto log_expected_ingestion_delay =
      sct_auditing_cache->log_expected_ingestion_delay();
  auto log_max_ingestion_random_delay =
      sct_auditing_cache->log_max_ingestion_random_delay();
  auto report_uri = sct_auditing_cache->report_uri();
  auto hashdance_lookup_uri = sct_auditing_cache->hashdance_lookup_uri();
  auto traffic_annotation = sct_auditing_cache->traffic_annotation();
  auto hashdance_traffic_annotation =
      sct_auditing_cache->hashdance_traffic_annotation();

  auto reporter = std::make_unique<SCTAuditingReporter>(
      owner_network_context_, reporter_key, std::move(report),
      mode_ == mojom::SCTAuditingMode::kHashdance, std::move(sct_metadata),
      GetURLLoaderFactory(), log_expected_ingestion_delay,
      log_max_ingestion_random_delay, report_uri, hashdance_lookup_uri,
      traffic_annotation, hashdance_traffic_annotation,
      base::BindRepeating(&SCTAuditingHandler::OnReporterStateUpdated,
                          GetWeakPtr()),
      base::BindOnce(&SCTAuditingHandler::OnReporterFinished, GetWeakPtr()),
      std::move(backoff_entry));
  reporter->Start();
  pending_reporters_.Put(reporter->key(), std::move(reporter));

  if (pending_reporters_.size() > pending_reporters_size_hwm_)
    pending_reporters_size_hwm_ = pending_reporters_.size();

  // Trigger updating the persisted state.
  if (writer_) {
    writer_->ScheduleWrite(this);
  }
}

void SCTAuditingHandler::OnReportsLoadedFromDisk(
    const std::string& serialized) {
  DCHECK(foreground_runner_->RunsTasksInCurrentSequence());
  DCHECK(is_after_startup_);
  DCHECK(!persisted_reports_read_);

  persisted_reports_read_ = true;

  DeserializeData(serialized);
}

// TODO(crbug.com/1144205): This method should take a completion callback (for
// callers like NetworkContext::ClearNetworkingHistoryBetween() that want to be
// able to wait for the write completing), and pass it through to the `writer_`,
// like TransportSecurityState does.
void SCTAuditingHandler::ClearPendingReports() {
  // Delete any outstanding Reporters. This will delete any extant URLLoader
  // instances owned by the Reporters, which will cancel any outstanding
  // requests/connections. Pending (delayed) retry tasks will fast-fail when
  // they trigger as they use a WeakPtr to the Reporter instance that posted the
  // task.
  pending_reporters_.Clear();
  if (writer_) {
    writer_->ScheduleWrite(this);
  }
}

void SCTAuditingHandler::SetMode(mojom::SCTAuditingMode mode) {
  mode_ = mode;

  // High-water-mark metrics get logged hourly (rather than once-per-session at
  // shutdown, as Network Service shutdown is not consistent and non-browser
  // processes can fail to report metrics during shutdown). The timer should
  // only be running if SCT auditing is enabled.
  if (mode != mojom::SCTAuditingMode::kDisabled) {
    histogram_timer_.Start(FROM_HERE, base::Hours(1), this,
                           &SCTAuditingHandler::ReportHWMMetrics);
  } else {
    histogram_timer_.Stop();
    ClearPendingReports();
  }
}

base::WeakPtr<SCTAuditingHandler> SCTAuditingHandler::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void SCTAuditingHandler::OnReporterStateUpdated() {
  DCHECK(foreground_runner_->RunsTasksInCurrentSequence());

  // Trigger updating the persisted state.
  if (writer_) {
    writer_->ScheduleWrite(this);
  }
}

void SCTAuditingHandler::OnReporterFinished(net::HashValue reporter_key) {
  DCHECK(foreground_runner_->RunsTasksInCurrentSequence());

  auto it = pending_reporters_.Get(reporter_key);
  if (it != pending_reporters_.end()) {
    pending_reporters_.Erase(it);
  }

  // Trigger updating the persisted state.
  if (writer_) {
    writer_->ScheduleWrite(this);
  }
}

void SCTAuditingHandler::ReportHWMMetrics() {
  if (mode_ == mojom::SCTAuditingMode::kDisabled) {
    return;
  }
  base::UmaHistogramCounts1000("Security.SCTAuditing.OptIn.ReportersHWM",
                               pending_reporters_size_hwm_);
}

network::mojom::URLLoaderFactory* SCTAuditingHandler::GetURLLoaderFactory() {
  // Create the URLLoaderFactory as needed.
  if (url_loader_factory_ && url_loader_factory_.is_connected()) {
    return url_loader_factory_.get();
  }

  network::mojom::URLLoaderFactoryParamsPtr params =
      network::mojom::URLLoaderFactoryParams::New();
  params->process_id = network::mojom::kBrowserProcessId;
  params->is_corb_enabled = false;
  params->is_trusted = true;
  params->automatically_assign_isolation_info = true;

  url_loader_factory_.reset();
  owner_network_context_->CreateURLLoaderFactory(
      url_loader_factory_.BindNewPipeAndPassReceiver(), std::move(params));

  return url_loader_factory_.get();
}

}  // namespace network
