import { describe, expect, it } from "vitest";
import { ChatGptDetailFetcher, graphFindings } from "../../src/chatgpt/capture";
import { normalizeConversation } from "../../src/chatgpt/normalize";
import { renderConversationMarkdown } from "../../src/core/markdown";
import { discoverAssets } from "../../src/chatgpt/assets";
import { ControlledTransport } from "../../src/core/request-control";
import { conversationDetail } from "../fixtures/chatgpt";
import { workspace, item, message, page, history, pagesTransport, transport, http } from "../fixtures/adversarial";

const fetchPages = async (pages: any[]) => (await new ChatGptDetailFetcher(pagesTransport(pages), workspace).fetchAll([item()])).conversations[0]!;

describe("X1 pagination completeness", () => {
  it.each([1, 3, 80])("captures %i pages", async count => {
    const result = await fetchPages(history(count * 2, 2));
    expect(Object.keys(result.detail.mapping)).toHaveLength(count * 2 + 1);
    expect((result.raw as any).__pagination_evidence.page_count).toBe(count);
  });
  it("terminates at exactly the 10000-page bound", async () => {
    // Empty intermediary pages avoid conflating page limit with graph-size costs.
    const pages = Array.from({length: 10000}, (_, i) => page(i === 0 ? ["last"] : [], i < 9999 ? `c-${i}` : null));
    expect((await fetchPages(pages)).detail.current_node).toBe("last");
  }, 15000);
  it("fails continuation beyond the page bound", async () => {
    const pages = Array.from({length: 10001}, (_, i) => page(i === 0 ? ["last"] : [], `c-${i}`));
    await expect(fetchPages(pages)).rejects.toMatchObject({code: "PAGINATION_PAGE_LIMIT"});
  }, 15000);
  it.each([
    ["missing cursor", [page(["m"], "x", {page_info:{has_previous_page:true}})], "PAGINATION_CURSOR_MISSING"],
    ["repeated cursor", [page(["m"], "a"), page([], "a")], "PAGINATION_CURSOR_REPEATED"],
    ["long cursor cycle", [page(["m"], "a"), page([], "b"), page([], "a")], "PAGINATION_CURSOR_REPEATED"],
    ["no messages anywhere", [page([])], "PAGINATION_EMPTY"],
    ["missing current node", [page(["m"], null, {current_node:"absent"})], "PAGINATION_CURRENT_NODE_MISSING"],
    ["missing initial identity", [page(["m"], null, {conversation_id:undefined})], "PAGINATION_IDENTITY_MISSING"],
    ["wrong initial identity", [page(["m"], null, {conversation_id:"other"})], "DETAIL_ID_MISMATCH"],
    ["wrong older identity", [page(["m"], "a"), page(["old"], null, {conversation_id:"other"})], "DETAIL_ID_MISMATCH"],
  ])("rejects %s", async (_name, pages, code) => { await expect(fetchPages(pages as any[])).rejects.toMatchObject({code}); });
  it.each([
    ["empty intermediate", [page(["m"], "a"), page([], "b"), page(["old"])], 3],
    ["empty terminal", [page(["m"], "a"), page([])], 2],
    ["overlap-only with fresh cursor", [page(["m"], "a"), page(["m"], "b"), page(["old"])], 3],
    ["multiple overlapping messages", [page(["b","c","d"], "a"), page(["a","b","c"])], 5],
    ["whole page overlap", [page(["a","b"], "a"), page(["a","b"])], 3],
  ])("handles %s under the bounded local contract", async (_name, pages, nodes) => {
    expect(Object.keys((await fetchPages(pages as any[])).detail.mapping)).toHaveLength(nodes as number);
  });
  it.each(["content", "metadata", "author", "create_time", "asset"])("rejects changed duplicate %s", async field => {
    const older = message("m");
    if (field === "asset") older.metadata = {attachments:[{file_id:"file-changed"}]};
    else if (field === "author") older.author = {role:"user"};
    else if (field === "create_time") older.create_time = 3;
    else if (field === "content") older.content = {content_type:"text",parts:["changed"]};
    else older.metadata = {changed:true};
    await expect(fetchPages([page(["m"], "a"), page([], null, {messages:[older]})])).rejects.toMatchObject({code:"PAGINATION_MESSAGE_CONFLICT"});
  });
  it("rejects contradictory order even when overlapping message bytes agree", async () => {
    await expect(fetchPages([page(["b","c"], "a"), page(["a","c","b"])])).rejects.toThrow();
  });
  it.each(["b","a",null])("resolves current node %s on boundary, oldest page, or null local policy", async current => {
    const result = await fetchPages([page(["b","c"], "a", {current_node:current}), page(["a","b"])]);
    expect(result.detail.current_node).toBe(current ?? "c");
  });
});

