import { beforeAll, describe, expect, it } from "vitest";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { normalizeConversation } from "../../src/chatgpt/normalize";
import { renderConversationMarkdown } from "../../src/core/markdown";
import { workspaceFingerprint } from "../../src/chatgpt/client";
import { parseConversationDetail } from "../../src/chatgpt/envelopes";

const native = path.resolve("native");
beforeAll(() => {
  const built = spawnSync("make", ["views"], { cwd: native, encoding: "utf8" });
  expect(built.status, built.stderr).toBe(0);
});
async function compare(content?: unknown, metadata?: unknown, archived = false, author?: unknown, timestamp?: string) {
  const root = await mkdtemp(path.join(tmpdir(), "native-canonical-"));
  try {
    const raw = JSON.parse(await readFile(path.join(native, "fixtures/capture/conversation-1.json"), "utf8"));
    if (content !== undefined) raw.mapping["assistant-1"].message.content = content;
    if (metadata !== undefined) raw.mapping["assistant-1"].message.metadata = metadata;
    if (author !== undefined) raw.mapping["assistant-1"].message.author = author;
    if (timestamp !== undefined) {
      raw.create_time = timestamp; raw.update_time = timestamp;
      raw.mapping["assistant-1"].message.create_time = timestamp;
      raw.mapping["assistant-1"].message.update_time = timestamp;
    }
    const listing = { id: raw.id, title: "Listing title", create_time: 123, update_time: 456 };
    await writeFile(path.join(root, "detail.json"), JSON.stringify(raw));
    await writeFile(path.join(root, "listing.json"), JSON.stringify(listing));
    const result = spawnSync(path.join(native, "build/cgw-views"), [path.join(root, "detail.json"), path.join(root, "listing.json"), "synthetic-workspace", archived ? "archived" : "main", path.join(root, "out.json"), path.join(root, "out.md")], { encoding: "utf8" });
    expect(result.status, result.stderr).toBe(0);
    const fingerprint = await workspaceFingerprint("synthetic-workspace");
    const expected = normalizeConversation(parseConversationDetail(raw), {
      logicalKey: `${fingerprint}/${raw.id}`, conversationId: raw.id, title: listing.title,
      createTime: listing.create_time, updateTime: listing.update_time,
      memberships: [{ scope: archived ? "archived" : "main" }], listingHashes: [],
    }, fingerprint);
    const actual = JSON.parse(await readFile(path.join(root, "out.json"), "utf8"));
    expect(actual.normalizerVersion).toBe("chatgpt-native-c1");
    expect({ ...actual, normalizerVersion: expected.normalizerVersion }).toEqual(expected);
    expect(await readFile(path.join(root, "out.md"), "utf8")).toBe(renderConversationMarkdown(expected));
    return actual;
  } finally { await rm(root, { recursive: true, force: true }); }
}

describe("native C views versus independently maintained TypeScript reference", () => {
  it("retains every branch, inactive message, unknown field, and canonical workspace identity", async () => { await compare(); });
  it("retains archived membership and metadata", async () => { await compare(undefined, { model_slug: "synthetic", future: { a: [null, 2, true] } }, true, { role: "tool", name: "Synthetic λ <tool>" }); });
  it.each([
    { content_type: "text", parts: ["Unicode λ 😀 & < >\n", "NUL\u0000retained"] },
    { content_type: "multimodal_text", parts: ["text", { type: "image_asset_pointer", asset_pointer: "file-service://test" }, { type: "audio_asset_pointer", file_id: "audio-test" }, { type: "video_asset_pointer", id: "video-test" }] },
    { content_type: "text", parts: [null, 9, ["future"], { future_unknown: 1 }, { type: "text", text: "other" }] },
    { content_type: "code", text: "hello ```\n<code>", language: "c" },
    { content_type: "execution_output", output: "result" },
    { content_type: "tether_browsing_display", parts: ["a", { unknown: 1 }, "b"] },
    { content_type: "tool_call", result: "call" },
    { content_type: "computer_initialize_state", text: "state" },
    { content_type: "reasoning_recap", parts: ["a", "b"] },
    { content_type: "canvas", text: "document" },
    { content_type: "textdoc", code: "code" },
    { content_type: "code_edit", result: "edit" },
    { content_type: "future-block", future: { preserved: true } },
    { content_type: "text", parts: [{ type: "code", code: "int x;", language: "c" }, { type: "tool_call", data: 1 }, { type: "browsing_result", text: "answer" }] },
    { content_type: "text", parts: [{ type: "citation", title: "Source & stuff", url: "https://example.com" }, { type: "content_reference", href: "javascript:alert(1)" }] },
  ])("normalizes $content_type without a TypeScript runtime in native", async content => { await compare(content); });
  it("preserves research, citations and all reference metadata", async () => { await compare(
    { content_type: "text", parts: ["Research result"] },
    { deep_research_version: "synthetic", content_references: [{ url: "https://example.com/path", title: "Source" }], citations: [{ text: "quote" }], sources: [{ href: "file:///private" }] },
  ); });
  it.each(["1970-01-01T00:00:00Z", "2024-02-29T12:30:01.234Z", "2026-10-31T03:10:00-04:00", "2000-01-01T00:00:00+05:30", "1969-12-31T23:59:59.999Z", "2026-10-31"])("matches ISO-8601 timestamp semantics for %s", async timestamp => { await compare(undefined, undefined, false, undefined, timestamp); });
});
