import type { InventoryConversation, JsonValue } from "../core/types";
import type { ChatGptTransport, DiscoveredWorkspace } from "./client";
import { EnvelopeError, parseConversationDetail, type ChatGptConversationDetail } from "./envelopes";

export interface RawBatchCapture {
  requestedConversationIds: string[];
  returnedConversationIds: string[];
  missingConversationIds: string[];
  suspiciousConversationIds: string[];
  response: JsonValue;
  responseBytes: number;
  correlationId: string;
}

export interface RetrievedConversationDetail {
  inventory: InventoryConversation;
  detail: ChatGptConversationDetail;
  raw: JsonValue;
  source: "batch" | "single" | "shared" | "current";
  fallbackReason?: "batch_missing" | "batch_duplicate" | "batch_invalid" | "batch_graph_suspicious" | "batch_http_404" | "current_http_404";
  correlationId: string;
  responseBytes: number;
}

type BatchFallbackReason = NonNullable<RetrievedConversationDetail["fallbackReason"]>;

export interface DetailCaptureResult {
  batches: RawBatchCapture[];
  conversations: RetrievedConversationDetail[];
}

export type DetailCaptureCheckpoint = (result: DetailCaptureResult) => Promise<void>;

export class ChatGptDetailFetcher {
  constructor(
    private readonly transport: ChatGptTransport,
    private readonly workspace: DiscoveredWorkspace,
    private readonly batchSize = 10,
  ) {
    if (!Number.isInteger(batchSize) || batchSize < 1 || batchSize > 10) throw new DetailCaptureError("INVALID_BATCH_SIZE", "Batch size must be 1-10.");
  }

  async fetchAll(inventory: InventoryConversation[], checkpoint?: DetailCaptureCheckpoint): Promise<DetailCaptureResult> {
    const output: RetrievedConversationDetail[] = [];
    const batches: RawBatchCapture[] = [];
    const regular = inventory.filter((conversation) => !shareIdFor(conversation));
    const shared = inventory.filter((conversation) => shareIdFor(conversation));

    for (let offset = 0; offset < regular.length; offset += this.batchSize) {
      const group = regular.slice(offset, offset + this.batchSize);
      const result = await this.fetchBatch(group);
      if (result.batch) batches.push(result.batch);
      output.push(...result.conversations);
      await checkpoint?.({ batches: result.batch ? [result.batch] : [], conversations: result.conversations });
    }
    for (const conversation of shared) {
      const retrieved = await this.fetchShared(conversation, shareIdFor(conversation)!);
      output.push(retrieved);
      await checkpoint?.({ batches: [], conversations: [retrieved] });
    }

    if (output.length !== inventory.length) {
      throw new DetailCaptureError("DETAIL_COUNT_MISMATCH", `Retrieved ${output.length} details for ${inventory.length} inventory records.`);
    }
    const logicalKeys = new Set(output.map((item) => item.inventory.logicalKey));
    if (logicalKeys.size !== inventory.length) throw new DetailCaptureError("DETAIL_DUPLICATE", "Detail retrieval produced duplicate logical conversations.");
    return { batches, conversations: output };
  }

