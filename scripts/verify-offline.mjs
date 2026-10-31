#!/usr/bin/env node
/** Reconstruct acceptance from disk and supplied raw input; never trust flags. */
import { createReadStream } from "node:fs";
import { lstat, mkdtemp, readFile, rm } from "node:fs/promises";
import { createInterface } from "node:readline";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { conversationRecords, hashFile, json, sha256, sourceMembers } from "./offline-stream.mjs";
import { importCore } from "./import-offline.mjs";

export async function verifyOfflineArchive(root, expectedSourceHash) {
  root = path.resolve(root);
  const privateState = state => !state.isSymbolicLink() && !(state.mode & 0o077)
    && (typeof process.getuid !== "function" || state.uid === process.getuid());
  const rootState = await lstat(root);
  if (!rootState.isDirectory() || !privateState(rootState)) throw new Error("Archive root is not a private directory.");
  async function file(relative, cap = Number.MAX_SAFE_INTEGER) {
    if (typeof relative !== "string" || !relative || relative.includes("\\") || relative.split("/").some(p => !p || p === "." || p === "..")) throw new Error("Unsafe archive path.");
    const parts = relative.split("/"); let current = root;
    for (const [i, part] of parts.entries()) {
      current = path.join(current, part); const state = await lstat(current);
      if (!privateState(state) || (i === parts.length - 1 ? !state.isFile() || state.size > cap : !state.isDirectory())) throw new Error("Archive file mode, size or path refused.");
    }
    return current;
  }
  const receipt = json(await readFile(await file("reports/offline-import.json", 4 * 1024 * 1024)));
  if (![1, 2].includes(receipt.schemaVersion) || receipt.provider !== "chatgpt-offline-file" || !Array.isArray(receipt.members)
    || receipt.liveExporterCompletion !== false || receipt.chatgptSessionUsed !== false || receipt.chatgptNetworkCalls !== 0
    || receipt.scope !== "only_records_in_supplied_file" || receipt.providerSnapshot !== "unknown" || receipt.assetCoverage !== "not_imported") throw new Error("Invalid offline source contract.");
  const sourcePath = receipt.sourcePath ?? (await lstat(path.join(root, "source/input.zip")).then(() => "source/input.zip").catch(() => "source/input.json"));
  if (!["source/input.json", "source/input.zip"].includes(sourcePath)) throw new Error("Invalid source evidence path.");
  const input = await file(sourcePath), actual = await hashFile(input);
  if (actual.sha256 !== receipt.sourceSha256 || actual.byteSize !== receipt.sourceSize || expectedSourceHash && actual.sha256 !== expectedSourceHash) throw new Error("Source evidence hash or size mismatch.");
  const indexPath = await file("indexes/conversations.jsonl"), indexHash = await hashFile(indexPath);
  if (receipt.schemaVersion === 2 && receipt.indexSha256 !== indexHash.sha256) throw new Error("Index hash mismatch.");
  const rows = new Map();
  const lines = createInterface({ input: createReadStream(indexPath), crlfDelay: Infinity });
  for await (const line of lines) {
    if (!line || line.length > 16384) throw new Error("Invalid offline index line.");
    const row = json(Buffer.from(line));
    if (typeof row.conversationId !== "string" || rows.has(row.conversationId)) throw new Error("Duplicate or invalid index identity.");
    rows.set(row.conversationId, row);
  }
  const scratch = await mkdtemp(path.join(tmpdir(), "chatgpt-offline-verify-"));
  let count = 0; const seen = new Set();
  try {
    // Re-extract from the original ZIP, checking central/local metadata and CRC,
    // rather than accepting a member hash asserted by the importing process.
    const source = await sourceMembers(input, path.join(scratch, "members"));
    if (source.members.length !== receipt.members.length) throw new Error("Source member set mismatch.");
    const { projectOfflineConversations } = await importCore();
    for (const [slot, member] of source.members.entries()) {
      const declared = receipt.members[slot], memberPath = "source/members/" + String(slot + 1).padStart(4, "0") + ".json";
      const retained = await file(memberPath), memberHash = await hashFile(retained);
      if (member.name !== declared.member || member.sha256 !== declared.sha256 || member.byteSize !== declared.byteSize
        || memberHash.sha256 !== member.sha256 || memberHash.byteSize !== member.byteSize) throw new Error("Source member provenance mismatch.");
      for await (const record of conversationRecords(member.filename, receipt.maxRecordBytes)) {
        const [projection] = projectOfflineConversations([record.value], actual.sha256), row = rows.get(projection.conversationId);
        if (!row || seen.has(projection.conversationId) || row.sourceMember !== member.name || row.sourceMemberSha256 !== member.sha256) throw new Error("Source/index identity or membership mismatch.");
        seen.add(projection.conversationId);
        const expectedRaw = receipt.schemaVersion === 1 ? Buffer.from(projection.raw) : record.raw;
        const rawHash = sha256(expectedRaw);
        if (row.rawPath !== projection.directory + "/source/detail-" + rawHash + ".json"
          || row.normalizedPath !== projection.directory + "/conversation.json" || row.markdownPath !== projection.directory + "/conversation.md") throw new Error("Unexpected derived artifact identity.");
        for (const [key, expected] of [["raw", expectedRaw], ["normalized", projection.normalized], ["markdown", projection.markdown]]) {
          const artifact = await file(row[key + "Path"]), hashed = await hashFile(artifact);
          if (hashed.sha256 !== row[key + "Sha256"] || hashed.sha256 !== sha256(expected)) throw new Error("Artifact hash or source reconstruction mismatch.");
        }
        count++;
      }
    }
  } finally { await rm(scratch, { recursive: true, force: true }); }
  if (rows.size !== count || count !== receipt.observedConversationCount || count !== receipt.importedConversationCount) throw new Error("Omitted or extra conversation records.");
  return { completionState: "offline_import_complete", conversations: count, sourceSha256: actual.sha256, liveAccountCaptureComplete: false, assets: "not_imported" };
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  if (![1, 3].includes(args.length) || args.length === 3 && (args[1] !== "--source-sha256" || !/^[0-9a-f]{64}$/.test(args[2]))) {
    console.error("Usage: npm run verify:offline -- PRIVATE_ARCHIVE [--source-sha256 HEX]"); process.exitCode = 1;
  } else verifyOfflineArchive(args[0], args[2]).then(result => console.log(JSON.stringify(result))).catch(() => {
    console.error("Offline archive verification FAILED: source, index, graph, artifact or private-path evidence was rejected."); process.exitCode = 1;
  });
}
