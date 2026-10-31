#!/usr/bin/env node
/** Session-free, streaming import. All source data stays in private files. */
import { spawn } from "node:child_process";
import { createReadStream, createWriteStream, existsSync } from "node:fs";
import { mkdir, mkdtemp, lstat, open, realpath, rename, rm, writeFile } from "node:fs/promises";
import { pipeline } from "node:stream/promises";
import { once } from "node:events";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { ByteBudget, DEFAULT_RECORD_BYTES, conversationRecords, hashFile, sourceMembers, sha256 } from "./offline-stream.mjs";

const projectRoot = path.resolve(fileURLToPath(new URL("../", import.meta.url)));
const ID = /^[A-Za-z0-9_-]{1,256}$/;
function usage() {
  return "Usage: npm run import:offline -- (--file FILE | --drive-id ID | --github-repo OWNER/REPO --github-path PATH [--github-ref REF]) --output PRIVATE_DIR [--max-bytes BYTES] [--max-record-bytes BYTES]\n"
    + "Drive uses the already-authorized google-drive-files command; GitHub uses gh. No ChatGPT session is used.\n"
    + "Input and ZIP members stream to disk. --max-bytes is an optional total/unpacked budget; one record defaults to 64 MiB.";
}
function positive(value, fallback) {
  const n = value === undefined ? fallback : Number(value);
  if (!Number.isSafeInteger(n) || n < 1) throw new Error("Byte budgets must be positive exact integers.");
  return n;
}
function parseOptions(args) {
  const options = {};
  for (let i = 0; i < args.length; i += 2) {
    const key = args[i];
    if (key === "--help" && args.length === 1) { console.log(usage()); process.exit(0); }
    if (!["--file", "--drive-id", "--github-repo", "--github-path", "--github-ref", "--output", "--max-bytes", "--max-record-bytes"].includes(key)
      || i + 1 >= args.length || Object.hasOwn(options, key)) throw new Error("Invalid arguments.\n" + usage());
    options[key] = args[i + 1];
  }
  if (!options["--output"]) throw new Error("An explicit --output directory is required.");
  if (["--file", "--drive-id", "--github-repo"].filter(key => options[key]).length !== 1
    || Boolean(options["--github-path"]) !== Boolean(options["--github-repo"])
    || options["--github-ref"] && !options["--github-repo"]) throw new Error("Select exactly one complete source.");
  const output = path.resolve(options["--output"]);
  if (output === projectRoot || output.startsWith(projectRoot + path.sep)) throw new Error("Private imports must not be saved in the public source checkout.");
  if (options["--github-repo"] && !/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(options["--github-repo"])) throw new Error("Invalid GitHub repository.");
  const file = options["--github-path"];
  if (file && (!/^[A-Za-z0-9_.\/-]+$/.test(file) || file.split("/").some(p => !p || p === "." || p === ".."))) throw new Error("GitHub file path must be a safe relative repository path.");
  const ref = options["--github-ref"];
  if (ref && (!/^[A-Za-z0-9_./-]+$/.test(ref) || ref.includes(".."))) throw new Error("Invalid GitHub reference.");
  if (options["--drive-id"] && !ID.test(options["--drive-id"])) throw new Error("Invalid Drive file ID.");
  return { options, output, maxBytes: positive(options["--max-bytes"], Number.MAX_SAFE_INTEGER), recordBytes: positive(options["--max-record-bytes"], DEFAULT_RECORD_BYTES) };
}
async function acquire(opts, destination) {
  const { options, maxBytes } = opts;
  if (options["--file"]) {
    const filename = path.resolve(options["--file"]), state = await lstat(filename);
    if (!state.isFile() || state.size > maxBytes) throw new Error("Source is not a regular file within the configured byte limit.");
    await pipeline(createReadStream(filename), new ByteBudget(maxBytes), createWriteStream(destination, { flags: "wx", mode: 0o600 }));
    return { kind: "local-file", identity: { path: filename } };
  }
  let command, args, kind, identity;
  if (options["--drive-id"]) {
    command = "google-drive-files"; args = ["download", options["--drive-id"]]; kind = "google-drive";
    identity = { fileId: options["--drive-id"] };
  } else {
    command = "gh"; kind = "github-repository";
    const endpoint = "repos/" + options["--github-repo"] + "/contents/" + options["--github-path"].split("/").map(encodeURIComponent).join("/")
      + (options["--github-ref"] ? "?ref=" + encodeURIComponent(options["--github-ref"]) : "");
    args = ["api", "--method", "GET", "-H", "Accept: application/vnd.github.raw+json", endpoint];
    identity = { repository: options["--github-repo"], path: options["--github-path"], requestedRef: options["--github-ref"] ?? "default" };
  }
  const child = spawn(command, args, { cwd: projectRoot, stdio: ["ignore", "pipe", "ignore"], windowsHide: true });
  const closed = once(child, "close"); closed.catch(() => {});
  try {
    await pipeline(child.stdout, new ByteBudget(maxBytes), createWriteStream(destination, { flags: "wx", mode: 0o600 }));
    const [code] = await closed;
    if (code !== 0) throw new Error("source command failed");
  } catch {
    child.kill();
    throw new Error(command + " could not retrieve the requested file within the byte budget; check authorization and source identity.");
  }
  return { kind, identity };
}
export async function importCore() {
  const packaged = new URL("./offline-core.mjs", import.meta.url);
  if (existsSync(packaged)) return import(packaged.href);
  const scratch = await mkdtemp(path.join(tmpdir(), "chatgpt-offline-code-"));
  try {
    const { build } = await import("esbuild");
    const result = await build({ entryPoints: [path.join(projectRoot, "src/chatgpt/offline-source.ts")], bundle: true, platform: "node", format: "esm", write: false, target: "node22", legalComments: "none" });
    const bundled = path.join(scratch, "offline-core.mjs");
    await writeFile(bundled, result.outputFiles[0].contents, { mode: 0o600 });
    return await import(pathToFileURL(bundled).href);
  } finally { await rm(scratch, { recursive: true, force: true }); }
}
async function persist(opts) {
  const realParent = await realpath(path.dirname(opts.output)), realProject = await realpath(projectRoot);
  if (realParent === realProject || realParent.startsWith(realProject + path.sep)) throw new Error("Private imports must not be saved in the public source checkout.");
  if (existsSync(opts.output)) throw new Error("Output already exists; refusing to overwrite any private archive.");
  const scratch = await mkdtemp(path.join(path.dirname(opts.output), "." + path.basename(opts.output) + ".partial-"));
  const inside = async (filename, bytes) => {
    const absolute = path.join(scratch, ...filename.split("/"));
    await mkdir(path.dirname(absolute), { recursive: true, mode: 0o700 });
    const file = await open(absolute, "wx", 0o600);
    try { await file.writeFile(bytes); await file.sync(); } finally { await file.close(); }
  };
  let index;
  try {
    await mkdir(path.join(scratch, "source"), { mode: 0o700 });
    const input = path.join(scratch, "source/input.bin"), source = await acquire(opts, input), inputHash = await hashFile(input);
    const supplied = await sourceMembers(input, path.join(scratch, "source/members"), opts.maxBytes);
    const sourcePath = supplied.zip ? "source/input.zip" : "source/input.json";
    await rename(input, path.join(scratch, sourcePath));
    if (!supplied.zip) {
      await mkdir(path.join(scratch, "source/members"), { mode: 0o700 });
      await pipeline(createReadStream(path.join(scratch, sourcePath)), createWriteStream(path.join(scratch, "source/members/0001.json"), { flags: "wx", mode: 0o600 }));
      supplied.members[0].filename = path.join(scratch, "source/members/0001.json");
    }
    await mkdir(path.join(scratch, "indexes"), { mode: 0o700 });
    index = await open(path.join(scratch, "indexes/conversations.jsonl"), "wx", 0o600);
    const { projectOfflineConversations } = await importCore(), seen = new Set(); let count = 0;
    for (const [slot, member] of supplied.members.entries()) {
      const sourceMemberPath = "source/members/" + String(slot + 1).padStart(4, "0") + ".json";
      for await (const record of conversationRecords(member.filename, opts.recordBytes)) {
        const [row] = projectOfflineConversations([record.value], inputHash.sha256);
        const key = row.directory.toLowerCase();
        if (seen.has(key)) throw new Error("Duplicate conversation identity across supplied records."); seen.add(key);
        const rawHash = sha256(record.raw), rawPath = row.directory + "/source/detail-" + rawHash + ".json";
        const normalizedPath = row.directory + "/conversation.json", markdownPath = row.directory + "/conversation.md";
        await inside(rawPath, record.raw); await inside(normalizedPath, row.normalized); await inside(markdownPath, row.markdown);
        await index.writeFile(JSON.stringify({ conversationId: row.conversationId, sourceMember: member.name, sourceMemberPath, sourceMemberSha256: member.sha256, rawPath, rawSha256: rawHash, normalizedPath, normalizedSha256: sha256(row.normalized), markdownPath, markdownSha256: sha256(row.markdown) }) + "\n"); count++;
      }
    }
    await index.sync(); await index.close(); index = null;
    const receipt = {
      schemaVersion: 2, provider: "chatgpt-offline-file", sourceKind: source.kind, sourceIdentity: source.identity,
      sourcePath, sourceSha256: inputHash.sha256, sourceSize: inputHash.byteSize,
      members: supplied.members.map((m, slot) => ({ member: m.name, path: "source/members/" + String(slot + 1).padStart(4, "0") + ".json", sha256: m.sha256, byteSize: m.byteSize })),
      observedConversationCount: count, importedConversationCount: count, providerSnapshot: "unknown", scope: "only_records_in_supplied_file",
      completionState: "offline_import_complete", chatgptSessionUsed: false, chatgptNetworkCalls: 0, assetCoverage: "not_imported", liveExporterCompletion: false,
      indexSha256: (await hashFile(path.join(scratch, "indexes/conversations.jsonl"))).sha256, maxRecordBytes: opts.recordBytes,
    };
    await inside("reports/offline-import.json", JSON.stringify(receipt, null, 2) + "\n");
    const { verifyOfflineArchive } = await import("./verify-offline.mjs");
    await verifyOfflineArchive(scratch, inputHash.sha256);
    await mkdir(opts.output, { mode: 0o700 });
    for (const name of ["source", "conversations", "indexes", "reports"]) if (existsSync(path.join(scratch, name))) await rename(path.join(scratch, name), path.join(opts.output, name));
    await rm(scratch, { recursive: true });
    return receipt;
  } catch (error) {
    if (index) await index.close();
    await rm(scratch, { recursive: true, force: true }); throw error;
  }
}
async function main() {
  const receipt = await persist(parseOptions(process.argv.slice(2)));
  console.log("Imported " + receipt.importedConversationCount + " supplied conversation records to a private local directory.");
  console.log("ChatGPT web session used: no. Provider-wide completeness: unknown. Assets: not imported.");
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main().catch(error => {
  console.error("Offline import failed: " + (error instanceof Error ? error.message : "Unknown error")); process.exitCode = 1;
});
