#!/usr/bin/env node
/**
 * Offline source adapter: ChatGPT export JSON/ZIP stored locally, in Drive, or
 * as a file in a GitHub repository. No ChatGPT web session is ever requested.
 * Data and full source bytes remain only in the explicitly chosen private
 * output directory, never in a GitHub workflow or remote issue.
 */
import { execFile } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync } from "node:fs";
import { mkdir, mkdtemp, readFile, rename, rm, stat, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { promisify } from "node:util";
import { fileURLToPath, pathToFileURL } from "node:url";
import { build } from "esbuild";
import { unzipSync } from "fflate";

const exec = promisify(execFile);
const projectRoot = fileURLToPath(new URL("../", import.meta.url));
const MAX_ALLOWED_BYTES = 256 * 1024 * 1024;
const DEFAULT_MAX_BYTES = 64 * 1024 * 1024;
const ID = /^[A-Za-z0-9_-]{1,256}$/;
const HASH = value => createHash("sha256").update(value).digest("hex");

function usage() {
  return [
    "Usage: npm run import:offline -- SOURCE --output /private/directory [--max-bytes BYTES]",
    "Sources (choose exactly one):",
    "  --file /private/conversations.json",
    "  --file /private/chatgpt-export.zip",
    "  --drive-id FILE_ID",
    "  --github-repo OWNER/REPO --github-path exports/conversations.json [--github-ref main]",
    "Google Drive requires the locally authorized google-drive-files command.",
    "GitHub requires locally authorized gh; a repository must already contain the selected file.",
    "The output is private; this command never uploads or accesses the ChatGPT web API.",
  ].join("\n");
}

function parseOptions(args) {
  const options = {};
  for (let index = 0; index < args.length; index += 2) {
    const key = args[index];
    if (key === "--help" && args.length === 1) { console.log(usage()); process.exit(0); }
    if (!["--file", "--drive-id", "--github-repo", "--github-path", "--github-ref", "--output", "--max-bytes"].includes(key)
      || index + 1 >= args.length || Object.hasOwn(options, key)) throw new Error("Invalid arguments.\n" + usage());
    options[key] = args[index + 1];
  }
  if (!options["--output"]) throw new Error("An explicit --output directory is required.");
  const kinds = [Boolean(options["--file"]), Boolean(options["--drive-id"]), Boolean(options["--github-repo"])]
    .filter(Boolean).length;
  if (kinds !== 1 || Boolean(options["--github-path"]) !== Boolean(options["--github-repo"])
    || (options["--github-ref"] && !options["--github-repo"])) {
    throw new Error("Select exactly one complete file, Drive, or GitHub source.");
  }
  const maxBytes = options["--max-bytes"] === undefined ? DEFAULT_MAX_BYTES : Number(options["--max-bytes"]);
  if (!Number.isSafeInteger(maxBytes) || maxBytes < 1 || maxBytes > MAX_ALLOWED_BYTES) {
    throw new Error("--max-bytes must be an integer from 1 to 268435456.");
  }
  const output = path.resolve(options["--output"]);
  if (output === projectRoot || output.startsWith(projectRoot + path.sep)) {
    throw new Error("Private imports must not be saved in the public exporter source checkout.");
  }
  const repo = options["--github-repo"];
  const file = options["--github-path"];
  if (repo && !/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(repo)) {
    throw new Error("GitHub repository must be OWNER/REPO.");
  }
  if (file && (!/^[A-Za-z0-9_.\/-]+$/.test(file)
    || file.split("/").some(s => !s || s === "." || s === ".."))) {
    throw new Error("GitHub file path must be a safe relative repository path.");
  }
  const ref = options["--github-ref"] ?? "";
  if (ref && (!/^[A-Za-z0-9_./-]+$/.test(ref) || ref.includes(".."))) {
    throw new Error("GitHub reference is invalid.");
  }
  if (options["--drive-id"] && !ID.test(options["--drive-id"])) {
    throw new Error("Drive file ID is invalid.");
  }
  return { options, output, maxBytes };
}

async function acquireFile({ options, maxBytes }) {
  if (options["--file"]) {
    const name = path.resolve(options["--file"]);
    const details = await stat(name);
    if (!details.isFile() || details.size > maxBytes) throw new Error("Source is not a regular file within the configured byte limit.");
    return { data: await readFile(name), kind: "local-file" };
  }
  if (options["--drive-id"]) {
    return {
      data: await commandBytes("google-drive-files", ["download", options["--drive-id"]], maxBytes),
      kind: "google-drive",
    };
  }
  const [owner, repo] = options["--github-repo"].split("/");
  const filePath = options["--github-path"].split("/").map(encodeURIComponent).join("/");
  const ref = options["--github-ref"];
  const url = "repos/" + encodeURIComponent(owner) + "/" + encodeURIComponent(repo) + "/contents/"
    + filePath + (ref ? "?ref=" + encodeURIComponent(ref) : "");
  return {
    data: await commandBytes("gh", ["api", "--method", "GET", "-H", "Accept: application/vnd.github.raw+json", url], maxBytes),
    kind: "github-repository",
  };
}

async function commandBytes(command, args, maxBytes) {
  try {
    // No shell and no secrets on the command line; each provider CLI manages
    // its own already-authorized local credential. Never echo stderr/content.
    const result = await exec(command, args, {
      cwd: projectRoot,
      encoding: "buffer",
      maxBuffer: maxBytes,
      timeout: 120000,
      windowsHide: true,
    });
    const bytes = Buffer.isBuffer(result.stdout) ? result.stdout : Buffer.from(result.stdout);
    if (bytes.length > maxBytes) throw new Error("Source exceeds the configured byte limit.");
    return bytes;
  } catch {
    throw new Error(command + " could not retrieve the requested file; check private local authorization and source identity.");
  }
}

function decodeJson(bytes, label) {
  let value;
  try { value = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes)); }
  catch { throw new Error("Supplied " + label + " is not valid UTF-8 JSON."); }
  return value;
}

