#include "state.h"
#include <frida-gumjs.h>
#include <jni.h>
#include <android/log.h>
#include <agent_js.h>
#include <thread>
#include <fstream>
#include <sstream>
#include <csignal>
#include <cstring>
#include <unwind.h>
#include <dlfcn.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,"libtool",__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,"libtool",__VA_ARGS__)

static GumScript* g_script=nullptr; static GMainContext* g_ctx=nullptr;
static std::string g_dir;

// ---------- crash handler ----------
struct BtState { void** cur; void** end; };
static _Unwind_Reason_Code btCb(_Unwind_Context* c, void* p){
  BtState* s=(BtState*)p; void* pc=(void*)_Unwind_GetIP(c);
  if(s->cur<s->end) *s->cur++=pc; return _URC_NO_REASON;
}
static struct sigaction g_old[64];
static void crashHandler(int sig, siginfo_t* si, void*){
  LOGE("crash signal %d addr %p", sig, si->si_addr);
  if(!g_dir.empty()){
    char path[512]; snprintf(path,sizeof path,"%s/crash_%ld.txt",g_dir.c_str(),(long)time(nullptr));
    FILE* f=fopen(path,"w");
    if(f){
      fprintf(f,"signal %d fault %p\n",sig,si->si_addr);
      void* bt[64]; BtState s{bt,bt+64}; _Unwind_Backtrace(btCb,&s);
      for(void** p=bt;p<s.cur;p++){ Dl_info di; if(dladdr(*p,&di))
        fprintf(f,"%s (%s+0x%lx)\n",di.dli_fname,di.dli_sname?di.dli_sname:"?",
                (unsigned long)((char*)*p-(char*)di.dli_saddr)); else fprintf(f,"%p\n",*p); }
      fclose(f);
    }
  }
  sigaction(sig,&g_old[sig],nullptr); raise(sig);
}
static void installCrash(){
  struct sigaction sa{}; sa.sa_sigaction=crashHandler; sa.sa_flags=SA_SIGINFO|SA_RESETHAND;
  for(int s:{SIGSEGV,SIGABRT,SIGBUS,SIGFPE,SIGILL}) sigaction(s,&sa,&g_old[s]);
}

// ---------- agent message sink ----------
static void onMessage(GumScript*, const gchar* msg, GBytes* data, gpointer){
  json j; try{ j=json::parse(msg); }catch(...){
    std::lock_guard<std::mutex> lk(G.mtx);
    G.scriptLog.push_back({msg?msg:"(bad json)","",{},{}}); return;
  }
  if(data && j.value("t","")=="dex"){
    gsize n=0; gconstpointer p=g_bytes_get_data(data,&n);
    std::string name=j.value("name","classes.dex");
    std::string path=g_dir.empty()?("/data/local/tmp/"+name):(g_dir+"/"+name);
    FILE* f=fopen(path.c_str(),"wb"); if(f){ fwrite(p,1,n,f); fclose(f); }
    std::lock_guard<std::mutex> lk(G.mtx);
    G.dumped.push_back({path,(long long)n}); ui::toast("dumped "+path);
    return;
  }
  ui::route(j);
}
static gboolean doPost(gpointer p){
  char* s=(char*)p;
#ifdef GUM_POST_NO_DATA
  gum_script_post(g_script,s);
#else
  gum_script_post(g_script,s,nullptr);
#endif
  g_free(s); return G_SOURCE_REMOVE;
}
namespace bridge {
  void post(const json& j){
    if(!g_script||!g_ctx) return;
    json m=j; m["type"]="cmd";
    g_main_context_invoke(g_ctx,doPost,g_strdup(m.dump().c_str()));
  }
}
static void gumThread(){
  gum_init_embedded();
  g_ctx=g_main_context_new(); g_main_context_push_thread_default(g_ctx);
  GMainLoop* loop=g_main_loop_new(g_ctx,FALSE);
  GumScriptBackend* be=gum_script_backend_obtain_qjs();
  GError* err=nullptr;
  std::string src((const char*)agent_js,agent_js_len);
#ifdef GUM_CREATE_NO_SNAPSHOT
  g_script=gum_script_backend_create_sync(be,"tool",src.c_str(),nullptr,&err);
#else
  g_script=gum_script_backend_create_sync(be,"tool",src.c_str(),nullptr,nullptr,&err);
#endif
  if(err){ LOGE("script create: %s",err->message); return; }
  gum_script_set_message_handler(g_script,onMessage,nullptr,nullptr);
  gum_script_load_sync(g_script,nullptr,&err);
  if(err){ LOGE("script load: %s",err->message); return; }
  LOGI("agent loaded");
  g_main_loop_run(loop);
}
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM*, void*){
  LOGI("libtool starting");
  installCrash();
  std::thread(gumThread).detach();
  return JNI_VERSION_1_6;
}
// settings persistence + dir used by crash handler
void toolSetDir(const std::string& d){ g_dir=d; }
