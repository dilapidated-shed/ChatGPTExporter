import { validateConversationEvidence } from "./evidence";
import { normalizeConversation, NORMALIZER_VERSION } from "./normalize";
import { renderConversationMarkdown } from "../core/markdown";
import { conversationBasePath } from "../core/paths";
import { prettyJson } from "../core/serialization";
import type { InventoryConversation, JsonValue } from "../core/types";

/**
 * Projection of an explicitly supplied file. This is NOT a ChatGPT web capture:
 * there is no session, inventory enumeration, provider snapshot or asset fetch.
 */
export interface OfflineProjection {
  conversationId: string;
  directory: string;
  raw: string;
  normalized: string;
  markdown: string;
}

function objectsFromSuppliedSource(value: unknown): unknown[] {
  if (Array.isArray(value)) return value;
  if (value && typeof value === "object" && !Array.isArray(value)) {
    const record = value as Record<string, unknown>;
    if (Array.isArray(record.conversations)) return record.conversations;
    if (record.mapping && typeof record.mapping === "object") return [value];
  }
  throw new Error("Offline source must be a conversation array, one conversation, or an object containing a conversations array.");
}

export function projectOfflineConversations(value: unknown, inputSha256: string): OfflineProjection[] {
  if (!/^[0-9a-f]{64}$/.test(inputSha256)) throw new Error("Source digest is invalid.");
  const workspaceFingerprint = "offline-" + inputSha256.slice(0, 24);
  const output: OfflineProjection[] = [];
  const seenIds = new Set<string>();
  const seenPaths = new Set<string>();

  for (const raw of objectsFromSuppliedSource(value)) {
    if (!raw || typeof raw !== "object" || Array.isArray(raw)) throw new Error("Offline conversation must be an object.");
    const source = raw as Record<string, unknown>;
    const id = source.id ?? source.conversation_id;
    if (typeof id !== "string" || !/^[A-Za-z0-9_-]{1,256}$/.test(id)) {
      throw new Error("Offline conversation has an invalid identifier.");
    }
    const directory = conversationBasePath(id);
    if (seenIds.has(id) || seenPaths.has(directory.toLowerCase())) {
      throw new Error("Offline source contains duplicate or colliding conversation IDs.");
    }
    seenIds.add(id);
    seenPaths.add(directory.toLowerCase());

    // Reuse the same fail-closed graph and identity validator as live archives.
    // The single-source validator has no network effects. Plural page evidence
    // is not silently recast as an official-export conversation.
    const { detail } = validateConversationEvidence(raw, id, "single");
    const inventory: InventoryConversation = {
      logicalKey: workspaceFingerprint + "/" + id,
      conversationId: id,
      title: detail.title,
      createTime: detail.create_time,
      updateTime: detail.update_time,
      memberships: [], // No fabricated main/archive/project scope.
      listingHashes: [], // There was no ChatGPT listing request.
    };
    const normalized = normalizeConversation(detail, inventory, workspaceFingerprint);
    if (normalized.normalizerVersion !== NORMALIZER_VERSION
      || normalized.findings.some(finding => finding.severity === "error")) {
      throw new Error("Source graph could not be normalized without errors.");
    }
    output.push({
      conversationId: id,
      directory,
      raw: prettyJson(raw as JsonValue),
      normalized: prettyJson(normalized),
      markdown: renderConversationMarkdown(normalized),
    });
  }
  return output.sort((left, right) => left.conversationId.localeCompare(right.conversationId));
}
