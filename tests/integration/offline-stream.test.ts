import { describe, expect, it } from "vitest";
import { mkdtemp, writeFile, readFile, rm, chmod, symlink, open, cp } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { zipSync, strToU8 } from "fflate";
import { conversationDetail } from "../fixtures/chatgpt";

const cli = fileURLToPath(new URL("../../scripts/import-offline.mjs", import.meta.url));
const verify = fileURLToPath(new URL("../../scripts/verify-offline.mjs", import.meta.url));
const execute = (script: string, args: string[], heap = "128") => spawnSync(process.execPath, ["--max-old-space-size=" + heap, script, ...args], { encoding: "utf8", timeout: 90000, maxBuffer: 100000 });
async function temp(run: (root: string) => Promise<void>) {
  const root = await mkdtemp(path.join(tmpdir(), "offline-stream-test-"));
  try { await run(root); } finally { await rm(root, { recursive: true, force: true }); }
}
async function imported(root: string, shape: unknown = [conversationDetail({ id: "synthetic-1" })]) {
  const input = path.join(root, "input.json"), output = path.join(root, "archive");
  await writeFile(input, JSON.stringify(shape));
  const result = execute(cli, ["--file", input, "--output", output]);
  expect(result.status, result.stderr).toBe(0);
  return output;
}
describe("bounded offline evidence and independent disk replay", () => {
  it("runs the built offline importer and verifier outside the checkout without npm runtime dependencies", async () => temp(async root => {
    const build = fileURLToPath(new URL("../../scripts/build.mjs", import.meta.url));
    const built = spawnSync(process.execPath, [build], { cwd: path.dirname(path.dirname(build)), encoding: "utf8", timeout: 20000 });
    expect(built.status, built.stderr).toBe(0);
    const runtime = path.join(root, "runtime");
    await cp(fileURLToPath(new URL("../../dist/offline", import.meta.url)), runtime, { recursive: true });
    const input = path.join(root, "input.json"), output = path.join(root, "archive");
    await writeFile(input, JSON.stringify([conversationDetail({ id: "synthetic-package" })]));
    const imported = execute(path.join(runtime, "scripts/import-offline.mjs"), ["--file", input, "--output", output]);
    expect(imported.status, imported.stderr).toBe(0);
    const verified = execute(path.join(runtime, "scripts/verify-offline.mjs"), [output]);
    expect(verified.status, verified.stderr).toBe(0);
  }));
  it.each(["array", "wrapper", "single", "empty"])("replays %s sources from retained input", async shape => temp(async root => {
    const record = conversationDetail({ id: "synthetic-1" });
    const output = await imported(root, shape === "array" ? [record] : shape === "wrapper" ? { extra: { future: true }, conversations: [record], tail: null } : shape === "single" ? record : []);
    const result = execute(verify, [output]);
    expect(result.status, result.stderr).toBe(0);
    expect(JSON.parse(result.stdout)).toMatchObject({ completionState: "offline_import_complete", conversations: shape === "empty" ? 0 : 1, liveAccountCaptureComplete: false });
  }));
  it("imports more than the old 256 MiB ceiling with a 96 MiB JS heap", async () => temp(async root => {
    const input = path.join(root, "large.json"), file = await open(input, "w", 0o600);
    try {
      await file.writeFile("["); const padding = Buffer.alloc(1024 * 1024, 32);
      for (let i = 0; i < 260; i++) await file.writeFile(padding);
      await file.writeFile(JSON.stringify(conversationDetail({ id: "synthetic-large" })) + "]");
    } finally { await file.close(); }
    const result = execute(cli, ["--file", input, "--output", path.join(root, "archive")], "96");
    expect(result.status, result.stderr).toBe(0);
    expect(result.stdout).toContain("Imported 1");
  }), 90000);
  it.each(["raw", "markdown", "omission", "source", "mode", "symlink", "flag", "account-flag", "snapshot-flag", "wrong-state"])("rejects %s mutations despite declared completion", async mutation => temp(async root => {
    const output = await imported(root), reportPath = path.join(output, "reports/offline-import.json");
    const receipt = JSON.parse(await readFile(reportPath, "utf8"));
    const indexPath = path.join(output, "indexes/conversations.jsonl"), row = JSON.parse(await readFile(indexPath, "utf8"));
    if (mutation === "raw") await writeFile(path.join(output, row.rawPath), "{}");
    if (mutation === "markdown") await writeFile(path.join(output, row.markdownPath), "false derived view");
    if (mutation === "omission") { await writeFile(indexPath, ""); receipt.importedConversationCount = 0; receipt.observedConversationCount = 0; }
    if (mutation === "source") await writeFile(path.join(output, "source/input.json"), "[]");
    if (mutation === "mode") await chmod(path.join(output, row.rawPath), 0o644);
    if (mutation === "symlink") { const location = path.join(output, row.rawPath); const bytes = await readFile(location); await writeFile(path.join(root, "external"), bytes, { mode: 0o600 }); await rm(location); await symlink(path.join(root, "external"), location); }
    if (mutation === "flag") receipt.liveExporterCompletion = true;
    if (mutation === "account-flag") receipt.fullAccountCaptureComplete = true;
    if (mutation === "snapshot-flag") receipt.atomicProviderSnapshotProven = true;
    if (mutation === "wrong-state") receipt.completionState = "complete_for_verified_scope";
    await writeFile(reportPath, JSON.stringify(receipt));
    const result = execute(verify, [output]);
    expect(result.status).not.toBe(0); expect(result.stderr).not.toContain("Synthetic response.");
  }));
  it("detects an omitted index row even after the attacker recomputes the index hash", async () => temp(async root => {
    const output = await imported(root), reportPath = path.join(output, "reports/offline-import.json");
    const receipt = JSON.parse(await readFile(reportPath, "utf8"));
    const { createHash } = await import("node:crypto");
    await writeFile(path.join(output, "indexes/conversations.jsonl"), "");
    receipt.indexSha256 = createHash("sha256").update("").digest("hex"); receipt.importedConversationCount = receipt.observedConversationCount = 0;
    await writeFile(reportPath, JSON.stringify(receipt));
    expect(execute(verify, [output]).status).not.toBe(0);
  }));
  it.each(["crc", "traversal", "duplicate"])("refuses ZIP %s defects", async mutation => temp(async root => {
    const record = strToU8(JSON.stringify([conversationDetail({ id: "synthetic-1" })]));
    const bytes = Buffer.from(zipSync(mutation === "traversal" ? { "../conversations.json": record } : mutation === "duplicate" ? { "conversations.json": record, "CONVERSATIONS.JSON": record } : { "conversations.json": record }, { level: 0 }));
    if (mutation === "crc") {
      const central = bytes.indexOf(Buffer.from([0x50, 0x4b, 0x01, 0x02])); bytes.writeUInt32LE(123, central + 16);
    }
    const input = path.join(root, "input.zip"); await writeFile(input, bytes);
    const result = execute(cli, ["--file", input, "--output", path.join(root, "archive")]); expect(result.status).not.toBe(0);
  }));
  it.each(["[{},]", "[]x", '{"conversations":[],"conversations":[]}', '[{"secret":"private-text"}'])("refuses malformed framing without printing source text", async input => temp(async root => {
    const filename = path.join(root, "bad.json"); await writeFile(filename, input);
    const result = execute(cli, ["--file", filename, "--output", path.join(root, "archive")]);
    expect(result.status).not.toBe(0); expect(result.stderr).not.toContain("private-text");
  }));
});
