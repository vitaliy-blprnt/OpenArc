// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/workspace_record_store.h"

#include <limits>
#include <set>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"

namespace openarc::workspace {
namespace {

using Error = WorkspaceRecordError;
using BytesResult = base::expected<std::optional<std::string>, Error>;
using FileState =
    std::pair<std::optional<std::string>, std::optional<std::string>>;

template <class Result>
class ReplyGuard {
 public:
  ReplyGuard(base::OnceCallback<void(Result)> callback, Result failure)
      : callback_(base::BindPostTaskToCurrentDefault(std::move(callback))),
        failure_(std::move(failure)) {}
  ~ReplyGuard() {
    if (callback_) {
      std::move(callback_).Run(std::move(failure_));
    }
  }
  void Reply(Result result) {
    std::move(callback_).Run(std::move(result));
  }

 private:
  base::OnceCallback<void(Result)> callback_;
  Result failure_;
};

template <class Result>
base::OnceCallback<void(Result)> GuardReply(
    base::OnceCallback<void(Result)> callback, Result failure) {
  return base::BindOnce(
      &ReplyGuard<Result>::Reply,
      std::make_unique<ReplyGuard<Result>>(std::move(callback),
                                           std::move(failure)));
}

bool ValidLocator(const std::optional<BookmarkLocator>& locator) {
  return !locator || (locator->storage == BookmarkStorage::kLocalOrSyncable &&
                      locator->uuid.is_valid());
}

bool ValidRecord(const WorkspaceRecord& record) {
  if (record.revision < 1 || !record.default_space_id->is_valid() ||
      record.label.empty() || record.label.size() > 128 ||
      !base::IsStringUTF8(record.label) || record.icon != "circle" ||
      record.theme != "system" || !ValidLocator(record.container) ||
      !ValidLocator(record.root)) {
    return false;
  }
  if (record.container && record.root &&
      record.container->uuid == record.root->uuid) {
    return false;
  }
  if (record.pending) {
    const auto& pending = *record.pending;
    if (!pending.operation_id.is_valid() || !pending.container_uuid.is_valid() ||
        !pending.root_uuid.is_valid() || !pending.bookmark_uuid.is_valid() ||
        !pending.entry_id->is_valid() ||
        std::set{pending.container_uuid, pending.root_uuid, pending.bookmark_uuid}
                .size() != 3 ||
        (record.container &&
         record.container->uuid != pending.container_uuid) ||
        (record.root && record.root->uuid != pending.root_uuid)) {
      return false;
    }
  }
  return true;
}

base::Value EncodeLocator(const std::optional<BookmarkLocator>& locator) {
  if (!locator) {
    return base::Value();
  }
  base::ListValue value;
  value.Append(0);  // Version 1 writes only local-or-syncable roots.
  value.Append(locator->uuid.AsLowercaseString());
  return base::Value(std::move(value));
}

base::expected<std::optional<BookmarkLocator>, Error> DecodeLocator(
    const base::Value& value) {
  if (value.is_none()) {
    return std::nullopt;
  }
  const auto* row = value.GetIfList();
  if (!row || row->size() != 2 || (*row)[0].GetIfInt() != 0 ||
      !(*row)[1].is_string()) {
    return base::unexpected(Error::kMalformed);
  }
  const auto uuid = base::Uuid::ParseLowercase((*row)[1].GetString());
  if (!uuid.is_valid()) {
    return base::unexpected(Error::kMalformed);
  }
  return BookmarkLocator{BookmarkStorage::kLocalOrSyncable, uuid};
}

base::expected<std::string, Error> Encode(const WorkspaceRecord& record) {
  if (!ValidRecord(record)) {
    return base::unexpected(Error::kInvalidRecord);
  }
  // Exact arrays avoid duplicate object-key ambiguity. No URLs or page titles.
  base::ListValue value;
  value.Append(1);
  value.Append(record.revision);
  value.Append(record.default_space_id->AsLowercaseString());
  value.Append(record.label);
  value.Append(record.icon);
  value.Append(record.theme);
  value.Append(EncodeLocator(record.container));
  value.Append(EncodeLocator(record.root));
  if (record.pending) {
    const auto& pending = *record.pending;
    base::ListValue intent;
    intent.Append(pending.operation_id.AsLowercaseString());
    intent.Append(pending.container_uuid.AsLowercaseString());
    intent.Append(pending.root_uuid.AsLowercaseString());
    intent.Append(pending.bookmark_uuid.AsLowercaseString());
    intent.Append(pending.entry_id->AsLowercaseString());
    value.Append(std::move(intent));
  } else {
    value.Append(base::Value());
  }
  auto bytes = base::WriteJson(value, 3);
  if (!bytes || bytes->size() > WorkspaceRecordStore::kMaxRecordBytes) {
    return base::unexpected(Error::kInvalidRecord);
  }
  return std::move(*bytes);
}

base::expected<WorkspaceRecord, Error> Decode(std::string_view bytes) {
  if (bytes.size() > WorkspaceRecordStore::kMaxRecordBytes) {
    return base::unexpected(Error::kMalformed);
  }
  const auto value = base::JSONReader::ReadList(bytes, base::JSON_PARSE_RFC, 3);
  if (!value || value->empty() || !(*value)[0].is_int()) {
    return base::unexpected(Error::kMalformed);
  }
  if ((*value)[0].GetInt() != 1) {
    return base::unexpected(Error::kUnsupportedVersion);
  }
  if (value->size() != 9 || !(*value)[1].is_int()) {
    return base::unexpected(Error::kMalformed);
  }
  for (size_t i = 2; i <= 5; ++i) {
    if (!(*value)[i].is_string()) {
      return base::unexpected(Error::kMalformed);
    }
  }
  WorkspaceRecord record;
  record.revision = (*value)[1].GetInt();
  record.default_space_id =
      SpaceId(base::Uuid::ParseLowercase((*value)[2].GetString()));
  record.label = (*value)[3].GetString();
  record.icon = (*value)[4].GetString();
  record.theme = (*value)[5].GetString();
  auto container = DecodeLocator((*value)[6]);
  auto root = DecodeLocator((*value)[7]);
  if (!container.has_value() || !root.has_value()) {
    return base::unexpected(Error::kMalformed);
  }
  record.container = *container;
  record.root = *root;
  if (!(*value)[8].is_none()) {
    const auto* row = (*value)[8].GetIfList();
    if (!row || row->size() != 5) {
      return base::unexpected(Error::kMalformed);
    }
    for (const auto& field : *row) {
      if (!field.is_string()) {
        return base::unexpected(Error::kMalformed);
      }
    }
    record.pending = PendingBookmarkCreation{
        base::Uuid::ParseLowercase((*row)[0].GetString()),
        base::Uuid::ParseLowercase((*row)[1].GetString()),
        base::Uuid::ParseLowercase((*row)[2].GetString()),
        base::Uuid::ParseLowercase((*row)[3].GetString()),
        EntryId(base::Uuid::ParseLowercase((*row)[4].GetString()))};
  }
  if (!ValidRecord(record)) {
    return base::unexpected(Error::kMalformed);
  }
  return record;
}

BytesResult Read(const base::FilePath& path) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    if (file.error_details() == base::File::FILE_ERROR_NOT_FOUND) {
      return std::nullopt;
    }
    return base::unexpected(Error::kReadFailed);
  }
  std::string bytes;
  if (!base::ReadFileToStringWithMaxSize(
          path, &bytes, WorkspaceRecordStore::kMaxRecordBytes)) {
    return base::unexpected(Error::kReadFailed);
  }
  return std::move(bytes);
}

