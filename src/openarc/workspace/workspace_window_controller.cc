// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/workspace_window_controller.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "openarc/workspace/workspace_tab_state.h"

namespace openarc::workspace {

WorkspaceWindowController::WorkspaceWindowController(
    TabStripModel& tab_strip,
    const SavedEntryCatalog& catalog,
    SpaceId default_space,
    CreateTabCallback create_tab)
    : catalog_(&catalog),
      default_space_(default_space),
      create_tab_(std::move(create_tab)) {
  snapshot_.default_space = default_space_;
  // Do not observe the regular-profile catalog through an OTR/guest window.
  // A future factory must reject those contexts before acquiring the catalog.
  enabled_ = default_space_.value().is_valid() && create_tab_ &&
             tab_strip.profile()->IsRegularProfile() &&
             !tab_strip.profile()->IsOffTheRecord() &&
             !tab_strip.profile()->IsGuestSession() &&
             tab_strip.delegate()->IsNormalWindow();
  if (!enabled_) {
    return;
  }
  tab_strip_ = &tab_strip;
  tab_strip_->AddObserver(this);
  catalog_subscription_ = catalog_->ObserveChanges(base::BindRepeating(
      &WorkspaceWindowController::OnCatalogChanged, base::Unretained(this)));
  Refresh();
}

WorkspaceWindowController::~WorkspaceWindowController() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (tab_strip_) {
    tab_strip_->RemoveObserver(this);
  }
}

const WorkspaceWindowSnapshot& WorkspaceWindowController::GetSnapshot() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return snapshot_;
}

base::CallbackListSubscription WorkspaceWindowController::ObserveChanges(
    base::RepeatingClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return changed_->data.Add(base::BindRepeating(
      [](base::WeakPtr<WorkspaceWindowController> self,
         const base::RepeatingClosure& callback) {
        if (self) {
          callback.Run();
        }
      },
      weak_factory_.GetWeakPtr(), std::move(callback)));
}

WorkspaceWindowCommandResult WorkspaceWindowController::ActivateSaved(
    EntryId entry,
    TabStripUserGestureDetails gesture) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (command_running_ || refreshing_) {
    return WorkspaceWindowCommandResult::kBusy;
  }
  if (!tab_strip_ || !CatalogIsReady()) {
    return WorkspaceWindowCommandResult::kNotReady;
  }
  const auto alive = weak_factory_.GetWeakPtr();
  command_running_ = true;
  base::ScopedClosureRunner command(base::BindOnce(
      [](base::WeakPtr<WorkspaceWindowController> self) {
        if (self) {
          self->command_running_ = false;
        }
      },
      alive));
  Refresh();
  if (!alive || !tab_strip_) {
    return WorkspaceWindowCommandResult::kNotReady;
  }
  const SavedCatalogRow* row = FindEntry(entry);
  if (!row) {
    return WorkspaceWindowCommandResult::kMissingEntry;
  }
  if (auto* tab = ResolveOwnedTab(FindSavedInstance(entry))) {
    tab_strip_->ActivateTab(tab, gesture);
    return WorkspaceWindowCommandResult::kActivated;
  }
  if (!row->url || !row->url->is_valid()) {
    return WorkspaceWindowCommandResult::kInvalidDestination;
  }

  // A callback may synchronously insert/remove tabs or change bookmarks. Keep
  // no node/view pointers across it and never accidentally bind an old page.
  const BookmarkLocator locator = row->locator;
  const GURL destination = *row->url;
  std::set<tabs::TabHandle> preexisting_tabs;
  for (const auto& tab : snapshot_.tabs) {
    preexisting_tabs.insert(tab.tab);
  }
  auto create_tab = create_tab_;
  const tabs::TabHandle handle = create_tab.Run(destination);
  if (!alive) {
    return WorkspaceWindowCommandResult::kNotReady;
  }
  auto* tab = ResolveOwnedTab(handle);
  if (!tab || preexisting_tabs.contains(handle)) {
    return WorkspaceWindowCommandResult::kCreationFailed;
  }
  row = FindEntry(entry);
  auto* state = WorkspaceTabState::From(*tab);
  if (!row || row->locator != locator || !state) {
    return WorkspaceWindowCommandResult::kCreationFailed;
  }
  const auto association = state->GetAssociation();
  if ((association && association->entry_id) ||
      FindSavedInstance(entry).Get()) {
    return WorkspaceWindowCommandResult::kBindingConflict;
  }
  state->SetAssociation(TabWorkspaceBinding{default_space_, entry});
  if (!alive || !tab_strip_) {
    return WorkspaceWindowCommandResult::kNotReady;
  }
  Refresh();
  return alive && FindSavedInstance(entry) == handle
             ? WorkspaceWindowCommandResult::kCreatedAndBound
             : WorkspaceWindowCommandResult::kBindingConflict;
}

