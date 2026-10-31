import {describe,expect,it} from "vitest";
import {ChatGptCaptureEngine} from "../../src/chatgpt/capture-engine";
import {auditArchive} from "../../src/chatgpt/audit";
import {CaptureStore} from "../../src/core/capture-store";
import {sha256Hex} from "../../src/core/hash";
import {prettyJson} from "../../src/core/serialization";
import {MemoryArchiveFileSystem, type ArchiveFileSystem} from "../../src/core/filesystem";
import {conversationDetail} from "../fixtures/chatgpt";
import {workspace,item,page,history,pagesTransport,transport,http,filesystem,fixedNow} from "../fixtures/adversarial";

const base="conversations/conversation-1";
function run(fs:ArchiveFileSystem,t:any,id="capture",assets=false){return new ChatGptCaptureEngine({filesystem:fs,transport:t,workspace,runId:id,now:fixedNow,includeAssets:assets,includeAccountArtifacts:false}).run();}
async function captured(){const fs=await filesystem();await run(fs,pagesTransport(history(6,2)));return fs;}
async function changeRaw(fs:MemoryArchiveFileSystem,mutate:(raw:any,marker:any)=>void){
  const marker=JSON.parse((await fs.readText(`${base}/raw-complete.json`))!);
  const raw=JSON.parse((await fs.readText(marker.detailPath))!);mutate(raw,marker);
  const text=prettyJson(raw); marker.detailHash=await sha256Hex(text);
  // Use a consistent content-addressed name, so rejection cannot rely on stale filenames.
  marker.detailPath=`${base}/source/detail-${marker.detailHash}.json`;
  await fs.writeTextAtomic(marker.detailPath,text);
  const markerText=prettyJson(marker);await fs.writeTextAtomic(`${base}/raw-complete.json`,markerText);
  const completion=JSON.parse((await fs.readText(`${base}/complete.json`))!);
  completion.detailHash=marker.detailHash;completion.rawMarkerHash=await sha256Hex(markerText);
  await fs.writeTextAtomic(`${base}/complete.json`,prettyJson(completion));
}
const mutations:Array<[string,(raw:any,marker:any)=>void]>=[
  ["remove first page",r=>{r.__pagination_evidence.pages.shift();r.__pagination_evidence.page_count--;}],
  ["remove middle page",r=>{r.__pagination_evidence.pages.splice(1,1);r.__pagination_evidence.page_count--;}],
  ["remove last page",r=>{r.__pagination_evidence.pages.pop();r.__pagination_evidence.page_count--;}],
  ["reorder pages",r=>r.__pagination_evidence.pages.reverse()],
  ["duplicate page",r=>{r.__pagination_evidence.pages.splice(1,0,r.__pagination_evidence.pages[1]);r.__pagination_evidence.page_count++;}],
  ["alter before",r=>r.__pagination_evidence.pages[1].request.before="wrong"],
  ["alter start_cursor",r=>r.__pagination_evidence.pages[0].response.page_info.start_cursor="wrong"],
  ["alter page_count",r=>r.__pagination_evidence.page_count++],
  ["append after terminal",r=>{r.__pagination_evidence.pages.push(structuredClone(r.__pagination_evidence.pages.at(-1)));r.__pagination_evidence.page_count++;}],
  ["complete flag with unfinished tail",r=>{r.__pagination_evidence.pages.at(-1).response.page_info={has_previous_page:true,start_cursor:"unfetched"};r.__pagination_evidence.complete=true;}],
  ["current source without evidence",r=>delete r.__pagination_evidence],
  ["wrong response identity",r=>r.__pagination_evidence.pages[1].response.conversation_id="other-conversation"],
  ["wrong request identity",r=>r.__pagination_evidence.pages[1].request.conversation_id="other-conversation"],
  ["wrong operation",r=>r.__pagination_evidence.pages[0].request.operation="conversation_messages"],
  ["missing provider messages",r=>delete r.__pagination_evidence.pages[1].response.messages],
  ["mapping contradicts retained content",r=>r.mapping["m-00000"].message.content.parts=["altered"]],
  ["source tag launders incomplete plural evidence",(r,m)=>{m.retrievalSource="single";r.__pagination_evidence.pages.pop();r.__pagination_evidence.page_count--;}],
];

describe("X1 structural completeness independent of hashes",()=>{
  it.each(mutations)("resume rejects %s after recomputing hashes",async(_name,mutate)=>{
    const fs=await captured();await changeRaw(fs,mutate);
    expect(await new CaptureStore(fs,"resume",workspace.workspaceFingerprint).validRawMarker(item())).toBeUndefined();
  });
  it.each(mutations)("audit rejects %s after recomputing hashes",async(_name,mutate)=>{
    const fs=await captured();await changeRaw(fs,mutate);
    expect((await auditArchive({filesystem:fs,extensionVersion:"synthetic",now:fixedNow})).terminalState).toBe("incomplete");
  });
  it.each(["conversation.json","conversation.md","raw-complete.json","complete.json"])("audit rejects corrupted %s",async name=>{
    const fs=await captured();await fs.writeTextAtomic(`${base}/${name}`,"corrupt");
    expect((await auditArchive({filesystem:fs,extensionVersion:"synthetic"})).terminalState).toBe("incomplete");
  });
  it("current-branch archive must expose graph/version coverage",async()=>{
    const fs=await captured();const report=await auditArchive({filesystem:fs,extensionVersion:"synthetic"});
    // Accept any explicit representation, not one arbitrarily prescribed property name.
    const labels=JSON.stringify({report,normalized:JSON.parse((await fs.readText(`${base}/conversation.json`))!)});
    expect(labels).toMatch(/current.branch|branch.coverage|graph.coverage|provider.graph.unknown/i);
  });
});

