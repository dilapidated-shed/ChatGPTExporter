import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { parseConversationDetail } from "../../src/chatgpt/envelopes";
import { graphFindings } from "../../src/chatgpt/capture";
import { conversationDetail } from "../fixtures/chatgpt";

// These are specification data, not output from the native parser. Independent
// ai-ci checks actual stored bytes against these exact fixture files; this test
// checks their meaning through the pre-existing TypeScript implementation.
describe("native capture's independently specified raw oracle", () => {
  for (const id of ["conversation-1", "conversation-2", "archived-1", "archived-2", "archived-3"]) {
    it(`preserves complete branches and unknown fields for ${id}`, () => {
      const bytes = readFileSync(new URL(`../../native/fixtures/capture/${id}.json`, import.meta.url), "utf8");
      const detail = parseConversationDetail(JSON.parse(bytes) as unknown);
      expect(detail.id).toBe(id);
      expect(Object.keys(detail.mapping).sort()).toEqual(["assistant-1", "assistant-alt", "root-1", "user-1"]);
      expect(detail.current_node).toBe("assistant-1");
      expect(detail.mapping["user-1"]!.children).toEqual(["assistant-1", "assistant-alt"]);
      expect(detail.mapping["assistant-alt"]!.message!.author.role).toBe("future-role");
      expect(detail.mapping["assistant-alt"]!.message!.content.content_type).toBe("future");
      expect((JSON.parse(bytes) as { future_unknown: unknown }).future_unknown).toEqual({ preserve: [1, "λ", null] });
      expect(graphFindings(detail)).toEqual([]);
      const prior = conversationDetail();
      expect(detail.mapping["root-1"]!.children).toEqual(prior.mapping["root-1"]!.children);
      expect(detail.mapping["assistant-1"]!.parent).toBe(prior.mapping["assistant-1"]!.parent);
      expect(detail.mapping["user-1"]!.message!.content.parts).toEqual(prior.mapping["user-1"]!.message!.content.parts);
    });
  }
});
