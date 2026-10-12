import { describe, expect, it } from "vitest";
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { chmod, mkdtemp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { zipSync, strToU8 } from "fflate";

import { projectOfflineConversations } from "../../src/chatgpt/offline-source";
import { conversationDetail } from "../fixtures/chatgpt";

const repo = fileURLToPath(new URL("../../", import.meta.url));
const cli = path.join(repo, "scripts/import-offline.mjs");
const fingerprint = "a".repeat(64);
const sample = () => conversationDetail({ id: "conversation-1" });
const sha = (bytes: Uint8Array) => createHash("sha256").update(bytes).digest("hex");

async function inTemp(run: (root: string) => Promise<void>) {
  const root = await mkdtemp(path.join(os.tmpdir(), "chatgpt-existing-source-"));
  try { await run(root); }
  finally { await rm(root, { recursive: true, force: true }); }
}
function execute(args: string[], env = process.env) {
  return spawnSync(process.execPath, [cli, ...args], {
    cwd: os.tmpdir(), env, encoding: "utf8", timeout: 20000,
    maxBuffer: 2_000_000,
  });
}

describe("offline source graph projection", () => {
  it("reuses canonical graph normalization without claiming a ChatGPT session", () => {
    const record = sample();
    record.future_extension = { newField: [1, "unknown"] };
    record.mapping["user-1"]!.message!.author.role = "future-role";
    const result = projectOfflineConversations([record], fingerprint);
    expect(result).toHaveLength(1);
    expect(result[0]!.directory).toBe("conversations/conversation-1");
    const normalized = JSON.parse(result[0]!.normalized);
    expect(normalized.coverage).toMatchObject({ providerGraph: "unknown", versions: "unknown" });
    expect(normalized.extensions.chatgpt.future_extension).toEqual({ newField: [1, "unknown"] });
    expect(normalized.messages.some((message: { role: string }) => message.role === "unknown")).toBe(true);
    expect(normalized.memberships).toEqual([]);
  });
  it("refuses duplicates, conflicting identities, cycles, unsupported schema and source claims", () => {
    expect(() => projectOfflineConversations([sample(), sample()], fingerprint)).toThrow(/duplicate/);
    expect(() => projectOfflineConversations([{ ...sample(), conversation_id: "other" }], fingerprint)).toThrow();
    const broken = sample(); broken.mapping["root-1"]!.parent = "assistant-1";
    expect(() => projectOfflineConversations([broken], fingerprint)).toThrow();
    expect(() => projectOfflineConversations({ notes: "Memory Cane is not chat history" }, fingerprint)).toThrow(/array/);
    expect(() => projectOfflineConversations([sample()], "not-a-hash")).toThrow();
  });
  it("distinguishes accepted IDs that previously collided on disk", () => {
    const left = conversationDetail({ id: "-conversation" });
    const right = conversationDetail({ id: "conversation" });
    const rows = projectOfflineConversations([left, right], fingerprint);
    expect(new Set(rows.map(row => row.directory.toLowerCase())).size).toBe(2);
  });
});

describe("offline importer local, Drive and GitHub source adapters", () => {
  it("imports an existing conversations.json without network and keeps its original bytes", async () => inTemp(async root => {
    const input = path.join(root, "existing.json");
    const output = path.join(root, "private-import");
    const bytes = Buffer.from(JSON.stringify([sample()]));
    await writeFile(input, bytes);
    const result = execute(["--file", input, "--output", output]);
    expect(result.status, result.stderr).toBe(0);
    expect(result.stdout).toContain("Imported 1 supplied conversation");
    expect(result.stdout).not.toContain("Synthetic response.");
    const original = await readFile(path.join(output, "source/input.json"));
    expect(original.equals(bytes)).toBe(true);
    const manifest = JSON.parse(await readFile(path.join(output, "reports/offline-import.json"), "utf8"));
    expect(manifest).toMatchObject({
      sourceKind: "local-file", sourceSha256: sha(bytes), importedConversationCount: 1,
      providerSnapshot: "unknown", scope: "only_records_in_supplied_file",
      chatgptSessionUsed: false, chatgptNetworkCalls: 0,
      assetCoverage: "not_imported", liveExporterCompletion: false,
    });
    const normalized = JSON.parse(await readFile(path.join(output, "conversations/conversation-1/conversation.json"), "utf8"));
    expect(normalized.messages).toHaveLength(2);
    expect(normalized.extensions.chatgpt.title).toBe("Synthetic conversation");
    const markdown = await readFile(path.join(output, "conversations/conversation-1/conversation.md"), "utf8");
    expect(markdown).toContain("Synthetic response.");
    const index = await readFile(path.join(output, "indexes/conversations.jsonl"), "utf8");
    expect(index).toContain('"conversationId":"conversation-1"');
    expect(index).not.toContain("Synthetic response.");
  }));

  it("accepts a ZIP containing only selected conversation JSON; ignores nonconversation assets", async () => inTemp(async root => {
    const zipPath = path.join(root, "export.zip"), output = path.join(root, "result");
    const zipped = zipSync({
      "conversations.json": strToU8(JSON.stringify([sample()])),
      "chat.html": strToU8("Private synthetic HTML not parsed"),
      "uploads/private-image.png": new Uint8Array(2500),
    });
    await writeFile(zipPath, zipped);
    const result = execute(["--file", zipPath, "--output", output]);
    expect(result.status, result.stderr).toBe(0);
    const receipt = JSON.parse(await readFile(path.join(output, "reports/offline-import.json"), "utf8"));
    expect(receipt.members).toHaveLength(1);
    expect(receipt.members[0].member).toBe("conversations.json");
    expect(receipt.assetCoverage).toBe("not_imported");
    expect(Buffer.from(await readFile(path.join(output, "source/input.zip"))).equals(Buffer.from(zipped))).toBe(true);
  }));

  it.each(["drive", "github"])("reads %s bytes through an already-authorized local CLI, without accessing ChatGPT", async source => inTemp(async root => {
    const bin = path.join(root, "bin"); await mkdir(bin);
    const fixture = path.join(root, "supplied.json");
    await writeFile(fixture, JSON.stringify([sample()]));
    const name = source === "drive" ? "google-drive-files" : "gh";
    const command = path.join(bin, name);
    await writeFile(command, [
      "#!/bin/sh",
      "case \"" + source + "\" in",
      "  drive) test \"$1\" = \"download\" && test \"$2\" = \"TEST_FILE\" || exit 2 ;;",
      "  github) test \"$1\" = \"api\" && test \"$2\" = \"--method\" || exit 2 ;;",
      "esac",
      "cat \"$OFFLINE_FIXTURE\"",
      "",
    ].join("\n"));
    await chmod(command, 0o700);
    const output = path.join(root, "private");
    const args = source === "drive"
      ? ["--drive-id", "TEST_FILE", "--output", output]
      : ["--github-repo", "owner/private", "--github-path", "exports/conversations.json", "--github-ref", "main", "--output", output];
    const result = execute(args, {
      ...process.env,
      PATH: bin + path.delimiter + process.env.PATH,
      OFFLINE_FIXTURE: fixture,
    });
    expect(result.status, result.stderr).toBe(0);
    const receipt = JSON.parse(await readFile(path.join(output, "reports/offline-import.json"), "utf8"));
    expect(receipt).toMatchObject({ sourceKind: source === "drive" ? "google-drive" : "github-repository",
      chatgptSessionUsed: false, importedConversationCount: 1 });
  }));

  it("refuses a populated destination without modifying it", async () => inTemp(async root => {
    const input = path.join(root, "existing.json");
    const output = path.join(root, "already");
    await writeFile(input, JSON.stringify([sample()]));
    await mkdir(output);
    await writeFile(path.join(output, "keep.txt"), "do not overwrite");
    const result = execute(["--file", input, "--output", output]);
    expect(result.status).not.toBe(0);
    expect((await readFile(path.join(output, "keep.txt"), "utf8"))).toBe("do not overwrite");
    expect(result.stderr).not.toContain("Synthetic response.");
  }));

  it("rejects duplicated JSON conversations across numbered ZIP members", async () => inTemp(async root => {
    const zipPath = path.join(root, "duplicate.zip");
    await writeFile(zipPath, zipSync({
      "conversations_0001.json": strToU8(JSON.stringify([sample()])),
      "conversations_0002.json": strToU8(JSON.stringify([sample()])),
    }));
    const output = path.join(root, "result");
    const result = execute(["--file", zipPath, "--output", output]);
    expect(result.status).not.toBe(0);
    expect(result.stderr).toMatch(/Duplicate/);
  }));

  it("refuses to place private archives inside the public source checkout", async () => inTemp(async root => {
    const input = path.join(root, "existing.json");
    await writeFile(input, JSON.stringify([sample()]));
    const destination = path.join(repo, "PRIVATE_EXPORT_MUST_NOT_BE_CREATED");
    const result = execute(["--file", input, "--output", destination]);
    expect(result.status).not.toBe(0);
    expect(result.stderr).toMatch(/must not be saved/);
  }));

  it("refuses oversized inputs and unsafe GitHub paths before running any command", async () => inTemp(async root => {
    const input = path.join(root, "source.json");
    await writeFile(input, JSON.stringify([sample()]));
    const tooLarge = execute(["--file", input, "--max-bytes", "10", "--output", path.join(root, "reject")]);
    expect(tooLarge.status).not.toBe(0);
    const badPath = execute(["--github-repo", "owner/private", "--github-path", "../secrets.json",
      "--output", path.join(root, "reject-other")]);
    expect(badPath.status).not.toBe(0);
    expect(badPath.stderr).toMatch(/safe relative/);
  }));
});