  private async fetchBatch(group: InventoryConversation[]): Promise<{ batch: RawBatchCapture | null; conversations: RetrievedConversationDetail[] }> {
    const requestedIds = group.map((conversation) => conversation.conversationId);
    let response: Awaited<ReturnType<ChatGptTransport["request"]>>;
    try {
      response = await this.transport.request({
        operation: "conversation_batch",
        parameters: { conversationIds: requestedIds },
      }, this.workspace.accountId, 120_000);
    } catch (error) {
      if (!isHttpStatus(error, 404)) throw error;
      const conversations: RetrievedConversationDetail[] = [];
      for (const conversation of group) conversations.push(await this.fetchCurrent(conversation, "batch_http_404"));
      return { batch: null, conversations };
    }
    const candidates = batchCandidates(response.body);
    const candidateById = new Map<string, { raw: JsonValue; parsed?: ChatGptConversationDetail; issue?: BatchFallbackReason }>();
    const duplicateIds = new Set<string>();
    for (const raw of candidates) {
      let id: string | null = null;
      let parsed: ChatGptConversationDetail | undefined;
      let issue: BatchFallbackReason | undefined;
      try {
        parsed = parseConversationDetail(raw);
        id = parsed.id ?? parsed.conversation_id ?? null;
        if (!id || !requestedIds.includes(id)) continue;
        if (!hasOnlySafeCompactNullRoot(raw) || graphFindings(parsed).length) issue = "batch_graph_suspicious";
      } catch (error) {
        if (!(error instanceof EnvelopeError)) throw error;
        id = looseConversationId(raw);
        issue = "batch_invalid";
      }
      if (!id || !requestedIds.includes(id)) continue;
      if (candidateById.has(id)) {
        duplicateIds.add(id);
        candidateById.set(id, { raw, issue: "batch_duplicate" });
      } else {
        candidateById.set(id, { raw, ...(parsed === undefined ? {} : { parsed }), ...(issue === undefined ? {} : { issue }) });
      }
    }

    const returnedIds = requestedIds.filter((id) => candidateById.has(id));
    const missingIds = requestedIds.filter((id) => !candidateById.has(id));
    const suspiciousIds = requestedIds.filter((id) => candidateById.get(id)?.issue !== undefined);
    const conversations: RetrievedConversationDetail[] = [];
    for (const conversation of group) {
      const candidate = candidateById.get(conversation.conversationId);
      if (!candidate || candidate.issue || !candidate.parsed) {
        conversations.push(await this.fetchSingle(conversation, candidate?.issue ?? "batch_missing"));
      } else {
        conversations.push({
          inventory: conversation,
          detail: candidate.parsed,
          raw: candidate.raw,
          source: "batch",
          correlationId: response.correlationId,
          responseBytes: response.responseBytes,
        });
      }
    }
    return {
      batch: {
        requestedConversationIds: requestedIds,
        returnedConversationIds: returnedIds,
        missingConversationIds: missingIds,
        suspiciousConversationIds: [...new Set([...suspiciousIds, ...duplicateIds])],
        response: response.body,
        responseBytes: response.responseBytes,
        correlationId: response.correlationId,
      },
      conversations,
    };
  }

