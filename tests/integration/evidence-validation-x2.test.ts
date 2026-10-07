import { describe, expect, it } from "vitest";
import { auditArchive } from "../../src/chatgpt/audit";
import { ChatGptCaptureEngine } from "../../src/chatgpt/capture-engine";
import { validateConversationEvidence } from "../../src/chatgpt/evidence";
import { ChatGptInventoryEngine, DEFAULT_INVENTORY_SETTINGS } from "../../src/chatgpt/inventory";
import { validateInventoryEvidence } from "../../src/chatgpt/inventory-evidence";
import { CaptureStore } from "../../src/core/capture-store";
import { MemoryArchiveFileSystem } from "../../src/core/filesystem";
import { hashJson, sha256Hex } from "../../src/core/hash";
import { prettyJson } from "../../src/core/serialization";
import { conversationDetail } from "../fixtures/chatgpt";
import { filesystem, fixedNow, history, item, pagesTransport, transport, workspace } from "../fixtures/adversarial";

const base = "conversations/conversation-1";
function run(fs: MemoryArchiveFileSystem, t: ReturnType<typeof transport>, runId = "capture", includeAssets = false) {
  return new ChatGptCaptureEngine({ filesystem: fs, transport: t, workspace, runId, includeAssets, includeAccountArtifacts: false, now: fixedNow }).run();
}
async function captured(assets = false) {
  const fs = await filesystem();
  const pages = history(6, 2, 1);
  if (assets) pages.at(-1).messages[0].content = { content_type: "multimodal_text", parts: [{ content_type: "file", asset_pointer: "data:text/plain;base64,c3ludGhldGlj" }] };
  await run(fs, pagesTransport(pages), "capture", assets);
  const marker = JSON.parse((await fs.readText(`${base}/raw-complete.json`))!);
  const raw = JSON.parse((await fs.readText(marker.detailPath))!);
  return { fs, marker, raw };
}
async function replaceRaw(fs: MemoryArchiveFileSystem, marker: any, raw: any) {
  const text = prettyJson(raw);
  marker.detailHash = await sha256Hex(text);
  marker.detailPath = `${base}/source/detail-${marker.detailHash}.json`;
  await fs.writeTextAtomic(marker.detailPath, text);
  const markerText = prettyJson(marker);
  await fs.writeTextAtomic(`${base}/raw-complete.json`, markerText);
  const completion = JSON.parse((await fs.readText(`${base}/complete.json`))!);
  completion.rawMarkerHash = await sha256Hex(markerText);
  completion.detailHash = marker.detailHash;
  await fs.writeTextAtomic(`${base}/complete.json`, prettyJson(completion));
}
async function replaceAssets(fs: MemoryArchiveFileSystem, change: (index: any) => void) {
  const index = JSON.parse((await fs.readText(`${base}/assets.json`))!);
  change(index);
  const text = prettyJson(index);
  await fs.writeTextAtomic(`${base}/assets.json`, text);
  const completion = JSON.parse((await fs.readText(`${base}/complete.json`))!);
  completion.assetsHash = await sha256Hex(text);
  completion.assetStatus = index.status;
  await fs.writeTextAtomic(`${base}/complete.json`, prettyJson(completion));
  const rows = index.assets.map((asset: any) => ({ logicalKey: item().logicalKey, conversationId: item().conversationId, ...asset }));
  await fs.writeTextAtomic("indexes/assets.jsonl", rows.map((row: any) => JSON.stringify(row)).join("\n") + (rows.length ? "\n" : ""));
}

