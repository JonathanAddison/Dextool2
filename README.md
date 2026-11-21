# libtool — in-app Frida-powered ImGui runtime toolbox (no root / no frida-server / no gadget)

One standalone `libtool.so`. Drop it into an APK's `lib/<abi>/`, add
`System.loadLibrary("tool")` to the main activity, rebuild, sign, install.
The ImGui menu renders on a translucent SurfaceView inside the app's own
window — **no overlay permission**.

## How it works
- Embeds **frida-gum + GumJS (QuickJS)** devkit statically (the only part of
  Frida we need — no frida-core, no server, no gadget, no root).
- Bundles **frida-java-bridge** into the agent via frida-compile.
- Java-side overlay/IME/lifecycle is done entirely from the agent
  (`Java.registerClass`), so the .so is truly self-contained.
- UI: Dear ImGui (GLES3) on its own EGL surface + hidden EditText IME proxy.

## Tabs
Settings / Patches / Hooks / Trackers / Dumper / Recorder / Monitor / Script / Classes.
Classes windows are isolated (own filters), `+` adds windows, class search by
class/method(name,return,param)/field(name,type), per-method Call/Patch/Track/Hook,
per-field value/Set/Patch(lock)/Track(watch), instance Find (Java.choose) / Create,
object-arg picker with find/create. Logs are clickable -> open caller class.

## Build
Push `.github/workflows/tool.yml` to an empty repo and run the workflow.
Artifacts: `libtool-arm64-v8a.so`, `libtool-armeabi-v7a.so`, signed
`tooltest.apk`, `inject.py`, full sources (auto-committed on first run).

## Inject into your own APK
python3 tools/inject.py app.apk --libs ./libs --out patched.apk
(needs apktool, zipalign, apksigner on PATH)

## Crash logs & dumps
Written to `Android/data/<pkg>/files/tool/` (always accessible), and copied
to public **Downloads** via MediaStore on API 29+ (toggle in Settings).

## Honest limitations / roadmap
- **Smali live edit = phase 2.** Plan: embed baksmali/smali (dexlib2) as a
  dex payload loaded with InMemoryDexClassLoader; disassemble target class,
  reassemble edited method, load and redirect via the hook dispatcher.
  UI slot exists; Java(JS) and Noop modes work today.
- Field tracking is a 1s value-change watch (no ART field watchpoints).
- Recorder hooks all methods under a scope prefix (capped 300 classes /
  3000 methods). Whole-app unbounded tracing is not practical in-process.
- `Java.choose` depends on frida-java-bridge heap walking; may fail on some
  new ROMs. Monitor covers TLS plaintext + URL opens (OkHttp-specific
  request/response enrichment is phase 2).
- If a pinned Frida devkit 404s, CI falls back to the latest 16.x release.
  If the gum C API ever changes arity, build with
  `-DCMAKE_CXX_FLAGS="-DGUM_CREATE_NO_SNAPSHOT -DGUM_POST_NO_DATA"`.

Use only on apps you own or are authorized to test.

## Licenses
Frida/gum: wxWindows Library Licence. frida-java-bridge: MIT.
Dear ImGui: MIT. nlohmann/json: MIT. This repo: MIT.
