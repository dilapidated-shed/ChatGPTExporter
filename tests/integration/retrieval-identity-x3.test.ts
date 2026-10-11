import { describe, expect, it } from "vitest";

import { ChatGptDetailFetcher } from "../../src/chatgpt/capture";
import { ChatGptCaptureEngine } from "../../src/chatgpt/capture-engine";
import { auditArchive } from "../../src/chatgpt/audit";
import { validateConversationEvidence } from "../../src/chatgpt/evidence";
import { MemoryArchiveFileSystem } from "../../src/core/filesystem";
import { conversationBasePath, legacyConversationBasePath } from "../../src/core/paths";
import { prettyJson } from "../../src/core/serialization";
import { conversationDetail } from "../fixtures/chatgpt";
import { filesystem, http, item, page, pagesTransport, transport, workspace } from "../fixtures/adversarial";

function capture(fs: MemoryArchiveFileSystem, t: ReturnType<typeof transport>, runId: string) {
  return new ChatGptCaptureEngine({
    filesystem: fs, transport: t, workspace, runId, batchSize: 2,
    includeAssets: false, includeAccountArtifacts: false,
  }).run();
}

describe("X3 D04 checkpoint survival", () => {
  it("commits an earlier batch sibling before a later plural fallback fails, then skips it on resume", async () => {
    const fs = await filesystem([item("good"), item("bad")]);
    const first = transport(op => {
      if (op.operation === "conversation_batch") return [conversationDetail({ id: "good" })] as any;
      if (op.operation === "conversation_current") throw http(503);
      throw new Error("unexpected first capture operation");
    });
    await expect(capture(fs, first, "first")).rejects.toMatchObject({ status: 503 });
    const goodBase = conversationBasePath("good");
    const badBase = conversationBasePath("bad");
    expect(await fs.readText(goodBase + "/complete.json")).toBeDefined();
    expect(await fs.readText(goodBase + "/raw-complete.json")).toBeDefined();
    expect(await fs.readText(badBase + "/complete.json")).toBeUndefined();
    const marker = JSON.parse((await fs.readText(goodBase + "/raw-complete.json"))!);
    expect(marker.retrievalSource).toBe("batch");
    expect(marker.batchHash).toBeTruthy();
    const firstReport = JSON.parse((await fs.readText("reports/capture-first.json"))!);
    expect(firstReport).toMatchObject({ capturedCount: 1, failedCount: 1 });

    const second = transport(op => {
      if (op.operation === "conversation_batch") {
        expect(op.parameters.conversationIds).toEqual(["bad"]);
        return [conversationDetail({ id: "bad" })] as any;
      }
      throw new Error("previously committed conversation must never refetch");
    });
    const result = await capture(fs, second, "second");
    expect(result).toMatchObject({ inventoryCount: 2, capturedCount: 1, skippedCount: 1, failedCount: 0 });
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("complete");
  });

  it("saves the first plural-recovered sibling after batch 404 when the next history fails", async () => {
    const fs = await filesystem([item("first"), item("second")]);
    const firstPage = page(["m-1"], null, { conversation_id: "first" });
    const secondPage = page(["m-2"], "cursor", { conversation_id: "second" });
    const firstTransport = transport(op => {
      if (op.operation === "conversation_batch") throw http(404);
      if (op.operation === "conversation_current") return op.parameters.conversationId === "first" ? firstPage : secondPage;
      if (op.operation === "conversation_messages") throw http(429);
      throw new Error("unexpected operation");
    });
    await expect(capture(fs, firstTransport, "plural-first")).rejects.toMatchObject({ status: 429 });
    expect(await fs.readText(conversationBasePath("first") + "/complete.json")).toBeDefined();
    expect(await fs.readText(conversationBasePath("second") + "/complete.json")).toBeUndefined();
  });
});

describe("X3 D07 pagination ordering", () => {
  it("rejects contradictory order with byte-identical overlapping messages", async () => {
    const t = pagesTransport([page(["b", "c"], "older"), page(["a", "c", "b"])]);
    await expect(new ChatGptDetailFetcher(t, workspace).fetchAll([item()]))
      .rejects.toMatchObject({ code: "PAGINATION_ORDER_CONFLICT" });
  });
  it("accepts stable overlaps and validates replay", async () => {
    const t = pagesTransport([page(["b", "c"], "older"), page(["a", "b"])]);
    const result = (await new ChatGptDetailFetcher(t, workspace).fetchAll([item()])).conversations[0]!;
    expect(validateConversationEvidence(result.raw, "conversation-1", "current").coverage.messages).toBe("current_branch");
    expect(Object.values(result.detail.mapping).filter(node => node.message)).toHaveLength(3);
  });
  it("rejects an older page inventing a new message after a previously observed anchor", async () => {
    const t = pagesTransport([page(["b", "c"], "older"), page(["a", "b", "x"])]);
    await expect(new ChatGptDetailFetcher(t, workspace).fetchAll([item()]))
      .rejects.toMatchObject({ code: "PAGINATION_ORDER_CONFLICT" });
  });
});

