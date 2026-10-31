import {describe,expect,it} from "vitest";
import {ChatGptAssetManager} from "../../src/chatgpt/assets";
import {conversationBasePath} from "../../src/core/paths";
import {MemoryArchiveFileSystem} from "../../src/core/filesystem";
import {conversationDetail} from "../fixtures/chatgpt";
import {workspace,item,transport} from "../fixtures/adversarial";

describe("X1 asset and archive identity isolation",()=>{
  it("same file id in different contexts requires independently scoped retrieval",async()=>{
    const fs=new MemoryArchiveFileSystem();const handles=new Map<string,Uint8Array>();
    const t=transport(op=>{
      if(op.operation==="asset_open"){
        const handleId=crypto.randomUUID();const context=op.parameters.conversationId??op.parameters.projectId!;
        const bytes=new TextEncoder().encode(context);handles.set(handleId,bytes);
        return {handleId,mediaType:"text/plain",expectedBytes:bytes.length,maxChunkBytes:1048576};
      }
      if(op.operation==="asset_chunk"){
        const bytes=handles.get(op.parameters.handleId)!;return {handleId:op.parameters.handleId,offset:0,nextOffset:bytes.length,byteLength:bytes.length,dataBase64:btoa(String.fromCharCode(...bytes)),eof:true,totalBytes:bytes.length,mediaType:"text/plain"};
      }
      if(op.operation==="asset_close")return {handleId:op.parameters.handleId,closed:handles.delete(op.parameters.handleId)};
      throw new Error("unexpected");
    });
    const manager=new ChatGptAssetManager({filesystem:fs,transport:t,workspace});
    const detail=conversationDetail();detail.mapping["user-1"]!.message!.content={content_type:"multimodal_text",parts:[{content_type:"file",file_id:"file-same"}]};
    const a=await manager.capture(detail,item("first"));const b=await manager.capture(detail,item("second"));
    const c=await manager.captureProject({projectId:"project-1",name:null,description:null,instructions:null,createTime:null,updateTime:null,rawHash:"synthetic",files:[{logicalId:"project-file",providerId:"file-same",originalName:null,mediaType:null,byteSize:null,rawDescriptor:{file_id:"file-same"}}]});
    expect([a.status,b.status,c.status]).toEqual(["complete","complete","complete"]);
    expect(new Set([a.assets[0]!.sha256,b.assets[0]!.sha256,c.assets[0]!.sha256]).size).toBe(3);
    expect(t.request.mock.calls.filter(([op])=>op.operation==="asset_open").map(([op])=>op.parameters)).toEqual([
      {fileId:"file-same",conversationId:"first",projectId:null},{fileId:"file-same",conversationId:"second",projectId:null},{fileId:"file-same",conversationId:null,projectId:"project-1"}
    ]);
  });
  it("accepted long conversation ids cannot collide on archive path truncation",()=>{
    const prefix="a".repeat(160);expect(conversationBasePath(prefix+"1")).not.toBe(conversationBasePath(prefix+"2"));
  });
  it("accepted leading-hyphen conversation ids cannot collide after sanitizing",()=>{
    expect(conversationBasePath("-conversation")).not.toBe(conversationBasePath("conversation"));
  });
});
