import type { ArchiveFileSystem } from "../core/filesystem";
import { hashJson, sha256Hex } from "../core/hash";
import { isJsonObject } from "../core/json";
import { safePathSegment } from "../core/paths";
import { parseJson, prettyJson } from "../core/serialization";
import type { ConversationInventory, JsonObject, ScopeMembership } from "../core/types";
import { parseProject } from "./inventory";

export interface InventoryEvidenceResult {
  coverage: "page_receipts" | "legacy_unverified";
  errors: string[];
}

/** Read authoritative files and replay every declared chain, without trusting flags. */
export async function validateInventoryEvidence(filesystem: ArchiveFileSystem, inventory: ConversationInventory): Promise<InventoryEvidenceResult> {
  const errors: string[] = [];
  if (!Array.isArray(inventory.pages) || !Array.isArray(inventory.chains) || !Array.isArray(inventory.conversations)) {
    return { coverage: "page_receipts", errors: ["invalid inventory collections"] };
  }
  if (!inventory.pages.length && inventory.evidenceModel === undefined) {
    return { coverage: "legacy_unverified", errors };
  }
  const observed = new Map<string, { hashes: Set<string>; memberships: Set<string> }>();
  const projects = new Map<string, JsonObject>();
  const declaredChains = new Set<string>();
  for (const chain of inventory.chains) {
    if (chain.complete !== true) errors.push("declared incomplete chain");
    const key = `${chain.scope}/${chain.chainId}`;
    if (declaredChains.has(key)) errors.push(`duplicate chain: ${key}`);
    declaredChains.add(key);
    const pages = inventory.pages.filter(page => page.scope === chain.scope && page.chainId === chain.chainId);
    if (!pages.length || pages.length !== chain.pageCount) errors.push(`page count: ${key}`);
    let offset = 0;
    let cursor: string | null = chain.projectId ? "0" : null;
    const seenCursors = new Set<string>();
    const seenIds = new Set<string>();
    let itemCount = 0;
    for (const [index, page] of pages.entries()) {
      const path = `source/inventory/${safePathSegment(page.scope)}/${safePathSegment(page.chainId, "chain", 220)}/page-${String(page.pageNumber).padStart(6, "0")}-${page.rawResponseHash}.json`;
      const text = await filesystem.readText(path);
      const raw = parseJson<JsonObject>(text);
      if (!isJsonObject(raw) || await hashJson(raw) !== page.rawResponseHash || !Array.isArray(raw.items) || !raw.items.every(isJsonObject)) {
        errors.push(`missing or invalid authoritative page: ${path}`);
        continue;
      }
      const items = raw.items as JsonObject[];
      const ids: string[] = [];
      for (const [itemIndex, item] of items.entries()) {
        let id: string;
        try {
          id = chain.scope === "project" && !chain.projectId ? parseProject(item, itemIndex).projectId : String(item.id ?? "");
          if (!/^[A-Za-z0-9_-]{1,256}$/.test(id)) throw new Error("id");
        } catch {
          errors.push(`item identity: ${path}`);
          continue;
        }
        ids.push(id);
        if (chain.scope === "project" && !chain.projectId) {
          projects.set(id, item);
          continue;
        }
        const conversationId = chain.scope === "shared" ? typeof item.conversation_id === "string" ? item.conversation_id : `share_${id}` : id;
        const membership: ScopeMembership = { scope: chain.scope,
          ...(chain.projectId ? { projectId: chain.projectId } : {}), ...(chain.scope === "shared" ? { shareId: id } : {}) };
        const record = observed.get(conversationId) ?? { hashes: new Set<string>(), memberships: new Set<string>() };
        record.hashes.add(await hashJson(item));
        record.memberships.add(membershipKey(membership));
        observed.set(conversationId, record);
      }
      const duplicates = ids.filter(id => seenIds.has(id)).length;
      ids.forEach(id => seenIds.add(id));
      itemCount += items.length;
      if (page.pageNumber !== index + 1 || page.itemCount !== items.length || page.duplicateCount !== duplicates
        || page.orderedIdHash !== await sha256Hex(ids.join("\n"))) errors.push(`page summary: ${path}`);
      const final = index === pages.length - 1;
      let termination: string | null = null;
      if (chain.scope === "project") {
        const next = raw.cursor ?? null;
        if ((page.request.cursor ?? null) !== cursor || (page.request.projectId ?? undefined) !== chain.projectId
          || next !== page.nextCursor || (next !== null && (typeof next !== "string" || !next.length || next.length > 2_048 || /[\u0000-\u001f\u007f]/.test(next) || seenCursors.has(next)))
          || (!items.length && next !== null)) errors.push(`cursor linkage: ${path}`);
        if (next === null) termination = "cursor_exhausted";
        else if (typeof next === "string") seenCursors.add(next);
        cursor = typeof next === "string" ? next : null;
      } else {
        const total = raw.total ?? null;
        if (page.request.offset !== offset || !Number.isInteger(page.request.limit) || page.request.limit! < 1 || page.nextCursor !== null
          || (total !== null && (!Number.isInteger(total) || Number(total) < 0))) errors.push(`offset linkage: ${path}`);
        offset += items.length;
        if (!items.length) {
          if (total !== null && offset < Number(total)) errors.push(`premature empty: ${path}`);
          termination = total === 0 && offset === 0 ? "recognized_empty_account" : "empty_page";
        } else if (total !== null && offset >= Number(total)) termination = "declared_total_reached";
      }
      if (page.terminationReason !== termination || Boolean(termination) !== final || (final && chain.terminationReason !== termination)) errors.push(`termination: ${path}`);
    }
    if (chain.itemCount !== itemCount || chain.uniqueConversationCount !== (chain.scope === "project" && !chain.projectId ? 0 : seenIds.size)) errors.push(`chain summary: ${key}`);
  }
  for (const page of inventory.pages) if (!declaredChains.has(`${page.scope}/${page.chainId}`)) errors.push("undeclared page chain");
  if (!declaredChains.size) errors.push("missing inventory chains");
  const expectedIds = inventory.conversations.map(item => item.conversationId).sort();
  if (prettyJson(expectedIds) !== prettyJson([...observed.keys()].sort())) errors.push("inventory conversation set differs from raw pages");
  for (const conversation of inventory.conversations) {
    const record = observed.get(conversation.conversationId);
    if (conversation.logicalKey !== `${inventory.workspaceFingerprint}/${conversation.conversationId}` || !record
      || prettyJson([...record.hashes].sort()) !== prettyJson([...conversation.listingHashes].sort())
      || prettyJson([...record.memberships].sort()) !== prettyJson(conversation.memberships.map(membershipKey).sort())) errors.push(`conversation evidence: ${conversation.conversationId}`);
  }
  if (prettyJson((inventory.projects ?? []).map(project => project.projectId).sort()) !== prettyJson([...projects.keys()].sort())) errors.push("project set differs from raw pages");
  for (const project of inventory.projects ?? []) {
    const raw = projects.get(project.projectId);
    if (!raw) continue;
    const expected = { ...parseProject(raw, 0), rawHash: await hashJson(raw) };
    if (prettyJson(project) !== prettyJson(expected)) errors.push(`project evidence: ${project.projectId}`);
    if (!inventory.chains.some(chain => chain.projectId === project.projectId)) errors.push(`missing project conversation chain: ${project.projectId}`);
  }
  return { coverage: "page_receipts", errors };
}

function membershipKey(membership: ScopeMembership): string {
  return `${membership.scope}/${membership.projectId ?? ""}/${membership.shareId ?? ""}`;
}
