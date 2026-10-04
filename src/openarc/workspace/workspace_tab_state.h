// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#ifndef OPENARC_WORKSPACE_WORKSPACE_TAB_STATE_H_
#define OPENARC_WORKSPACE_WORKSPACE_TAB_STATE_H_

#include <optional>

#include "base/callback_list.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/tabs/contents_observing_tab_feature.h"
#include "openarc/workspace/tab_session_codec.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"

namespace openarc::workspace {

class WorkspaceWindowController;
class WorkspaceTabStateTestPeer;

// Owned by TabFeatures, not WebContents. Navigation, discard, transfer and a
// canceled close must not erase the identifier binding. This class never writes
// bookmarks/sessions, owns a page, or interprets a detach as a confirmed close.
// A future window controller is the sole production mutation authority.
class WorkspaceTabState : public tabs::ContentsObservingTabFeature {
 public:
  DECLARE_USER_DATA(WorkspaceTabState);

  explicit WorkspaceTabState(tabs::TabInterface& tab);
  ~WorkspaceTabState() override;

  static WorkspaceTabState* From(tabs::TabInterface& tab);
  std::optional<TabWorkspaceBinding> GetAssociation() const;
  base::CallbackListSubscription ObserveChanges(base::RepeatingClosure callback);

 private:
  friend class WorkspaceWindowController;
  friend class WorkspaceTabStateTestPeer;

  // Invalid input leaves the current binding intact. A repeated valid value is
  // a no-op. Clearing a saved entry while retaining Space requires a bound
  // value whose entry_id is empty; nullopt clears the entire association.
  bool SetAssociation(std::optional<TabWorkspaceBinding> association);

  std::optional<TabWorkspaceBinding> association_;
  base::RepeatingClosureList changes_;
  ui::ScopedUnownedUserData<WorkspaceTabState> scoped_data_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace openarc::workspace

#endif  // OPENARC_WORKSPACE_WORKSPACE_TAB_STATE_H_
