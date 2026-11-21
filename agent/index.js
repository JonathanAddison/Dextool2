const Java = require('frida-java-bridge');
const core = require('./core');
const overlay = require('./overlay');
const features = require('./features');

const origLog = console.log.bind(console);
['log','warn','error'].forEach(k => {
  console[k] = (...a) => { try { send({t:'log', ch:'script', text:a.map(String).join(' ')}); } catch(e){} origLog(...a); };
});

Java.perform(() => {
  const app = Java.use('android.app.ActivityThread').currentApplication();
  const ctx = app.getApplicationContext();
  const File = Java.use('java.io.File');
  const dir = File.$new(ctx.getExternalFilesDir(null), 'tool');
  dir.mkdirs();
  const Build = Java.use('android.os.Build$VERSION');
  send({t:'paths', dir:dir.getAbsolutePath(), pkg:ctx.getPackageName(), sdk:Build.SDK_INT.value});
  core.init(ctx);
  features.init(ctx, dir.getAbsolutePath());
  overlay.start(app);
  function loop(m){
    try { Java.performNow(() => core.dispatch(m)); }
    catch(e){ send({t:'toast', msg:''+e}); }
    recv('cmd', loop);
  }
  recv('cmd', loop);
  send({t:'ready'});
});
