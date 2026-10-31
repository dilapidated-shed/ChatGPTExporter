import { describe, expect, it, vi } from "vitest";
import { ChatGptCaptureEngine } from "../../src/chatgpt/capture-engine";
import { auditArchive } from "../../src/chatgpt/audit";
import { verifyPhysicalAsset } from "../../src/chatgpt/assets";
import type { ChatGptTransport, DiscoveredWorkspace } from "../../src/chatgpt/client";
import type { ChatGptOperationParameters } from "../../src/chatgpt/endpoints";
import { MemoryArchiveFileSystem } from "../../src/core/filesystem";
import { prettyJson } from "../../src/core/serialization";
import type { AssetRecord, ConversationInventory, InventoryProject, JsonValue } from "../../src/core/types";
import { BRIDGE_PROTOCOL_VERSION, type ApiSuccessResponse } from "../../src/extension/protocol";
import { conversationDetail } from "../fixtures/chatgpt";
import { filesystem as historyFilesystem, history, pagesTransport } from "../fixtures/adversarial";

const workspace: DiscoveredWorkspace = {
  accountId: "synthetic-account", workspaceFingerprint: "a".repeat(32),
  label: "synthetic", kind: "personal", deactivated: false,
};
const text = (value: string) => new TextEncoder().encode(value);
const fileData: Record<string, Uint8Array> = {
  "file-one": text("First synthetic physical document"),
  "file-two": text("Second synthetic physical document"),
  "project-one": text("First project physical document"),
  "project-two": text("Second project physical document"),
};
const conversationDir = "conversations/conversation-1";

function detail(ids: string[] = []) {
  const record = conversationDetail();
  if (ids.length) record.mapping["user-1"]!.message!.content = {
    content_type: "multimodal_text",
    parts: ids.map(fileId => ({ content_type: "file", file_id: fileId, filename: fileId + ".txt", mime_type: "text/plain" })),
  };
  return record;
}

function project(): InventoryProject {
  return {
    projectId: "project-1", name: "Synthetic", description: null, instructions: null,
    createTime: null, updateTime: null, rawHash: "project-raw",
    files: (["project-one", "project-two"] as const).map((id, i) => ({
      logicalId: "project-file-" + i, providerId: id, originalName: id + ".txt",
      mediaType: "text/plain", byteSize: fileData[id]!.length,
      rawDescriptor: { file_id: id, name: id + ".txt" },
    })),
  };
}

async function filesystem(projects: InventoryProject[] = []) {
  const fs = new MemoryArchiveFileSystem();
  const inventory: ConversationInventory = {
    schemaVersion: 1, provider: "chatgpt-web", workspaceFingerprint: workspace.workspaceFingerprint,
    generatedAt: "2026-10-11T00:00:00Z", complete: true,
    chains: [{ chainId: "main", scope: "main", complete: true, terminationReason: "declared_total_reached",
      pageCount: 1, itemCount: 1, uniqueConversationCount: 1 }],
    pages: [], projects,
    conversations: [{
      logicalKey: workspace.workspaceFingerprint + "/conversation-1",
      conversationId: "conversation-1", title: "Synthetic", createTime: 1, updateTime: 2,
      memberships: [{ scope: "main" }], listingHashes: ["listing"],
      listingRecords: [{ id: "conversation-1", title: "Synthetic" }],
    }],
  };
  await fs.writeTextAtomic("inventory.json", prettyJson(inventory));
  return fs;
}

function provider(record = detail(), failedIds: string[] = []) {
  const handles = new Map<string, string>();
  let next = 0;
  const request = vi.fn(async (op: ChatGptOperationParameters): Promise<ApiSuccessResponse> => {
    let body: JsonValue;
    if (op.operation === "conversation_batch") body = [record as unknown as JsonValue];
    else if (op.operation === "asset_open") {
      const id = op.parameters.fileId;
      if (failedIds.includes(id)) throw Object.assign(new Error("Synthetic asset unavailable"), { code: "SYNTHETIC_ASSET_FAILURE" });
      const bytes = fileData[id];
      if (!bytes) throw new Error("Unexpected file " + id);
      const handleId = "handle-" + ++next;
      handles.set(handleId, id);
      body = { handleId, expectedBytes: bytes.length, mediaType: "text/plain" };
    } else if (op.operation === "asset_chunk") {
      const id = handles.get(op.parameters.handleId);
      if (!id) throw new Error("Unknown handle");
      const bytes = fileData[id]!;
      const chunk = bytes.slice(op.parameters.offset, op.parameters.offset + op.parameters.length);
      const nextOffset = op.parameters.offset + chunk.length;
      body = {
        handleId: op.parameters.handleId, offset: op.parameters.offset, nextOffset,
        byteLength: chunk.length, dataBase64: btoa(String.fromCharCode(...chunk)),
        eof: nextOffset >= bytes.length,
      };
    } else if (op.operation === "asset_close") {
      body = { handleId: op.parameters.handleId, closed: handles.delete(op.parameters.handleId) };
    } else throw new Error("Unexpected request " + op.operation);
    return { requestId: "synthetic", protocolVersion: BRIDGE_PROTOCOL_VERSION,
      ok: true, status: 200, body, responseBytes: JSON.stringify(body).length, correlationId: "synthetic" };
  });
  return { request } as ChatGptTransport & { request: ReturnType<typeof vi.fn> };
}

