import {describe,expect,it} from "vitest";
import {ChatGptInventoryEngine,DEFAULT_INVENTORY_SETTINGS} from "../../src/chatgpt/inventory";
import {MemoryArchiveFileSystem} from "../../src/core/filesystem";
import {auditArchive} from "../../src/chatgpt/audit";
import {workspace,transport,fixedNow} from "../fixtures/adversarial";

const settings={...DEFAULT_INVENTORY_SETTINGS,pageSize:2,includeArchived:false,includeProjects:false,includeShared:false};
const listing=(ids:string[],total:number|null=null)=>({items:ids.map(id=>({id,title:id,create_time:1,update_time:2})),total});
describe("X1 inventory movement and terminal evidence",()=>{
  it("does not certify a count hint while a full page can have unseen successors",async()=>{
    const t=transport(op=>{if(op.operation!=="conversation_page")throw new Error("unexpected");return op.parameters.offset===0?listing(["a","b"],2):op.parameters.offset===2?listing(["c"],3):listing([],3);});
    const inv=await new ChatGptInventoryEngine({transport:t,filesystem:new MemoryArchiveFileSystem(),workspace,settings}).run();
    expect(inv.conversations.map(c=>c.conversationId)).toContain("c");
  });
  it.each([
    ["insert at top",[["a","b"],["b","c"],["d"],[]], ["a","b","c","d"]],
    ["update moves unseen item to top",[["a","b"],["b","d"],[]], ["a","b","d"]],
    ["later duplicate",[["a","b"],["b","c"],[]],["a","b","c"]],
    ["observed item disappears",[["a","b"],["d"],[]],["a","b","d"]],
  ])("retains every observed id during %s without assuming snapshot isolation",async(_name,pages,expected)=>{
    let index=0;const t=transport(()=>listing((pages as string[][])[index++]!));
    const inv=await new ChatGptInventoryEngine({transport:t,filesystem:new MemoryArchiveFileSystem(),workspace,settings}).run();
    expect(inv.conversations.map(c=>c.conversationId)).toEqual(expected);
    expect(new Set(inv.conversations.map(c=>c.conversationId)).size).toBe(inv.conversations.length);
  });
  it("does not label a demonstrated moving offset listing as proven complete",async()=>{
    const pages=[["a","b"],["b","d"],[]];let index=0;
    const inv=await new ChatGptInventoryEngine({transport:transport(()=>listing(pages[index++]!)),filesystem:new MemoryArchiveFileSystem(),workspace,settings}).run();
    // c moved from unread offset 2 to the top. Empty termination cannot prove c absent.
    expect(inv.complete).toBe(false);
  });
  it("unions changing archived/project/shared membership for the same id",async()=>{
    const t=transport(op=>{
      if(op.operation==="conversation_page")return listing(["a"],1);
      if(op.operation==="project_page")return {items:[{gizmo:{gizmo:{id:"project-1"}}}],cursor:null};
      if(op.operation==="project_conversation_page")return {items:[{id:"a"}],cursor:null};
      if(op.operation==="shared_page")return {items:[{id:"share-1",conversation_id:"a"}],total:1};
      throw new Error("unexpected");
    });
    const inv=await new ChatGptInventoryEngine({transport:t,filesystem:new MemoryArchiveFileSystem(),workspace,settings:DEFAULT_INVENTORY_SETTINGS}).run();
    expect(inv.conversations).toHaveLength(1);expect(inv.conversations[0]!.memberships.map(m=>m.scope).sort()).toEqual(["archived","main","project","shared"]);
  });
  it.each(["same page","malformed total","non-array items"])("rejects %s without publishing complete inventory",async kind=>{
    const fs=new MemoryArchiveFileSystem();const t=transport(()=>kind==="same page"?listing(["a","b"]):kind==="malformed total"?{items:[],total:"no"}:{items:{},total:0});
    await expect(new ChatGptInventoryEngine({transport:t,filesystem:fs,workspace,settings}).run()).rejects.toThrow();
    expect(await fs.exists("inventory.json")).toBe(false);
  });
  it("audit detects missing authoritative inventory pages",async()=>{
    const fs=new MemoryArchiveFileSystem();await new ChatGptInventoryEngine({transport:transport(()=>listing([],0)),filesystem:fs,workspace,settings,now:fixedNow}).run();
    for(const path of await fs.listPaths("source/inventory"))await fs.remove(path);
    expect((await auditArchive({filesystem:fs,extensionVersion:"synthetic"})).terminalState).toBe("incomplete");
  });
});