describe("X2 pure conversation replay and independent audit", () => {
  it("reconstructs valid overlaps without mutating evidence and qualifies coverage", async () => {
    const { raw } = await captured();
    const before = prettyJson(raw);
    const result = validateConversationEvidence(raw, "conversation-1", "current");
    expect(result.coverage).toEqual({ messages: "current_branch", providerGraph: "unknown", versions: "unknown" });
    expect(prettyJson(raw)).toBe(before);
    expect(Object.values(result.detail.mapping).filter(node => node.message)).toHaveLength(6);
  });
  const mutations: Array<[string, (raw: any) => void]> = [
    ["request turn count", raw => { raw.__pagination_evidence.pages[1].request.num_turns = 11; }],
    ["response aliases", raw => { raw.__pagination_evidence.pages[1].response.id = "other"; }],
    ["overlap bytes", raw => { raw.__pagination_evidence.pages[1].response.messages.at(-1).content.parts = ["changed"]; }],
    ["duplicate page messages", raw => { const page = raw.__pagination_evidence.pages[0].response; page.messages.push(page.messages[0]); }],
    ["mapping edges", raw => { raw.mapping["m-00000"].children = []; }],
    ["mapping current node", raw => { raw.current_node = "m-00000"; }],
    ["mapping message omitted", raw => { delete raw.mapping["m-00000"]; }],
    ["cursor control", raw => { raw.__pagination_evidence.pages[0].response.page_info.start_cursor = "\n"; }],
    ["cursor oversized", raw => { raw.__pagination_evidence.pages[0].response.page_info.start_cursor = "x".repeat(2049); }],
  ];
  it.each(mutations)("both consumers reject %s even with refreshed hashes", async (_name, mutate) => {
    const { fs, raw, marker } = await captured();
    mutate(raw);
    await replaceRaw(fs, marker, raw);
    expect(await new CaptureStore(fs, "resume", workspace.workspaceFingerprint).validRawMarker(item())).toBeUndefined();
    const report = await auditArchive({ filesystem: fs, extensionVersion: "synthetic" });
    expect(report.terminalState).toBe("incomplete");
    expect(report.completeConversationCount).toBe(0);
    expect(report.findings.some(finding => finding.code === "RAW_EVIDENCE_INVALID")).toBe(true);
  });
  it("permits omitted identity on history pages but requires initial identity", async () => {
    const { raw } = await captured();
    delete raw.__pagination_evidence.pages[1].response.conversation_id;
    expect(validateConversationEvidence(raw, "conversation-1", "current").coverage.messages).toBe("current_branch");
    delete raw.__pagination_evidence.pages[0].response.conversation_id;
    expect(() => validateConversationEvidence(raw, "conversation-1", "current")).toThrow();
  });
  it.each(["single", "batch", "shared"] as const)("source %s cannot relabel even complete plural evidence", async source => {
    const { fs, raw, marker } = await captured();
    marker.retrievalSource = source;
    await replaceRaw(fs, marker, raw);
    expect(await new CaptureStore(fs, "resume", workspace.workspaceFingerprint).validRawMarker(item())).toBeUndefined();
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("incomplete");
  });
  it.each(["batch", "single", "shared"] as const)("valid legacy %s evidence remains supported and graph-qualified", source => {
    const raw = conversationDetail({ id: source === "shared" ? "provider-share" : "conversation-1" });
    expect(validateConversationEvidence(raw, "conversation-1", source).coverage).toEqual({ messages: "returned_provider_graph", providerGraph: "unknown", versions: "unknown" });
  });
  it.each(["batch", "single"] as const)("legacy %s derived files without coverage can skip and rebuild offline", async source => {
    const fs = await filesystem();
    await run(fs, transport(op => {
      if (source === "batch" && op.operation === "conversation_batch") return [conversationDetail()] as any;
      if (source === "single" && op.operation === "conversation_detail") return conversationDetail() as any;
      throw Object.assign(new Error("synthetic unavailable"), { status: 404 });
    }));
    const normalized = JSON.parse((await fs.readText(`${base}/conversation.json`))!);
    delete normalized.coverage;
    const text = prettyJson(normalized);
    await fs.writeTextAtomic(`${base}/conversation.json`, text);
    const completion = JSON.parse((await fs.readText(`${base}/complete.json`))!);
    completion.normalizedHash = await sha256Hex(text);
    await fs.writeTextAtomic(`${base}/complete.json`, prettyJson(completion));
    const offline = transport(() => { throw new Error("network forbidden"); });
    expect((await run(fs, offline, "skip-legacy")).skippedCount).toBe(1);
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("complete");
    await fs.remove(`${base}/conversation.md`);
    expect((await run(fs, offline, "rebuild-legacy")).rebuiltCount).toBe(1);
    expect(offline.request).not.toHaveBeenCalled();
  });
  it.each(["missing parent", "nonreciprocal edges", "cycle", "conflicting identity", "identity only"])("rejects malformed legacy graph: %s", kind => {
    const raw: any = conversationDetail();
    if (kind === "missing parent") raw.mapping["user-1"].parent = "absent";
    if (kind === "nonreciprocal edges") raw.mapping["root-1"].children = [];
    if (kind === "cycle") raw.mapping["root-1"].parent = "assistant-1";
    if (kind === "conflicting identity") raw.conversation_id = "other";
    if (kind === "identity only") delete raw.mapping;
    expect(() => validateConversationEvidence(raw, "conversation-1", "single")).toThrow();
  });
  it("independently checks retained conversations absent from current inventory", async () => {
    const { fs, raw, marker } = await captured();
    raw.__pagination_evidence.pages.pop(); raw.__pagination_evidence.page_count--;
    await replaceRaw(fs, marker, raw);
    const inventory = JSON.parse((await fs.readText("inventory.json"))!);
    inventory.absentConversations = inventory.conversations; inventory.conversations = [];
    await fs.writeTextAtomic("inventory.json", prettyJson(inventory));
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("incomplete");
  });
  it.each(["provider content", "derived text", "derived role"])("rejects normalized %s changed with refreshed completion hash", async kind => {
    const { fs } = await captured();
    const normalized = JSON.parse((await fs.readText(`${base}/conversation.json`))!);
    if (kind === "provider content") normalized.messages[0].extensions.chatgpt.content.parts = ["altered"];
    if (kind === "derived text") normalized.messages[0].parts[0].text = "altered";
    if (kind === "derived role") normalized.messages[0].role = "user";
    const text = prettyJson(normalized);
    await fs.writeTextAtomic(`${base}/conversation.json`, text);
    const completion = JSON.parse((await fs.readText(`${base}/complete.json`))!);
    completion.normalizedHash = await sha256Hex(text);
    await fs.writeTextAtomic(`${base}/complete.json`, prettyJson(completion));
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).findings.some(finding => finding.code === "GRAPH_CONTENT_MISMATCH")).toBe(true);
  });
});

