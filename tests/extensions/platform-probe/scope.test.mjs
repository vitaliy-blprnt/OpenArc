// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause
import test from "node:test";
import assert from "node:assert/strict";
import {prefix, fixtureURL, exampleURL, ownsURL, ownsBookmark, bookmarkCleanupOrder, validateLedger, ProbeError, Blocked, diagnosticMessage, validNativeReply} from "./scope.mjs";
import {checkNativeHost} from "./native.mjs";

const run = "c638f088-bfe0-4a18-a667-15b293258055";
const other = "a638f088-bfe0-4a18-a667-15b293258055";
const base = "chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/";
const record = {id: "13", parentId: "12", title: `${prefix(run)} link`, url: exampleURL(run)};

test("ownership accepts only this run's packaged fixture and exact example page", () => {
  assert.equal(ownsURL(fixtureURL(base, run, "first"), base, run), true);
  assert.equal(ownsURL(exampleURL(run), base, run), true);
  for (const url of [
    "https://example.com/", exampleURL(other), "https://private.example/",
    fixtureURL(base, other, "first"), fixtureURL(base.replaceAll("a", "b"), run, "first"),
    fixtureURL(base, run, "first").replace("fixture.html", "dashboard.html"),
    `${exampleURL(run)}#changed`, `${fixtureURL(base, run, "first")}&unrelated=1`,
  ]) assert.equal(ownsURL(url, base, run), false, url);
});

test("bookmark cleanup preserves changed, moved, renamed, and foreign entries", () => {
  assert.equal(ownsBookmark({...record}, record, run), true);
  for (const change of [
    {id: "99"}, {parentId: "1"}, {title: "A personal bookmark"},
    {url: "https://example.com/private"}, {url: undefined},
  ]) assert.equal(ownsBookmark({...record, ...change}, record, run), false);
  assert.equal(ownsBookmark({...record}, record, other), false);
});

test("cleanup orders moved folders after their current descendants", () => {
  const records = [
    {id: "root", parentId: "external"}, {id: "A", parentId: "B"},
    {id: "B", parentId: "root"}, {id: "leaf", parentId: "A"},
  ];
  assert.deepEqual(bookmarkCleanupOrder(records).map((node) => node.id), ["leaf", "A", "B", "root"]);
  assert.deepEqual(records.map((node) => node.id), ["root", "A", "B", "leaf"]);
});

test("invalid cleanup journals fail closed", () => {
  const valid = {version: 1, runId: run, tabs: [4], bookmarks: [record], menu: null, intent: null};
  assert.equal(validateLedger(valid), valid);
  for (const change of [
    {version: 2}, {runId: "not-a-run"}, {tabs: [-1]}, {tabs: [4, 4]},
    {tabs: ["4"]}, {bookmarks: [{...record, url: "https://private.example/"}]},
    {bookmarks: [{...record, title: "not a fixture"}]}, {menu: "another-menu"},
  ]) assert.throws(() => validateLedger({...valid, ...change}), /Invalid resource journal/);
});

test("pending creation requires exact run-owned resources", () => {
  const valid = {version: 1, runId: run, tabs: [], bookmarks: [], menu: null,
    intent: {kind: "tab", title: `${prefix(run)} first`, url: fixtureURL(base, run, "first")}};
  assert.equal(validateLedger(valid, base), valid);
  assert.throws(() => validateLedger({...valid, intent: {...valid.intent, url: fixtureURL(base, other, "first")}}, base));
  assert.throws(() => validateLedger({...valid, intent: {kind: "bookmark", title: "Personal bookmark"}}, base));
});

test("reports withhold arbitrary API errors that may contain unrelated URLs", () => {
  const error = new Error("Cannot access page https://private.example/account?token=secret");
  assert.equal(diagnosticMessage(error).includes("private.example"), false);
  assert.equal(diagnosticMessage(error).includes("secret"), false);
  assert.equal(diagnosticMessage(new ProbeError("Fixture changed; preserved.")), "Fixture changed; preserved.");
});

test("native transport passes only the exact nonce and protocol contract", () => {
  const reply = {type: "pong", nonce: run, protocol: 1};
  assert.equal(validNativeReply(reply, run), true);
  for (const invalid of [null, [], {}, {...reply, nonce: other}, {...reply, protocol: "1"},
    {...reply, protocol: 2}, {...reply, type: "ping"}, {...reply, extra: "unexpected"}]) {
    assert.equal(validNativeReply(invalid, run), false);
  }
});

