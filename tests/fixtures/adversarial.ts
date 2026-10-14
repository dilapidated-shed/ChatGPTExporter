import { vi } from "vitest";
import type { JsonValue, InventoryConversation, ConversationInventory } from "../../src/core/types";
import type { DiscoveredWorkspace } from "../../src/chatgpt/client";
import type { ChatGptOperationParameters } from "../../src/chatgpt/endpoints";
import { resolveEndpoint } from "../../src/chatgpt/endpoints";
import { MemoryArchiveFileSystem } from "../../src/core/filesystem";
import { prettyJson } from "../../src/core/serialization";

// Synthetic fixtures only. Deliberately open JSON types allow malformed-provider tests.
export const workspace: DiscoveredWorkspace = { accountId: "synthetic-account", workspaceFingerprint: "a".repeat(32), label: "Synthetic", kind: "personal", deactivated: false };
export const fixedNow = () => new Date("2026-10-07T06:30:00Z");
export function item(id = "conversation-1"): InventoryConversation {
  return { logicalKey: `${workspace.workspaceFingerprint}/${id}`, conversationId: id, title: "Synthetic", createTime: 1, updateTime: 2, memberships: [{ scope: "main" }], listingHashes: ["listing"], listingRecords: [{ id, title: "Synthetic" }] };
}
export function message(id: string, role = "assistant"): any {
  return { id, author: { role }, create_time: 1, update_time: 2, content: { content_type: "text", parts: [`text-${id}`] }, metadata: {}, status: "finished_successfully", end_turn: true, recipient: "all" };
}
export function page(ids: string[], cursor: string | null = null, extra: Record<string, unknown> = {}): any {
  return { conversation_id: "conversation-1", title: "Synthetic", create_time: 1, update_time: 2, current_node: null, messages: ids.map(id => message(id)), page_info: { has_previous_page: cursor !== null, start_cursor: cursor }, ...extra };
}
export function http(status: number) { return Object.assign(new Error(`synthetic HTTP ${status}`), { status, retryable: status === 429 || status === 408 || status >= 500, correlationId: `http-${status}` }); }
export function transport(handler: (op: ChatGptOperationParameters) => JsonValue | Promise<JsonValue>) {
  let count = 0;
  return { request: vi.fn(async (op: ChatGptOperationParameters, _workspace?: string | null, _timeout?: number) => {
    if (op.operation !== "asset_chunk" && op.operation !== "asset_close") resolveEndpoint(op);
    const body = await handler(op);
    count++;
    return { requestId: `request-${count}`, protocolVersion: 1 as const, ok: true as const, status: 200, body, responseBytes: JSON.stringify(body).length, correlationId: `correlation-${count}` };
  }) };
}
export function pagesTransport(pages: any[]) {
  let index = 0;
  return transport(op => {
    if (op.operation === "conversation_batch") throw http(404);
    if (op.operation === "conversation_current" || op.operation === "conversation_messages") {
      const next = pages[index++];
      if (next instanceof Error) throw next;
      if (next === undefined) throw new Error("fixture exhausted");
      return structuredClone(next);
    }
    throw new Error(`unexpected ${op.operation}`);
  });
}
export function history(size: number, width: number, overlap = 0): any[] {
  const ids = Array.from({length: size}, (_, i) => `m-${String(i).padStart(5, "0")}`);
  const pages: any[] = [];
  let end = size;
  while (end > 0) {
    const start = Math.max(0, end - width);
    pages.push(page(ids.slice(start, Math.min(size, end + overlap)), start ? `cursor-${start}` : null, { current_node: ids.at(-1) }));
    end = start;
  }
  return pages;
}
export async function filesystem(items = [item()]) {
  const fs = new MemoryArchiveFileSystem();
  const inventory: ConversationInventory = { schemaVersion: 1, provider: "chatgpt-web", workspaceFingerprint: workspace.workspaceFingerprint, generatedAt: fixedNow().toISOString(), complete: true, chains: [{chainId: "main", scope: "main", complete: true, terminationReason: "empty_page", pageCount: 1, itemCount: items.length, uniqueConversationCount: items.length}], pages: [], projects: [], conversations: items };
  await fs.writeTextAtomic("inventory.json", prettyJson(inventory));
  return fs;
}