describe("X2 authoritative inventory replay", () => {
  async function emptyInventory() {
    const fs = new MemoryArchiveFileSystem();
    const inventory = await new ChatGptInventoryEngine({ filesystem: fs, workspace,
      transport: transport(() => ({ items: [], total: 0 })), now: fixedNow,
      settings: { ...DEFAULT_INVENTORY_SETTINGS, includeArchived: false, includeProjects: false, includeShared: false } }).run();
    return { fs, inventory };
  }
  it("validates authoritative empty pages", async () => {
    const { fs, inventory } = await emptyInventory();
    expect(await validateInventoryEvidence(fs, inventory)).toEqual({ coverage: "page_receipts", errors: [] });
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("complete");
  });
  it.each(["delete file", "corrupt file", "delete page receipts", "invent conversation", "duplicate chain", "change request", "false terminal"])("resume and audit reject %s", async kind => {
    const { fs, inventory } = await emptyInventory();
    const path = (await fs.listPaths("source/inventory"))[0]!;
    if (kind === "delete file") await fs.remove(path);
    if (kind === "corrupt file") await fs.writeTextAtomic(path, "{}");
    if (kind === "delete page receipts") inventory.pages = [];
    if (kind === "invent conversation") inventory.conversations.push(item());
    if (kind === "duplicate chain") inventory.chains.push(inventory.chains[0]!);
    if (kind === "change request") inventory.pages[0]!.request.offset = 1;
    if (kind === "false terminal") inventory.pages[0]!.terminationReason = "cursor_exhausted";
    await fs.writeTextAtomic("inventory.json", prettyJson(inventory));
    const offline = transport(() => { throw new Error("network forbidden"); });
    await expect(run(fs, offline)).rejects.toThrow("Authoritative inventory evidence");
    expect(offline.request).not.toHaveBeenCalled();
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("incomplete");
  });
  it("replaying refreshed raw hashes still detects contradictory inventory summaries", async () => {
    const { fs, inventory } = await emptyInventory();
    const raw = { items: [{ id: "unlisted", title: "Synthetic" }], total: 1 };
    const page = inventory.pages[0]!;
    page.rawResponseHash = await hashJson(raw);
    const path = `source/inventory/main/main/page-000001-${page.rawResponseHash}.json`;
    await fs.writeTextAtomic(path, prettyJson(raw));
    await fs.writeTextAtomic("inventory.json", prettyJson(inventory));
    expect((await validateInventoryEvidence(fs, inventory)).errors).not.toEqual([]);
    expect((await auditArchive({ filesystem: fs, extensionVersion: "synthetic" })).terminalState).toBe("incomplete");
  });
  it("labels legacy inventory evidence as unverified", async () => {
    const { fs } = await captured();
    const report = await auditArchive({ filesystem: fs, extensionVersion: "synthetic" });
    expect(report.inventoryCoverage).toBe("legacy_unverified");
    expect(report.findings.some(finding => finding.code === "INVENTORY_LEGACY_UNVERIFIED")).toBe(true);
  });
  it.each(["valid", "cursor linkage", "project files", "missing project chain"])("replays project inventory: %s", async kind => {
    const fs = new MemoryArchiveFileSystem();
    const inventory = await new ChatGptInventoryEngine({ filesystem: fs, workspace, now: fixedNow,
      settings: { ...DEFAULT_INVENTORY_SETTINGS, includeArchived: false, includeShared: false },
      transport: transport(op => {
        if (op.operation === "conversation_page") return { items: [], total: 0 };
        if (op.operation === "project_page") return op.parameters.cursor === null
          ? { items: [{ gizmo: { gizmo: { id: "project-1" }, files: [{ id: "file-1", name: "Synthetic" }] } }], cursor: "next-project" }
          : { items: [{ gizmo: { gizmo: { id: "project-2" } } }], cursor: null };
        if (op.operation === "project_conversation_page") return { items: [], cursor: null };
        throw new Error("unexpected operation");
      }) }).run();
    expect(inventory.projects![0]!.files).toHaveLength(1);
    if (kind === "cursor linkage") inventory.pages.find(page => page.chainId === "project-index" && page.pageNumber === 2)!.request.cursor = "wrong";
    if (kind === "project files") inventory.projects![0]!.files = [];
    if (kind === "missing project chain") inventory.chains = inventory.chains.filter(chain => chain.projectId !== "project-1");
    const result = await validateInventoryEvidence(fs, inventory);
    if (kind === "valid") expect(result.errors).toEqual([]);
    else expect(result.errors.length).toBeGreaterThan(0);
  });
});

