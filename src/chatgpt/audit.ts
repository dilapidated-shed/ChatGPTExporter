import type { ConversationCompletionMarker } from "./capture-engine";
import type { RawCompletionMarker } from "../core/capture-store";
import type { ArchiveFileSystem } from "../core/filesystem";
import { sha256Hex } from "../core/hash";
import { conversationBasePath, assertSafeRelativePath } from "../core/paths";
import { IncrementalSha256 } from "../core/sha256-stream";
import { parseJson, prettyJson } from "../core/serialization";
import { validateConversationEvidence, type ConversationCoverage, type ValidatedConversationEvidence } from "./evidence";
import { validateInventoryEvidence } from "./inventory-evidence";
import { assetReferenceFindings } from "./assets";
import { normalizeConversation, linkNormalizedAssets } from "./normalize";
import type {
  ArchiveManifest,
  AssetRecord,
  ConversationAssetIndex,
  ConversationInventory,
  NormalizedConversation,
  ProjectAssetIndex,
  InventoryConversation,
} from "../core/types";

export interface ArchiveAuditFinding {
  severity: "warning" | "error";
  code: string;
  message: string;
  path?: string;
}

export interface ArchiveAuditReport {
  schemaVersion: 1;
  provider: "chatgpt-web";
  workspaceFingerprint: string;
  auditedAt: string;
  terminalState: "complete" | "conversations_complete_assets_partial" | "incomplete";
  expectedConversationCount: number;
  completeConversationCount: number;
  extraRetainedConversationCount: number;
  projectCount: number;
  logicalAssetReferenceCount: number;
  physicalAssetCount: number;
  partialAssetReferenceCount: number;
  archiveBytes: number;
  assetBytes: number;
  inventorySetHash: string;
  completionSetHash: string;
  normalizedSetHash: string;
  conversationsIndexHash: string;
  assetsIndexHash: string;
  inventoryCoverage: "page_receipts" | "legacy_unverified";
  conversationCoverage: Array<{ conversationId: string; coverage: ConversationCoverage }>;
  providerGraphCoverage: "unknown";
  versionCoverage: "unknown";
  findings: ArchiveAuditFinding[];
}

