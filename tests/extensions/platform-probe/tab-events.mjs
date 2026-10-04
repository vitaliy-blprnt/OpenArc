// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause
import {Blocked, ProbeError} from "./scope.mjs";
import {fixturePair} from "./tab-structure.mjs";

function assert(condition, message) { if (!condition) throw new ProbeError(message); }

export async function tabEventDelivery(tabs, context, timers = {
  setTimeout: (callback, delay) => globalThis.setTimeout(callback, delay),
  clearTimeout: (id) => globalThis.clearTimeout(id),
}) {
  if (!tabs.onMoved || !tabs.onActivated) throw new Blocked("Tab event APIs are unavailable.");
  let active = true;
  let timer;
  const deadline = new Promise((_, reject) => {
    timer = timers.setTimeout(() => {
      active = false;
      reject(new ProbeError("Synthetic tab events did not complete within 10 seconds."));
    }, 10000);
  });
  // Every awaited read/mutation is bounded. A late API resolution after timeout
  // cannot advance to another mutation; Chrome cannot cancel an in-flight call.
  const bounded = (operation) => Promise.race([deadline, Promise.resolve().then(() => {
    if (!active) throw new ProbeError("Synthetic tab event check has ended.");
    return operation();
  })]);
  const pair = async () => {
    const found = await bounded(() => fixturePair(tabs, context));
    assert(found.every((tab) => tab.pinned === false && tab.groupId === -1),
      "Tab event check requires ordinary unpinned, ungrouped synthetic tabs.");
    assert(found.filter((tab) => tab.active === true).length === 1 &&
      found.every((tab) => typeof tab.active === "boolean"),
    "Synthetic window does not have exactly one active tab.");
    return found;
  };
  const observeMutation = async (event, accepts, validate, mutate) => {
    let listener;
    let armed = false;
    const observed = new Promise((resolve, reject) => {
      listener = (...args) => {
        if (!active || !armed) return;
        try { if (accepts(...args)) resolve(); }
        catch (error) { reject(error); }
      };
    });
    // Attach a rejection handler before awaiting ownership checks. An event
    // may arrive before the mutation's Promise is fulfilled. Events before
    // dispatch cannot satisfy this action's check.
    observed.catch(() => {});
    try {
      event.addListener(listener);
      await validate();
      await bounded(() => Promise.all([observed, bounded(() => {
        armed = true;
        return mutate();
      })]));
    } finally {
      armed = false;
      event.removeListener(listener);
    }
  };
  try {
    const initial = await pair();
    const windowId = context.windowId;
    const movedId = initial[1].id;
    const otherId = initial[0].id;
    await observeMutation(tabs.onMoved, (id, info) => {
      if (id !== movedId || info?.windowId !== windowId) return false;
      assert(info.fromIndex === 1 && info.toIndex === 0,
        "Synthetic tab move event contained unexpected indices.");
      return true;
    }, async () => {
      const current = await pair();
      assert(current[0].id === otherId && current[1].id === movedId,
        "Synthetic order changed before the event mutation; tabs were preserved.");
    }, () => tabs.move(movedId, {windowId, index: 0}));
    const moved = await pair();
    assert(moved[0].id === movedId && moved[1].id === otherId,
      "Synthetic tab move did not match its delivered event.");
    const activateId = moved.find((tab) => !tab.active).id;
    await observeMutation(tabs.onActivated, (info) =>
      info?.tabId === activateId && info.windowId === windowId,
    async () => {
      const current = await pair();
      assert(current[0].id === movedId && current[1].id === otherId &&
        current.some((tab) => tab.id === activateId && !tab.active),
      "Synthetic tab state changed before activation; tabs were preserved.");
    }, () => tabs.update(activateId, {active: true}));
    const activated = await pair();
    assert(activated.some((tab) => tab.id === activateId && tab.active),
      "Synthetic tab activation did not match its delivered event.");
    return "Observed onMoved with exact fixture indices and onActivated for the recorded window/tab; API completion and owned state matched both events.";
  } finally {
    active = false;
    timers.clearTimeout(timer);
  }
}
