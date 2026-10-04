// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause
export const LEDGER_KEY = "openarc.probe.resources.v1";
export const REPORT_KEY = "openarc.probe.report.v1";
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;
export class ProbeError extends Error {}
export class Blocked extends ProbeError {}
export function diagnosticMessage(error) {
  return error instanceof ProbeError ? error.message : "Browser API rejected this operation; raw error details are withheld to protect unrelated browsing data.";
}
export function validNativeReply(reply, nonce) {
  return reply !== null && typeof reply === "object" && !Array.isArray(reply) &&
    Object.keys(reply).sort().join(",") === "nonce,protocol,type" &&
    reply.type === "pong" && reply.protocol === 1 && reply.nonce === nonce;
}

export function prefix(runId) {
  return `[OpenArc probe ${runId}]`;
}

export function fixtureURL(base, runId, label) {
  const url = new URL("fixture.html", base);
  url.searchParams.set("run", runId);
  url.searchParams.set("label", label);
  return url.href;
}

export function exampleURL(runId) {
  return `https://example.com/?openarc_probe=${runId}`;
}

export function ownsURL(value, base, runId) {
  if (!UUID.test(runId) || typeof value !== "string") return false;
  if (value === exampleURL(runId)) return true;
  try {
    const url = new URL(value);
    const expected = new URL("fixture.html", base);
    return url.protocol === expected.protocol && url.host === expected.host &&
      url.pathname === expected.pathname && url.searchParams.get("run") === runId &&
      [...url.searchParams.keys()].every((key) => key === "run" || key === "label") && !url.hash;
  } catch {
    return false;
  }
}

export function ownsBookmark(node, record, runId) {
  return node.id === record.id && node.parentId === record.parentId &&
    node.title === record.title && node.title.startsWith(prefix(runId)) &&
    (record.url ? node.url === record.url && record.url === exampleURL(runId) : !node.url);
}

export function bookmarkCleanupOrder(records) {
  const byId = new Map(records.map((record) => [record.id, record]));
  function depth(record) {
    const seen = new Set([record.id]);
    let parent = byId.get(record.parentId);
    while (parent && !seen.has(parent.id)) {
      seen.add(parent.id); parent = byId.get(parent.parentId);
    }
    return seen.size;
  }
  return [...records].sort((left, right) => depth(right) - depth(left));
}

export function validateLedger(value, base) {
  if (!value || value.version !== 1 || !UUID.test(value.runId) ||
      !Array.isArray(value.tabs) || value.tabs.length > 16 ||
      !value.tabs.every((id) => Number.isSafeInteger(id) && id >= 0) ||
      new Set(value.tabs).size !== value.tabs.length ||
      !Array.isArray(value.bookmarks) || value.bookmarks.length > 16 ||
      !value.bookmarks.every((node) => typeof node.id === "string" && node.id.length <= 128 &&
        typeof node.parentId === "string" && typeof node.title === "string" &&
        node.title.startsWith(prefix(value.runId)) &&
        (node.url === undefined || node.url === exampleURL(value.runId))) ||
      (value.menu !== null && value.menu !== `openarc-probe-${value.runId}`) ||
      (value.intent !== null && (!value.intent ||
        (value.intent.kind === "tab"
          ? !base || !ownsURL(value.intent.url, base, value.runId) ||
            (value.intent.title !== null && (typeof value.intent.title !== "string" || !value.intent.title.startsWith(prefix(value.runId))))
          : value.intent.kind !== "bookmark" || typeof value.intent.title !== "string" || !value.intent.title.startsWith(prefix(value.runId)) ||
            (value.intent.parentId !== undefined && typeof value.intent.parentId !== "string") ||
            (value.intent.url !== undefined && value.intent.url !== exampleURL(value.runId)))))) {
    throw new ProbeError("Invalid resource journal; automatic cleanup is disabled. Inspect fixture resources manually.");
  }
  return value;
}
