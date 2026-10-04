// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause
import {Blocked, ProbeError, ownsURL, prefix} from "./scope.mjs";

function assert(condition, message) { if (!condition) throw new ProbeError(message); }

async function fixturePair(tabs, context, query = {}) {
  const {windowId, ids, recordedIds, base, runId} = context;
  if (!Number.isInteger(windowId) || ids.length !== 2 || ids.some((id) => !Number.isInteger(id))) {
    throw new Blocked("Two synthetic tabs in their original window are required.");
  }
  assert(new Set(ids).size === 2 && ids.every((id) => recordedIds.includes(id)),
    "Tab structure check requires this run's recorded tab IDs.");
  // Read only this synthetic window. Unexpected tabs stop the check; their
  // metadata is never retained in the journal or returned in a report.
  const found = await tabs.query({windowId, ...query});
  assert(found.length === 2 && new Set(found.map((tab) => tab.id)).size === 2 &&
    found.every((tab) => ids.includes(tab.id) && tab.windowId === windowId &&
      ownsURL(tab.pendingUrl || tab.url, base, runId)),
    "Synthetic window membership or page ownership changed; structure check stopped.");
  const ordered = [...found].sort((a, b) => a.index - b.index);
  assert(ordered[0].index === 0 && ordered[1].index === 1,
    "Synthetic tab indices are not the expected contiguous zero-based positions.");
  return ordered;
}

function unpinnedAndUngrouped(tabs) {
  assert(tabs.every((tab) => tab.pinned === false && tab.groupId === -1),
    "Synthetic tabs must be unpinned and ungrouped before this check.");
}

export async function pinRoundTrip(tabs, context) {
  const initial = await fixturePair(tabs, context);
  unpinnedAndUngrouped(initial);
  const target = initial[1].id;
  await tabs.update(target, {pinned: true});
  let current = await fixturePair(tabs, context);
  assert(current[0].id === target && current[0].pinned === true && current[1].pinned === false,
    "Pinning did not place the real pinned tab before the unpinned tab.");
  await tabs.update(target, {pinned: false});
  current = await fixturePair(tabs, context);
  unpinnedAndUngrouped(current);
  assert(current[0].id === target, "Unpinning did not leave the tab at the unpinned boundary.");
  await tabs.move(target, {windowId: context.windowId, index: 1});
  current = await fixturePair(tabs, context);
  assert(current.every((tab, index) => tab.id === initial[index].id),
    "Original synthetic tab order was not restored after pinning.");
  return "Pinned/unpinned a recorded tab, read back real indices and pinned ordering, then restored the original order.";
}

export async function groupRoundTrip(tabs, groups, context) {
  unpinnedAndUngrouped(await fixturePair(tabs, context));
  const groupId = await tabs.group({tabIds: context.ids, createProperties: {windowId: context.windowId}});
  assert(Number.isInteger(groupId) && groupId >= 0, "Grouping did not return a valid group ID.");
  let current = await fixturePair(tabs, context);
  assert(current.every((tab) => tab.groupId === groupId && tab.pinned === false),
    "Group membership did not match the two synthetic tabs.");
  let group = await groups.get(groupId);
  assert(group.id === groupId && group.windowId === context.windowId && group.shared !== true,
    "Created group is outside the synthetic window or is unexpectedly shared.");
  const title = `${prefix(context.runId)} group`;
  await groups.update(groupId, {title, color: "purple", collapsed: false});
  group = await groups.get(groupId);
  assert(group.id === groupId && group.windowId === context.windowId && group.title === title &&
    group.color === "purple" && group.collapsed === false && group.shared !== true,
    "Group metadata did not round trip.");
  const matching = await groups.query({windowId: context.windowId, title});
  assert(matching.length === 1 && matching[0].id === groupId,
    "Group query did not return exactly the nonce-labeled synthetic group.");
  current = await fixturePair(tabs, context, {groupId});
  assert(current.every((tab) => tab.groupId === groupId), "Group-filtered tab query returned different membership.");
  // Recheck the whole synthetic window immediately before the next mutation.
  current = await fixturePair(tabs, context);
  assert(current.every((tab) => tab.groupId === groupId), "Synthetic group membership changed before ungrouping.");
  await tabs.ungroup(context.ids);
  unpinnedAndUngrouped(await fixturePair(tabs, context));
  assert((await groups.query({windowId: context.windowId, title})).length === 0,
    "Synthetic group remained after all its tabs were ungrouped.");
  return "Grouped only the recorded pair, round-tripped group metadata, verified API membership and indices, then ungrouped both tabs.";
}

export async function removeOwnedTab(tabs, id, readOwned) {
  const tab = await readOwned(id);
  if (Number.isInteger(tab.groupId) && tab.groupId >= 0) {
    // Tab IDs were persisted before grouping. This also recovers an interrupted
    // group call without needing to trust a stored or potentially reused group ID.
    await tabs.ungroup(id);
    await readOwned(id);
  }
  await tabs.remove(id);
}
