// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#ifndef OPENARC_WORKSPACE_WORKSPACE_RECORD_STORE_H_
#define OPENARC_WORKSPACE_WORKSPACE_RECORD_STORE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/types/expected.h"
#include "openarc/workspace/saved_entry_catalog.h"
#include "openarc/workspace/tab_session_codec.h"

namespace base {
class SequencedTaskRunner;
}

namespace openarc::workspace {

// Initial schema intentionally supports one default Space and one pending save.
// BookmarkModel is the sole saved URL/title/order authority. Recovery intent
// contains identities only; recreating an absent URL needs a new user request.
struct PendingBookmarkCreation {
  base::Uuid operation_id;
  base::Uuid container_uuid;
  base::Uuid root_uuid;
  base::Uuid bookmark_uuid;
  EntryId entry_id;
  bool operator==(const PendingBookmarkCreation&) const = default;
};

struct WorkspaceRecord {
  int revision = 1;
  SpaceId default_space_id;
  std::string label = "Personal";
  std::string icon = "circle";
  std::string theme = "system";
  std::optional<BookmarkLocator> container;
  std::optional<BookmarkLocator> root;
  std::optional<PendingBookmarkCreation> pending;
  bool operator==(const WorkspaceRecord&) const = default;
};

enum class WorkspaceRecordError {
  kPrivatePersistenceDisallowed,
  kNotReady,
  kBusy,
  kReadFailed,
  kMalformed,
  kUnsupportedVersion,
  kRecoveryRequired,
  kWriteFailed,
  kConflict,
  kInvalidRecord,
};

using WorkspaceRecordLoadResult =
    base::expected<std::optional<WorkspaceRecord>, WorkspaceRecordError>;
using WorkspaceRecordWriteResult =
    base::expected<void, WorkspaceRecordError>;

// Concrete regular-profile record I/O. The profile directory is constructor
// context, never a command argument. Uses fixed filenames and a sequenced worker
// for bounded reads and atomic replacement. Existing bytes must still match the
// last successful load/write before replacement, including on the first write.
// Corrupt/future/externally changed records are preserved, never defaulted.
//
// Callback replies run on the creating sequence. Destruction cancels store
// updates and replies with failure; callers should bind their own WeakPtr.
// Already posted atomic writes may finish using owned bytes and paths only.
// This protects application crashes, not arbitrary system/power-loss rollback.
// The factory owns one store per exclusively owned profile. External edits
// during a commit are unsupported; the pre-write comparison is not a file lock.
class WorkspaceRecordStore {
 public:
  static constexpr size_t kMaxRecordBytes = 64 * 1024;
  static constexpr char kFileName[] = "OpenArcWorkspaces.json";
  static constexpr char kPreviousFileName[] = "OpenArcWorkspaces.previous.json";

  static base::expected<std::unique_ptr<WorkspaceRecordStore>,
                        WorkspaceRecordError>
  Create(PersistenceContext context,
         const base::FilePath& profile_directory,
         scoped_refptr<base::SequencedTaskRunner> file_runner);
  ~WorkspaceRecordStore();
  WorkspaceRecordStore(const WorkspaceRecordStore&) = delete;
  WorkspaceRecordStore& operator=(const WorkspaceRecordStore&) = delete;

  void Load(base::OnceCallback<void(WorkspaceRecordLoadResult)> callback);
  void Commit(const WorkspaceRecord& record,
              base::OnceCallback<void(WorkspaceRecordWriteResult)> callback);

 private:
  using FileState =
      std::pair<std::optional<std::string>, std::optional<std::string>>;
  WorkspaceRecordStore(const base::FilePath& profile_directory,
                       scoped_refptr<base::SequencedTaskRunner> file_runner);
  void OnLoaded(base::OnceCallback<void(WorkspaceRecordLoadResult)> callback,
                base::expected<FileState, WorkspaceRecordError> files);
  void OnCommitted(
      std::string bytes,
      int revision,
      base::OnceCallback<void(WorkspaceRecordWriteResult)> callback,
      WorkspaceRecordWriteResult result);

  const base::FilePath path_;
  const base::FilePath previous_path_;
  const scoped_refptr<base::SequencedTaskRunner> file_runner_;
  std::optional<std::string> committed_bytes_;
  std::optional<std::string> committed_previous_bytes_;
  int committed_revision_ = 0;
  bool loaded_ = false;
  bool busy_ = false;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<WorkspaceRecordStore> weak_factory_{this};
};

}  // namespace openarc::workspace

#endif  // OPENARC_WORKSPACE_WORKSPACE_RECORD_STORE_H_
