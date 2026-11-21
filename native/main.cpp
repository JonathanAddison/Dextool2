#include "state.h"
#include "diag.h"
#include <frida-gumjs.h>
#include <jni.h>
#include <agent_js.h>
#include <thread>
static GumScript* g_script=nullptr; static GMainContext* g_ctx=nullptr;
static void onMessage(const gchar* msg, GBytes* data, gpointer){
  json j; try{ j=json::parse(msg); }catch(...){ diag::log("bad json from agent: %s",msg?msg:"(null)"); return; }
  if(j.contains("type") && j["type"].is_string()){
    std::string ty=j["type"].get<std::string>();
    if(ty=="send" && j.contains("payload")) j=j["payload"];
    else if(ty=="error"){ diag::log("AGENT ERROR: %s",j.dump().c_str()); return; }
    else if(ty=="log"){ diag::log("agent log: %s",j.dump().c_str()); return; }
  }
  if(!j.is_object()){ diag::log("agent non-object: %s",j.dump().c_str()); return; }
  std::string t=j.value("t","");
  if(t.empty()){ diag::log("agent msg(no t): %s",j.dump().c_str()); return; }
  if(t=="diag"){ diag::log("agent: %s",j.value("msg","").c_str()); return; }
  if(data && t=="dex"){
    gsize n=0; gconstpointer p=g_bytes_get_data(data,&n);
    std::string path=diag::dir()+"/"+j.value("name","classes.dex");
    FILE* f=fopen(path.c_str(),"wb"); if(f){ fwrite(p,1,n,f); fclose(f); }
    diag::log("dex dumped %s",path.c_str());
    std::lock_guard<std::mutex> lk(G.mtx);
    G.dumped.push_back({path,(long long)n}); ui::toast("dumped "+path);
    return; }
  ui::route(j);
}
static gboolean doPost(gpointer p){ char* s=(char*)p; gum_script_post(g_script,s,nullptr); g_free(s); return G_SOURCE_REMOVE; }
namespace bridge {
  void post(const json& j){ if(!g_script||!g_ctx) return; json m=j; m["type"]="cmd";
    g_main_context_invoke(g_ctx,doPost,g_strdup(m.dump().c_str())); }
}
static void gumThread(){
  diag::log("gum thread start");
  gum_init_embedded();
  g_ctx=g_main_context_new(); g_main_context_push_thread_default(g_ctx);
  GMainLoop* loop=g_main_loop_new(g_ctx,FALSE);
  GumScriptBackend* be=gum_script_backend_obtain_qjs();
  diag::log("qjs backend ok");
  GError* err=nullptr;
  std::string src((const char*)agent_js,agent_js_len);
  g_script=gum_script_backend_create_sync(be,"tool",src.c_str(),nullptr,nullptr,&err);
  if(err){ diag::log("script create ERROR: %s",err->message); return; }
  diag::log("script created");
  gum_script_set_message_handler(g_script,onMessage,nullptr,nullptr);
  gum_script_load_sync(g_script,nullptr);
  diag::log("script loaded");
  g_main_loop_run(loop);
}
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm,void*){
  diag::init(vm); diag::log("JNI_OnLoad");
  std::thread(gumThread).detach();
  return JNI_VERSION_1_6;
}
void toolSetDir(const std::string& d){ diag::setDir(d); }
