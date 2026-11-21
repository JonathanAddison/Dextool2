import Java from 'frida-java-bridge';
function diag(m){ try{ send({t:'diag',msg:'overlay: '+m}); }catch(e){} }
let nf=null, overlayView=null, proxy=null, lastText='', imeShown=false, currentDecor=null;
function natives(){
  if(nf) return nf;
  const lib='libtool.so'; const get=n=>Module.getExportByName(lib,n);
  nf={
    surface:new NativeFunction(get('tool_surface'),'void',['pointer','int','int','float']),
    resize:new NativeFunction(get('tool_surface_resize'),'void',['int','int']),
    destroy:new NativeFunction(get('tool_surface_destroy'),'void',[]),
    touch:new NativeFunction(get('tool_touch'),'int',['int','float','float']),
    text:new NativeFunction(get('tool_text'),'void',['pointer']),
    special:new NativeFunction(get('tool_special'),'void',['int']),
    awFromSurface:new NativeFunction(Module.getExportByName('libandroid.so','ANativeWindow_fromSurface'),'pointer',['pointer','pointer'])
  };
  return nf;
}
function sendText(s){ const p=Memory.allocUtf8String(s); natives().text(p); }
function showIME(v,on){
  if(on===imeShown) return; imeShown=on;
  try{
    const ctx=v.getContext();
    const imm=Java.cast(ctx.getSystemService('input_method'),Java.use('android.view.inputmethod.InputMethodManager'));
    if(on){ proxy.requestFocus(); imm.showSoftInput(proxy,0); }
    else { imm.hideSoftInputFromWindow(v.getWindowToken(),0); proxy.clearFocus(); }
  }catch(e){}
}
function attach(activity){
  try{
    Java.performNow(()=>{
      diag('attach begin');
      const N=natives();
      const decor=activity.getWindow().getDecorView();
      if(currentDecor===decor){ diag('same decor'); return; }
      if(overlayView&&currentDecor){ try{ Java.cast(currentDecor,Java.use('android.view.ViewGroup')).removeView(overlayView); }catch(e){} }
      currentDecor=decor;
      const SurfaceView=Java.use('android.view.SurfaceView');
      const view=SurfaceView.$new(activity);
      view.setZOrderOnTop(true);
      view.getHolder().setFormat(Java.use('android.graphics.PixelFormat').TRANSLUCENT.value);
      const density=activity.getResources().getDisplayMetrics().density.value;
      const env=Java.vm.getEnv().handle;
      const CB=Java.registerClass({
        name:'re.tool.SurfCB'+Date.now(),
        implements:[Java.use('android.view.SurfaceHolder$Callback')],
        methods:{
          surfaceCreated(h){ try{ diag('surfaceCreated');
            const win=N.awFromSurface(env,h.getSurface().$h);
            const r=h.getSurfaceFrame();
            diag('win='+win+' '+r.width()+'x'+r.height());
            N.surface(win,r.width(),r.height(),density);
          }catch(e){ diag('surfaceCreated err '+e); } },
          surfaceChanged(h,f,w,hh){ N.resize(w,hh); },
          surfaceDestroyed(h){ diag('surfaceDestroyed'); N.destroy(); }
        }
      });
      view.getHolder().addCallback(CB.$new());
      const Touch=Java.registerClass({
        name:'re.tool.Touch'+Date.now(),
        implements:[Java.use('android.view.View$OnTouchListener')],
        methods:{
          onTouch(v,e){ const a=e.getActionMasked(); const r=N.touch(a,e.getX(),e.getY());
            if(a===0) showIME(v,(r&2)!==0); return (r&1)!==0; }
        }
      });
      view.setOnTouchListener(Touch.$new());
      const EditText=Java.use('android.widget.EditText');
      proxy=EditText.$new(activity);
      proxy.setTextSize(1); proxy.setAlpha(0.0);
      const Watcher=Java.registerClass({
        name:'re.tool.Watch'+Date.now(),
        implements:[Java.use('android.text.TextWatcher')],
        methods:{
          beforeTextChanged(s,a,b,c){}, onTextChanged(s,a,b,c){},
          afterTextChanged(editable){ try{
            const s=''+editable.toString();
            if(s.length>lastText.length&&s.startsWith(lastText)){
              const add=s.substring(lastText.length);
              for(const ch of add){ if(ch==='\n') N.special(2); else sendText(ch); }
            } else if(s.length<lastText.length&&lastText.startsWith(s)){
              for(let i=0;i<lastText.length-s.length;i++) N.special(1);
            } else { sendText(s); }
            lastText=s;
          }catch(e){} }
        }
      });
      proxy.addTextChangedListener(Watcher.$new());
      const LP=Java.use('android.widget.FrameLayout$LayoutParams');
      const vg=Java.cast(decor,Java.use('android.view.ViewGroup'));
      vg.addView(proxy,LP.$new(1,1));
      vg.addView(view,LP.$new(-1,-1));
      overlayView=view;
      diag('attach done');
    });
  }catch(e){ diag('attach err '+e); }
}
function start(app){
  try{
    diag('start');
    const CB=Java.registerClass({
      name:'re.tool.Lifecycle'+Date.now(),
      implements:[Java.use('android.app.Application$ActivityLifecycleCallbacks')],
      methods:{
        onActivityCreated(a,b){}, onActivityStarted(a){},
        onActivityResumed(a){ diag('resumed'); attach(a); },
        onActivityPaused(a){}, onActivityStopped(a){},
        onActivitySaveInstanceState(a,b){}, onActivityDestroyed(a){}
      }
    });
    app.registerActivityLifecycleCallbacks(CB.$new());
    diag('lifecycle registered');
    try{
      const at=Java.use('android.app.ActivityThread').currentActivityThread();
      const it=at.mActivities.value.values().iterator();
      while(it.hasNext()){
        const rec=it.next(); const act=rec.activity.value;
        if(act!==null&&!act.isFinishing()){ attach(act); break; }
      }
    }catch(e){ diag('current activity attach skip: '+e); }
  }catch(e){ diag('start err '+e); }
}
export { start };