base::expected<FileState, Error> LoadFromDisk(
    const base::FilePath& path, const base::FilePath& previous_path) {
  auto bytes = Read(path);
  auto previous = Read(previous_path);
  if (!bytes.has_value() || !previous.has_value()) {
    return base::unexpected(Error::kReadFailed);
  }
  if (!*bytes && *previous) {
    // A missing primary with any previous copy requires explicit recovery.
    return base::unexpected(Error::kRecoveryRequired);
  }
  return FileState(std::move(*bytes), std::move(*previous));
}

WorkspaceRecordWriteResult CommitToDisk(
    const base::FilePath& path,
    const base::FilePath& previous_path,
    const std::optional<std::string>& expected,
    const std::optional<std::string>& expected_previous,
    const std::string& next) {
  auto current = Read(path);
  auto previous = Read(previous_path);
  if (!current.has_value() || *current != expected) {
    return base::unexpected(Error::kConflict);
  }
  // A failed previous attempt may have rotated the known current bytes before
  // its primary replacement failed. Only these already-verified bytes can be
  // accepted in place of the originally loaded previous copy.
  if (!previous.has_value() ||
      (*previous != expected_previous && *previous != expected)) {
    return base::unexpected(Error::kConflict);
  }
  if (expected &&
      !base::ImportantFileWriter::WriteFileAtomically(previous_path, *expected)) {
    return base::unexpected(Error::kWriteFailed);
  }
  if (!base::ImportantFileWriter::WriteFileAtomically(path, next)) {
    return base::unexpected(Error::kWriteFailed);
  }
  return {};
}

}  // namespace