WorkspaceWindowCommandResult WorkspaceWindowController::BindSavedTab(
    tabs::TabHandle handle,
    EntryId entry,
    BookmarkLocator bookmark) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (command_running_ || refreshing_) {
    return WorkspaceWindowCommandResult::kBusy;
  }
  if (!tab_strip_ || !CatalogIsReady()) {
    return WorkspaceWindowCommandResult::kNotReady;
  }
  const auto alive = weak_factory_.GetWeakPtr();
  command_running_ = true;
  base::ScopedClosureRunner command(base::BindOnce(
      [](base::WeakPtr<WorkspaceWindowController> self) {
        if (self) {
          self->command_running_ = false;
        }
      },
      alive));
  Refresh();
  if (!alive || !tab_strip_) {
    return WorkspaceWindowCommandResult::kNotReady;
  }
  const SavedCatalogRow* row = FindEntry(entry);
  if (!row || row->locator != bookmark) {
    return WorkspaceWindowCommandResult::kMissingEntry;
  }
  auto* tab = ResolveOwnedTab(handle);
  auto* state = tab ? WorkspaceTabState::From(*tab) : nullptr;
  if (!state) {
    return WorkspaceWindowCommandResult::kUnavailableTab;
  }
  const auto association = state->GetAssociation();
  const tabs::TabHandle existing = FindSavedInstance(entry);
  if ((association && association->entry_id &&
       association->entry_id != entry) ||
      (existing.Get() && existing != handle)) {
    return WorkspaceWindowCommandResult::kBindingConflict;
  }
  state->SetAssociation(TabWorkspaceBinding{default_space_, entry});
  if (!alive || !tab_strip_) {
    return WorkspaceWindowCommandResult::kNotReady;
  }
  Refresh();
  return alive && FindSavedInstance(entry) == handle
             ? WorkspaceWindowCommandResult::kBound
             : WorkspaceWindowCommandResult::kBindingConflict;
}

WorkspaceWindowCommandResult WorkspaceWindowController::CloseSavedInstance(
    EntryId entry) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (command_running_ || refreshing_) {
    return WorkspaceWindowCommandResult::kBusy;
  }
  if (!tab_strip_ || !CatalogIsReady()) {
    return WorkspaceWindowCommandResult::kNotReady;
  }
  const auto alive = weak_factory_.GetWeakPtr();
  command_running_ = true;
  base::ScopedClosureRunner command(base::BindOnce(
      [](base::WeakPtr<WorkspaceWindowController> self) {
        if (self) {
          self->command_running_ = false;
        }
      },
      alive));
  Refresh();
  if (!alive || !tab_strip_) {
    return WorkspaceWindowCommandResult::kNotReady;
  }
  if (!FindEntry(entry)) {
    return WorkspaceWindowCommandResult::kMissingEntry;
  }
  auto* tab = ResolveOwnedTab(FindSavedInstance(entry));
  if (!tab) {
    return WorkspaceWindowCommandResult::kUnavailableTab;
  }
  tab_strip_->delegate()->CloseTab(tab, CloseTabSource::kFromMouse);
  return WorkspaceWindowCommandResult::kCloseRequested;
}

tabs::TabHandle WorkspaceWindowController::FindSavedInstance(EntryId entry) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = claims_.find(entry);
  return found == claims_.end() || !ResolveOwnedTab(found->second)
             ? tabs::TabHandle()
             : found->second;
}

bool WorkspaceWindowController::ShouldShowOrdinaryTab(
    tabs::TabHandle handle) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!ResolveOwnedTab(handle)) {
    return false;
  }
  // During bookmark loading/batches there is no publishable saved projection.
  // Keep every actual page reachable in the ordinary strip.
  return snapshot_.readiness != WorkspaceWindowReadiness::kReady ||
         std::ranges::none_of(claims_, [handle](const auto& claim) {
           return claim.second == handle;
         });
}

const SavedCatalogRow* WorkspaceWindowController::FindEntry(EntryId entry) const {
  if (!CatalogIsReady() || !entry.value().is_valid()) {
    return nullptr;
  }
  for (const auto& space : catalog_->GetSnapshot().spaces) {
    if (space.binding.space_id != default_space_ ||
        space.status != CatalogRootStatus::kAvailable) {
      continue;
    }
    for (const auto& row : space.rows) {
      if (row.status == CatalogEntryStatus::kReady && row.entry_id == entry) {
        return &row;
      }
    }
  }
  return nullptr;
}

