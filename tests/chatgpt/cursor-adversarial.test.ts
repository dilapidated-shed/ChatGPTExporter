import {describe,expect,it} from "vitest";
import {resolveEndpoint} from "../../src/chatgpt/endpoints";

describe("X1 opaque cursor transport",()=>{
  for(const lane of ["project_page","project_conversation_page","conversation_messages"] as const){
    const op=(cursor:string):any=>lane==="project_page"?{operation:lane,parameters:{cursor}}:lane==="project_conversation_page"?{operation:lane,parameters:{projectId:"project-1",cursor}}:{operation:lane,parameters:{conversationId:"conversation-1",before:cursor,numTurns:10}};
    it.each(["=","+","/",":","%","?","&","x","x".repeat(2048),"λ日本語"])(`${lane} exact round trip (%#)`,cursor=>{
      const path=resolveEndpoint(op(cursor)).path; const url=new URL(path,"https://chatgpt.com");
      expect(url.searchParams.get(lane==="conversation_messages"?"before":"cursor")).toBe(cursor);
      expect(url.hash).toBe("");
      expect(path).toContain(new URLSearchParams({[lane==="conversation_messages"?"before":"cursor"]:cursor}).toString());
    });
    it.each(["","x".repeat(2049),"a\u0000b","a\nb","a\u007fb"])(`${lane} rejects local bounds/controls (%#)`,cursor=>{expect(()=>resolveEndpoint(op(cursor))).toThrow();});
  }
});