describe("X1 interruption and historical compatibility",()=>{
  it.each([0,1,2,3])("interruption before page %i cannot create a completion",async index=>{
    const fs=await filesystem();const pages=history(8,2);pages[index]=http(500);
    await expect(run(fs,pagesTransport(pages))).rejects.toThrow();
    expect(await fs.exists(`${base}/raw-complete.json`)).toBe(false);
    expect(await fs.exists(`${base}/complete.json`)).toBe(false);
    const result=await run(fs,pagesTransport(history(8,2)),"resume");expect(result.capturedCount).toBe(1);
  });
  it.each(["conversation.json","conversation.md","complete.json"])("rebuilds without network after interruption before %s",async filename=>{
    const fs=await filesystem();const write=fs.writeTextAtomic.bind(fs);let failed=false;
    fs.writeTextAtomic=async(path,text)=>{if(!failed&&path===`${base}/${filename}`){failed=true;throw new Error("synthetic interrupted write");}return write(path,text);};
    await expect(run(fs,pagesTransport(history(6,2)))).rejects.toThrow("synthetic interrupted write");
    const t=transport(()=>{throw new Error("network forbidden");});
    expect((await run(fs,t,"resume")).rebuiltCount).toBe(1);expect(t.request).not.toHaveBeenCalled();
  });
  it("corrupted completion marker rebuilds from validated raw",async()=>{
    const fs=await captured();await fs.writeTextAtomic(`${base}/complete.json`,"broken");
    const t=transport(()=>{throw new Error("network forbidden");});
    expect((await run(fs,t,"resume")).rebuiltCount).toBe(1);expect(t.request).not.toHaveBeenCalled();
  });
  it.each(["batch","single"])("legacy %s raw format remains network-free resumable",async source=>{
    const fs=await filesystem();const t=transport(op=>{
      if(source==="batch"&&op.operation==="conversation_batch")return [conversationDetail()] as any;
      if(source==="single"&&op.operation==="conversation_detail")return conversationDetail() as any;
      throw http(404);
    });
    await run(fs,t);await fs.remove(`${base}/conversation.md`);
    const offline=transport(()=>{throw new Error("network forbidden");});expect((await run(fs,offline,"resume")).rebuiltCount).toBe(1);
    expect(offline.request).not.toHaveBeenCalled();
  });
  it("legacy source label does not grandfather a disconnected graph",async()=>{
    const fs=await filesystem();await run(fs,transport(()=>[conversationDetail()] as any));
    await changeRaw(fs,r=>{r.mapping["user-1"].parent="missing";});
    expect(await new CaptureStore(fs,"resume",workspace.workspaceFingerprint).validRawMarker(item())).toBeUndefined();
  });
});

