#include "state.h"
#include "diag.h"
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <thread>
#include <chrono>
struct TouchEv { int a; float x,y; };
static std::atomic<ANativeWindow*> g_win{nullptr};
static std::atomic<int> g_w{0}, g_h{0};
static std::atomic<float> g_density{2.0f};
static std::atomic<bool> g_run{false}, g_capture{false}, g_wantText{false};
static std::mutex g_evMtx; static std::vector<TouchEv> g_ev;
static std::string g_text; static std::vector<int> g_special;
static std::thread g_thr;
static ImGuiContext* g_igui=nullptr;

static void renderMain(){
  diag::log("renderMain start");
  ANativeWindow* win=g_win.load();
  EGLDisplay dpy=eglGetDisplay(EGL_DEFAULT_DISPLAY);
  eglInitialize(dpy,nullptr,nullptr);
  const EGLint cfgAttr[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
    EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig cfg; EGLint n=0;
  eglChooseConfig(dpy,cfgAttr,&cfg,1,&n);
  if(!n){ diag::log("egl: no config"); ANativeWindow_release(win); return; }
  EGLSurface surf=eglCreateWindowSurface(dpy,cfg,win,nullptr);
  const EGLint ctxAttr[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLContext ctx=eglCreateContext(dpy,cfg,EGL_NO_CONTEXT,ctxAttr);
  eglMakeCurrent(dpy,surf,surf,ctx);
  diag::log("egl makeCurrent ok");
  eglSwapInterval(dpy,1);
  if(!g_igui){ g_igui=ImGui::CreateContext(); }
  ImGui::SetCurrentContext(g_igui);
  ImGuiIO& io=ImGui::GetIO();
  float d=g_density.load();
  ImFontConfig fc; fc.OversampleH=2;
  if(io.Fonts->Fonts.empty())
    io.Fonts->AddFontFromFileTTF("/system/fonts/Roboto-Regular.ttf",16.0f*d,&fc);
  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes(d*0.9f);
  ImGui_ImplOpenGL3_Init("#version 300 es");
  diag::log("imgui gl init ok");
  auto last=std::chrono::steady_clock::now();
  diag::log("render loop start");
  while(g_run.load()){
    { std::lock_guard<std::mutex> lk(g_evMtx);
      for(auto& e:g_ev){
        ImGui_ImplOpenGL3_NewFrame(); // no-op cost; keep io current below
        io.AddMousePosEvent(e.x,e.y);
        if(e.a==0) io.AddMouseButtonEvent(0,true);
        else if(e.a==1||e.a==3) io.AddMouseButtonEvent(0,false);
      }
      g_ev.clear();
      if(!g_text.empty()){ io.AddInputCharactersUTF8(g_text.c_str()); g_text.clear(); }
      for(int k:g_special){ ImGuiKey key=k==1?ImGuiKey_Backspace:ImGuiKey_Enter;
        io.AddKeyEvent(key,true); io.AddKeyEvent(key,false); }
      g_special.clear();
    }
    auto now=std::chrono::steady_clock::now();
    io.DeltaTime=std::chrono::duration<float>(now-last).count(); last=now;
    if(io.DeltaTime<=0) io.DeltaTime=0.016f;
    io.DisplaySize=ImVec2((float)g_w.load(),(float)g_h.load());
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ui::draw();
    g_capture.store(io.WantCaptureMouse);
    g_wantText.store(io.WantTextInput);
    ImGui::Render();
    { static bool ff=false; if(!ff){ ff=true; diag::log("SMOKE PASS first ImGui frame"); } }
    glViewport(0,0,g_w.load(),g_h.load());
    glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    eglSwapBuffers(dpy,surf);
  }
  ImGui_ImplOpenGL3_Shutdown();
  eglMakeCurrent(dpy,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroySurface(dpy,surf); eglDestroyContext(dpy,ctx); eglTerminate(dpy);
  ANativeWindow_release(win);
}
extern "C" __attribute__((visibility("default")))
void tool_surface(void* win,int w,int h,float density){
  diag::log("tool_surface %dx%d d=%.2f",w,h,density);
  if(g_run.load()) return;
  g_win.store((ANativeWindow*)win); g_w.store(w); g_h.store(h); g_density.store(density);
  g_run.store(true); g_thr=std::thread(renderMain);
}
extern "C" __attribute__((visibility("default")))
void tool_surface_from_java(void* surface,int w,int h,float density){
  diag::log("tool_surface_from_java surface=%p %dx%d d=%.2f",surface,w,h,density);
  JavaVM* vm=diag::vm();
  if(!vm){ diag::log("FAIL no JavaVM"); return; }
  JNIEnv* env=nullptr;
  if(vm->GetEnv((void**)&env,JNI_VERSION_1_6)!=0||!env){ diag::log("FAIL GetEnv"); return; }
  ANativeWindow* win=ANativeWindow_fromSurface(env,(jobject)surface);
  if(!win){ diag::log("FAIL ANativeWindow_fromSurface null"); return; }
  diag::log("ANativeWindow=%p",win);
  tool_surface(win,w,h,density);
}
extern "C" __attribute__((visibility("default")))
void tool_surface_resize(int w,int h){ g_w.store(w); g_h.store(h); }
extern "C" __attribute__((visibility("default")))
void tool_surface_destroy(){
  if(!g_run.load()) return;
  g_run.store(false); if(g_thr.joinable()) g_thr.join();
}
extern "C" __attribute__((visibility("default")))
int tool_touch(int a,float x,float y){
  if(!g_run.load()) return 0;
  if(a==0||a==1||a==2||a==3){ std::lock_guard<std::mutex> lk(g_evMtx); g_ev.push_back({a,x,y}); }
  return (g_capture.load()?1:0)|(g_wantText.load()?2:0);
}
extern "C" __attribute__((visibility("default")))
void tool_text(const char* s){ std::lock_guard<std::mutex> lk(g_evMtx); g_text+=s; }
extern "C" __attribute__((visibility("default")))
void tool_special(int k){ std::lock_guard<std::mutex> lk(g_evMtx); g_special.push_back(k); }
