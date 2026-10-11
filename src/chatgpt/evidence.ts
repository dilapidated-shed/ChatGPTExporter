import { graphFindings, linearRootId, parsePaginatedPage } from "./capture";
import { parseConversationDetail, type ChatGptConversationDetail } from "./envelopes";
import { prettyJson } from "../core/serialization";
import type { JsonObject, JsonValue } from "../core/types";

export interface ConversationCoverage {
  messages: "current_branch" | "returned_provider_graph";
  providerGraph: "unknown";
  versions: "unknown";
}

export interface ValidatedConversationEvidence {
  detail: ChatGptConversationDetail;
  coverage: ConversationCoverage;
}

/** Pure replay: stored complete flags and refreshed hashes cannot replace proof. */
export function validateConversationEvidence(
  value: unknown,
  conversationId: string,
  source: "batch" | "single" | "shared" | "current",
): ValidatedConversationEvidence {
  if (!["batch", "single", "shared", "current"].includes(source)) fail("source");
  const raw = object(value);
  const plural = Object.hasOwn(raw, "__pagination_evidence");
  if (plural && source !== "current") fail("source/evidence agreement");
  // A source-label edit must never disable replay of retained plural evidence.
  if (plural || source === "current") replayPagination(raw, conversationId);
  const detail = parseConversationDetail(raw);
  identity(raw, conversationId, true, source === "shared" && !plural);
  if (graphFindings(detail).length) fail("graph");
  const messages = new Set<string>();
  for (const node of Object.values(detail.mapping)) {
    if (node.message) {
      if (messages.has(node.message.id)) fail("duplicate graph message");
      messages.add(node.message.id);
    }
    if (node.parent !== null && !detail.mapping[node.parent]?.children.includes(node.id)) fail("parent/child agreement");
    if (new Set(node.children).size !== node.children.length) fail("duplicate child");
    for (const child of node.children) if (detail.mapping[child]?.parent !== node.id) fail("child/parent agreement");
  }
  return { detail, coverage: {
    messages: plural ? "current_branch" : "returned_provider_graph",
    providerGraph: "unknown",
    versions: "unknown",
  } };
}

function replayPagination(raw: JsonObject, conversationId: string): void {
  const evidence = object(raw.__pagination_evidence);
  if (evidence.schema_version !== 1 || evidence.complete !== true || !Array.isArray(evidence.pages)
    || evidence.pages.length < 1 || evidence.pages.length > 10_000 || evidence.page_count !== evidence.pages.length) fail("page count");
  const messages = new Map<string, JsonValue>();
  let chronologicalIds: string[] = [];
  let expectedBefore: string | null = null;
  let currentNode: string | null | undefined;
  const cursors = new Set<string>();
  for (const [index, value] of evidence.pages.entries()) {
    const retained = object(value);
    const request = object(retained.request);
    const response = object(retained.response);
    if (request.operation !== (index === 0 ? "conversation_current" : "conversation_messages")
      || request.conversation_id !== conversationId || request.before !== expectedBefore
      || request.num_turns !== 10) fail("page request");
    identity(response, conversationId, index === 0);
    const page = parsePaginatedPage(response, index + 1);
    if (index === 0) currentNode = page.currentNode;
    const freshIds: string[] = [];
    for (const message of page.messages) {
      const previous = messages.get(message.id);
      if (previous !== undefined) {
        if (prettyJson(previous) !== prettyJson(message.raw)) fail("message agreement");
      } else {
        messages.set(message.id, message.raw);
        freshIds.push(message.id);
      }
    }
    chronologicalIds = [...freshIds, ...chronologicalIds];
    if (page.hasPreviousPage) {
      const cursor = page.startCursor;
      if (cursor === null || cursor.length > 2_048 || /[\u0000-\u001f\u007f]/.test(cursor)
        || cursors.has(cursor) || index === evidence.pages.length - 1) fail("cursor/termination");
      cursors.add(cursor);
      expectedBefore = cursor;
    } else if (index !== evidence.pages.length - 1) fail("page after terminal");
  }
  if (!chronologicalIds.length || (currentNode != null && !messages.has(currentNode))) fail("current node");
  const rootId = linearRootId(conversationId, new Set(chronologicalIds));
  const mapping: JsonObject = {
    [rootId]: { id: rootId, message: null, parent: null, children: [chronologicalIds[0]!] },
  };
  chronologicalIds.forEach((id, index) => {
    mapping[id] = { id, message: messages.get(id)!, parent: index ? chronologicalIds[index - 1]! : rootId,
      children: index + 1 < chronologicalIds.length ? [chronologicalIds[index + 1]!] : [] };
  });
  if (raw.current_node !== (currentNode ?? chronologicalIds.at(-1)) || prettyJson(raw.mapping) !== prettyJson(mapping)) fail("reconstructed mapping");
}

function identity(raw: JsonObject, expected: string, required: boolean, shared = false): void {
  const aliases = [raw.id, raw.conversation_id].filter(value => value !== undefined);
  if (required && !aliases.length) fail("missing identity");
  if (aliases.some(value => typeof value !== "string" || !value.length)
    || (aliases.length === 2 && aliases[0] !== aliases[1])
    || (!shared && aliases.some(value => value !== expected))) fail("identity agreement");
}

function object(value: unknown): JsonObject {
  if (!value || typeof value !== "object" || Array.isArray(value)) fail("object shape");
  return value as JsonObject;
}

function fail(boundary: string): never {
  throw new Error(`Invalid conversation evidence: ${boundary}.`);
}