tabs::TabInterface* WorkspaceWindowController::ResolveOwnedTab(
    tabs::TabHandle handle) const {
  auto* tab = handle.Get();
  return tab_strip_ && tab &&
                 tab_strip_->GetIndexOfTab(tab) != TabStripModel::kNoTab
             ? tab
             : nullptr;
}

bool WorkspaceWindowController::CatalogIsReady() const {
  return enabled_ &&
         catalog_->GetSnapshot().readiness == CatalogReadiness::kReady;
}

void WorkspaceWindowController::Refresh() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (refreshing_) {
    refresh_pending_ = true;
    return;
  }
  const auto alive = weak_factory_.GetWeakPtr();
  refreshing_ = true;
  do {
    refresh_pending_ = false;
    RefreshAndPublish();
    if (!alive) {
      return;
    }
  } while (refresh_pending_);
  refreshing_ = false;
}

void WorkspaceWindowController::RefreshAndPublish() {
  if (!tab_strip_) {
    return;
  }
  const auto alive = weak_factory_.GetWeakPtr();
  const bool ready = CatalogIsReady();
  std::map<EntryId, tabs::TabHandle> new_claims;
  if (ready) {
    // Existing destination-window claims win a transfer collision even when
    // the incoming tab is inserted before them in the native strip.
    for (const auto& [entry, handle] : claims_) {
      auto* tab = ResolveOwnedTab(handle);
      auto* state = tab ? WorkspaceTabState::From(*tab) : nullptr;
      const auto association = state ? state->GetAssociation() : std::nullopt;
      if (association && association->space_id == default_space_ &&
          association->entry_id == entry && FindEntry(entry)) {
        new_claims.emplace(entry, handle);
      }
    }
    for (int i = 0; i < tab_strip_->count(); ++i) {
      auto* tab = tab_strip_->GetTabAtIndex(i);
      auto* state = WorkspaceTabState::From(*tab);
      if (!state) {
        continue;
      }
      auto binding = state->GetAssociation().value_or(
          TabWorkspaceBinding{default_space_, std::nullopt});
      // This checkpoint supports one default Space only. Other-Space bindings
      // are left intact and visible until the multi-Space controller exists.
      if (binding.space_id != default_space_) {
        continue;
      }
      if (binding.entry_id) {
        if (!FindEntry(*binding.entry_id)) {
          binding.entry_id.reset();
        } else {
          auto [claim, inserted] =
              new_claims.emplace(*binding.entry_id, tab->GetHandle());
          if (!inserted && claim->second != tab->GetHandle()) {
            binding.entry_id.reset();
          }
        }
      }
      state->SetAssociation(binding);
      // A state observer may destroy the window or alter the strip/catalog.
      // Restart from fresh handles after a nested observation, and never let a
      // scope cleanup write back through a destroyed controller.
      if (!alive || !tab_strip_ || refresh_pending_) {
        return;
      }
    }
    claims_ = std::move(new_claims);
  }

  snapshot_.readiness = ready ? WorkspaceWindowReadiness::kReady
                              : WorkspaceWindowReadiness::kWaitingForCatalog;
  snapshot_.tabs.clear();
  for (int i = 0; i < tab_strip_->count(); ++i) {
    auto* tab = tab_strip_->GetTabAtIndex(i);
    auto* state = WorkspaceTabState::From(*tab);
    snapshot_.tabs.push_back(
        {tab->GetHandle(), state ? state->GetAssociation() : std::nullopt,
         i == tab_strip_->active_index(), tab->IsPinned()});
  }
  ++snapshot_.generation;
  auto changed = changed_;
  changed->data.Notify();
}

void WorkspaceWindowController::OnCatalogChanged() {
  Refresh();
}

void WorkspaceWindowController::OnTabStripModelChanged(
    TabStripModel*,
    const TabStripModelChange&,
    const TabStripSelectionChange&) {
  Refresh();
}

void WorkspaceWindowController::OnTabPinnedStateChanged(tabs::TabInterface*,
                                                        int) {
  Refresh();
}

void WorkspaceWindowController::OnTabChangedAt(tabs::TabInterface*,
                                               TabChangeType) {
  Refresh();
}

void WorkspaceWindowController::OnTabStripModelDestroyed(TabStripModel*) {
  // TabStripModelObserver::ModelDestroyed already removed this observation.
  tab_strip_ = nullptr;
  catalog_subscription_ = {};
  claims_.clear();
  snapshot_.tabs.clear();
  snapshot_.readiness = WorkspaceWindowReadiness::kTabStripDestroyed;
  ++snapshot_.generation;
  auto changed = changed_;
  changed->data.Notify();
}

}  // namespace openarc::workspace