  private async fetchCurrent(conversation: InventoryConversation, fallbackReason: BatchFallbackReason): Promise<RetrievedConversationDetail> {
    const numTurns = 10;
    let response: Awaited<ReturnType<ChatGptTransport["request"]>>;
    try {
      response = await this.transport.request({
        operation: "conversation_current",
        parameters: { conversationId: conversation.conversationId, numTurns },
      }, this.workspace.accountId, 120_000);
    } catch (error) {
      if (!isHttpStatus(error, 404)) throw error;
      return this.fetchSingle(conversation, "current_http_404");
    }

    const initialResponse = response;
    const initial = jsonObject(initialResponse.body, "paginated conversation initial response");
    const pages: JsonValue[] = [];
    const messages = new Map<string, JsonValue>();
    let chronologicalIds: string[] = [];
    const seenCursors = new Set<string>();
    let currentNode: string | null | undefined;
    let responseBytes = 0;
    let before: string | null = null;
    let complete = false;

    for (let pageNumber = 1; pageNumber <= 10_000; pageNumber += 1) {
      const page = parsePaginatedPage(response.body, pageNumber);
      assertPaginatedIdentity(conversation.conversationId, page.conversationId, pageNumber === 1);
      if (pageNumber === 1) currentNode = page.currentNode;
      pages.push({
        request: {
          operation: before === null ? "conversation_current" : "conversation_messages",
          conversation_id: conversation.conversationId,
          before,
          num_turns: numTurns,
        },
        response: response.body,
        response_bytes: response.responseBytes,
        correlation_id: response.correlationId,
      });
      responseBytes += response.responseBytes;

      const freshIds: string[] = [];
      for (const message of page.messages) {
        const existing = messages.get(message.id);
        if (existing !== undefined) {
          if (canonicalJson(existing) !== canonicalJson(message.raw)) {
            throw new DetailCaptureError("PAGINATION_MESSAGE_CONFLICT", `Message ${message.id} changed across pagination pages for ${conversation.conversationId}.`);
          }
          continue;
        }
        messages.set(message.id, message.raw);
        freshIds.push(message.id);
      }
      chronologicalIds = [...freshIds, ...chronologicalIds];

      if (!page.hasPreviousPage) {
        complete = true;
        break;
      }
      const cursor = page.startCursor;
      if (cursor === null) {
        throw new DetailCaptureError("PAGINATION_CURSOR_MISSING", `Conversation ${conversation.conversationId} reported older messages without a start cursor.`);
      }
      if (seenCursors.has(cursor)) {
        throw new DetailCaptureError("PAGINATION_CURSOR_REPEATED", `Conversation ${conversation.conversationId} repeated pagination cursor ${cursor}.`);
      }
      seenCursors.add(cursor);
      before = cursor;
      response = await this.transport.request({
        operation: "conversation_messages",
        parameters: { conversationId: conversation.conversationId, before: cursor, numTurns },
      }, this.workspace.accountId, 120_000);
    }

    if (!complete) {
      throw new DetailCaptureError("PAGINATION_PAGE_LIMIT", `Conversation ${conversation.conversationId} exceeded the pagination page limit.`);
    }
    if (chronologicalIds.length === 0) {
      throw new DetailCaptureError("PAGINATION_EMPTY", `Conversation ${conversation.conversationId} returned no messages.`);
    }

    if (currentNode !== undefined && currentNode !== null && !messages.has(currentNode)) {
      throw new DetailCaptureError("PAGINATION_CURRENT_NODE_MISSING", `Conversation ${conversation.conversationId} current node ${currentNode} was absent from the complete paginated branch.`);
    }
    const resolvedCurrentNode = currentNode ?? chronologicalIds.at(-1) ?? null;
    const rootId = linearRootId(conversation.conversationId, new Set(chronologicalIds));
    const mapping: Record<string, JsonValue> = {
      [rootId]: {
        id: rootId,
        message: null,
        parent: null,
        children: chronologicalIds.length ? [chronologicalIds[0]!] : [],
      },
    };
    for (let index = 0; index < chronologicalIds.length; index += 1) {
      const id = chronologicalIds[index]!;
      mapping[id] = {
        id,
        message: messages.get(id)!,
        parent: index === 0 ? rootId : chronologicalIds[index - 1]!,
        children: index + 1 < chronologicalIds.length ? [chronologicalIds[index + 1]!] : [],
      };
    }

    const {
      messages: _messages,
      page_info: _pageInfo,
      mapping: _mapping,
      ...metadata
    } = initial;
    const raw: JsonValue = {
      ...metadata,
      conversation_id: conversation.conversationId,
      title: Object.hasOwn(metadata, "title") ? metadata.title : conversation.title,
      create_time: Object.hasOwn(metadata, "create_time") ? metadata.create_time : conversation.createTime,
      update_time: Object.hasOwn(metadata, "update_time") ? metadata.update_time : conversation.updateTime,
      current_node: resolvedCurrentNode,
      mapping,
      __pagination_evidence: {
        schema_version: 1,
        complete: true,
        page_count: pages.length,
        pages,
      },
    };
    const detail = parseConversationDetail(raw);
    const findings = graphFindings(detail);
    if (findings.length) {
      throw new DetailCaptureError("PAGINATED_GRAPH_INVALID", `Paginated conversation ${conversation.conversationId} is invalid: ${findings.join(", ")}`);
    }
    return {
      inventory: conversation,
      detail,
      raw,
      source: "current",
      fallbackReason,
      correlationId: initialResponse.correlationId,
      responseBytes,
    };
  }

  private async fetchSingle(conversation: InventoryConversation, fallbackReason: BatchFallbackReason): Promise<RetrievedConversationDetail> {
    const response = await this.transport.request({
      operation: "conversation_detail",
      parameters: { conversationId: conversation.conversationId },
    }, this.workspace.accountId, 120_000);
    const detail = parseConversationDetail(response.body);
    assertRequestedIdentity(conversation.conversationId, detail);
    const findings = graphFindings(detail);
    if (findings.length) throw new DetailCaptureError("SINGLE_GRAPH_INVALID", `Single-conversation detail for ${conversation.conversationId} is invalid: ${findings.join(", ")}`);
    return {
      inventory: conversation,
      detail,
      raw: response.body,
      source: "single",
      fallbackReason,
      correlationId: response.correlationId,
      responseBytes: response.responseBytes,
    };
  }

  private async fetchShared(conversation: InventoryConversation, shareId: string): Promise<RetrievedConversationDetail> {
    const response = await this.transport.request({ operation: "shared_detail", parameters: { shareId } }, this.workspace.accountId, 120_000);
    const detail = parseConversationDetail(response.body);
    const findings = graphFindings(detail);
    if (findings.length) throw new DetailCaptureError("SHARED_GRAPH_INVALID", `Shared detail ${shareId} is invalid: ${findings.join(", ")}`);
    return {
      inventory: conversation,
      detail,
      raw: response.body,
      source: "shared",
      correlationId: response.correlationId,
      responseBytes: response.responseBytes,
    };
  }
}

