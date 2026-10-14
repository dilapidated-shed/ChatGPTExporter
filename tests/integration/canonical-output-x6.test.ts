import { describe, expect, it } from "vitest";

import { ChatGptCaptureEngine } from "../../src/chatgpt/capture-engine";
import { graphFindings, ChatGptDetailFetcher } from "../../src/chatgpt/capture";
import { auditArchive } from "../../src/chatgpt/audit";
import { validateConversationEvidence } from "../../src/chatgpt/evidence";
import { NORMALIZER_VERSION, normalizeConversation } from "../../src/chatgpt/normalize";
import { sha256Hex } from "../../src/core/hash";
import { prettyJson } from "../../src/core/serialization";
import { filesystem, history, item, pagesTransport, transport, workspace } from "../fixtures/adversarial";

const dir = "conversations/conversation-1";
const run = async (fs: Awaited<ReturnType<typeof filesystem>>, pages: any[], runId: string) =>
  new ChatGptCaptureEngine({
    filesystem: fs,
    transport: pagesTransport(pages),
    workspace,
    runId,
    includeAssets: false,
    includeAccountArtifacts: false,
  }).run();

describe("Sun X6: semantic content is independent of page evidence", () => {
  it("stores page requests and responses in raw source, not canonical conversation JSON", async () => {
    const fs = await filesystem();
    const result = await run(fs, history(12, 2, 1), "x6-source");
    expect(result).toMatchObject({ capturedCount: 1 });
    const marker = JSON.parse((await fs.readText(dir + "/raw-complete.json"))!);
    const raw = JSON.parse((await fs.readText(marker.detailPath))!);
    const normalized = JSON.parse((await fs.readText(dir + "/conversation.json"))!);
    const completed = JSON.parse((await fs.readText(dir + "/complete.json"))!);
    expect(raw.__pagination_evidence.page_count).toBeGreaterThan(1);
    expect(raw.__pagination_evidence.pages[0].request.operation).toBe("conversation_current");
    expect(normalized.coverage).toMatchObject({ messages: "current_branch", providerGraph: "unknown", versions: "unknown" });
    expect(normalized.normalizerVersion).toBe("chatgpt-web-v2");
    expect(completed.normalizerVersion).toBe(NORMALIZER_VERSION);
    expect(Object.hasOwn(normalized.extensions.chatgpt, "__pagination_evidence")).toBe(false);
    expect(JSON.stringify(normalized)).not.toContain("__pagination_evidence");
    expect(normalized.messages).toHaveLength(12);
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("complete");
  });

  it.each([[12,12,0],[4,1,0],[6,3,1],[5,2,2]])(
    "normalizes equal content from pages of width %i and %i overlap %i identically",
    async (_messages, width, overlap) => {
      const one = (await new ChatGptDetailFetcher(pagesTransport(history(12, 12)), workspace).fetchAll([item()])).conversations[0]!;
      const split = (await new ChatGptDetailFetcher(pagesTransport(history(12, width, overlap)), workspace).fetchAll([item()])).conversations[0]!;
      const a = normalizeConversation(one.detail, item(), workspace.workspaceFingerprint);
      const b = normalizeConversation(split.detail, item(), workspace.workspaceFingerprint);
      expect(b).toEqual(a);
      expect(prettyJson(b)).toBe(prettyJson(a));
      if (width !== 12 || overlap !== 0) {
        expect(prettyJson(split.raw)).not.toBe(prettyJson(one.raw));
      }
      expect(validateConversationEvidence(split.raw, "conversation-1", "current").coverage.messages).toBe("current_branch");
    },
  );

  it("retains provider-owned unknown metadata and unknown roles without pagination layout", async () => {
    const pages = history(3, 1);
    pages[0].experimental_provider_extension = { some_future_setting: [1, "preserve"] };
    for (const page of pages) {
      for (const message of page.messages) {
        if (message.id === "m-00000") {
          message.author.role = "unknown-future-role";
          message.content = { content_type: "brand-new-block", parts: [{ future: true }], nested: { x: 1 } };
          message.metadata = { future: [1, 2, 3] };
        }
      }
    }
    const result = (await new ChatGptDetailFetcher(pagesTransport(pages), workspace).fetchAll([item()])).conversations[0]!;
    const normalized = normalizeConversation(result.detail, item(), workspace.workspaceFingerprint);
    expect(normalized.extensions.chatgpt).toMatchObject({ experimental_provider_extension: { some_future_setting: [1, "preserve"] } });
    expect(normalized.messages.find(m => m.id === "m-00000")!.extensions.chatgpt).toMatchObject({
      author: { role: "unknown-future-role" },
      content: { content_type: "brand-new-block", nested: { x: 1 } },
      metadata: { future: [1, 2, 3] },
    });
    expect(Object.hasOwn(normalized.extensions.chatgpt as object, "__pagination_evidence")).toBe(false);
  });

  it("verifies historical v1 pagination extensions during audit and upgrades offline from trusted raw", async () => {
    const fs = await filesystem();
    await run(fs, history(4, 2, 1), "legacy-seed");
    const rawMarker = JSON.parse((await fs.readText(dir + "/raw-complete.json"))!);
    const raw = JSON.parse((await fs.readText(rawMarker.detailPath))!);
    const normalized = JSON.parse((await fs.readText(dir + "/conversation.json"))!);
    normalized.normalizerVersion = "chatgpt-web-v1";
    normalized.extensions.chatgpt.__pagination_evidence = raw.__pagination_evidence;
    const oldText = prettyJson(normalized);
    await fs.writeTextAtomic(dir + "/conversation.json", oldText);
    const completion = JSON.parse((await fs.readText(dir + "/complete.json"))!);
    completion.normalizerVersion = "chatgpt-web-v1";
    completion.normalizedHash = await sha256Hex(oldText);
    await fs.writeTextAtomic(dir + "/complete.json", prettyJson(completion));

    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("complete");
    const offline = transport(() => { throw new Error("Provider contact forbidden for compatible legacy archive"); });
    const result = await new ChatGptCaptureEngine({
      filesystem: fs,
      transport: offline,
      workspace,
      runId: "legacy-upgrade",
      includeAssets: false,
      includeAccountArtifacts: false,
    }).run();
    expect(result).toMatchObject({ rebuiltCount: 1, capturedCount: 0 });
    expect(offline.request).not.toHaveBeenCalled();
    expect(JSON.parse((await fs.readText(dir + "/conversation.json"))!).normalizerVersion).toBe(NORMALIZER_VERSION);
    const after = JSON.parse((await fs.readText(dir + "/conversation.json"))!);
    expect(Object.hasOwn(after.extensions.chatgpt, "__pagination_evidence")).toBe(false);
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("complete");
  });

  it("does not launder a modified v1 page receipt into a valid canonical archive", async () => {
    const fs = await filesystem();
    await run(fs, history(4, 2, 1), "tampered-seed");
    const rawMarker = JSON.parse((await fs.readText(dir + "/raw-complete.json"))!);
    const raw = JSON.parse((await fs.readText(rawMarker.detailPath))!);
    const normalized = JSON.parse((await fs.readText(dir + "/conversation.json"))!);
    normalized.normalizerVersion = "chatgpt-web-v1";
    normalized.extensions.chatgpt.__pagination_evidence = structuredClone(raw.__pagination_evidence);
    normalized.extensions.chatgpt.__pagination_evidence.pages[0].request.before = "a forged cursor";
    const text = prettyJson(normalized);
    await fs.writeTextAtomic(dir + "/conversation.json", text);
    const completion = JSON.parse((await fs.readText(dir + "/complete.json"))!);
    completion.normalizerVersion = "chatgpt-web-v1";
    completion.normalizedHash = await sha256Hex(text);
    await fs.writeTextAtomic(dir + "/complete.json", prettyJson(completion));
    const report = await auditArchive({ filesystem: fs, extensionVersion: "synthetic" });
    expect(report.findings.some(f => f.code === "GRAPH_CONTENT_MISMATCH")).toBe(true);
    expect(report.terminalState).toBe("incomplete");
  });
});