describe("X3 D08 identity aliases", () => {
  it("rejects conflicting aliases in initial and older paginated pages", async () => {
    for (const pages of [
      [page(["m"], null, { id: "other" })],
      [page(["b"], "cursor"), page(["a", "b"], null, { id: "other" })],
    ]) {
      await expect(new ChatGptDetailFetcher(pagesTransport(pages), workspace).fetchAll([item()]))
        .rejects.toMatchObject({ code: "PAGINATION_IDENTITY_CONFLICT" });
    }
  });
  it("does not trust a batch record with contradicting aliases", async () => {
    const conflicting = { ...conversationDetail(), conversation_id: "other" };
    const t = transport(op => {
      if (op.operation === "conversation_batch") return [conflicting] as any;
      if (op.operation === "conversation_current") return page(["m"]);
      throw new Error("unexpected");
    });
    const result = await new ChatGptDetailFetcher(t, workspace).fetchAll([item()]);
    expect(result.conversations[0]?.source).toBe("current");
    expect(result.conversations[0]?.fallbackReason).toBe("batch_invalid");
  });
  it("rejects conflicting legacy singular and shared aliases", async () => {
    const t = transport(op => {
      if (op.operation === "conversation_detail" || op.operation === "shared_detail") {
        return { ...conversationDetail(), conversation_id: "other" };
      }
      throw http(404);
    });
    await expect(new ChatGptDetailFetcher(t, workspace).fetchAll([item()]))
      .rejects.toMatchObject({ code: "DETAIL_IDENTITY_CONFLICT" });
    const share = item("share_shared");
    share.memberships = [{ scope: "shared", shareId: "shared" }];
    await expect(new ChatGptDetailFetcher(t, workspace).fetchAll([share]))
      .rejects.toMatchObject({ code: "DETAIL_IDENTITY_CONFLICT" });
  });
});

describe("X3 D13 injective archival paths", () => {
  it("maps long, leading/trailing-hyphen, uppercase and reserved IDs to distinct safe paths", () => {
    const prefix = "a".repeat(160);
    const ids = [prefix + "x", prefix + "y", "conversation", "-conversation", "conversation-", "A", "a", "CON", "_con"];
    const paths = ids.map(id => conversationBasePath(id));
    expect(new Set(paths.map(path => path.toLowerCase())).size).toBe(ids.length);
    expect(conversationBasePath("conversation-1")).toBe("conversations/conversation-1");
    expect(legacyConversationBasePath("-conversation")).toBe("conversations/conversation");
    expect(conversationBasePath("-conversation")).not.toBe(conversationBasePath("conversation"));
    expect(paths.every(path => path.split("/").every(segment => segment.length <= 160))).toBe(true);
  });
  it("blocks a prior lossy-layout conversation before writing anything", async () => {
    const fs = await filesystem([item("-conversation-1")]);
    const legacy = legacyConversationBasePath("-conversation-1");
    await fs.writeTextAtomic(legacy + "/raw-complete.json", prettyJson({ conversationId: "-conversation-1" }));
    const before = await fs.listPaths();
    const offline = transport(() => { throw new Error("network forbidden"); });
    await expect(capture(fs, offline, "legacy-refusal")).rejects.toThrow(/migration/);
    expect(await fs.listPaths()).toEqual(before);
    expect(offline.request).not.toHaveBeenCalled();
  });
  it("archives two formerly colliding IDs independently and resumes both", async () => {
    const ids = ["conversation-1", "-conversation-1"];
    const fs = await filesystem(ids.map(id => item(id)));
    const first = transport(op => {
      if (op.operation === "conversation_batch") return ids.map(id => conversationDetail({ id })) as any;
      throw new Error("unexpected");
    });
    const result = await capture(fs, first, "pair");
    expect(result).toMatchObject({ capturedCount: 2, failedCount: 0 });
    for (const id of ids) {
      const root = conversationBasePath(id);
      const marker = JSON.parse((await fs.readText(root + "/complete.json"))!);
      expect(marker.conversationId).toBe(id);
      expect(await fs.readText(root + "/conversation.json")).toBeDefined();
    }
    const offline = transport(() => { throw new Error("network forbidden"); });
    expect((await capture(fs, offline, "pair-repeat")).skippedCount).toBe(2);
    expect(offline.request).not.toHaveBeenCalled();
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("complete");
  });
});