export async function auditArchive(options: {
  filesystem: ArchiveFileSystem;
  extensionVersion: string;
  now?: () => Date;
}): Promise<ArchiveAuditReport> {
  const { filesystem } = options;
  const inventory = parseJson<ConversationInventory>(await filesystem.readText("inventory.json"));
  if (!inventory || inventory.schemaVersion !== 1 || inventory.provider !== "chatgpt-web" || !inventory.complete) {
    throw new Error("A complete ChatGPT inventory is required for archive audit.");
  }
  const findings: ArchiveAuditFinding[] = [];
  const inventoryEvidence = await validateInventoryEvidence(filesystem, inventory);
  for (const message of inventoryEvidence.errors) findings.push(error("INVENTORY_EVIDENCE_INVALID", message, "inventory.json"));
  if (inventoryEvidence.coverage === "legacy_unverified") findings.push({ severity: "warning", code: "INVENTORY_LEGACY_UNVERIFIED", message: "Legacy inventory has no page receipts; authoritative listing coverage is unverified." });
  const conversationCoverage: ArchiveAuditReport["conversationCoverage"] = [];
  const allPaths = await filesystem.listPaths();
  const completionPaths = allPaths.filter((path) => /^conversations\/[^/]+\/complete\.json$/.test(path));
  const normalizedPaths = allPaths.filter((path) => /^conversations\/[^/]+\/conversation\.json$/.test(path));
  const expectedIds = inventory.conversations.map((item) => item.conversationId).sort();
  const expectedPathById = new Map(inventory.conversations.map((item) => [item.conversationId, `${conversationBasePath(item.conversationId)}/complete.json`]));
  const completionIds: string[] = [];
  const normalizedIds: string[] = [];
  const conversationRows: Array<Record<string, unknown>> = [];
  const assetRows: Array<Record<string, unknown>> = [];
  let partialAssetReferenceCount = 0;
  let logicalAssetReferenceCount = 0;

  for (const conversation of inventory.conversations) {
    const findingStart = findings.length;
    const base = conversationBasePath(conversation.conversationId);
    const markerPath = `${base}/complete.json`;
    const marker = parseJson<ConversationCompletionMarker>(await filesystem.readText(markerPath));
    if (!marker) {
      findings.push(error("CONVERSATION_COMPLETION_MISSING", "Expected conversation has no readable completion marker.", markerPath));
      continue;
    }
    const rawMarkerPath = `${base}/raw-complete.json`;
    const normalizedPath = `${base}/conversation.json`;
    const markdownPath = `${base}/conversation.md`;
    const assetsPath = `${base}/assets.json`;
    const rawMarkerText = await filesystem.readText(rawMarkerPath);
    const normalizedText = await filesystem.readText(normalizedPath);
    const markdownText = await filesystem.readText(markdownPath);
    const assetsText = await filesystem.readText(assetsPath);
    const identitiesMatch = marker.schemaVersion === 1
      && marker.provider === "chatgpt-web"
      && marker.logicalKey === conversation.logicalKey
      && marker.conversationId === conversation.conversationId
      && marker.workspaceFingerprint === inventory.workspaceFingerprint;
    if (!identitiesMatch) findings.push(error("CONVERSATION_COMPLETION_IDENTITY", "Completion marker identity does not match inventory.", markerPath));
    await verifyTextHash(rawMarkerText, marker.rawMarkerHash, rawMarkerPath, findings);
    await verifyTextHash(normalizedText, marker.normalizedHash, normalizedPath, findings);
    await verifyTextHash(markdownText, marker.markdownHash, markdownPath, findings);
    await verifyTextHash(assetsText, marker.assetsHash, assetsPath, findings);
    if (normalizedText !== undefined) normalizedIds.push(conversation.conversationId);

    const rawMarker = parseJson<RawCompletionMarker>(rawMarkerText);
    const normalized = parseJson<NormalizedConversation>(normalizedText);
    let evidence: ValidatedConversationEvidence | undefined;
    if (!rawMarker || !isNormalizedConversation(normalized)) {
      findings.push(error("CONVERSATION_DERIVED_INVALID", "Raw completion or normalized conversation JSON is invalid.", base));
    } else {
      if (marker.detailHash !== rawMarker.detailHash) findings.push(error("RAW_COMPLETION_DETAIL_MISMATCH", "Completion detail hash disagrees with raw marker.", markerPath));
      evidence = await verifyRawGraph(filesystem, rawMarker, normalized, findings, conversation, inventory.workspaceFingerprint);
      if (evidence) conversationCoverage.push({ conversationId: conversation.conversationId, coverage: evidence.coverage });
      conversationRows.push({
        logicalKey: conversation.logicalKey,
        conversationId: conversation.conversationId,
        title: normalized.title,
        createTime: normalized.createTime,
        updateTime: normalized.updateTime,
        memberships: normalized.memberships,
        normalizedPath,
        rawPath: rawMarker.detailPath,
        normalizedHash: marker.normalizedHash,
        assetStatus: marker.assetStatus,
        ...(evidence ? { coverage: evidence.coverage } : {}),
      });
    }
    const assets = parseJson<ConversationAssetIndex>(assetsText);
    if (!assets || !Array.isArray(assets.assets)) findings.push(error("ASSET_INDEX_INVALID", "Conversation asset index is invalid.", assetsPath));
    else {
      logicalAssetReferenceCount += assets.assets.length;
      partialAssetReferenceCount += assets.assets.filter((asset) => asset.status === "failed").length;
      await verifyAssets(filesystem, assets.assets, findings);
      if (evidence) await verifyAssetReferences(evidence, assets, conversation.conversationId, findings);
      for (const asset of assets.assets) assetRows.push({ logicalKey: conversation.logicalKey, conversationId: conversation.conversationId, ...asset });
    }
    if (!findings.slice(findingStart).some(isConversationError)) completionIds.push(conversation.conversationId);
  }

  for (const project of inventory.projects ?? []) {
    const path = `projects/${project.projectId}/assets.json`;
    const index = parseJson<ProjectAssetIndex>(await filesystem.readText(path));
    if (!index || !Array.isArray(index.assets)) {
      findings.push(error("PROJECT_ASSET_INDEX_MISSING", "Inventoried project has no readable asset index.", path));
      continue;
    }
    logicalAssetReferenceCount += index.assets.length;
    partialAssetReferenceCount += index.assets.filter((asset) => asset.status === "failed").length;
    await verifyAssets(filesystem, index.assets, findings);
    for (const asset of index.assets) assetRows.push({ logicalKey: `${inventory.workspaceFingerprint}/project/${project.projectId}`, projectId: project.projectId, ...asset });
    if (index.schemaVersion !== 1 || index.projectId !== project.projectId) findings.push(error("PROJECT_ASSET_IDENTITY", "Project asset index identity is invalid.", path));
    if (index.status !== "not_requested") {
      const expected = project.files.map(file => `${file.logicalId}/${file.providerId}`).sort();
      const actual = index.assets.map(asset => `${asset.logicalId}/${asset.providerId}`).sort();
      if (!sameSet(expected, actual)) findings.push(error("PROJECT_ASSET_REFERENCE_MISMATCH", "Project descriptors and asset references differ.", path));
    } else if (project.files.length) {
      findings.push(error("PROJECT_ASSET_NOT_CAPTURED", "Raw project asset references were not requested.", path));
    }
  }

  for (const path of completionPaths) {
    if (![...expectedPathById.values()].includes(path)) {
      const marker = parseJson<ConversationCompletionMarker>(await filesystem.readText(path));
      if (!marker?.conversationId) {
        findings.push(error("RETAINED_COMPLETION_INVALID", "Retained completion marker is not readable.", path));
        continue;
      }
      const base = path.replace(/\/complete\.json$/, "");
      const normalizedPath = `${base}/conversation.json`;
      const rawMarkerPath = `${base}/raw-complete.json`;
      const normalizedText = await filesystem.readText(normalizedPath);
      const rawMarkerText = await filesystem.readText(rawMarkerPath);
      const assetsText = await filesystem.readText(`${base}/assets.json`);
      await verifyTextHash(rawMarkerText, marker.rawMarkerHash, rawMarkerPath, findings);
      await verifyTextHash(normalizedText, marker.normalizedHash, normalizedPath, findings);
      await verifyTextHash(assetsText, marker.assetsHash, `${base}/assets.json`, findings);
      await verifyTextHash(await filesystem.readText(`${base}/conversation.md`), marker.markdownHash, `${base}/conversation.md`, findings);
      const normalized = parseJson<NormalizedConversation>(normalizedText);
      const rawMarker = parseJson<RawCompletionMarker>(rawMarkerText);
      if (!isNormalizedConversation(normalized) || !rawMarker) {
        findings.push(error("RETAINED_CONVERSATION_INVALID", "Retained conversation raw or normalized record is invalid.", base));
        continue;
      }
      if (marker.schemaVersion !== 1 || marker.provider !== "chatgpt-web" || marker.workspaceFingerprint !== inventory.workspaceFingerprint
        || marker.conversationId !== rawMarker.conversationId || marker.logicalKey !== rawMarker.logicalKey
        || marker.detailHash !== rawMarker.detailHash
        || base !== conversationBasePath(marker.conversationId)) findings.push(error("RETAINED_COMPLETION_IDENTITY", "Retained marker identity disagrees with its archive location or raw marker.", path));
      const evidence = await verifyRawGraph(filesystem, rawMarker, normalized, findings, undefined, inventory.workspaceFingerprint);
      if (evidence) conversationCoverage.push({ conversationId: marker.conversationId, coverage: evidence.coverage });
      conversationRows.push({
        logicalKey: marker.logicalKey,
        conversationId: marker.conversationId,
        title: normalized.title,
        createTime: normalized.createTime,
        updateTime: normalized.updateTime,
        memberships: normalized.memberships,
        normalizedPath,
        rawPath: rawMarker.detailPath,
        normalizedHash: marker.normalizedHash,
        assetStatus: marker.assetStatus,
        absentFromCurrentInventory: true,
        ...(evidence ? { coverage: evidence.coverage } : {}),
      });
      const assets = parseJson<ConversationAssetIndex>(assetsText);
      if (assets && Array.isArray(assets.assets)) {
        logicalAssetReferenceCount += assets.assets.length;
        partialAssetReferenceCount += assets.assets.filter((asset) => asset.status === "failed").length;
        await verifyAssets(filesystem, assets.assets, findings);
        if (evidence) await verifyAssetReferences(evidence, assets, marker.conversationId, findings);
        for (const asset of assets.assets) assetRows.push({ logicalKey: marker.logicalKey, conversationId: marker.conversationId, ...asset });
      } else {
        findings.push(error("ASSET_INDEX_INVALID", "Retained conversation asset index is invalid.", `${base}/assets.json`));
      }
    }
  }
  for (const path of allPaths.filter((candidate) => candidate.startsWith("staging/") || candidate.endsWith(".part"))) {
    findings.push(error("TEMPORARY_FILE_REMAINS", "A temporary or partial file remains in the archive.", path));
  }

  conversationRows.sort((left, right) => String(left.logicalKey).localeCompare(String(right.logicalKey)));
  const conversationsIndexText = jsonl(conversationRows);
  await filesystem.writeTextAtomic("indexes/conversations.jsonl", conversationsIndexText);
  const assetsIndexText = await filesystem.readText("indexes/assets.jsonl") ?? "";
  try {
    const storedRows = assetsIndexText.split("\n").filter(Boolean).map(line => JSON.parse(line) as Record<string, unknown>);
    const canonicalRows = (rows: Array<Record<string, unknown>>) => rows.map(row => prettyJson(row)).sort();
    if (prettyJson(canonicalRows(storedRows)) !== prettyJson(canonicalRows(assetRows))) findings.push(error("ASSET_IMPORT_INDEX_MISMATCH", "Asset import index differs from validated logical references.", "indexes/assets.jsonl"));
  } catch {
    findings.push(error("ASSET_IMPORT_INDEX_INVALID", "Asset import index is not readable JSONL.", "indexes/assets.jsonl"));
  }
  const measuredPaths = await filesystem.listPaths();
  const physicalAssetPaths = measuredPaths.filter((path) => path.startsWith("assets/"));
  let archiveBytes = 0;
  let assetBytes = 0;
  for (const path of measuredPaths) {
    const size = await byteSize(filesystem, path);
    archiveBytes += size;
    if (path.startsWith("assets/")) assetBytes += size;
  }
  const expectedSet = unique(expectedIds);
  const completionSet = unique(completionIds.filter((id) => expectedIds.includes(id)));
  const normalizedSet = unique(normalizedIds);
  if (!sameSet(expectedSet, completionSet)) findings.push(error("COMPLETION_SET_MISMATCH", "Inventory and completion-marker sets differ."));
  if (!sameSet(expectedSet, normalizedSet)) findings.push(error("NORMALIZED_SET_MISMATCH", "Inventory and normalized-conversation sets differ."));

  const conversationErrors = findings.filter(isConversationError);
  const terminalState = conversationErrors.length > 0
    ? "incomplete" as const
    : partialAssetReferenceCount > 0 || findings.some((finding) => finding.severity === "error")
      ? "conversations_complete_assets_partial" as const
      : "complete" as const;
  const report: ArchiveAuditReport = {
    schemaVersion: 1,
    provider: "chatgpt-web",
    workspaceFingerprint: inventory.workspaceFingerprint,
    auditedAt: (options.now ?? (() => new Date()))().toISOString(),
    terminalState,
    expectedConversationCount: expectedSet.length,
    completeConversationCount: completionSet.length,
    extraRetainedConversationCount: Math.max(0, completionPaths.length - completionSet.length),
    projectCount: inventory.projects?.length ?? 0,
    logicalAssetReferenceCount,
    physicalAssetCount: physicalAssetPaths.length,
    partialAssetReferenceCount,
    archiveBytes,
    assetBytes,
    inventorySetHash: await setHash(expectedSet),
    completionSetHash: await setHash(completionSet),
    normalizedSetHash: await setHash(normalizedSet),
    conversationsIndexHash: await sha256Hex(conversationsIndexText),
    assetsIndexHash: await sha256Hex(assetsIndexText),
    inventoryCoverage: inventoryEvidence.coverage,
    conversationCoverage,
    providerGraphCoverage: "unknown",
    versionCoverage: "unknown",
    findings,
  };
  await filesystem.writeTextAtomic("reports/validation.json", prettyJson(report));
  await filesystem.writeTextAtomic("reports/validation.md", renderValidation(report));
  const previousManifest = parseJson<ArchiveManifest>(await filesystem.readText("archive.json"));
  const manifest: ArchiveManifest = {
    schemaVersion: 1,
    provider: "chatgpt-web",
    workspaceFingerprint: inventory.workspaceFingerprint,
    selectedScopes: unique(inventory.chains.map((chain) => chain.scope)),
    extensionVersion: options.extensionVersion,
    normalizerVersion: conversationRows.length ? String((parseJson<NormalizedConversation>(await filesystem.readText(String(conversationRows[0]!.normalizedPath)))?.normalizerVersion) ?? "unknown") : "unknown",
    createdAt: previousManifest?.createdAt ?? report.auditedAt,
    updatedAt: report.auditedAt,
    runIds: unique((await filesystem.listPaths("runs")).map((path) => path.split("/").at(-1)?.replace(/\.json$/, "") ?? "").filter(Boolean)),
    currentIndexHashes: {
      conversations: report.conversationsIndexHash,
      assets: report.assetsIndexHash,
      inventorySet: report.inventorySetHash,
      completionSet: report.completionSetHash,
    },
  };
  await filesystem.writeTextAtomic("archive.json", prettyJson(manifest));
  return report;
}

