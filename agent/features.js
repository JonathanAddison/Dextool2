const Java = require('frida-java-bridge');
let ctxRef=null, dirPath='', publish=true;
let sslHooks=[], urlHook=null, recording=[], monitorOn=false;

function writeFile(name,bytes){
  const File=Java.use('java.io.File');
  const f=File.$new(dirPath,name);
  const fos=Java.use('java.io.FileOutputStream').$new(f);
  fos.write(bytes); fos.close();
  return f.getAbsolutePath();
}
function publishToDownloads(name,path){
  if(!publish) return;
  try {
    const Build=Java.use('android.os.Build$VERSION');
    if(Build.SDK_INT.value>=29){
      const CV=Java.use('android.content.ContentValues');
      const cv=CV.$new();
      cv.put('_display_name',name);
      cv.put('mime_type','application/octet-stream');
      const MS=Java.use('android.provider.MediaStore$Downloads');
      const resolver=ctxRef.getContentResolver();
      const uri=resolver.insert(MS.EXTERNAL_CONTENT_URI.value,cv);
      if(uri!==null){
        const os=resolver.openOutputStream(uri);
        const fis=Java.use('java.io.FileInputStream').$new(path);
        const buf=Java.array('byte',new Array(8192).fill(0));
        let n; while((n=fis.read(buf))>0) os.write(buf,0,n);
        os.close(); fis.close();
      }
    }
  } catch(e){}
}
function installJavaCrash(){
  const prev=Java.use('java.lang.Thread').getDefaultUncaughtExceptionHandler();
  const H=Java.registerClass({
    name:'re.tool.Crash'+Date.now(),
    implements:[Java.use('java.lang.Thread$UncaughtExceptionHandler')],
    methods:{
      uncaughtException(t,e){
        try {
          const Log=Java.use('android.util.Log');
          const s=Log.getStackTraceString(e);
          const p=writeFile('jcrash_'+Date.now()+'.txt',Java.array('byte',Array.from(s).map(c=>c.charCodeAt(0))));
          publishToDownloads(p.split('/').pop(),p);
        } catch(x){}
        if(prev!==null) prev.uncaughtException(t,e);
      }
    }
  });
  Java.use('java.lang.Thread').setDefaultUncaughtExceptionHandler(H.$new());
}
function dumpDex(){
  const seen=new Set(); let count=0;
  const ranges=Process.enumerateRanges('r--');
  for(const r of ranges){
    if(r.size>128*1024*1024) continue;
    let hits=[];
    try { hits=Memory.scanSync(r.base,r.size,'64 65 78 0a 30 33'); } catch(e){ continue; }
    for(const h of hits){
      const base=h.address;
      if(seen.has(base.toString())) continue;
      seen.add(base.toString());
      try {
        const size=base.add(32).readU32();
        if(size<112||size>r.size) continue;
        const bytes=base.readByteArray(size);
        send({t:'dex', name:'classes_'+base+'.dex', size:size}, bytes);
        count++;
      } catch(e){}
    }
  }
  send({t:'toast', msg:'dumped '+count+' dex image(s)'});
}
function record(on,scope,state,getSt,ensure,maybeRemove){
  if(on){
    const names=Java.enumerateLoadedClassesSync().filter(n=>n.startsWith(scope)&&!n.startsWith('['));
    let classes=0,methods=0;
    for(const n of names){
      if(classes>=300||methods>=3000) break;
      try {
        const k=Java.use(n);
        const ms=k.class.getDeclaredMethods();
        const done=new Set();
        for(let i=0;i<ms.length;i++){
          const name=ms[i].getName();
          if(done.has(name)) continue; done.add(name);
          let ovs; try{ ovs=k[name].overloads; }catch(e){ continue; }
          for(let o=0;o<ovs.length;o++){
            if(methods>=3000) break;
            const st=getSt(n,name,o);
            if(!st.record){ st.record=true; ensure(st); recording.push(st); methods++; }
          }
        }
        classes++;
      } catch(e){}
    }
    send({t:'toast', msg:'recording '+methods+' methods / '+classes+' classes (capped)'});
  } else {
    for(const st of recording){ st.record=false; maybeRemove(st); }
    recording=[];
    send({t:'toast', msg:'recorder stopped'});
  }
}
function javaStack(){
  try {
    if(Java.vm.tryGetEnv()===null) return [];
    let out=[];
    Java.performNow(()=>{
      const st=Java.use('java.lang.Thread').currentThread().getStackTrace();
      for(let i=3;i<st.length&&out.length<8;i++){
        const f=st[i]; out.push({c:f.getClassName(), s:''+f});
      }
    });
    return out;
  } catch(e){ return []; }
}
function preview(buf,len){
  const n=Math.min(len,512);
  const bytes=new Uint8Array(buf.readByteArray(n));
  let s='';
  for(const b of bytes) s+=(b>=32&&b<127)?String.fromCharCode(b):'.';
  return s;
}
function monitor(on){
  monitorOn=on;
  if(on){
    const ssl=Process.findModuleByName('libssl.so');
    if(ssl){
      const getfd=new NativeFunction(ssl.getExportByName('SSL_get_fd'),'int',['pointer']);
      const w=ssl.findExportByName('SSL_write');
      const r=ssl.findExportByName('SSL_read');
      if(w) sslHooks.push(Interceptor.attach(w,{
        onEnter(a){
          if(!monitorOn) return;
          const len=a[2].toInt32();
          send({t:'log', ch:'monitor', cls:'', name:'SSL_write',
            text:'-> fd'+getfd(a[0])+' '+len+'B',
            preview:preview(a[1],len), stack:javaStack()});
        }
      }));
      if(r) sslHooks.push(Interceptor.attach(r,{
        onEnter(a){ this.ssl=a[0]; this.buf=a[1]; },
        onLeave(ret){
          if(!monitorOn) return;
          const n=ret.toInt32(); if(n<=0) return;
          send({t:'log', ch:'monitor', cls:'', name:'SSL_read',
            text:'<- fd'+getfd(this.ssl)+' '+n+'B',
            preview:preview(this.buf,n), stack:javaStack()});
        }
      }));
    }
    try {
      const URL=Java.use('java.net.URL');
      urlHook=URL.openConnection.overload().implementation=function(){
        if(monitorOn) send({t:'log', ch:'monitor', cls:'java.net.URL', name:'openConnection',
          text:'URL '+this.toString(), preview:this.toString(), stack:javaStack()});
        return this.openConnection();
      };
    } catch(e){}
    send({t:'toast', msg:'monitor started (TLS plaintext + URL)'});
  } else {
    for(const h of sslHooks) Interceptor.detach(h);
    sslHooks=[];
    if(urlHook){ try{ Java.use('java.net.URL').openConnection.overload().implementation=null; }catch(e){} urlHook=null; }
    send({t:'toast', msg:'monitor stopped'});
  }
}
function init(ctx,dir){
  ctxRef=ctx; dirPath=dir;
  installJavaCrash();
}
module.exports={init:init, dumpDex:dumpDex, record:record, monitor:monitor,
                setPublish(p){ publish=p; }};
