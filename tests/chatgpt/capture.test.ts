import { describe, expect, it, vi } from "vitest";

import type { InventoryConversation, JsonValue } from "../../src/core/types";
import { ChatGptDetailFetcher, DetailCaptureError } from "../../src/chatgpt/capture";
import type { ChatGptTransport, DiscoveredWorkspace } from "../../src/chatgpt/client";
import type { ChatGptOperationParameters } from "../../src/chatgpt/endpoints";
import { BRIDGE_PROTOCOL_VERSION, type ApiSuccessResponse } from "../../src/extension/protocol";
import { conversationDetail } from "../fixtures/chatgpt";

const workspace: DiscoveredWorkspace = {
  accountId: "account-1",
  workspaceFingerprint: "a".repeat(32),
  label: "Synthetic",
  kind: "personal",
  deactivated: false,
};

describe("ChatGPT batch-first detail retrieval", () => {
  it("uses complete batch records without single-detail requests", async () => {
    const transport = transportFor((operation) => {
      if (operation.operation === "conversation_batch") return [conversationDetail() as unknown as JsonValue];
      throw new Error(`unexpected ${operation.operation}`);
    });
    const result = await new ChatGptDetailFetcher(transport, workspace).fetchAll([inventoryItem()]);
    expect(result.conversations[0]).toMatchObject({ source: "batch" });
    expect(result.batches[0]).toMatchObject({ missingConversationIds: [], suspiciousConversationIds: [] });
    expect(transport.request).toHaveBeenCalledTimes(1);
  });

  it("accepts only the live compact null-root batch variant", async () => {
    const compact = conversationDetail() as unknown as { mapping: Record<string, Record<string, JsonValue>> };
    delete compact.mapping["root-1"]!.parent;
    delete compact.mapping["root-1"]!.message;
    const transport = transportFor((operation) => {
      if (operation.operation === "conversation_batch") return [compact as unknown as JsonValue];
      throw new Error(`unexpected ${operation.operation}`);
    });
    const result = await new ChatGptDetailFetcher(transport, workspace).fetchAll([inventoryItem()]);
    expect(result.conversations[0]).toMatchObject({ source: "batch", detail: { mapping: { "root-1": { parent: null, message: null } } } });
    expect(transport.request).toHaveBeenCalledTimes(1);
  });

  it("keeps compact live batches with ISO and omitted timestamps on the batch path", async () => {
    const compact = conversationDetail() as unknown as {
      create_time: JsonValue;
      update_time: JsonValue;
      mapping: Record<string, Record<string, JsonValue>>;
    };
    compact.create_time = "2026-08-01T12:00:00.000Z";
    compact.update_time = "2026-08-01T12:00:01.000Z";
    delete compact.mapping["root-1"]!.parent;
    delete compact.mapping["root-1"]!.message;
    delete (compact.mapping["user-1"]!.message as Record<string, JsonValue>).create_time;
    const transport = transportFor((operation) => {
      if (operation.operation === "conversation_batch") return [compact as unknown as JsonValue];
      throw new Error(`unexpected ${operation.operation}`);
    });

    const result = await new ChatGptDetailFetcher(transport, workspace).fetchAll([inventoryItem()]);

    expect(result.conversations[0]).toMatchObject({
      source: "batch",
      detail: {
        create_time: 1_785_585_600,
        mapping: { "user-1": { message: { create_time: null } } },
      },
    });
    expect(transport.request).toHaveBeenCalledTimes(1);
  });

  it("falls back when a compact placeholder is not a detached root", async () => {
    const compactNonRoot = conversationDetail() as unknown as { mapping: Record<string, Record<string, JsonValue>> };
    delete compactNonRoot.mapping["user-1"]!.parent;
    delete compactNonRoot.mapping["user-1"]!.message;
    const transport = transportFor((operation) => {
      if (operation.operation === "conversation_batch") return [compactNonRoot as unknown as JsonValue];
      if (operation.operation === "conversation_detail") return conversationDetail() as unknown as JsonValue;
      throw new Error(`unexpected ${operation.operation}`);
    });
    const result = await new ChatGptDetailFetcher(transport, workspace).fetchAll([inventoryItem()]);
    expect(result.conversations[0]).toMatchObject({ source: "single", fallbackReason: "batch_graph_suspicious" });
    expect(transport.request).toHaveBeenCalledTimes(2);
  });

  it("falls back individually for omitted, malformed, duplicate, and graph-suspicious batch records", async () => {
    const items = ["conversation-1", "conversation-2", "conversation-3", "conversation-4"].map(inventoryItem);
    const transport = transportFor((operation) => {
      if (operation.operation === "conversation_batch") {
        const malformed = { ...conversationDetail({ id: "conversation-2" }), mapping: [] } as unknown as JsonValue;
        const duplicate = conversationDetail({ id: "conversation-3" }) as unknown as JsonValue;
        const suspicious = conversationDetail({ id: "conversation-4", current_node: "missing-node" }) as unknown as JsonValue;
        return [malformed, duplicate, duplicate, suspicious];
      }
      if (operation.operation === "conversation_detail") return conversationDetail({ id: operation.parameters.conversationId }) as unknown as JsonValue;
      throw new Error(`unexpected ${operation.operation}`);
    });
    const result = await new ChatGptDetailFetcher(transport, workspace).fetchAll(items);
    expect(result.conversations.map((item) => item.source)).toEqual(["single", "single", "single", "single"]);
    expect(result.conversations.map((item) => item.fallbackReason)).toEqual([
      "batch_missing",
      "batch_invalid",
      "batch_duplicate",
      "batch_graph_suspicious",
    ]);
    expect(transport.request).toHaveBeenCalledTimes(5);
  });

  it("uses the share adapter for share-only inventory records", async () => {
    const shared = inventoryItem("share_share-1");
    shared.memberships = [{ scope: "shared", shareId: "share-1" }];
    const transport = transportFor((operation) => {
      if (operation.operation === "shared_detail") return conversationDetail({ id: "shared-provider-record" }) as unknown as JsonValue;
      throw new Error(`unexpected ${operation.operation}`);
    });
    const result = await new ChatGptDetailFetcher(transport, workspace).fetchAll([shared]);
    expect(result.conversations[0]?.source).toBe("shared");
  });

  it("refuses an invalid single-detail fallback instead of accepting partial capture", async () => {
    const transport = transportFor((operation) => {
      if (operation.operation === "conversation_batch") return [];
      if (operation.operation === "conversation_detail") return conversationDetail({ current_node: "missing" }) as unknown as JsonValue;
      throw new Error(`unexpected ${operation.operation}`);
    });
    await expect(new ChatGptDetailFetcher(transport, workspace).fetchAll([inventoryItem()])).rejects.toBeInstanceOf(DetailCaptureError);
  });

  it("captures a complete three-page current branch and reconciles stable overlap messages", async () => {
    const transport = transportFor((operation) => {
      if (operation.operation === "conversation_batch") throw httpFailure(404);
      if (operation.operation === "conversation_current") {
        return paginatedPage({
          messages: [paginatedMessage("message-3", "user"), paginatedMessage("message-4", "assistant")],
          hasPreviousPage: true,
          startCursor: "cursor-2",
          currentNode: "message-4",
        });
      }
      if (operation.operation === "conversation_messages" && operation.parameters.before === "cursor-2") {
        return paginatedPage({
          messages: [paginatedMessage("message-2", "assistant"), paginatedMessage("message-3", "user")],
          hasPreviousPage: true,
          startCursor: "cursor-1",
          includeIdentity: false,
        });
      }
      if (operation.operation === "conversation_messages" && operation.parameters.before === "cursor-1") {
        return paginatedPage({
          messages: [paginatedMessage("message-1", "user"), paginatedMessage("message-2", "assistant")],
          hasPreviousPage: false,
          startCursor: null,
          includeIdentity: false,
        });
      }
      throw new Error(`unexpected ${operation.operation}`);
    });

    const result = await new ChatGptDetailFetcher(transport, workspace).fetchAll([inventoryItem()]);
    const retrieved = result.conversations[0]!;
    expect(retrieved).toMatchObject({ source: "current", fallbackReason: "batch_http_404" });
    expect(retrieved.detail.current_node).toBe("message-4");
    expect(Object.keys(retrieved.detail.mapping)).toHaveLength(5);
    expect(retrieved.detail.mapping["paginated_root_conversation-1"]).toMatchObject({
      parent: null,
      children: ["message-1"],
      message: null,
    });
    expect(retrieved.detail.mapping["message-1"]?.children).toEqual(["message-2"]);
    expect(retrieved.detail.mapping["message-2"]?.children).toEqual(["message-3"]);
    expect(retrieved.detail.mapping["message-3"]?.children).toEqual(["message-4"]);
    expect(retrieved.detail.mapping["message-4"]?.children).toEqual([]);
    expect(Object.values(retrieved.detail.mapping).every((node) => node.children.length <= 1)).toBe(true);

    const raw = retrieved.raw as Record<string, JsonValue>;
    const evidence = raw.__pagination_evidence as Record<string, JsonValue>;
    expect(evidence).toMatchObject({ schema_version: 1, complete: true, page_count: 3 });
    expect(evidence.pages).toHaveLength(3);
    expect(transport.request.mock.calls.map(([operation]) => operation.operation)).toEqual([
      "conversation_batch",
      "conversation_current",
      "conversation_messages",
      "conversation_messages",
    ]);
  });

  it("rejects a paginated response that advertises older history without a cursor", async () => {
    const transport = transportFor((operation) => {
      if (operation.operation === "conversation_batch") throw httpFailure(404);
      if (operation.operation === "conversation_current") {
        return paginatedPage({
          messages: [paginatedMessage("message-1", "user")],
          hasPreviousPage: true,
          startCursor: null,
          currentNode: "message-1",
        });
      }
      throw new Error(`unexpected ${operation.operation}`);
    });

    await expect(new ChatGptDetailFetcher(transport, workspace).fetchAll([inventoryItem()]))
      .rejects.toMatchObject({ code: "PAGINATION_CURSOR_MISSING" });
  });

  it("rejects a repeated history cursor instead of silently truncating", async () => {
    const transport = transportFor((operation) => {
      if (operation.operation === "conversation_batch") throw httpFailure(404);
      if (operation.operation === "conversation_current") {
        return paginatedPage({
          messages: [paginatedMessage("message-3", "user"), paginatedMessage("message-4", "assistant")],
          hasPreviousPage: true,
          startCursor: "cursor-repeat",
          currentNode: "message-4",
        });
      }
      if (operation.operation === "conversation_messages") {
        return paginatedPage({
          messages: [paginatedMessage("message-1", "user"), paginatedMessage("message-2", "assistant")],
          hasPreviousPage: true,
          startCursor: "cursor-repeat",
          includeIdentity: false,
        });
      }
      throw new Error(`unexpected ${operation.operation}`);
    });

    await expect(new ChatGptDetailFetcher(transport, workspace).fetchAll([inventoryItem()]))
      .rejects.toMatchObject({ code: "PAGINATION_CURSOR_REPEATED" });
  });

  it("rejects conflicting overlap messages at a pagination boundary", async () => {
    const transport = transportFor((operation) => {
      if (operation.operation === "conversation_batch") throw httpFailure(404);
      if (operation.operation === "conversation_current") {
        return paginatedPage({
          messages: [paginatedMessage("message-2", "assistant"), paginatedMessage("message-3", "assistant")],
          hasPreviousPage: true,
          startCursor: "cursor-1",
          currentNode: "message-3",
        });
      }
      if (operation.operation === "conversation_messages") {
        const changed = paginatedMessage("message-2", "assistant") as Record<string, JsonValue>;
        changed.content = { content_type: "text", parts: ["changed overlap"] };
        return paginatedPage({
          messages: [paginatedMessage("message-1", "user"), changed],
          hasPreviousPage: false,
          startCursor: null,
          includeIdentity: false,
        });
      }
      throw new Error(`unexpected ${operation.operation}`);
    });

    await expect(new ChatGptDetailFetcher(transport, workspace).fetchAll([inventoryItem()]))
      .rejects.toMatchObject({ code: "PAGINATION_MESSAGE_CONFLICT" });
  });

  it("rejects identity drift from the current plural endpoint", async () => {
    const transport = transportFor((operation) => {
      if (operation.operation === "conversation_batch") throw httpFailure(404);
      if (operation.operation === "conversation_current") {
        return paginatedPage({
          conversationId: "different-conversation",
          messages: [paginatedMessage("message-1", "assistant")],
          hasPreviousPage: false,
          startCursor: null,
          currentNode: "message-1",
        });
      }
      throw new Error(`unexpected ${operation.operation}`);
    });

    await expect(new ChatGptDetailFetcher(transport, workspace).fetchAll([inventoryItem()]))
      .rejects.toMatchObject({ code: "DETAIL_ID_MISMATCH" });
  });

  it("does not downgrade a non-404 current-endpoint failure to legacy detail", async () => {
    const currentFailure = httpFailure(500);
    const transport = transportFor((operation) => {
      if (operation.operation === "conversation_batch") throw httpFailure(404);
      if (operation.operation === "conversation_current") throw currentFailure;
      if (operation.operation === "conversation_detail") throw new Error("legacy detail must not be requested");
      throw new Error(`unexpected ${operation.operation}`);
    });

    await expect(new ChatGptDetailFetcher(transport, workspace).fetchAll([inventoryItem()])).rejects.toBe(currentFailure);
    expect(transport.request.mock.calls.map(([operation]) => operation.operation)).toEqual([
      "conversation_batch",
      "conversation_current",
    ]);
  });

  it("uses legacy singular detail only when the current plural endpoint also returns 404", async () => {
    const transport = transportFor((operation) => {
      if (operation.operation === "conversation_batch") throw httpFailure(404);
      if (operation.operation === "conversation_current") throw httpFailure(404);
      if (operation.operation === "conversation_detail") return conversationDetail() as unknown as JsonValue;
      throw new Error(`unexpected ${operation.operation}`);
    });

    const result = await new ChatGptDetailFetcher(transport, workspace).fetchAll([inventoryItem()]);
    expect(result.conversations[0]).toMatchObject({ source: "single", fallbackReason: "current_http_404" });
    expect(transport.request.mock.calls.map(([operation]) => operation.operation)).toEqual([
      "conversation_batch",
      "conversation_current",
      "conversation_detail",
    ]);
  });

  it("checkpoints each completed batch before requesting the next batch", async () => {
    const transient = Object.assign(new Error("synthetic throttle"), { retryable: true });
    const transport = transportFor((operation) => {
      if (operation.operation !== "conversation_batch") throw new Error(`unexpected ${operation.operation}`);
      const id = operation.parameters.conversationIds[0]!;
      if (id === "conversation-2") throw transient;
      return [conversationDetail({ id }) as unknown as JsonValue];
    });
    const checkpoints: string[][] = [];
    const capture = new ChatGptDetailFetcher(transport, workspace, 1).fetchAll(
      [inventoryItem("conversation-1"), inventoryItem("conversation-2")],
      async (checkpoint) => { checkpoints.push(checkpoint.conversations.map((item) => item.inventory.conversationId)); },
    );

    await expect(capture).rejects.toBe(transient);
    expect(checkpoints).toEqual([["conversation-1"]]);
  });
});