describe("X1 fallback request order", () => {
  it("rejects conflicting conversation identity aliases",async()=>{
    await expect(fetchPages([page(["m"],null,{id:"different-conversation"})])).rejects.toThrow();
  });
  it("does not accept a batch record with contradictory identity aliases",async()=>{
    const bad:any=conversationDetail();bad.conversation_id="different-conversation";
    const t=transport(op=>op.operation==="conversation_batch"?[bad]:page(["m"]));
    const result=await new ChatGptDetailFetcher(t,workspace).fetchAll([item()]);
    expect(result.conversations[0]!.source).toBe("current");
  });
  it.each([401,403,408,429,500,503])("initial HTTP %i never requests singular", async status => {
    const t = pagesTransport([http(status)]);
    await expect(new ChatGptDetailFetcher(t, workspace).fetchAll([item()])).rejects.toMatchObject({status});
    expect(t.request.mock.calls.map(([op])=>op.operation)).toEqual(["conversation_batch","conversation_current"]);
  });
  it.each([401,403,404,408,429,500])("history HTTP %i never changes contract", async status => {
    const t = pagesTransport([page(["m"], "cursor"), http(status)]);
    await expect(new ChatGptDetailFetcher(t, workspace).fetchAll([item()])).rejects.toMatchObject({status});
    expect(t.request.mock.calls.map(([op])=>op.operation)).toEqual(["conversation_batch","conversation_current","conversation_messages"]);
  });
  it("schema failure never requests singular", async () => {
    const t = pagesTransport([{conversation_id:"conversation-1",messages:[],page_info:{has_previous_page:"false"}}]);
    await expect(new ChatGptDetailFetcher(t, workspace).fetchAll([item()])).rejects.toThrow();
    expect(t.request.mock.calls.map(([op])=>op.operation)).toEqual(["conversation_batch","conversation_current"]);
  });
  it.each(["omitted","malformed","duplicate","suspicious"])("batch %s falls back only for affected record", async kind => {
    const good = conversationDetail({id:"good"});
    const bad: any = conversationDetail();
    if (kind === "malformed") bad.mapping = [];
    if (kind === "suspicious") bad.current_node = "absent";
    const t = transport(op => {
      if (op.operation === "conversation_batch") return [good, ...(kind === "omitted" ? [] : kind === "duplicate" ? [bad,bad] : [bad])] as any;
      if (op.operation === "conversation_current") return page(["m"]);
      throw new Error("unexpected fallback");
    });
    const result = await new ChatGptDetailFetcher(t,workspace).fetchAll([item("good"),item()]);
    expect(result.conversations.map(c=>c.source)).toEqual(["batch","current"]);
    expect(t.request.mock.calls.map(([op])=>op.operation)).toEqual(["conversation_batch","conversation_current"]);
  });
  it("initial plural 404 alone enters singular fallback", async () => {
    const t = transport(op => { if (op.operation === "conversation_detail") return conversationDetail() as any; throw http(404); });
    expect((await new ChatGptDetailFetcher(t,workspace).fetchAll([item()])).conversations[0]!.source).toBe("single");
    expect(t.request.mock.calls.map(([op])=>op.operation)).toEqual(["conversation_batch","conversation_current","conversation_detail"]);
  });
  it("retry preserves cursor and does not duplicate messages", async () => {
    let attempts = 0;
    const t = transport(op => {
      if(op.operation === "conversation_batch") throw http(404);
      if(op.operation === "conversation_current") return page(["b"], "a+/=%?&:");
      if(op.operation === "conversation_messages") { if(attempts++ === 0) throw http(429); return page(["a","b"]); }
      throw new Error("unexpected");
    });
    const controlled = new ControlledTransport(t,{delayMs:0,maxConcurrency:1,maxRetries:1,sleep:async()=>undefined});
    const result = await new ChatGptDetailFetcher(controlled,workspace).fetchAll([item()]);
    const calls=t.request.mock.calls.map(([op])=>op);
    expect(calls[2]).toEqual(calls[3]);
    expect(calls.map(op=>op.operation)).toEqual(["conversation_batch","conversation_current","conversation_messages","conversation_messages"]);
    expect(Object.keys(result.conversations[0]!.detail.mapping)).toHaveLength(3);
  });
});