describe("X1 historical assets and isolation",()=>{
  function assetPages(){const pages=history(6,2,1);for(const p of pages)for(const m of p.messages)if(m.id==="m-00000"||m.id==="m-00002")m.content={content_type:"multimodal_text",parts:[{content_type:"file",asset_pointer:"data:text/plain;base64,c3ludGhldGlj"}]};return pages;}
  it("oldest-page and overlap references yield one physical asset and stable index",async()=>{
    const fs=await filesystem();await run(fs,pagesTransport(assetPages()),"assets",true);
    const index=JSON.parse((await fs.readText(`${base}/assets.json`))!);
    expect(index.status).toBe("complete");expect(index.assets).toHaveLength(2);
    expect((await fs.listPaths("assets")).length).toBe(1);
    const rows=(await fs.readText("indexes/assets.jsonl"))!.trim().split("\n").map(x=>JSON.parse(x));
    expect(rows).toHaveLength(2);expect(new Set(rows.map(x=>x.logicalId)).size).toBe(2);
  });
  it("asset write interruption retries without conversation requests",async()=>{
    const fs=await filesystem();const write=fs.writeByteChunksAtomic.bind(fs);let failed=false;
    fs.writeByteChunksAtomic=async(path,bytes)=>{if(!failed&&path.startsWith("assets/")){failed=true;throw new Error("synthetic asset interruption");}return write(path,bytes);};
    const first=await run(fs,pagesTransport(assetPages()),"first",true);expect(first.partialAssetCount).toBe(1);
    const t=transport(()=>{throw new Error("network forbidden");});const second=await run(fs,t,"resume",true);
    expect(second.partialAssetCount).toBe(0);expect(t.request).not.toHaveBeenCalled();
  });
  it("failed assets prevent a complete archive",async()=>{
    const fs=await filesystem();const pages=history(2,1);pages[1].messages[0].content={content_type:"multimodal_text",parts:[{content_type:"file",asset_pointer:"sediment://file-missing"}]};
    await run(fs,pagesTransport(pages),"assets",true);
    expect((await auditArchive({filesystem:fs,extensionVersion:"synthetic"})).terminalState).not.toBe("complete");
  });
  it("resume repairs missing physical assets instead of trusting index hashes",async()=>{
    const fs=await filesystem();await run(fs,pagesTransport(assetPages()),"first",true);
    for(const path of await fs.listPaths("assets"))await fs.remove(path);
    const offline=transport(()=>{throw new Error("network forbidden");});
    await run(fs,offline,"resume",true);
    expect((await fs.listPaths("assets")).length).toBe(1);
    expect(offline.request).not.toHaveBeenCalled();
  });
  it("audit detects asset references removed from the index with refreshed hashes",async()=>{
    const fs=await filesystem();await run(fs,pagesTransport(assetPages()),"first",true);
    const assets=JSON.parse((await fs.readText(`${base}/assets.json`))!);assets.assets=[];
    const assetsText=prettyJson(assets);await fs.writeTextAtomic(`${base}/assets.json`,assetsText);
    const completion=JSON.parse((await fs.readText(`${base}/complete.json`))!);completion.assetsHash=await sha256Hex(assetsText);
    await fs.writeTextAtomic(`${base}/complete.json`,prettyJson(completion));await fs.writeTextAtomic("indexes/assets.jsonl","");
    expect((await auditArchive({filesystem:fs,extensionVersion:"synthetic"})).terminalState).not.toBe("complete");
  });
  it.each([1,2,3])("semantic metadata and nonempty asset index agree at page width %i",async width=>{
    const captures=[];
    for(const w of [6,width]){
      const fs=await filesystem();const pages=history(6,w,1);
      for(const p of pages)for(const m of p.messages)if(m.id==="m-00000")m.content={content_type:"multimodal_text",parts:[{content_type:"file",asset_pointer:"data:text/plain;base64,c3ludGhldGlj"}]};
      await run(fs,pagesTransport(pages),"determinism",true);
      const metadata=JSON.parse((await fs.readText(`${base}/metadata.json`))!);delete metadata.detailHash;
      captures.push({metadata,assets:await fs.readText(`${base}/assets.json`),index:await fs.readText("indexes/assets.jsonl"),markdown:await fs.readText(`${base}/conversation.md`)});
    }
    expect(captures[1]).toEqual(captures[0]);
  });
  it("a fully captured earlier record survives a later failure in the same batch",async()=>{
    const fs=await filesystem([item("good"),item("bad")]);
    const t=transport(op=>{
      if(op.operation==="conversation_batch")return [conversationDetail({id:"good"})] as any;
      if(op.operation==="conversation_current")return page(["bad-new"],"bad-cursor",{conversation_id:"bad"});
      throw http(500);
    });
    await expect(run(fs,t)).rejects.toThrow();
    expect(await fs.exists("conversations/good/complete.json")).toBe(true);
    expect(await fs.exists("conversations/bad/complete.json")).toBe(false);
  });
  it("parallel independent captures keep raw pages, ids and failures isolated",async()=>{
    const ids=["batch","plural","bad","share_shared","asset"];
    const runs=ids.map(async id=>{
      const inv=item(id);if(id==="share_shared")inv.memberships=[{scope:"shared",shareId:"shared"}];
      const fs=await filesystem([inv]);let pageNumber=0;
      const t=transport(async op=>{
        await Promise.resolve();
        if(op.operation==="conversation_batch"){if(id==="batch")return [conversationDetail({id})] as any;throw http(404);}
        if(op.operation==="shared_detail")return conversationDetail({id:"provider-share"}) as any;
        if(op.operation==="conversation_current"||op.operation==="conversation_messages"){
          if(id==="bad"&&pageNumber>0)throw http(500);
          const p=page([`${id}-${pageNumber}`],pageNumber++===0?`${id}-cursor`:null,{conversation_id:id});
          if(id==="asset")p.messages[0].content={content_type:"multimodal_text",parts:[{content_type:"file",asset_pointer:"data:text/plain;base64,c3ludGhldGlj"}]};
          return p;
        }throw new Error("unexpected");
      });
      const result=await Promise.allSettled([run(fs,t,id,true)]);
      expect(result[0]!.status).toBe(id==="bad"?"rejected":"fulfilled");
      const paths=await fs.listPaths("conversations");expect(paths.every(p=>p.startsWith(`conversations/${id}/`))).toBe(true);
      if(id!=="bad"){
        const marker=JSON.parse((await fs.readText(`conversations/${id}/raw-complete.json`))!);expect(marker.conversationId).toBe(id);
        const raw=JSON.parse((await fs.readText(marker.detailPath))!);
        for(const p of raw.__pagination_evidence?.pages??[])expect(p.request.conversation_id).toBe(id);
      }
    });await Promise.all(runs);
  });
});
