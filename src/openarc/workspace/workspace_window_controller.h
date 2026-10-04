// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#ifndef OPENARC_WORKSPACE_WORKSPACE_WINDOW_CONTROLLER_H_
#define OPENARC_WORKSPACE_WORKSPACE_WINDOW_CONTROLLER_H_

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "components/tabs/public/tab_interface.h"
#include "openarc/workspace/saved_entry_catalog.h"
#include "openarc/workspace/tab_session_codec.h"

class TabStripModel;

namespace openarc::workspace {

enum class WorkspaceWindowReadiness {
  kDisabled,
  kWaitingForCatalog,
  kReady,
  kTabStripDestroyed,
};

struct WorkspaceLiveTab {
  tabs::TabHandle tab;
  std::optional<TabWorkspaceBinding> association;
  bool active = false;
  bool pinned = false;
  bool operator==(const WorkspaceLiveTab&) const = default;
};

struct WorkspaceWindowSnapshot {
  uint64_t generation = 0;
  WorkspaceWindowReadiness readiness = WorkspaceWindowReadiness::kDisabled;
  SpaceId default_space;
  // Same order as the real strip. These handles are live-process identities,
  // never serialized or used as bookmark identity. No row index is a tab index.
  std::vector<WorkspaceLiveTab> tabs;
};

enum class WorkspaceWindowCommandResult {
  kActivated,
  kCreatedAndBound,
  kBound,
  kCloseRequested,
  kNotReady,
  kBusy,
  kMissingEntry,
  kUnavailableTab,
  kBindingConflict,
  kInvalidDestination,
  kCreationFailed,
};

// First, default-Space integration slice. The actual TabStripModel owns all
// pages, ordering, pins, groups, selection and close behavior. This controller
// owns only saved-entry associations and an identifier projection. It does not
// persist sessions, mutate bookmarks, infer identity from URLs, or supply a
// multi-Space/private workspace implementation.
//
// The catalog must outlive this object. Navigation must use the browser's
// ordinary new-tab path, synchronously returning the newly created tab handle
// (or a null handle). It must not return an unrelated preexisting tab. Both
// commands and observations run on the UI sequence. Observers may inspect the
// snapshot; recursive commands are rejected while a transition is publishing.
class WorkspaceWindowController final : private TabStripModelObserver {
 public:
  using CreateTabCallback = base::RepeatingCallback<tabs::TabHandle(const GURL&)>;

  WorkspaceWindowController(TabStripModel& tab_strip,
                            const SavedEntryCatalog& catalog,
                            SpaceId default_space,
                            CreateTabCallback create_tab);
  ~WorkspaceWindowController() override;
  WorkspaceWindowController(const WorkspaceWindowController&) = delete;
  WorkspaceWindowController& operator=(const WorkspaceWindowController&) = delete;

  const WorkspaceWindowSnapshot& GetSnapshot() const;
  base::CallbackListSubscription ObserveChanges(base::RepeatingClosure callback);

  // A loaded entry is activated without navigation. A new entry is reserved
  // through creation and then revalidated against the current catalog. Failed
  // revalidation preserves the newly opened page as an ordinary tab.
  WorkspaceWindowCommandResult ActivateSaved(
      EntryId entry,
      TabStripUserGestureDetails gesture = TabStripUserGestureDetails(
          TabStripUserGestureDetails::GestureType::kOther));

  // Called after a save operation actually creates its bookmark. If another
  // operation has since bound/closed/moved the tab, keep the new bookmark
  // unloaded. The locator guards against stale/copied EntryId metadata.
  WorkspaceWindowCommandResult BindSavedTab(tabs::TabHandle tab,
                                             EntryId entry,
                                             BookmarkLocator bookmark);

  // Requests the same delegate close used by the stock tab strip. The binding
  // survives policy refusal and deferred/canceled beforeunload. Only confirmed
  // strip removal drops this window's claim; transfer retains the tab's state.
  WorkspaceWindowCommandResult CloseSavedInstance(EntryId entry);

  tabs::TabHandle FindSavedInstance(EntryId entry) const;
  bool ShouldShowOrdinaryTab(tabs::TabHandle tab) const;

 private:
  const SavedCatalogRow* FindEntry(EntryId entry) const;
  tabs::TabInterface* ResolveOwnedTab(tabs::TabHandle handle) const;
  bool CatalogIsReady() const;
  void Refresh();
  void RefreshAndPublish();
  void OnCatalogChanged();
  void OnTabStripModelChanged(TabStripModel*,
                              const TabStripModelChange&,
                              const TabStripSelectionChange&) override;
  void OnTabPinnedStateChanged(tabs::TabInterface*, int) override;
  void OnTabChangedAt(tabs::TabInterface*, TabChangeType) override;
  void OnTabStripModelDestroyed(TabStripModel*) override;

  raw_ptr<TabStripModel> tab_strip_ = nullptr;
  const raw_ptr<const SavedEntryCatalog> catalog_;
  const SpaceId default_space_;
  CreateTabCallback create_tab_;
  WorkspaceWindowSnapshot snapshot_;
  std::map<EntryId, tabs::TabHandle> claims_;
  // Retain the notification list through callbacks that close this window.
  scoped_refptr<base::RefCountedData<base::RepeatingClosureList>> changed_ =
      base::MakeRefCounted<base::RefCountedData<base::RepeatingClosureList>>();
  base::CallbackListSubscription catalog_subscription_;
  bool enabled_ = false;
  bool command_running_ = false;
  bool refreshing_ = false;
  bool refresh_pending_ = false;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<WorkspaceWindowController> weak_factory_{this};
};

}  // namespace openarc::workspace

#endif  // OPENARC_WORKSPACE_WORKSPACE_WINDOW_CONTROLLER_H_