base::expected<std::unique_ptr<WorkspaceRecordStore>, WorkspaceRecordError>
WorkspaceRecordStore::Create(
    PersistenceContext context,
    const base::FilePath& profile_directory,
    scoped_refptr<base::SequencedTaskRunner> file_runner) {
  if (context != PersistenceContext::kRegularProfile) {
    return base::unexpected(Error::kPrivatePersistenceDisallowed);
  }
  if (!file_runner || !profile_directory.IsAbsolute()) {
    return base::unexpected(Error::kInvalidRecord);
  }
  return std::unique_ptr<WorkspaceRecordStore>(
      new WorkspaceRecordStore(profile_directory, std::move(file_runner)));
}

WorkspaceRecordStore::WorkspaceRecordStore(
    const base::FilePath& profile_directory,
    scoped_refptr<base::SequencedTaskRunner> file_runner)
    : path_(profile_directory.AppendASCII(kFileName)),
      previous_path_(profile_directory.AppendASCII(kPreviousFileName)),
      file_runner_(std::move(file_runner)) {}

WorkspaceRecordStore::~WorkspaceRecordStore() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void WorkspaceRecordStore::Load(
    base::OnceCallback<void(WorkspaceRecordLoadResult)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  callback = GuardReply(
      std::move(callback),
      WorkspaceRecordLoadResult(base::unexpected(Error::kReadFailed)));
  if (busy_ || loaded_) {
    std::move(callback).Run(base::unexpected(Error::kNotReady));
    return;
  }
  busy_ = true;
  const bool posted = file_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&LoadFromDisk, path_, previous_path_),
      base::BindOnce(&WorkspaceRecordStore::OnLoaded, weak_factory_.GetWeakPtr(),
                     std::move(callback)));
  if (!posted) {
    busy_ = false;
  }
}

void WorkspaceRecordStore::OnLoaded(
    base::OnceCallback<void(WorkspaceRecordLoadResult)> callback,
    base::expected<FileState, Error> files) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  busy_ = false;
  if (!files.has_value()) {
    std::move(callback).Run(base::unexpected(files.error()));
    return;
  }
  if (files->second) {
    auto previous = Decode(*files->second);
    if (!previous.has_value()) {
      std::move(callback).Run(base::unexpected(previous.error()));
      return;
    }
  }
  if (!files->first) {
    loaded_ = true;
    std::move(callback).Run(std::nullopt);
    return;
  }
  auto record = Decode(*files->first);
  if (!record) {
    std::move(callback).Run(base::unexpected(record.error()));
    return;
  }
  loaded_ = true;
  committed_revision_ = record->revision;
  committed_bytes_ = std::move(files->first);
  committed_previous_bytes_ = std::move(files->second);
  std::move(callback).Run(std::move(*record));
}

void WorkspaceRecordStore::Commit(
    const WorkspaceRecord& record,
    base::OnceCallback<void(WorkspaceRecordWriteResult)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  callback = GuardReply(
      std::move(callback),
      WorkspaceRecordWriteResult(base::unexpected(Error::kWriteFailed)));
  if (!loaded_ || busy_) {
    std::move(callback).Run(base::unexpected(busy_ ? Error::kBusy
                                                 : Error::kNotReady));
    return;
  }
  if (committed_revision_ == std::numeric_limits<int>::max() ||
      record.revision != committed_revision_ + 1) {
    std::move(callback).Run(base::unexpected(Error::kInvalidRecord));
    return;
  }
  auto encoded = Encode(record);
  if (!encoded) {
    std::move(callback).Run(base::unexpected(encoded.error()));
    return;
  }
  busy_ = true;
  const std::string bytes = std::move(*encoded);
  const bool posted = file_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&CommitToDisk, path_, previous_path_, committed_bytes_,
                     committed_previous_bytes_, bytes),
      base::BindOnce(&WorkspaceRecordStore::OnCommitted,
                     weak_factory_.GetWeakPtr(), bytes, record.revision,
                     std::move(callback)));
  if (!posted) {
    busy_ = false;
  }
}

void WorkspaceRecordStore::OnCommitted(
    std::string bytes,
    int revision,
    base::OnceCallback<void(WorkspaceRecordWriteResult)> callback,
    WorkspaceRecordWriteResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  busy_ = false;
  if (result) {
    committed_previous_bytes_ = committed_bytes_;
    committed_bytes_ = std::move(bytes);
    committed_revision_ = revision;
  }
  std::move(callback).Run(std::move(result));
}

}  // namespace openarc::workspace
