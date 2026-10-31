import { describe, expect, it } from "vitest";

import { ChatGptInventoryEngine, DEFAULT_INVENTORY_SETTINGS, InventoryError } from "../../src/chatgpt/inventory";
import { validateInventoryEvidence } from "../../src/chatgpt/inventory-evidence";
import { ChatGptCaptureEngine } from "../../src/chatgpt/capture-engine";
import { auditArchive } from "../../src/chatgpt/audit";
import { MemoryArchiveFileSystem } from "../../src/core/filesystem";
import { prettyJson } from "../../src/core/serialization";
import { transport, workspace } from "../fixtures/adversarial";
import type { ChatGptOperationParameters } from "../../src/chatgpt/endpoints";
import type { JsonValue } from "../../src/core/types";

const settings = {
  ...DEFAULT_INVENTORY_SETTINGS,
  pageSize: 2,
  includeArchived: false,
  includeProjects: false,
  includeShared: false,
};

function listing(ids: string[], total: number | null): JsonValue {
  return { items: ids.map(id => ({ id, title: id, create_time: 1, update_time: 2 })), total };
}

function engine(filesystem: MemoryArchiveFileSystem, handler: (op: ChatGptOperationParameters) => JsonValue,
  overrides: Partial<typeof settings> = {}) {
  return new ChatGptInventoryEngine({ filesystem, workspace, settings: { ...settings, ...overrides }, transport: transport(handler) });
}