describe("X2 raw asset reference agreement", () => {
  it.each(["remove", "duplicate", "provider id", "message id", "descriptor", "inline hash", "status laundering"])("independent audit rejects %s after index hashes are refreshed", async kind => {
    const { fs } = await captured(true);
    await replaceAssets(fs, index => {
      if (kind === "remove") index.assets = [];
      if (kind === "duplicate") index.assets.push(index.assets[0]);
      if (kind === "provider id") index.assets[0].providerId = "other";
      if (kind === "message id") index.assets[0].sourceMessageId = "other";
      if (kind === "descriptor") index.assets[0].rawDescriptor = {};
      if (kind === "inline hash") index.assets[0].sha256 = "0".repeat(64);
      if (kind === "status laundering") { index.assets = []; index.status = "not_requested"; }
    });
    const report = await auditArchive({ filesystem: fs, extensionVersion: "synthetic" });
    expect(report.terminalState).toBe("conversations_complete_assets_partial");
    expect(report.findings.some(finding => finding.code.startsWith("ASSET_") && finding.severity === "error")).toBe(true);
  });
  it("resume rebuilds stripped logical references from raw without network", async () => {
    const { fs } = await captured(true);
    await replaceAssets(fs, index => { index.assets = []; });
    const offline = transport(() => { throw new Error("network forbidden"); });
    expect((await run(fs, offline, "resume", true)).rebuiltCount).toBe(1);
    expect(offline.request).not.toHaveBeenCalled();
    expect(JSON.parse((await fs.readText(`${base}/assets.json`))!).assets).toHaveLength(1);
  });
  it("audit checks import rows independently of per-conversation asset hashes", async () => {
    const { fs } = await captured(true);
    await fs.writeTextAtomic("indexes/assets.jsonl", "");
    const report = await auditArchive({ filesystem: fs, extensionVersion: "synthetic" });
    expect(report.findings.some(finding => finding.code === "ASSET_IMPORT_INDEX_MISMATCH")).toBe(true);
    expect(report.terminalState).not.toBe("complete");
  });
});