async function verifyRawGraph(
  filesystem: ArchiveFileSystem,
  rawMarker: RawCompletionMarker,
  normalized: NormalizedConversation,
  findings: ArchiveAuditFinding[],
  conversation?: InventoryConversation,
  workspaceFingerprint?: string,
): Promise<ValidatedConversationEvidence | undefined> {
  const rawText = await filesystem.readText(rawMarker.detailPath);
  if (rawText === undefined || await sha256Hex(rawText) !== rawMarker.detailHash) {
    findings.push(error("RAW_DETAIL_HASH_MISMATCH", "Raw conversation detail is missing or does not match its marker.", rawMarker.detailPath));
    return;
  }
  if (rawMarker.schemaVersion !== 1 || rawMarker.provider !== "chatgpt-web"
    || !Array.isArray(rawMarker.listingHashes)
    || rawMarker.conversationId !== normalized.conversationId || rawMarker.logicalKey !== normalized.logicalKey
    || rawMarker.workspaceFingerprint !== workspaceFingerprint || normalized.workspaceFingerprint !== workspaceFingerprint
    || (conversation && (rawMarker.conversationId !== conversation.conversationId || rawMarker.logicalKey !== conversation.logicalKey
      || !sameSet([...rawMarker.listingHashes].sort(), [...conversation.listingHashes].sort())))) {
    findings.push(error("RAW_MARKER_IDENTITY", "Raw marker identity or listing evidence disagrees with inventory/normalized identity.", rawMarker.detailPath));
    return;
  }
  let evidence: ValidatedConversationEvidence;
  try {
    evidence = validateConversationEvidence(JSON.parse(rawText), rawMarker.conversationId, rawMarker.retrievalSource);
  } catch {
    findings.push(error("RAW_EVIDENCE_INVALID", "Conversation evidence cannot reconstruct a complete supported retrieval.", rawMarker.detailPath));
    return;
  }
  if ((rawMarker.batchHash === null) !== (rawMarker.batchPath === null)) findings.push(error("RAW_BATCH_INVALID", "Batch path/hash pair is inconsistent.", rawMarker.detailPath));
  if (rawMarker.batchHash && rawMarker.batchPath) await verifyTextHash(await filesystem.readText(rawMarker.batchPath), rawMarker.batchHash, rawMarker.batchPath, findings);
  const raw = evidence.detail;
  const rawNodeIds = Object.keys(raw.mapping).sort();
  const normalizedNodeIds = normalized.nodes.map((node) => node.id).sort();
  if (!sameSet(rawNodeIds, normalizedNodeIds)) findings.push(error("GRAPH_NODE_SET_MISMATCH", "Raw and normalized graph-node sets differ.", rawMarker.detailPath));
  const rawMessageIds = Object.values(raw.mapping).flatMap((node) => typeof node?.message?.id === "string" ? [node.message.id] : []).sort();
  const normalizedMessageIds = normalized.messages.map((message) => message.id).sort();
  if (!sameSet(rawMessageIds, normalizedMessageIds)) findings.push(error("GRAPH_MESSAGE_SET_MISMATCH", "Raw and normalized message sets differ.", rawMarker.detailPath));
  const sourceInventory = conversation ?? {
    logicalKey: rawMarker.logicalKey, conversationId: rawMarker.conversationId, listingHashes: rawMarker.listingHashes,
    title: normalized.title, createTime: normalized.createTime, updateTime: normalized.updateTime, memberships: normalized.memberships,
  };
  const rebuilt = normalizeConversation(raw, sourceInventory, rawMarker.workspaceFingerprint);
  const assets = parseJson<ConversationAssetIndex>(await filesystem.readText(`${conversationBasePath(rawMarker.conversationId)}/assets.json`));
  if (assets && Array.isArray(assets.assets)) linkNormalizedAssets(rebuilt, assets.assets);
  // Valid legacy derived files predate explicit coverage fields.
  if (normalized.coverage === undefined) delete rebuilt.coverage;
  const assetReferencesValid = assets && Array.isArray(assets.assets)
    && !(await assetReferenceFindings(raw, assets, rawMarker.conversationId)).length;
  // Broken asset indexes must not turn intact message content into graph failure.
  const comparable = (value: NormalizedConversation) => assetReferencesValid ? value : {
    ...value, messages: value.messages.map(message => ({ ...message, parts: message.parts.map(({ assetPath: _assetPath, ...part }) => part) })),
  };
  if (prettyJson(comparable(rebuilt)) !== prettyJson(comparable(normalized))) findings.push(error("GRAPH_CONTENT_MISMATCH", "Normalized content differs from independent raw reconstruction.", rawMarker.detailPath));
  if (normalized.coverage && prettyJson(normalized.coverage) !== prettyJson(evidence.coverage)) findings.push(error("GRAPH_COVERAGE_MISMATCH", "Normalized coverage disagrees with retained evidence.", rawMarker.detailPath));
  return evidence;
}