function paginatedMessage(id: string, role: "user" | "assistant"): JsonValue {
  return {
    id,
    author: { role },
    create_time: 1,
    content: { content_type: "text", parts: [`content for ${id}`] },
    status: "finished_successfully",
    end_turn: role === "assistant",
    recipient: "all",
    metadata: {},
  };
}

function paginatedPage(options: {
  messages: JsonValue[];
  hasPreviousPage: boolean;
  startCursor: string | null;
  currentNode?: string | null;
  conversationId?: string;
  includeIdentity?: boolean;
}): JsonValue {
  const conversationId = options.conversationId ?? "conversation-1";
  return {
    ...(options.includeIdentity === false ? {} : { conversation_id: conversationId }),
    title: "Synthetic",
    create_time: 1,
    update_time: 2,
    current_node: options.currentNode ?? null,
    messages: options.messages,
    page_info: {
      has_previous_page: options.hasPreviousPage,
      start_cursor: options.startCursor,
    },
  };
}

function httpFailure(status: number): Error & { status: number; retryable: boolean; correlationId: string } {
  return Object.assign(new Error(`synthetic HTTP ${status}`), {
    status,
    retryable: status >= 500,
    correlationId: `http-${status}`,
  });
}

function inventoryItem(id = "conversation-1"): InventoryConversation {
  return {
    logicalKey: `${workspace.workspaceFingerprint}/${id}`,
    conversationId: id,
    title: "Synthetic",
    createTime: 1,
    updateTime: 2,
    memberships: [{ scope: "main" }],
    listingHashes: ["listing-hash"],
  };
}

function transportFor(handler: (operation: ChatGptOperationParameters) => JsonValue): ChatGptTransport & { request: ReturnType<typeof vi.fn> } {
  const request = vi.fn(async (operation: ChatGptOperationParameters): Promise<ApiSuccessResponse> => {
    const body = handler(operation);
    return {
      requestId: "request-1",
      protocolVersion: BRIDGE_PROTOCOL_VERSION,
      ok: true,
      status: 200,
      body,
      responseBytes: JSON.stringify(body).length,
      correlationId: "correlation-1",
    };
  });
  return { request } as ChatGptTransport & { request: ReturnType<typeof vi.fn> };
}