export function graphFindings(detail: ChatGptConversationDetail): string[] {
  const findings: string[] = [];
  const nodes = detail.mapping;
  const ids = new Set(Object.keys(nodes));
  if (ids.size === 0) findings.push("mapping_empty");
  if (detail.current_node && !ids.has(detail.current_node)) findings.push("current_node_missing");
  for (const [id, node] of Object.entries(nodes)) {
    if (node.parent !== null && !ids.has(node.parent)) findings.push(`parent_missing:${id}`);
    for (const child of node.children) if (!ids.has(child)) findings.push(`child_missing:${id}`);
  }
  for (const start of ids) {
    const seen = new Set<string>();
    let current: string | null = start;
    while (current !== null) {
      if (seen.has(current)) {
        findings.push(`cycle:${start}`);
        break;
      }
      seen.add(current);
      current = nodes[current]?.parent ?? null;
    }
  }
  return [...new Set(findings)].sort();
}

export class DetailCaptureError extends Error {
  constructor(readonly code: string, message: string) {
    super(message);
    this.name = "DetailCaptureError";
  }
}

function batchCandidates(value: JsonValue): JsonValue[] {
  if (Array.isArray(value)) return value;
  if (!value || typeof value !== "object") throw new DetailCaptureError("INVALID_BATCH_ENVELOPE", "Batch response must be an array or object.");
  const object = value as Record<string, JsonValue>;
  if (Array.isArray(object.items)) return object.items;
  if (Array.isArray(object.conversations)) return object.conversations;
  return Object.values(object);
}

function hasOnlySafeCompactNullRoot(value: JsonValue): boolean {
  if (!value || typeof value !== "object" || Array.isArray(value)) return false;
  const conversation = value as Record<string, JsonValue>;
  if (!conversation.mapping || typeof conversation.mapping !== "object" || Array.isArray(conversation.mapping)) return false;
  const mapping = conversation.mapping as Record<string, JsonValue>;
  const placeholders = Object.entries(mapping).filter(([, rawNode]) => {
    if (!rawNode || typeof rawNode !== "object" || Array.isArray(rawNode)) return false;
    const node = rawNode as Record<string, JsonValue>;
    return node.parent === undefined && node.message === undefined;
  });
  if (placeholders.length === 0) return true;
  if (placeholders.length !== 1) return false;
  const [placeholderId, rawPlaceholder] = placeholders[0]!;
  const placeholder = rawPlaceholder as Record<string, JsonValue>;
  if (!Object.keys(placeholder).every((key) => key === "id" || key === "children")) return false;
  if (conversation.current_node === placeholderId) return false;
  for (const [nodeId, rawNode] of Object.entries(mapping)) {
    if (nodeId === placeholderId || !rawNode || typeof rawNode !== "object" || Array.isArray(rawNode)) continue;
    const children = (rawNode as Record<string, JsonValue>).children;
    if (Array.isArray(children) && children.includes(placeholderId)) return false;
  }
  return true;
}

interface ParsedPaginatedPage {
  conversationId: string | null;
  currentNode: string | null | undefined;
  messages: Array<{ id: string; raw: JsonValue }>;
  hasPreviousPage: boolean;
  startCursor: string | null;
}

