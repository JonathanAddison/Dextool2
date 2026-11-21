import './polyfill.js';
import Java from 'frida-java-bridge';

function D(m){ try{ send({t:'diag', msg:'SMOKE '+m}); }catch(e){} }
const origLog = console.log.bind(console);
['log','warn','error'].forEach(k=>{
  console[k]=(...a)=>{ const s=a.map(String).join(' '); D('console.'+k+': '+s); origLog(...a); };
});
function cmdLoop(m){
  if(m && m.t==='ping') D('PASS native->agent roundtrip');
  else D('cmd '+JSON.stringify(m));
  recv('cmd',cmdLoop);
}
recv('cmd',cmdLoop);
D('start');
['Buffer','TextEncoder','TextDecoder','setTimeout','NativeFunction','Module','Process','Interceptor','Memory']
  .forEach(n=>D((typeof globalThis[n]!=='undefined'?'PASS global ':'FAIL global ')+n));

let tries=0, attached=false, NF=null;
function natives(){
  if(NF) return NF;
  const g=n=>Module.getExportByName('libtool.so',n);
  NF={
    surface:new NativeFunction(g('tool_surface'),'void',['pointer','int','int','float']),
              surfaceJava:new NativeFunction(g('tool_surface_from_java'),'void',['pointer','int','int','float']),
    resize:new NativeFunction(g('tool_surface_resize'),'void',['int','int']),
    destroy:new NativeFunction(g('tool_surface_destroy'),'void',[]),
    touch:new NativeFunction(g('tool_touch'),'int',['int','float','float']),
    aw:new NativeFunction(Module.getExportByName('libandroid.so','ANativeWindow_fromSurface'),'pointer',['pointer','pointer'])
  };
  return NF;
}
function attach(activity){
  if(attached) return; attached=true;
  try{
    Java.performNow(()=>{
      D('attach begin');
      const N=natives();
      const SV=Java.use('android.view.SurfaceView');
      const v=SV.$new(activity);
      v.setZOrderOnTop(true);
      v.getHolder().setFormat(Java.use('android.graphics.PixelFormat').TRANSLUCENT.value);
      const density=activity.getResources().getDisplayMetrics().density.value;
      const env=Java.vm.getEnv().handle;
      const CB=Java.registerClass({
        name:'re.tool.SmokeCB'+Date.now(),
        implements:[Java.use('android.view.SurfaceHolder$Callback')],
        methods:{
          surfaceCreated(h){ try{
            D('sc: getSurface');
            const s=h.getSurface();
            if(s.$h===undefined||s.$h===null){ D('FAIL surface handle missing'); return; }
            D('sc: surface handle='+s.$h);
            const r=h.getSurfaceFrame();
            D('sc: frame '+r.width()+'x'+r.height());
            N.surfaceJava(s.$h,r.width(),r.height(),density);
            D('PASS tool_surface called');
          }catch(e){ D('FAIL surfaceCreated '+e); } },
          surfaceChanged(h,f,w,hh){ N.resize(w,hh); },
          surfaceDestroyed(h){ D('surfaceDestroyed'); N.destroy(); }
        }
      });
      v.getHolder().addCallback(CB.$new());
      const T=Java.registerClass({
        name:'re.tool.SmokeTouch'+Date.now(),
        implements:[Java.use('android.view.View$OnTouchListener')],
        methods:{ onTouch(v,e){ const r=N.touch(e.getActionMasked(),e.getX(),e.getY()); return (r&1)!==0; } }
      });
      v.setOnTouchListener(T.$new());
      const decor=activity.getWindow().getDecorView();
      const VG=Java.use('android.view.ViewGroup');
      const LP=Java.use('android.widget.FrameLayout$LayoutParams');
      Java.cast(decor,VG).addView(v,LP.$new(-1,-1));
      D('PASS overlay added');
    });
  }catch(e){ D('FAIL attach '+e); attached=false; }
}
function init(){
  try{
    Java.perform(()=>{
      try{
        const app=Java.use('android.app.ActivityThread').currentApplication();
        if(app===null){ if(tries++<60) D('app null retry '+tries); setTimeout(init,250); return; }
        D('PASS Java.perform + app');
        const ctx=app.getApplicationContext();
        const File=Java.use('java.io.File');
        const dir=File.$new(ctx.getExternalFilesDir(null),'tool'); dir.mkdirs();
        const Build=Java.use('android.os.Build$VERSION');
        send({t:'paths', dir:dir.getAbsolutePath(), pkg:ctx.getPackageName(), sdk:Build.SDK_INT.value});
        D('PASS Java.use Build sdk='+Build.SDK_INT.value);
        const R=Java.registerClass({
          name:'re.tool.SmokeRunnable'+Date.now(),
          implements:[Java.use('java.lang.Runnable')],
          methods:{ run(){ D('PASS registerClass Runnable.run invoked'); } }
        });
        const r=R.$new();
        D('PASS registerClass created');
        r.run();
        const LC=Java.registerClass({
          name:'re.tool.SmokeLC'+Date.now(),
          implements:[Java.use('android.app.Application$ActivityLifecycleCallbacks')],
          methods:{
            onActivityCreated(a,b){}, onActivityStarted(a){},
            onActivityResumed(a){ D('onActivityResumed'); attach(a); },
            onActivityPaused(a){}, onActivityStopped(a){},
            onActivitySaveInstanceState(a,b){}, onActivityDestroyed(a){}
          }
        });
        app.registerActivityLifecycleCallbacks(LC.$new());
        D('PASS lifecycle registered');
        try{
          const at=Java.use('android.app.ActivityThread').currentActivityThread();
          const it=at.mActivities.value.values().iterator();
          while(it.hasNext()){ const rec=it.next(); const a=rec.activity.value;
            if(a!==null&&!a.isFinishing()){ attach(a); break; } }
        }catch(e){ D('current activity lookup skip: '+e); }
        D('READY'); send({t:'ready'});
      }catch(e){ D('FAIL init '+e+' | '+(e.stack||'')); setTimeout(init,500); }
    });
  }catch(e){ D('FAIL perform '+e); setTimeout(init,500); }
}
init();