async function verifyAssetReferences(evidence: ValidatedConversationEvidence, index: ConversationAssetIndex, conversationId: string, findings: ArchiveAuditFinding[]): Promise<void> {
  for (const finding of await assetReferenceFindings(evidence.detail, index, conversationId)) findings.push(error(finding.code, finding.message));
}

async function verifyAssets(filesystem: ArchiveFileSystem, assets: AssetRecord[], findings: ArchiveAuditFinding[]): Promise<void> {
  for (const asset of assets) {
    if (asset.status === "failed") continue;
    if (asset.status === "not_requested") continue;
    if (asset.status !== "complete" || !asset.relativePath || !asset.sha256 || asset.byteSize === null) {
      findings.push(error("ASSET_RECORD_INCOMPLETE", "Completed asset record lacks a path, hash, or byte size."));
      continue;
    }
    const path = asset.relativePath.replace(/^\.\.\/\.\.\//, "");
    let safe = true;
    try { assertSafeRelativePath(path); } catch { safe = false; }
    if (!safe || !path.startsWith("assets/")) {
      findings.push(error("ASSET_PATH_INVALID", "Asset path does not resolve inside the archive asset store."));
      continue;
    }
    const actual = await hashAndSize(filesystem, path).catch(() => null);
    if (!actual || actual.sha256 !== asset.sha256 || actual.byteSize !== asset.byteSize) {
      findings.push(error("ASSET_HASH_MISMATCH", "Asset bytes do not match the logical reference.", path));
    } else if (actual.byteSize === 0) {
      findings.push(error("ASSET_ZERO_BYTES", "Downloaded asset is empty.", path));
    }
  }
}

async function verifyTextHash(value: string | undefined, expected: string, path: string, findings: ArchiveAuditFinding[]): Promise<void> {
  if (value === undefined || await sha256Hex(value) !== expected) findings.push(error("DERIVED_HASH_MISMATCH", "Required archive text is missing or has the wrong hash.", path));
}

async function hashAndSize(filesystem: ArchiveFileSystem, path: string): Promise<{ sha256: string; byteSize: number }> {
  const hash = new IncrementalSha256();
  let byteSize = 0;
  for await (const chunk of filesystem.readByteChunks(path)) {
    hash.update(chunk);
    byteSize += chunk.byteLength;
  }
  return { sha256: hash.digestHex(), byteSize };
}

async function byteSize(filesystem: ArchiveFileSystem, path: string): Promise<number> {
  let size = 0;
  for await (const chunk of filesystem.readByteChunks(path)) size += chunk.byteLength;
  return size;
}

function error(code: string, message: string, path?: string): ArchiveAuditFinding {
  return { severity: "error", code, message, ...(path === undefined ? {} : { path }) };
}

function isConversationError(finding: ArchiveAuditFinding): boolean {
  return finding.severity === "error" && !finding.code.startsWith("ASSET_") && !finding.code.startsWith("PROJECT_ASSET_");
}

function isNormalizedConversation(value: NormalizedConversation | undefined): value is NormalizedConversation {
  return value?.schemaVersion === 1
    && value.provider === "chatgpt-web"
    && Array.isArray(value.nodes)
    && Array.isArray(value.messages)
    && Array.isArray(value.memberships);
}

function unique<T>(values: T[]): T[] {
  return [...new Set(values)].sort((left, right) => String(left).localeCompare(String(right)));
}

function sameSet(left: string[], right: string[]): boolean {
  return left.length === right.length && left.every((value, index) => value === right[index]);
}

function setHash(values: string[]): Promise<string> {
  return sha256Hex(values.join("\n"));
}

function jsonl(rows: Array<Record<string, unknown>>): string {
  return rows.map((row) => JSON.stringify(row)).join("\n") + (rows.length ? "\n" : "");
}

function renderValidation(report: ArchiveAuditReport): string {
  const lines = [
    "# ChatGPTExporter validation",
    "",
    `Terminal state: **${report.terminalState}**`,
    `Inventory evidence: **${report.inventoryCoverage}**`,
    "Provider graph coverage: **unknown**; version coverage: **unknown**.",
    "Completeness applies to the validated returned graph or current branch, not an exhaustive provider export.",
    "",
    `- Expected conversations: ${report.expectedConversationCount}`,
    `- Complete conversations: ${report.completeConversationCount}`,
    `- Retained conversations absent from current inventory: ${report.extraRetainedConversationCount}`,
    `- Projects: ${report.projectCount}`,
    `- Logical asset references: ${report.logicalAssetReferenceCount}`,
    `- Partial asset references: ${report.partialAssetReferenceCount}`,
    `- Physical assets: ${report.physicalAssetCount}`,
    `- Archive bytes audited: ${report.archiveBytes}`,
    `- Asset bytes audited: ${report.assetBytes}`,
    ...report.conversationCoverage.map(item => `- ${item.conversationId} message coverage: ${item.coverage.messages}`),
    "",
    "## Set hashes",
    "",
    `- Inventory: \`${report.inventorySetHash}\``,
    `- Completion markers: \`${report.completionSetHash}\``,
    `- Normalized conversations: \`${report.normalizedSetHash}\``,
    "",
    "## Findings",
    "",
    ...(report.findings.length ? report.findings.map((finding) => `- ${finding.severity.toUpperCase()} ${finding.code}: ${finding.message}${finding.path ? ` (\`${finding.path}\`)` : ""}`) : ["- None."]),
    "",
  ];
  return lines.join("\n");
}
