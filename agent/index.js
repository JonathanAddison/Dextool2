import Java from 'frida-java-bridge';
import * as core from './core.js';
import * as overlay from './overlay.js';
import * as features from './features.js';
function diag(m){ try{ send({t:'diag', msg:m}); }catch(e){} }
const origLog = console.log.bind(console);
['log','warn','error'].forEach(k=>{
  console[k]=(...a)=>{ const s=a.map(String).join(' '); diag('console.'+k+': '+s);
    try{ send({t:'log',ch:'script',text:s}); }catch(e){} origLog(...a); };
});
diag('agent top-level');
let tries=0;
function attempt(){
  try {
    Java.perform(()=>{
      try {
        const app=Java.use('android.app.ActivityThread').currentApplication();
        if(app===null){ if(tries++<40) diag('app null, retry '+tries); setTimeout(attempt,250); return; }
        diag('app obtained');
        const ctx=app.getApplicationContext();
        const File=Java.use('java.io.File');
        const dir=File.$new(ctx.getExternalFilesDir(null),'tool');
        dir.mkdirs();
        const Build=Java.use('android.os.Build$VERSION');
        send({t:'paths', dir:dir.getAbsolutePath(), pkg:ctx.getPackageName(), sdk:Build.SDK_INT.value});
        diag('paths sent');
        core.init(ctx);
        features.init(ctx,dir.getAbsolutePath());
        diag('features init');
        overlay.start(app);
        diag('overlay start returned');
        function loop(m){
          try{ Java.performNow(()=>core.dispatch(m)); }
          catch(e){ diag('dispatch error: '+e); send({t:'toast',msg:''+e}); }
          recv('cmd',loop);
        }
        recv('cmd',loop);
        diag('agent ready');
        send({t:'ready'});
      } catch(e){ diag('init error: '+e+' | '+(e.stack||'')); setTimeout(attempt,500); }
    });
  } catch(e){ diag('perform error: '+e); setTimeout(attempt,500); }
}
attempt();