function jsonMembers(data, maxBytes) {
  const zip = data.length >= 4 && data[0] === 0x50 && data[1] === 0x4b
    && (data[2] === 3 || data[2] === 5 || data[2] === 7);
  if (!zip) return { zip: false, members: [{ name: "conversations.json", bytes: data }] };

  const selected = [];
  let totalUnpacked = 0;
  let extracted;
  try {
    extracted = unzipSync(new Uint8Array(data), {
      filter(file) {
        const pathParts = file.name.split("/");
        if (pathParts.some(part => part === "" || part === "." || part === ".." || part.includes("\\"))) {
          throw new Error("ZIP contains an unsafe member path.");
        }
        const basename = pathParts.at(-1) ?? "";
        const isConversation = /^conversations(?:[-_][0-9]+)?\.json$/i.test(basename);
        if (!isConversation) return false;
        if (file.originalSize < 2 || file.originalSize > maxBytes || ![0, 8].includes(file.compression)) {
          throw new Error("Conversation ZIP member is too large or uses unsupported compression.");
        }
        totalUnpacked += file.originalSize;
        if (totalUnpacked > maxBytes) throw new Error("Selected conversation JSON members exceed the size limit.");
        selected.push(file.name);
        return true;
      },
    });
  } catch (error) {
    if (error instanceof Error && (error.message.includes("unsafe") || error.message.includes("limit") || error.message.includes("large"))) throw error;
    throw new Error("Malformed or unsupported conversation ZIP input.");
  }
  if (selected.length === 0) throw new Error("ZIP has no conversations.json or numbered conversation JSON files.");
  selected.sort();
  const members = selected.map((name, index) => ({
    name, bytes: Buffer.from(extracted[name]), slot: index,
  }));
  const actualTotal = members.reduce((total, member) => total + member.bytes.length, 0);
  if (actualTotal > maxBytes) throw new Error("ZIP conversation members exceed the uncompressed limit.");
  return { zip: true, members };
}

async function importCore() {
  const scratch = await mkdtemp(path.join(tmpdir(), "chatgpt-offline-code-"));
  try {
    const result = await build({
      entryPoints: [path.join(projectRoot, "src/chatgpt/offline-source.ts")],
      bundle: true, platform: "node", format: "esm", write: false,
      target: "node22", legalComments: "none",
    });
    const bundled = path.join(scratch, "offline-core.mjs");
    await writeFile(bundled, result.outputFiles[0].contents, { mode: 0o600 });
    return await import(pathToFileURL(bundled).href);
  } finally {
    await rm(scratch, { recursive: true, force: true });
  }
}