function nativeHarness() {
  const event = () => ({listeners: new Set(),
    addListener(callback) { this.listeners.add(callback); },
    removeListener(callback) { this.listeners.delete(callback); },
    emit(value) { for (const callback of [...this.listeners]) callback(value); }});
  const port = {onMessage: event(), onDisconnect: event(), sent: [], disconnectCount: 0,
    postMessage(value) { this.sent.push(value); },
    disconnect() { this.disconnectCount++; }};
  const timers = {callback: null, cleared: false,
    setTimeout(callback, ms) { assert.equal(ms, 10000); this.callback = callback; return 42; },
    clearTimeout(id) { assert.equal(id, 42); this.cleared = true; }};
  const runtime = {lastError: undefined, connectNative(host) {
    assert.equal(host, "org.openarc.platform_probe"); return port;
  }};
  const closed = () => {
    assert.equal(port.disconnectCount, 1);
    assert.equal(port.onMessage.listeners.size, 0);
    assert.equal(port.onDisconnect.listeners.size, 0);
    assert.equal(timers.cleared, true);
  };
  return {port, timers, runtime, closed};
}

test("native success sends one challenge and closes the port and timer", async () => {
  const harness = nativeHarness();
  const result = checkNativeHost(harness.runtime, run, harness.timers);
  assert.deepEqual(harness.port.sent, [{type: "ping", nonce: run}]);
  harness.port.onMessage.emit({type: "pong", nonce: run, protocol: 1});
  await result;
  harness.closed();
});

test("native default timers preserve the global receiver required by browser timers", async (context) => {
  const harness = nativeHarness();
  context.mock.method(globalThis, "setTimeout", function (callback, ms) {
    assert.equal(this, globalThis, "browser timer must receive the global object");
    return harness.timers.setTimeout(callback, ms);
  });
  context.mock.method(globalThis, "clearTimeout", function (id) {
    assert.equal(this, globalThis, "browser timer cleanup must receive the global object");
    return harness.timers.clearTimeout(id);
  });
  const result = checkNativeHost(harness.runtime, run);
  harness.port.onMessage.emit({type: "pong", nonce: run, protocol: 1});
  await result;
  harness.closed();
});

test("native timeout disconnects and a late valid response cannot pass", async () => {
  const harness = nativeHarness();
  const result = checkNativeHost(harness.runtime, run, harness.timers);
  const rejected = assert.rejects(result, /did not answer within 10 seconds/);
  const lateReply = [...harness.port.onMessage.listeners][0];
  harness.timers.callback();
  await rejected;
  harness.closed();
  lateReply({type: "pong", nonce: run, protocol: 1});
  await assert.rejects(result, /did not answer within 10 seconds/);
  assert.equal(harness.port.sent.length, 1);
});

test("native first mismatched response fails even before a subsequent valid reply", async () => {
  const harness = nativeHarness();
  const result = checkNativeHost(harness.runtime, run, harness.timers);
  const rejected = assert.rejects(result, /invalid nonce or protocol/);
  harness.port.onMessage.emit({type: "pong", nonce: other, protocol: 1});
  harness.port.onMessage.emit({type: "pong", nonce: run, protocol: 1});
  await rejected;
  harness.closed();
});

test("native disconnect sanitizes errors while missing host remains blocked", async () => {
  for (const [message, expectedType] of [
    ["Specified native messaging host not found.", Blocked],
    ["A failure at /private/example/secret", ProbeError],
  ]) {
    const harness = nativeHarness();
    const result = checkNativeHost(harness.runtime, run, harness.timers);
    const rejected = assert.rejects(result, (error) => {
      assert.equal(error.constructor, expectedType);
      assert.equal(error.message.includes("secret"), false);
      return true;
    });
    harness.runtime.lastError = {message};
    harness.port.onDisconnect.emit();
    await rejected;
    harness.closed();
  }
});

test("native post failure still removes listeners and disconnects", async () => {
  const harness = nativeHarness();
  harness.port.postMessage = () => { throw new Error("Underlying transport failed at /private/secret"); };
  await assert.rejects(checkNativeHost(harness.runtime, run, harness.timers), (error) => {
    assert.equal(diagnosticMessage(error).includes("secret"), false);
    return true;
  });
  harness.closed();
});