function offline() {
  const request = vi.fn(async (op: ChatGptOperationParameters): Promise<ApiSuccessResponse> => {
    throw new Error("Forbidden provider request: " + op.operation);
  });
  return { request } as ChatGptTransport & { request: ReturnType<typeof vi.fn> };
}
async function run(fs: MemoryArchiveFileSystem, transport: ChatGptTransport, runId: string) {
  return new ChatGptCaptureEngine({
    filesystem: fs, workspace, transport, runId,
    includeAccountArtifacts: false,
  }).run();
}
function calls(t: ReturnType<typeof provider>, operation: ChatGptOperationParameters["operation"]) {
  return t.request.mock.calls.map(([op]) => op).filter(op => op.operation === operation);
}
async function assets(fs: MemoryArchiveFileSystem, projectScope = false): Promise<AssetRecord[]> {
  const path = projectScope ? "projects/project-1/assets.json" : conversationDir + "/assets.json";
  const index = JSON.parse((await fs.readText(path))!);
  return index.assets;
}
function physical(asset: AssetRecord): string { return asset.relativePath!.slice(6); }

describe("X5 physical conversation asset resume", () => {
  it("skips complete files with zero provider traffic after streamed validation", async () => {
    const fs = await filesystem();
    await run(fs, provider(detail(["file-one", "file-two"])), "first");
    const forbidden = offline();
    const result = await run(fs, forbidden, "again");
    expect(result).toMatchObject({ skippedCount: 1, rebuiltCount: 0 });
    expect(forbidden.request).not.toHaveBeenCalled();
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("complete");
  });

  it.each(["missing", "corrupt"])("repairs %s bytes only, not the whole conversation", async fault => {
    const fs = await filesystem(), original = detail(["file-one", "file-two"]);
    await run(fs, provider(original), "first-" + fault);
    const [broken, good] = await assets(fs);
    if (fault === "missing") await fs.remove(physical(broken!));
    else await fs.writeBytesAtomic(physical(broken!), text("x".repeat(broken!.byteSize!)));
    expect(await verifyPhysicalAsset(fs, broken!)).toBe(false);
    expect(await verifyPhysicalAsset(fs, good!)).toBe(true);
    const transport = provider(original), result = await run(fs, transport, "second-" + fault);
    expect(result).toMatchObject({ rebuiltCount: 1, capturedCount: 0, partialAssetCount: 0 });
    expect(calls(transport, "conversation_batch")).toHaveLength(0);
    expect(calls(transport, "asset_open").map(op => op.operation === "asset_open" ? op.parameters.fileId : "")).toEqual(["file-one"]);
    for (const record of await assets(fs)) expect(await verifyPhysicalAsset(fs, record)).toBe(true);
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("complete");
    expect((await fs.listPaths("staging")).length).toBe(0);
  });

  it("recovers inline content offline from retained raw bytes", async () => {
    const fs = await filesystem(), record = detail();
    record.mapping["user-1"]!.message!.content = { content_type: "multimodal_text",
      parts: [{ content_type: "file", asset_pointer: "data:text/plain;base64,c3ludGhldGljIGlubGluZQ==" }] };
    await run(fs, provider(record), "inline-first");
    const [asset] = await assets(fs);
    await fs.remove(physical(asset!));
    const forbidden = offline(), result = await run(fs, forbidden, "inline-second");
    expect(result).toMatchObject({ rebuiltCount: 1, partialAssetCount: 0 });
    expect(forbidden.request).not.toHaveBeenCalled();
    expect(await verifyPhysicalAsset(fs, (await assets(fs))[0]!)).toBe(true);
  });

  it("keeps a failed remote repair partial, preserves healthy bytes, and retries again", async () => {
    const fs = await filesystem(), record = detail(["file-one", "file-two"]);
    await run(fs, provider(record), "first");
    const [broken] = await assets(fs);
    await fs.remove(physical(broken!));
    const denied = provider(record, ["file-one"]);
    expect(await run(fs, denied, "denied")).toMatchObject({ rebuiltCount: 1, partialAssetCount: 1 });
    expect(calls(denied, "conversation_batch")).toHaveLength(0);
    const partial = await assets(fs);
    expect(partial.map(a => a.status)).toEqual(["failed", "complete"]);
    expect(await verifyPhysicalAsset(fs, partial[1]!)).toBe(true);
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("conversations_complete_assets_partial");
    const retry = provider(record);
    expect(await run(fs, retry, "recovered")).toMatchObject({ rebuiltCount: 1, partialAssetCount: 0 });
    expect(calls(retry, "asset_open").map(op => op.operation === "asset_open" ? op.parameters.fileId : "")).toEqual(["file-one"]);
    expect(calls(retry, "conversation_batch")).toHaveLength(0);
  });

  it("repairs a deduplicated physical object with one scoped asset request", async () => {
    const fs = await filesystem(), record = detail(["file-one", "file-one"]);
    await run(fs, provider(record), "duplicated");
    const old = await assets(fs);
    expect(old).toHaveLength(2);
    expect(old[0]?.relativePath).toBe(old[1]?.relativePath);
    await fs.remove(physical(old[0]!));
    const t = provider(record);
    expect((await run(fs, t, "duplicate-repair")).rebuiltCount).toBe(1);
    expect(calls(t, "asset_open")).toHaveLength(1);
    expect(calls(t, "conversation_batch")).toHaveLength(0);
  });

  it("rejects unsafe and inconsistent physical-index locations", async () => {
    const fs = await filesystem(), record = detail(["file-one"]);
    await run(fs, provider(record), "safety");
    const [asset] = await assets(fs);
    expect(await verifyPhysicalAsset(fs, { ...asset!, relativePath: "../../../assets/elsewhere" })).toBe(false);
    expect(await verifyPhysicalAsset(fs, { ...asset!, relativePath: "../../assets/" + "f".repeat(64) + ".txt" })).toBe(false);
    expect(await verifyPhysicalAsset(fs, { ...asset!, sha256: "f".repeat(64) })).toBe(false);
    expect(await verifyPhysicalAsset(fs, { ...asset!, byteSize: 0 })).toBe(false);
  });
});

