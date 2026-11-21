import { Buffer } from 'buffer';
globalThis.Buffer = Buffer;
globalThis.global = globalThis;
if (typeof globalThis.process === 'undefined') {
  globalThis.process = { env:{}, platform:'android', version:'', versions:{},
    nextTick:(f,...a)=>setTimeout(()=>f(...a),0) };
}
if (typeof globalThis.setImmediate === 'undefined') globalThis.setImmediate = (f,...a)=>setTimeout(()=>f(...a),0);
if (typeof globalThis.clearImmediate === 'undefined') globalThis.clearImmediate = id=>clearTimeout(id);
if (typeof globalThis.queueMicrotask === 'undefined') globalThis.queueMicrotask = f=>Promise.resolve().then(f);
if (typeof globalThis.TextEncoder === 'undefined') {
  globalThis.TextEncoder = class TextEncoder {
    get encoding(){ return 'utf-8'; }
    encode(s=''){ s=String(s); const out=[];
      for(let i=0;i<s.length;i++){ let c=s.codePointAt(i); if(c>0xffff)i++;
        if(c<0x80) out.push(c);
        else if(c<0x800) out.push(0xc0|(c>>6),0x80|(c&63));
        else if(c<0x10000) out.push(0xe0|(c>>12),0x80|((c>>6)&63),0x80|(c&63));
        else out.push(0xf0|(c>>18),0x80|((c>>12)&63),0x80|((c>>6)&63),0x80|(c&63)); }
      return new Uint8Array(out); }
    encodeInto(s,dst){ const b=this.encode(s); const n=Math.min(b.length,dst.length);
      dst.set(b.subarray(0,n)); return {read:String(s).length,written:n}; }
  };
}
if (typeof globalThis.TextDecoder === 'undefined') {
  globalThis.TextDecoder = class TextDecoder {
    constructor(enc='utf-8'){ this.encoding=String(enc).toLowerCase(); }
    decode(input){ if(!input) return '';
      const b = input instanceof ArrayBuffer ? new Uint8Array(input)
        : new Uint8Array(input.buffer, input.byteOffset||0, input.byteLength||input.length);
      let s='', i=0;
      while(i<b.length){ let c=b[i++], cp, extra;
        if(c<0x80){ cp=c; extra=0; }
        else if((c&0xe0)===0xc0){ cp=c&31; extra=1; }
        else if((c&0xf0)===0xe0){ cp=c&15; extra=2; }
        else if((c&0xf8)===0xf0){ cp=c&7; extra=3; }
        else { s+='\ufffd'; continue; }
        if(i+extra>b.length){ s+='\ufffd'; break; }
        let ok=true;
        for(let k=0;k<extra;k++){ const d=b[i++]; if((d&0xc0)!==0x80){ ok=false; break; } cp=(cp<<6)|(d&63); }
        if(!ok||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)){ s+='\ufffd'; continue; }
        if(cp>0xffff){ cp-=0x10000; s+=String.fromCharCode(0xd800+(cp>>10),0xdc00+(cp&1023)); }
        else s+=String.fromCharCode(cp);
      }
      return s; }
  };
}
