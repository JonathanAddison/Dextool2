import Java from 'frida-java-bridge';
function diag(m){ try{ send({t:'diag',msg:'overlay: '+m}); }catch(e){} }
let NF=null, overlayView=null, proxy=null, currentDecor=null, lastText='', imeShown=false;
function natives(){
  if(NF) return NF;
  const g=n=>Module.getExportByName('libtool.so',n);
  NF={
    surfaceJava:new NativeFunction(g('tool_surface_from_java'),'void',['pointer','int','int','float']),
    resize:new NativeFunction(g('tool_surface_resize'),'void',['int','int']),
    destroy:new NativeFunction(g('tool_surface_destroy'),'void',[]),
    touch:new NativeFunction(g('tool_touch'),'int',['int','float','float']),
    text:new NativeFunction(g('tool_text'),'void',['pointer']),
    special:new NativeFunction(g('tool_special'),'void',['int'])
  };
  return NF;
}
function sendText(s){ const p=Memory.allocUtf8String(s); natives().text(p); }
function jstrToJs(j){
  if(typeof j==='string') return j;
  try{
    const b=j.getBytes('UTF-8'); const n=b.length; const u=new Uint8Array(n);
    for(let i=0;i<n;i++){ let x=b[i]; if(x<0)x+=256; u[i]=x; }
    return new TextDecoder().decode(u);
  }catch(e){ return ''+j; }
}
function showIME(on){
  if(on===imeShown||!proxy) return; imeShown=on;
  try{
    const imm=Java.cast(proxy.getContext().getSystemService('input_method'),
      Java.use('android.view.inputmethod.InputMethodManager'));
    if(on){ proxy.setFocusableInTouchMode(true); proxy.requestFocus(); imm.showSoftInput(proxy,0); }
    else { imm.hideSoftInputFromWindow(proxy.getWindowToken(),0); proxy.clearFocus(); }
  }catch(e){ diag('IME err '+e); }
}
function detach(){
  try{ Java.performNow(()=>{
    if(currentDecor){
      const vg=Java.cast(currentDecor,Java.use('android.view.ViewGroup'));
      if(overlayView) vg.removeView(overlayView);
      if(proxy) vg.removeView(proxy);
    }
  }); }catch(e){}
  overlayView=null; proxy=null; currentDecor=null; imeShown=false; lastText='';
}
function attach(activity){
  try{ Java.performNow(()=>{
    diag('attach');
    detach();
    const N=natives();
    const decor=activity.getWindow().getDecorView();
    currentDecor=decor;
    activity.getWindow().setSoftInputMode(
      Java.use('android.view.WindowManager$LayoutParams').SOFT_INPUT_ADJUST_RESIZE.value);
    const SV=Java.use('android.view.SurfaceView');
    const v=SV.$new(activity);
    v.setZOrderOnTop(true);
    v.getHolder().setFormat(Java.use('android.graphics.PixelFormat').TRANSLUCENT.value);
    const density=activity.getResources().getDisplayMetrics().density.value;
    const CB=Java.registerClass({
      name:'re.tool.CB'+Date.now(),
      implements:[Java.use('android.view.SurfaceHolder$Callback')],
      methods:{
        surfaceCreated(h){ try{
          const s=h.getSurface(); const r=h.getSurfaceFrame();
          diag('surface '+r.width()+'x'+r.height());
          N.surfaceJava(s.$h,r.width(),r.height(),density);
        }catch(e){ diag('surfaceCreated err '+e); } },
        surfaceChanged(h,f,w,hh){ N.resize(w,hh); },
        surfaceDestroyed(h){ diag('surfaceDestroyed'); N.destroy(); }
      }
    });
    v.getHolder().addCallback(CB.$new());
    const T=Java.registerClass({
      name:'re.tool.T'+Date.now(),
      implements:[Java.use('android.view.View$OnTouchListener')],
      methods:{ onTouch(v,e){
        const a=e.getActionMasked(); const r=N.touch(a,e.getX(),e.getY());
        if(a===0) showIME((r&2)!==0);
        return (r&1)!==0;
      } }
    });
    v.setOnTouchListener(T.$new());
    const ET=Java.use('android.widget.EditText');
    proxy=ET.$new(activity);
    proxy.setTextSize(1); proxy.setAlpha(0.0);
    proxy.setFocusable(true); proxy.setFocusableInTouchMode(true);
    const IT=Java.use('android.text.InputType');
    proxy.setInputType(IT.TYPE_CLASS_TEXT.value | IT.TYPE_TEXT_FLAG_NO_SUGGESTIONS.value);
    const W=Java.registerClass({
      name:'re.tool.W'+Date.now(),
      implements:[Java.use('android.text.TextWatcher')],
      methods:{
        beforeTextChanged(s,a,b,c){}, onTextChanged(s,a,b,c){},
        afterTextChanged(ed){ try{
          const s=jstrToJs(ed.toString());
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
    proxy.addTextChangedListener(W.$new());
    const LP=Java.use('android.widget.FrameLayout$LayoutParams');
    const vg=Java.cast(decor,Java.use('android.view.ViewGroup'));
    vg.addView(proxy,LP.$new(1,1));
    vg.addView(v,LP.$new(-1,-1));
    overlayView=v;
    try{
      v.setVisibility(Java.use('android.view.View').VISIBLE.value);
      v.bringToFront(); v.requestLayout(); v.invalidate();
      decor.requestLayout(); decor.invalidate();
      const Kick=Java.registerClass({name:'re.tool.Kick'+Date.now(),
        implements:[Java.use('java.lang.Runnable')],
        methods:{ run(){ try{ v.requestLayout(); v.invalidate(); decor.requestLayout(); decor.invalidate(); }catch(e){} } }});
      Java.use('android.os.Handler').$new(Java.use('android.os.Looper').getMainLooper())
        .postDelayed(Kick.$new(),300);
    }catch(e){}
    diag('attached');
  }); }catch(e){ diag('attach err '+e); }
}
function start(app){
  try{
    const LC=Java.registerClass({
      name:'re.tool.LC'+Date.now(),
      implements:[Java.use('android.app.Application$ActivityLifecycleCallbacks')],
      methods:{
        onActivityCreated(a,b){}, onActivityStarted(a){},
        onActivityResumed(a){ attach(a); },
        onActivityPaused(a){ detach(); },
        onActivityStopped(a){}, onActivitySaveInstanceState(a,b){},
        onActivityDestroyed(a){ detach(); }
      }
    });
    app.registerActivityLifecycleCallbacks(LC.$new());
    diag('lifecycle registered');
    try{
      const at=Java.use('android.app.ActivityThread').currentActivityThread();
      const it=at.mActivities.value.values().iterator();
      while(it.hasNext()){ const rec=it.next(); const a=rec.activity.value;
        if(a!==null&&!a.isFinishing()){ attach(a); break; } }
    }catch(e){ diag('current activity skip '+e); }
  }catch(e){ diag('start err '+e); }
}
export { start };