describe("X5 original D11 adversarial regression", () => {
  it("repairs missing physical assets from a paginated older message without any provider request", async () => {
    const fs = await historyFilesystem();
    const pages = history(6, 2, 1);
    for (const page of pages) for (const message of page.messages) {
      if (message.id === "m-00000" || message.id === "m-00002") {
        message.content = {
          content_type: "multimodal_text",
          parts: [{ content_type: "file", asset_pointer: "data:text/plain;base64,c3ludGhldGlj" }],
        };
      }
    }
    await run(fs, pagesTransport(pages), "original-d11");
    const previous = await assets(fs);
    expect(previous).toHaveLength(2);
    expect((await fs.listPaths("assets")).length).toBe(1);
    for (const path of await fs.listPaths("assets")) await fs.remove(path);

    const disconnected = offline();
    const result = await run(fs, disconnected, "original-d11-repair");
    expect(result).toMatchObject({ rebuiltCount: 1, capturedCount: 0, partialAssetCount: 0 });
    expect((await fs.listPaths("assets")).length).toBe(1);
    expect(disconnected.request).not.toHaveBeenCalled();
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("complete");
  });
});

describe("X5 project scope and selective asset resume", () => {
  it.each(["missing", "corrupt"])("repairs %s project bytes with correct project authorization", async fault => {
    const fs = await filesystem([project()]);
    await run(fs, provider(), "project-first-" + fault);
    const before = await assets(fs, true);
    const path = physical(before[0]!);
    if (fault === "missing") await fs.remove(path);
    else await fs.writeBytesAtomic(path, text("z".repeat(before[0]!.byteSize!)));
    const t = provider(), restored = await run(fs, t, "project-second-" + fault);
    expect(restored).toMatchObject({ projectAssetCount: 2, partialProjectAssetCount: 0, projectAssetStatus: "complete", skippedCount: 1 });
    expect(calls(t, "asset_open").map(op => op.operation === "asset_open" ? op.parameters : null)).toEqual([
      { fileId: "project-one", projectId: "project-1", conversationId: null },
    ]);
    expect(calls(t, "conversation_batch")).toHaveLength(0);
    for (const asset of await assets(fs, true)) expect(await verifyPhysicalAsset(fs, asset)).toBe(true);
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("complete");
  });

  it("project retry failure stays partial without redownloading good files", async () => {
    const fs = await filesystem([project()]);
    await run(fs, provider(), "project-first");
    const before = await assets(fs, true);
    await fs.remove(physical(before[1]!));
    const t = provider(undefined, ["project-two"]);
    const state = await run(fs, t, "project-denied");
    expect(state).toMatchObject({ projectAssetCount: 2, partialProjectAssetCount: 1, projectAssetStatus: "partial", skippedCount: 1 });
    expect(calls(t, "asset_open").map(op => op.operation === "asset_open" ? op.parameters.fileId : "")).toEqual(["project-two"]);
    expect(calls(t, "conversation_batch")).toHaveLength(0);
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("conversations_complete_assets_partial");
  });

  it("healthy project files need no provider traffic on rerun", async () => {
    const fs = await filesystem([project()]);
    await run(fs, provider(), "project-healthy");
    const t = offline(), result = await run(fs, t, "project-repeat");
    expect(result).toMatchObject({ projectAssetCount: 2, partialProjectAssetCount: 0, skippedCount: 1 });
    expect(t.request).not.toHaveBeenCalled();
  });
});