function parsePaginatedPage(value: JsonValue, pageNumber: number): ParsedPaginatedPage {
  const object = jsonObject(value, `paginated conversation page ${pageNumber}`);
  if (!Array.isArray(object.messages)) {
    throw new DetailCaptureError("PAGINATION_INVALID_PAGE", `Paginated conversation page ${pageNumber} messages must be an array.`);
  }
  const messages = object.messages.map((raw, index) => {
    const message = jsonObject(raw, `paginated conversation page ${pageNumber} message ${index}`);
    const id = message.id;
    if (typeof id !== "string" || !/^[A-Za-z0-9_-]{1,256}$/.test(id)) {
      throw new DetailCaptureError("PAGINATION_INVALID_MESSAGE_ID", `Paginated conversation page ${pageNumber} message ${index} has an invalid id.`);
    }
    return { id, raw };
  });
  if (new Set(messages.map((message) => message.id)).size !== messages.length) {
    throw new DetailCaptureError("PAGINATION_DUPLICATE_MESSAGE", `Paginated conversation page ${pageNumber} repeats a message id.`);
  }

  const pageInfo = jsonObject(object.page_info, `paginated conversation page ${pageNumber}.page_info`);
  if (typeof pageInfo.has_previous_page !== "boolean") {
    throw new DetailCaptureError("PAGINATION_INVALID_PAGE_INFO", `Paginated conversation page ${pageNumber} has_previous_page must be boolean.`);
  }
  let startCursor: string | null = null;
  if (pageInfo.start_cursor !== undefined && pageInfo.start_cursor !== null) {
    if (typeof pageInfo.start_cursor !== "string" || pageInfo.start_cursor.length === 0) {
      throw new DetailCaptureError("PAGINATION_INVALID_CURSOR", `Paginated conversation page ${pageNumber} start_cursor is invalid.`);
    }
    startCursor = pageInfo.start_cursor;
  }

  const identity = object.conversation_id ?? object.id;
  if (identity !== undefined && (typeof identity !== "string" || identity.length === 0)) {
    throw new DetailCaptureError("PAGINATION_INVALID_IDENTITY", `Paginated conversation page ${pageNumber} conversation identity is invalid.`);
  }
  const currentNode = object.current_node;
  if (currentNode !== undefined && currentNode !== null && (typeof currentNode !== "string" || currentNode.length === 0)) {
    throw new DetailCaptureError("PAGINATION_INVALID_CURRENT_NODE", `Paginated conversation page ${pageNumber} current_node is invalid.`);
  }
  return {
    conversationId: typeof identity === "string" ? identity : null,
    currentNode: currentNode as string | null | undefined,
    messages,
    hasPreviousPage: pageInfo.has_previous_page,
    startCursor,
  };
}

function assertPaginatedIdentity(requestedId: string, returnedId: string | null, required: boolean): void {
  if (returnedId === null) {
    if (required) throw new DetailCaptureError("PAGINATION_IDENTITY_MISSING", `Initial paginated response for ${requestedId} omitted conversation identity.`);
    return;
  }
  if (returnedId !== requestedId) {
    throw new DetailCaptureError("DETAIL_ID_MISMATCH", `Requested ${requestedId} but ChatGPT returned ${returnedId}.`);
  }
}

function jsonObject(value: JsonValue | undefined, name: string): Record<string, JsonValue> {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new DetailCaptureError("PAGINATION_INVALID_PAGE", `${name} must be an object.`);
  }
  return value;
}

function canonicalJson(value: JsonValue): string {
  if (value === null || typeof value !== "object") return JSON.stringify(value);
  if (Array.isArray(value)) return `[${value.map(canonicalJson).join(",")}]`;
  return `{${Object.keys(value).sort().map((key) => `${JSON.stringify(key)}:${canonicalJson(value[key]!)}`).join(",")}}`;
}

function linearRootId(conversationId: string, messageIds: Set<string>): string {
  const base = `paginated_root_${conversationId}`.slice(0, 240);
  let candidate = base;
  let suffix = 0;
  while (messageIds.has(candidate)) {
    suffix += 1;
    candidate = `${base.slice(0, 230)}_${suffix}`;
  }
  return candidate;
}

function isHttpStatus(error: unknown, status: number): boolean {
  return Boolean(error && typeof error === "object" && "status" in error && (error as { status?: unknown }).status === status);
}

function looseConversationId(value: JsonValue): string | null {
  if (!value || typeof value !== "object" || Array.isArray(value)) return null;
  const object = value as Record<string, JsonValue>;
  for (const candidate of [object.id, object.conversation_id]) {
    if (typeof candidate === "string" && /^[A-Za-z0-9_-]{1,256}$/.test(candidate)) return candidate;
  }
  return null;
}

function assertRequestedIdentity(requestedId: string, detail: ChatGptConversationDetail): void {
  const returnedId = detail.id ?? detail.conversation_id;
  if (returnedId !== requestedId) throw new DetailCaptureError("DETAIL_ID_MISMATCH", `Requested ${requestedId} but ChatGPT returned ${returnedId ?? "no id"}.`);
}

function shareIdFor(conversation: InventoryConversation): string | null {
  if (!conversation.conversationId.startsWith("share_")) return null;
  return conversation.memberships.find((membership) => membership.scope === "shared")?.shareId ?? null;
}