describe("X1 determinism and provider-field preservation", () => {
  it.each([[4,0],[1,0],[3,1],[2,2]])("canonical JSON invariant at width %i overlap %i", async (width,overlap) => {
    const one=await fetchPages(history(12,12));
    const split=await fetchPages(history(12,width,overlap));
    expect(normalizeConversation(split.detail,item(),workspace.workspaceFingerprint)).toEqual(normalizeConversation(one.detail,item(),workspace.workspaceFingerprint));
  });
  it.each([[4,0],[1,0],[3,1],[2,2]])("Markdown, ordering and assets invariant at width %i overlap %i", async (width,overlap) => {
    const one=await fetchPages(history(12,12)); const split=await fetchPages(history(12,width,overlap));
    const a=normalizeConversation(one.detail,item(),workspace.workspaceFingerprint), b=normalizeConversation(split.detail,item(),workspace.workspaceFingerprint);
    expect(renderConversationMarkdown(b)).toBe(renderConversationMarkdown(a));
    expect(b.messages).toEqual(a.messages); expect(discoverAssets(split.detail)).toEqual(discoverAssets(one.detail));
  });
  it.each(["user","assistant","system","developer","tool","future-role"])("retains older %s message fields", async role => {
    const m=message("old",role); m.future_field={preserve:[1,"x"]};
    const result=await fetchPages([page(["new"],"a"),page([],null,{messages:[m]})]);
    const normalized=normalizeConversation(result.detail,item(),workspace.workspaceFingerprint);
    expect(normalized.messages.find(x=>x.id==="old")!.extensions.chatgpt).toMatchObject(m);
  });
  it.each(["text","code","canvas","browsing_result","deep_research","multimodal_text","image_asset_pointer","audio_asset_pointer","file","future-content"])("retains older %s content and citations", async type => {
    const m=message("old"); m.content={content_type:type,parts:["text",{content_type:"future-part",value:23}],future:42};
    m.metadata={content_references:[{url:"https://example.com",title:"synthetic"}],deep_research_version:"synthetic"};
    const result=await fetchPages([page(["new"],"a"),page([],null,{messages:[m]})]);
    expect(normalizeConversation(result.detail,item(),workspace.workspaceFingerprint).messages.find(x=>x.id==="old")!.extensions.chatgpt).toMatchObject(m);
  });
  it("does not copy transport account, headers or session metadata into page evidence", async () => {
    const result=await fetchPages(history(3,1)); const text=JSON.stringify(result.raw);
    for(const forbidden of [workspace.accountId,"Authorization","Cookie","accessToken","session-token"]) expect(text).not.toContain(forbidden);
    expect((result.raw as any).__pagination_evidence.pages[0].response.messages).toHaveLength(1);
  });
  it("large synthetic history has bounded retained size", async () => {
    const result=await fetchPages(history(2000,25,2));
    expect(Object.keys(result.detail.mapping)).toHaveLength(2001);
    expect(JSON.stringify(result.raw).length).toBeLessThan(3_000_000);
  },15000);
  it("graph validation visits parent edges linearly", () => {
    let reads=0; const mapping:any={}; const count=400;
    for(let i=0;i<count;i++) mapping[`n-${i}`]={id:`n-${i}`,message:null,get parent(){reads++;return i===0?null:`n-${i-1}`;},children:i+1<count?[`n-${i+1}`]:[]};
    expect(graphFindings({mapping,current_node:`n-${count-1}`} as any)).toEqual([]);
    expect(reads).toBeLessThan(count*10);
  });
});
