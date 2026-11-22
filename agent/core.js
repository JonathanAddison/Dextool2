import Java from 'frida-java-bridge';
import * as features from './features.js';
const handles = new Map(); let nextH = 1;
const state = new Map();   // key cls::name#ov -> {klass,name,ov,ret,types,patch,hook,track,record,trackId,installed}
const fieldState = new Map(); // key cls::field -> {lockTimer,watchTimer,last,patch}
let appCtx = null;

function retain(o){ const h=nextH++; handles.set(h, Java.retain(o)); return h; }
function safeStr(o){ try { return ''+o; } catch(e){ return '<obj>'; } }
function key(c,n,ov){ return c+'::'+n+'#'+ov; }
function isPrim(t){ return ['int','long','short','byte','float','double','boolean','char'].indexOf(t)>=0; }
function coerce(t,v){
  if(t==='boolean') return v===true||v==='true';
  if(t==='char') return (''+v).charCodeAt(0)||0;
  if(t==='long'||t==='int'||t==='short'||t==='byte') return parseInt(v)||0;
  if(t==='float'||t==='double') return parseFloat(v)||0;
  return v;
}
function dec(enc,t,orig){
  if(!enc) return orig;
  switch(enc.k){
    case 'skip': return orig;
    case 'null': return null;
    case 'str': return ''+enc.v;
    case 'prim': return coerce(t,enc.v);
    case 'obj': { const o=handles.get(enc.h); return o===undefined?null:o; }
    default: return orig;
  }
}
function defRet(t){
  if(t==='void') return undefined;
  if(t==='boolean') return false;
  if(isPrim(t)) return 0;
  return null;
}
function stack(){
  try {
    const T = Java.use('java.lang.Thread');
    const st = T.currentThread().getStackTrace();
    const out=[];
    for(let i=3;i<st.length&&out.length<8;i++){
      const f=st[i]; const c=f.getClassName();
      if(c.startsWith('re.tool.')||c.startsWith('java.lang.Thread')) continue;
      out.push({c:c, s:''+f});
    }
    return out;
  } catch(e){ return []; }
}
function logCall(st,args,ret,phase){
  try{
    const ch = st.track ? 'track' : 'record';
    const id = st.track ? st.trackId : key(st.cls,st.name,st.ov);
    const a = args.map(safeStr).join(', ');
    const text = phase==='exit'
      ? st.cls+'.'+st.name+'('+a+') -> '+safeStr(ret)
      : st.cls+'.'+st.name+'('+a+')';
    const stk = (ch==='track') ? stack() : [];
    send({t:'log', ch:ch, id:id, cls:st.cls, name:st.name, text:text, stack:stk});
  }catch(e){}
}
function ensure(st){
  if(st.installed) return;
  const ovf = st.klass[st.name].overloads[st.ov];
  ovf.implementation = function(...args){
    let a = args;
    if(st.patch && st.patch.enabled && !st.patch.isField)
      a = args.map((v,i)=>dec(st.patch.args[i], st.types[i], v));
    const tracing = (st.track&&st.trackOn)||st.record;
    if(st.track&&st.trackOn) logCall(st,a,null,'enter');
    let r;
    if(st.hook && st.hook.enabled){
      if(st.hook.mode==='noop') r = defRet(st.ret);
      else if(st.hook.fn) r = st.hook.fn.call(this,a,()=>ovf.apply(this,a));
      else r = ovf.apply(this,a);
    } else r = ovf.apply(this,a);
    if(st.patch && st.patch.enabled && !st.patch.isField && st.patch.ret && st.patch.ret.k!=='skip')
      r = dec(st.patch.ret, st.ret, r);
    if(tracing) logCall(st,a,r,'exit');
    return r;
  };
  st.installed = true;
}
function maybeRemove(st){
  if(st.patch||st.hook||(st.track&&st.trackOn)||st.record) return;
  try { st.klass[st.name].overloads[st.ov].implementation = null; } catch(e){}
  state.delete(st.key);
}
function getSt(cls,name,ov){
  const k = key(cls,name,ov);
  let st = state.get(k);
  if(!st){
    const klass = Java.use(cls);
    const ovf = klass[name].overloads[ov];
    st = { key:k, cls:cls, name:name, ov:ov, klass:klass,
           ret:ovf.returnType.className, types:ovf.argumentTypes.map(t=>t.className),
           installed:false };
    state.set(k,st);
  }
  return st;
}
function setField(cls,name,h,type,val){
  const klass = Java.use(cls);
  const v = isPrim(type)?coerce(type,val):(type==='java.lang.String'?''+val:val);
  if(h){ Java.cast(handles.get(h),klass)[name].value = v; }
  else klass[name].value = v;
}
function readField(cls,name,h){
  const klass = Java.use(cls);
  const o = h?Java.cast(handles.get(h),klass):klass;
  return safeStr(o[name].value);
}
function classInfo(cls,h){
  const klass = Java.use(cls);
  const Modifier = Java.use('java.lang.reflect.Modifier');
  const methods=[], fields=[], ctors=[];
  const ms = klass.class.getDeclaredMethods();
  for(let i=0;i<ms.length;i++){
    const m=ms[i]; const n=m.getName();
    const ps=m.getParameterTypes(); const p=[];
    for(let j=0;j<ps.length;j++) p.push(ps[j].getName());
    const mods=m.getModifiers();
    let ov=-1;
    try {
      const ovs=klass[n].overloads;
      for(let o=0;o<ovs.length;o++){
        const at=ovs[o].argumentTypes.map(t=>t.className);
        if(at.length===p.length&&at.every((t,x)=>t===p[x])){ ov=o; break; }
      }
    } catch(e){}
    methods.push({n:n, mods:Modifier.toString(mods), ret:m.getReturnType().getName(),
                  p:p, st:Modifier.isStatic(mods), ov:ov});
  }
  const fs = klass.class.getDeclaredFields();
  const inst = h?Java.cast(handles.get(h),klass):null;
  for(let i=0;i<fs.length;i++){
    const f=fs[i]; const mods=f.getModifiers(); const st=Modifier.isStatic(mods);
    let v='';
    try { v = st?safeStr(klass[f.getName()].value):(inst?safeStr(inst[f.getName()].value):''); } catch(e){}
    fields.push({n:f.getName(), mods:Modifier.toString(mods), t:f.getType().getName(), st:st, v:v});
  }
  const cs = klass.class.getDeclaredConstructors();
  for(let i=0;i<cs.length;i++){
    const ps=cs[i].getParameterTypes(); const p=[];
    for(let j=0;j<ps.length;j++) p.push(ps[j].getName());
    ctors.push(p);
  }
  return {methods:methods, fields:fields, ctors:ctors};
}
function enumerate(filter,mode,sub){
  const f=(filter||'').toLowerCase();
  const names = Java.enumerateLoadedClassesSync();
  const out=[];
  if(mode===0){
    for(const n of names){ if(out.length>=500) break;
      if(!f||n.toLowerCase().indexOf(f)>=0) out.push(n); }
    return out;
  }
  let scanned=0;
  for(const n of names){
    if(out.length>=300||scanned>=20000) break;
    if(n.startsWith('[')||n.startsWith('re.tool.')) continue;
    scanned++;
    try {
      const k=Java.use(n);
      if(mode===1){
        const ms=k.class.getDeclaredMethods();
        for(let i=0;i<ms.length;i++){
          const m=ms[i]; let hit=false;
          if(sub===0) hit=m.getName().toLowerCase().indexOf(f)>=0;
          else if(sub===1) hit=m.getReturnType().getName().toLowerCase().indexOf(f)>=0;
          else { const ps=m.getParameterTypes();
            for(let j=0;j<ps.length;j++) if(ps[j].getName().toLowerCase().indexOf(f)>=0) hit=true; }
          if(hit&&f){ out.push(n); break; }
        }
      } else {
        const fs=k.class.getDeclaredFields();
        for(let i=0;i<fs.length;i++){
          const fl=fs[i];
          const s=(sub===0?fl.getName():fl.getType().getName()).toLowerCase();
          if(f&&s.indexOf(f)>=0){ out.push(n); break; }
        }
      }
    } catch(e){}
  }
  return out;
}
function doCreate(m){
  const klass=Java.use(m.cls);
  const sel=m.sel;
  let args=[];
  let ctor;
  const news=klass.$new;
  if(sel>=0&&news.overloads&&news.overloads[sel]){
    ctor=news.overloads[sel];
    args=m.args.map((e,i)=>dec(e,ctor.argumentTypes[i].className,null));
  } else {
    // default-args create: pick ctor with fewest params
    let best=null,bn=1e9;
    if(news.overloads) for(const o of news.overloads)
      if(o.argumentTypes.length<bn){ bn=o.argumentTypes.length; best=o; }
    ctor=best;
    if(ctor) args=ctor.argumentTypes.map(t=>{
      const c=t.className;
      if(c==='boolean') return false;
      if(isPrim(c)) return 0;
      if(c==='java.lang.String') return '';
      return null;
    });
  }
  const obj = ctor?ctor.apply(klass,args):klass.$new();
  const h=retain(obj);
  send({t:'created', ctx:m.ctx, h:h, label:safeStr(obj), cls:m.cls});
}
function dispatch(m){
  const t=m.t;
  if(t==='enum'){ send({t:'classes', ctx:m.ctx, items:enumerate(m.filter,m.mode,m.sub)}); }
  else if(t==='info'){ const i=classInfo(m.cls,m.h); i.t='info'; i.ctx=m.ctx; i.cls=m.cls; send(i); }
  else if(t==='call'){
    try {
      const klass=Java.use(m.cls);
      const ovf=klass[m.name].overloads[m.ov];
      const recv=m.h?Java.cast(handles.get(m.h),klass):klass;
      const args=m.args.map((e,i)=>dec(e,ovf.argumentTypes[i].className,null));
      const r=ovf.apply(recv,args);
      send({t:'callResult', ctx:m.ctx, ok:true, text:safeStr(r), cls:m.cls});
    } catch(e){ send({t:'callResult', ctx:m.ctx, ok:false, text:''+e, cls:m.cls}); }
  }
  else if(t==='patch'){
    const r=m.rec;
    if(r.isField){
      const fk=r.cls+'::'+r.member;
      let fs=fieldState.get(fk)||{};
      if(fs.lockTimer){ clearInterval(fs.lockTimer); fs.lockTimer=null; }
      if(r.enabled){
        setField(r.cls,r.member,0,r.type,r.ret.v);
        if(r.lock) fs.lockTimer=setInterval(()=>{ try{ setField(r.cls,r.member,0,r.type,r.ret.v);}catch(e){} },1000);
      }
      fs.patch=r; fieldState.set(fk,fs);
    } else {
      const st=getSt(r.cls,r.member,r.ov);
      st.patch=r.enabled?r:null; ensure(st);
    }
    send({t:'toast', msg:'patch '+(r.enabled?'applied':'disabled')});
  }
  else if(t==='patchDel'){
    const p=m.id;
    for(const [k,st] of state) if(st.patch&&st.patch.id===p){ st.patch=null; maybeRemove(st); }
    const fs=fieldState.get(p); if(fs&&fs.lockTimer) clearInterval(fs.lockTimer);
    fieldState.delete(p);
  }
  else if(t==='hook'){
    const r=m.rec;
    if(r.mode==='smali'){ send({t:'toast', msg:'smali mode ships in phase 2'}); return; }
    const st=getSt(r.cls,r.name,r.ov);
    if(r.mode==='java'){
      try { r.fn=new Function('a','orig',r.code); }
      catch(e){ send({t:'toast', msg:'hook compile error: '+e}); return; }
    }
    st.hook=r.enabled?r:null; ensure(st);
    send({t:'toast', msg:'hook '+(r.enabled?'applied':'disabled')});
  }
  else if(t==='hookDel'){
    for(const [k,st] of state) if(st.hook&&st.hook.id===m.id){ st.hook=null; maybeRemove(st); }
  }
  else if(t==='track'){
    if(m.isField){
      const fk=m.cls+'::'+m.member;
      let fs=fieldState.get(fk)||{};
      if(fs.watchTimer){ clearInterval(fs.watchTimer); fs.watchTimer=null; }
      if(m.on){
        fs.last=readField(m.cls,m.member,0);
        fs.watchTimer=setInterval(()=>{
          try { const v=readField(m.cls,m.member,0);
            if(v!==fs.last){ send({t:'log', ch:'track', id:m.id, cls:m.cls, name:m.member,
              text:m.cls+'.'+m.member+': '+fs.last+' -> '+v, stack:[]}); fs.last=v; } } catch(e){}
        },1000);
      }
      fieldState.set(fk,fs);
    } else {
      const st=getSt(m.cls,m.member,m.ov);
      st.track=m.on; st.trackOn=m.on; st.trackId=m.id;
      if(m.on) ensure(st); else maybeRemove(st);
    }
  }
  else if(t==='trackDel'){
    for(const [k,st] of state) if(st.trackId===m.id){ st.track=false; st.trackOn=false; maybeRemove(st); }
    const fs=fieldState.get(m.cls+'::'+m.member);
    if(fs&&fs.watchTimer){ clearInterval(fs.watchTimer); fs.watchTimer=null; }
  }
  else if(t==='inst'){
    const items=[];
    Java.choose(m.cls,{
      onMatch(o){ if(items.length<200) items.push({h:retain(o), s:safeStr(o)}); },
      onComplete(){ send({t:'inst', ctx:m.ctx, items:items}); }
    });
  }
  else if(t==='create'){ doCreate(m); }
  else if(t==='fieldSet'){ try{ setField(m.cls,m.name,m.h,m.type,m.val); send({t:'toast',msg:'field set'});}catch(e){send({t:'toast',msg:''+e});} }
  else if(t==='fieldGet'){ try{ send({t:'fieldVal', ctx:m.ctx, val:readField(m.cls,m.name,m.h)});}catch(e){send({t:'fieldVal',ctx:m.ctx,val:''+e});} }
  else if(t==='eval'){ try{ const r=(0,eval)(m.code); if(r!==undefined) console.log(''+r);}catch(e){console.log('ERROR '+e);} }
  else if(t==='dump'){ features.dumpDex(); }
  else if(t==='record'){ features.record(m.on,m.scope,state,getSt,ensure,maybeRemove); }
  else if(t==='monitor'){ features.monitor(m.on); }
  else if(t==='settings'){ features.setPublish(m.publish); }
}
function init(ctx){ appCtx=ctx; }
export { init, dispatch, handles, getSt, ensure, maybeRemove };