describe("X4 count hints and honest coverage", () => {
  it("does not end a full page when total is reached: an unseen successor remains observable", async () => {
    const fs = new MemoryArchiveFileSystem();
    const offsets: number[] = [];
    const inventory = await engine(fs, op => {
      if (op.operation !== "conversation_page") throw new Error("unexpected");
      offsets.push(op.parameters.offset);
      return op.parameters.offset === 0 ? listing(["a", "b"], 2)
        : op.parameters.offset === 2 ? listing(["c"], 3)
        : (() => { throw new Error("unexpected offset"); })();
    }).run();
    expect(offsets).toEqual([0, 2]);
    expect(inventory.conversations.map(c => c.conversationId)).toEqual(["a", "b", "c"]);
    expect(inventory.chains[0]).toMatchObject({ terminationReason: "short_page", offsetInstability: true });
    expect(inventory).toMatchObject({
      complete: false,
      discovery: { traversalComplete: true, providerSnapshot: "unverified", offsetInstability: true },
    });
    expect((await validateInventoryEvidence(fs, inventory)).errors).toEqual([]);
    const report = JSON.parse((await fs.readText("reports/reconciliation.json"))!);
    expect(report).toMatchObject({ observedConversationCount: 3, reportedTotals: "hints_only", captureEligible: false });
    expect(report.discovery.offsetInstability).toBe(true);
  });

  it("a full page at the reported total requires an actual terminal probe", async () => {
    const fs = new MemoryArchiveFileSystem();
    const offsets: number[] = [];
    const inv = await engine(fs, op => {
      if (op.operation !== "conversation_page") throw new Error("unexpected");
      offsets.push(op.parameters.offset);
      return op.parameters.offset === 0 ? listing(["a", "b"], 2) : listing([], 2);
    }).run();
    expect(offsets).toEqual([0, 2]);
    expect(inv.complete).toBe(true);
    expect(inv.chains[0]?.terminationReason).toBe("empty_page");
    expect(inv.discovery).toEqual({ traversalComplete: true, providerSnapshot: "unverified", offsetInstability: false });
    expect((await validateInventoryEvidence(fs, inv)).errors).toEqual([]);
  });

  it("short page plus consistent total permits traversal termination, not a snapshot guarantee", async () => {
    const fs = new MemoryArchiveFileSystem();
    const inv = await engine(fs, op => {
      if (op.operation !== "conversation_page") throw new Error("unexpected");
      return op.parameters.offset === 0 ? listing(["a", "b"], 3) : listing(["c"], 3);
    }).run();
    expect(inv).toMatchObject({ complete: true, discovery: { providerSnapshot: "unverified", offsetInstability: false } });
    expect(inv.chains[0]?.terminationReason).toBe("short_page");
    expect((await validateInventoryEvidence(fs, inv)).errors).toEqual([]);
  });

  it("observed overlap preserves every retrieved ID but refuses complete capture claims", async () => {
    const fs = new MemoryArchiveFileSystem();
    const responses = [listing(["a", "b"], null), listing(["b", "d"], null), listing([], null)];
    let request = 0;
    const inv = await engine(fs, op => {
      if (op.operation !== "conversation_page") throw new Error("unexpected");
      return responses[request++]!;
    }).run();
    expect(inv.conversations.map(c => c.conversationId)).toEqual(["a", "b", "d"]);
    expect(inv.complete).toBe(false);
    expect(inv.discovery).toEqual({ traversalComplete: true, providerSnapshot: "unverified", offsetInstability: true });
    expect((await validateInventoryEvidence(fs, inv)).errors).toEqual([]);
    const report = await auditArchive({ filesystem: fs, extensionVersion: "synthetic" });
    expect(report.terminalState).toBe("incomplete");
    expect(report.inventoryDiscovery).toMatchObject({ offsetInstability: true });
    expect(report.findings.some(f => f.code === "INVENTORY_OFFSET_INSTABILITY")).toBe(true);
    await expect(new ChatGptCaptureEngine({
      filesystem: fs, workspace, transport: transport(() => { throw new Error("network must not be used"); }),
      runId: "unstable", includeAssets: false, includeAccountArtifacts: false,
    }).run()).rejects.toThrow(/complete ChatGPT inventory/);
  });

  it("early empty after returned records is a visible inconsistency, not a deletion proof", async () => {
    const fs = new MemoryArchiveFileSystem();
    const inv = await engine(fs, op => {
      if (op.operation !== "conversation_page") throw new Error("unexpected");
      return op.parameters.offset === 0 ? listing(["a", "b"], 8) : listing([], 8);
    }).run();
    expect(inv.conversations.map(c => c.conversationId)).toEqual(["a", "b"]);
    expect(inv.complete).toBe(false);
    expect(inv.discovery?.offsetInstability).toBe(true);
    expect((await validateInventoryEvidence(fs, inv)).errors).toEqual([]);
  });

  it("unions shared ownership across shifted full shared pages and keeps observed records", async () => {
    const fs = new MemoryArchiveFileSystem();
    const inv = await engine(fs, op => {
      if (op.operation === "conversation_page") return listing([], 0);
      if (op.operation === "shared_page") {
        const items = op.parameters.offset === 0
          ? [{ id: "s1", conversation_id: "a" }, { id: "s2", conversation_id: "b" }]
          : [{ id: "s3", conversation_id: "c" }];
        return { items, total: op.parameters.offset === 0 ? 2 : 3 };
      }
      throw new Error("unexpected scope");
    }, { includeShared: true }).run();
    expect(inv.conversations.map(c => c.conversationId)).toEqual(["a", "b", "c"]);
    expect(inv.conversations.every(c => c.memberships[0]?.scope === "shared")).toBe(true);
    expect(inv.chains.find(c => c.scope === "shared")).toMatchObject({ terminationReason: "short_page", offsetInstability: true });
    expect(inv.complete).toBe(false);
    expect((await validateInventoryEvidence(fs, inv)).errors).toEqual([]);
  });

  it("retains uncertain previous observations as unobserved, not deleted", async () => {
    const fs = new MemoryArchiveFileSystem();
    const responses = [listing(["a", "b"], null), listing(["b", "d"], null), listing([], null)];
    let count = 0;
    const initial = await engine(fs, () => responses[count++]!).run();
    expect(initial.complete).toBe(false);
    const followup = await engine(fs, () => listing([], 0)).run();
    expect(followup.complete).toBe(true);
    expect(followup.absentConversations?.map(c => c.conversationId)).toEqual(["a", "b", "d"]);
    expect((await fs.listPaths("source/inventory/snapshots")).length).toBe(1);
    expect((await validateInventoryEvidence(fs, followup)).errors).toEqual([]);
  });

  it("does not trust altered completion/offset-uncertainty flags with unchanged raw pages", async () => {
    const fs = new MemoryArchiveFileSystem();
    const pages = [listing(["a", "b"], null), listing(["b", "d"], null), listing([], null)];
    let next = 0;
    const inv = await engine(fs, () => pages[next++]!).run();
    inv.complete = true;
    inv.discovery!.offsetInstability = false;
    inv.chains[0]!.offsetInstability = false;
    await fs.writeTextAtomic("inventory.json", prettyJson(inv));
    const evidence = await validateInventoryEvidence(fs, inv);
    expect(evidence.errors.some(e => e.includes("instability") || e.includes("discovery"))).toBe(true);
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).findings
      .some(f => f.code === "INVENTORY_EVIDENCE_INVALID")).toBe(true);
  });

  it("replays historical total-terminated receipts without upgrading their snapshot coverage", async () => {
    const fs = new MemoryArchiveFileSystem();
    const inv = await engine(fs, () => listing(["a"], 1)).run();
    expect(inv.chains[0]?.terminationReason).toBe("short_page");
    inv.chains[0]!.terminationReason = "declared_total_reached";
    inv.pages[0]!.terminationReason = "declared_total_reached";
    delete inv.discovery;
    delete inv.chains[0]!.offsetInstability;
    await fs.writeTextAtomic("inventory.json", prettyJson(inv));
    expect((await validateInventoryEvidence(fs, inv)).errors).toEqual([]);
    const audit = await auditArchive({ filesystem: fs, extensionVersion: "synthetic" });
    expect(audit.inventoryDiscovery.providerSnapshot).toBe("unverified");
  });
});