async function persistArchive(output, data, kind, allMembers, rows, inputSha256) {
  if (existsSync(output)) throw new Error("Output already exists; refusing to overwrite any private archive.");
  const parent = path.dirname(output);
  if (!(await stat(parent)).isDirectory()) throw new Error("Output parent must exist.");
  const scratch = await mkdtemp(path.join(parent, "." + path.basename(output) + ".partial-"));
  const inside = async (filename, bytes) => {
    const absolute = path.join(scratch, ...filename.split("/"));
    await mkdir(path.dirname(absolute), { recursive: true, mode: 0o700 });
    await writeFile(absolute, bytes, { flag: "wx", mode: 0o600 });
  };
  try {
    const isZip = data.length >= 4 && data[0] === 0x50 && data[1] === 0x4b;
    await inside(isZip ? "source/input.zip" : "source/input.json", data);
    const index = [];
    for (const [memberIndex, member] of allMembers.entries()) {
      const sourceMemberPath = "source/members/" + String(memberIndex + 1).padStart(4, "0") + ".json";
      await inside(sourceMemberPath, member.bytes);
      for (const row of rows.filter(item => item.sourceMemberIndex === memberIndex)) {
        const rawHash = HASH(Buffer.from(row.raw));
        const normalizedHash = HASH(Buffer.from(row.normalized));
        const markdownHash = HASH(Buffer.from(row.markdown));
        const rawPath = row.directory + "/source/detail-" + rawHash + ".json";
        const normalizedPath = row.directory + "/conversation.json";
        const markdownPath = row.directory + "/conversation.md";
        await inside(rawPath, row.raw);
        await inside(normalizedPath, row.normalized);
        await inside(markdownPath, row.markdown);
        index.push({
          conversationId: row.conversationId,
          sourceMember: member.name,
          sourceMemberSha256: HASH(member.bytes),
          rawPath, rawSha256: rawHash,
          normalizedPath, normalizedSha256: normalizedHash,
          markdownPath, markdownSha256: markdownHash,
        });
      }
    }
    index.sort((a,b) => a.conversationId.localeCompare(b.conversationId));
    await inside("indexes/conversations.jsonl", index.map(record => JSON.stringify(record)).join("\n") + (index.length ? "\n" : ""));
    const receipt = {
      schemaVersion: 1,
      provider: "chatgpt-offline-file",
      sourceKind: kind,
      sourceSha256: inputSha256,
      sourceSize: data.length,
      members: allMembers.map(member => ({ member: member.name, sha256: HASH(member.bytes), byteSize: member.bytes.length })),
      observedConversationCount: rows.length,
      importedConversationCount: index.length,
      providerSnapshot: "unknown",
      scope: "only_records_in_supplied_file",
      chatgptSessionUsed: false,
      chatgptNetworkCalls: 0,
      assetCoverage: "not_imported",
      liveExporterCompletion: false,
    };
    await inside("reports/offline-import.json", JSON.stringify(receipt, null, 2) + "\n");
    if (existsSync(output)) throw new Error("Output appeared during import; refusing to overwrite.");
    await rename(scratch, output);
    return receipt;
  } catch (error) {
    await rm(scratch, { recursive: true, force: true });
    throw error;
  }
}

async function main() {
  const opts = parseOptions(process.argv.slice(2));
  const { data, kind } = await acquireFile(opts);
  if (data.length > opts.maxBytes) throw new Error("Source exceeds configured size limit.");
  const digest = HASH(data);
  const supplied = jsonMembers(data, opts.maxBytes);
  const { projectOfflineConversations } = await importCore();
  const rows = [];
  const seen = new Set();
  for (const [index, member] of supplied.members.entries()) {
    const projections = projectOfflineConversations(decodeJson(member.bytes, member.name), digest);
    for (const row of projections) {
      const key = row.directory.toLowerCase();
      if (seen.has(key)) throw new Error("Duplicate conversation identity across source members.");
      seen.add(key);
      rows.push({ ...row, sourceMemberIndex: index });
    }
  }
  const receipt = await persistArchive(opts.output, data, kind, supplied.members, rows, digest);
  console.log("Imported " + receipt.importedConversationCount + " supplied conversation records to a private local directory.");
  console.log("ChatGPT web session used: no. Provider-wide completeness: unknown. Assets: not imported.");
}

main().catch(error => {
  // Report only the kind of failure. Neither auth material nor source bytes are printed.
  console.error("Offline import failed: " + (error instanceof Error ? error.message : "Unknown error"));
  process.exitCode = 1;
});