describe("Sun X6: linear ancestry and bounded page history", () => {
  it("visits parent links linearly in a 2000-node chain", () => {
    const n = 2000;
    let parentReads = 0;
    const mapping: Record<string, any> = {};
    for (let index = 0; index < n; index++) {
      mapping["node-" + index] = {
        id: "node-" + index, message: null,
        get parent() { parentReads++; return index === 0 ? null : "node-" + (index - 1); },
        children: index + 1 < n ? ["node-" + (index + 1)] : [],
      };
    }
    expect(graphFindings({ mapping, current_node: "node-" + (n - 1) } as any)).toEqual([]);
    expect(parentReads).toBeLessThan(n * 5);
  });

  it("detects disconnected cycles and missing edges without recursive traversal", () => {
    const mapping: Record<string, any> = {
      root: { id: "root", message: null, parent: null, children: [] },
      a: { id: "a", message: null, parent: "b", children: ["b"] },
      b: { id: "b", message: null, parent: "a", children: ["a"] },
      c: { id: "c", message: null, parent: "not-present", children: ["also-missing"] },
    };
    const findings = graphFindings({ mapping, current_node: "root" } as any);
    expect(findings.some(x => x.startsWith("cycle:"))).toBe(true);
    expect(findings).toContain("parent_missing:c");
    expect(findings).toContain("child_missing:c");
  });
});
