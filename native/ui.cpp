#include "state.h"
#include "diag.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <fstream>
#include <cmath>
void toolSetDir(const std::string&);
static int req(){ return G.nextReq++; }
static ImU32 typeCol(const std::string& t){
  if(t=="void") return IM_COL32(150,150,150,255);
  if(t=="boolean") return IM_COL32(255,120,180,255);
  if(t=="int"||t=="long"||t=="short"||t=="byte") return IM_COL32(110,180,255,255);
  if(t=="float"||t=="double") return IM_COL32(90,220,220,255);
  if(t=="char") return IM_COL32(120,220,160,255);
  if(t=="java.lang.String"||t=="java.lang.CharSequence") return IM_COL32(240,220,110,255);
  if(!t.empty()&&t.back()==']') return IM_COL32(200,150,255,255);
  return IM_COL32(255,170,90,255);
}
static std::string sigOf(const MethodInfo& m){
  std::string s=m.ret+" "+m.n+"("; for(size_t i=0;i<m.p.size();i++){ if(i)s+=", "; s+=m.p[i]; } return s+")";
}
static std::string hookTemplate(const MethodInfo& m){
  std::string s="// "+sigOf(m)+"\n// a[i] = arguments (typed), orig() = call original\n";
  for(size_t i=0;i<m.p.size();i++) s+="// a["+std::to_string(i)+"] : "+m.p[i]+"\n";
  s+="return orig();\n"; return s;
}
namespace ui {
static json patchJson(const PatchRec& p);
static json hookJson(const HookRec& h);
void toast(const std::string& s){ G.toasts.push_back({s,4.0f}); }
static std::vector<std::pair<std::string,long long>> g_openQueue;
void openClass(const std::string& cls,long long h){ g_openQueue.push_back({cls,h}); }
static void flushOpen(){
  for(auto& q:g_openQueue){
    ClassWindow w; w.id=G.nextWin++; w.isList=false; w.cls=q.first; w.handle=q.second;
    w.title=q.second?(q.first+" @"+std::to_string(q.second)):q.first;
    G.wins.push_back(w); G.activeWin=w.id;
    bridge::post({{"t","info"},{"id",req()},{"cls",q.first},{"h",q.second},{"ctx",{{"w",w.id},{"k","info"}}}});
  }
  g_openQueue.clear();
}
static ClassWindow* winById(int id){ for(auto& w:G.wins) if(w.id==id) return &w; return nullptr; }
static ArgEditor* editorByCtx(ClassWindow& w,const json& c){
  std::string e=c.value("e",""), m=c.value("m","");
  if(e=="ctor") return &w.ctorArgs;
  if(w.mu.count(m)){ if(e=="call") return &w.mu[m].callArgs; if(e=="patch") return &w.mu[m].patchArgs; }
  return nullptr;
}
// ---------------- routing ----------------
void route(const json& j){
  std::lock_guard<std::mutex> lk(G.mtx);
  std::string t=j.value("t","");
  if(t=="paths"){ G.cfg.dir=j.value("dir",""); G.cfg.pkg=j.value("pkg",""); G.cfg.sdk=j.value("sdk",0);
    toolSetDir(G.cfg.dir); snprintf(G.recScope,sizeof G.recScope,"%s",G.cfg.pkg.c_str());
    std::ifstream f(G.cfg.dir+"/settings.json");
    if(f){ try{ json s; f>>s; G.cfg.fontScale=s.value("fontScale",1.0f); G.cfg.opacity=s.value("opacity",0.95f);
      G.cfg.theme=s.value("theme",0); G.cfg.logCap=s.value("logCap",2000);
      G.cfg.publishDownloads=s.value("publishDownloads",true);}catch(...){} }
    return; }
  if(t=="ready"){ toast("agent ready"); return; }
  if(t=="toast"){ toast(j.value("msg","")); return; }
  if(t=="clip"){ ImGui::SetClipboardText(j.value("text","").c_str()); return; }
  if(t=="log"){
    std::string ch=j.value("ch","script"); LogEntry e;
    e.text=j.value("text",""); e.cls=j.value("cls",""); e.raw=j;
    if(j.contains("stack")) for(auto& f:j["stack"]) e.stack.push_back({f.value("c",""),f.value("s","")});
    auto cap=[&](std::deque<LogEntry>& d){ d.push_back(e); while((int)d.size()>G.cfg.logCap) d.pop_front(); };
    if(ch=="track"){ std::string id=j.value("id",""); if(G.tracks.count(id)){ auto& d=G.tracks[id].log;
        d.push_back(e); while((int)d.size()>G.cfg.logCap) d.pop_front(); } }
    else if(ch=="record") cap(G.recordLog);
    else if(ch=="monitor") cap(G.monitorLog);
    else cap(G.scriptLog);
    return; }
  json c=j.value("ctx",json::object()); ClassWindow* wp=winById(c.value("w",-1));
  if(t=="callResult"&&wp){ std::string m=c.value("m",""); auto& mu=wp->mu[m];
    mu.callResult=(j.value("ok",false)?"= ":"ERROR: ")+j.value("text","");
    mu.callLog.push_back({mu.callResult,j.value("cls",""),{},{}}); return; }
  if(!wp) return;
  ClassWindow& w=*wp;
  if(t=="classes"){ w.results=j.value("items",std::vector<std::string>{}); w.searching=false; }
  else if(t=="info"){ w.loaded=true; w.methods.clear(); w.fields.clear();
    for(auto& m:j.value("methods",json::array())){ MethodInfo mi; mi.n=m.value("n",""); mi.mods=m.value("mods","");
      mi.ret=m.value("ret","void"); mi.st=m.value("st",false); mi.ov=m.value("ov",-1);
      mi.p=m.value("p",std::vector<std::string>{}); w.methods.push_back(mi); }
    for(auto& f:j.value("fields",json::array())){ FieldInfo fi; fi.n=f.value("n",""); fi.mods=f.value("mods","");
      fi.t=f.value("t",""); fi.st=f.value("st",false); fi.v=f.value("v",""); w.fields.push_back(fi); }
    w.ctors=j.value("ctors",json::array());
    if(!w.ctors.empty()){ std::vector<std::string> t0=w.ctors[0].get<std::vector<std::string>>();
      w.ctorArgs.init(t0,false); } }
  else if(t=="inst"){ std::string k=c.value("k","");
    std::vector<InstanceInfo> items;
    for(auto& i:j.value("items",json::array())) items.push_back({i.value("h",0LL),i.value("s","")});
    if(k=="find") w.instances=items;
    else if(k=="argfind"){ if(ArgEditor* e=editorByCtx(w,c)){ e->results=items; e->busy=false; } } }
  else if(t=="created"){ std::string k=c.value("k",""); long long h=j.value("h",0LL);
    if(k=="create") openClass(w.cls,h);
    else if(k=="argcreate"){ if(ArgEditor* e=editorByCtx(w,c)){ int i=c.value("i",0);
      if(i>=0&&i<(int)e->vals.size()){ e->vals[i]=json{{"k","obj"},{"h",h}}; e->popupFor=-1; e->busy=false; } } }
    else if(k=="ctorcreate") openClass(j.value("cls",w.cls),h); }
  else if(t=="fieldVal"){ std::string m=c.value("m",""); if(w.mu.count(m)) w.mu[m].fieldVal=j.value("val",""); }
}
// ---------------- shared widgets ----------------
static void drawStack(const std::vector<StackFrame>& st){
  for(auto& f:st){ ImGui::PushStyleColor(ImGuiCol_Text,typeCol("obj"));
    if(ImGui::Selectable(f.s.c_str())) openClass(f.c);
    ImGui::PopStyleColor(); }
}
static LogEntry g_detail; static bool g_detailOpen=false;
static void drawLog(std::deque<LogEntry>& log,bool openCls){
  ImGui::BeginChild("##log",ImVec2(-1,220),true);
  int n=(int)log.size(); int start=std::max(0,n-150);
  for(int i=start;i<n;i++){
    auto& e=log[i];
    ImGui::PushID(i);
    if(ImGui::Selectable(e.text.c_str())){
      if(openCls&&!e.cls.empty()) openClass(e.cls);
      else if(!e.stack.empty()){ g_detail=e; g_detailOpen=true; }
    }
    ImGui::PopID();
  }
  if(ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-4) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
}
static void argEditor(ArgEditor& ed,const char* base,int wid,const std::string& mkey,const char* ekey,bool skipMode){
  for(size_t i=0;i<ed.types.size();i++){
    ImGui::PushID((int)i);
    const std::string& ty=ed.types[i];
    json& val=ed.vals[i];
    bool skip=skipMode&&val.value("k","skip")=="skip";
    if(skipMode){ bool on=!skip; ImGui::Checkbox("patch",&on);
      val["k"]=on?(ArgEditor::isText(ty)?"prim":"obj"):"skip"; ImGui::SameLine(); }
    ImGui::TextColored(ImColor(typeCol(ty)),"%s",ty.c_str()); ImGui::SameLine();
    if(ArgEditor::isText(ty)){
      std::string v=val.value("v","");
      ImGui::SetNextItemWidth(-1);
      if(ImGui::InputText("##v",&v)){ val["k"]=ty=="java.lang.String"||ty=="java.lang.CharSequence"?"str":"prim"; val["v"]=v; }
    }else{
      std::string lab=val.value("k","null")=="obj"?("object #"+std::to_string(val.value("h",0LL))):"null";
      if(ImGui::Button(lab.c_str())) ed.popupFor=(int)i;
      ImGui::SameLine(); if(ImGui::SmallButton("null")) val=json{{"k","null"}};
    }
    if(ed.popupFor==(int)i){
      ImGui::Indent(); ImGui::BeginChild("##find",ImVec2(-1,170),true);
      ImGui::Text("find object: %s",ty.c_str());
      if(ImGui::Button("Find instances")){ ed.busy=true;
        bridge::post({{"t","inst"},{"id",req()},{"cls",ty},
          {"ctx",{{"w",wid},{"k","argfind"},{"m",mkey},{"e",ekey},{"i",(int)i}}}}); }
      ImGui::SameLine();
      if(ImGui::Button("Create (default args)")){ ed.busy=true;
        bridge::post({{"t","create"},{"id",req()},{"cls",ty},{"sel",-1},{"args",json::array()},
          {"ctx",{{"w",wid},{"k","argcreate"},{"m",mkey},{"e",ekey},{"i",(int)i}}}}); }
      ImGui::SameLine(); if(ImGui::Button("Close")) ed.popupFor=-1;
      if(ed.busy) ImGui::TextDisabled("working...");
      for(auto& in:ed.results){
        std::string l=in.label+" ##"+std::to_string(in.h);
        if(ImGui::Selectable(l.c_str())){ val=json{{"k","obj"},{"h",in.h}}; ed.popupFor=-1; }
      }
      ImGui::EndChild(); ImGui::Unindent();
    }
    ImGui::PopID();
  }
}
// ---------------- tabs ----------------
static void tabSettings(){
  ImGui::SliderFloat("Font scale",&G.cfg.fontScale,0.6f,2.5f);
  ImGui::SliderFloat("Window opacity",&G.cfg.opacity,0.3f,1.0f);
  ImGui::Combo("Theme",&G.cfg.theme,"Dark\0Light\0Classic\0");
  if(G.cfg.theme==0)ImGui::StyleColorsDark(); else if(G.cfg.theme==1)ImGui::StyleColorsLight(); else ImGui::StyleColorsClassic();
  ImGui::InputInt("Log cap",&G.cfg.logCap);
  ImGui::Checkbox("Publish dumps/crashes to Downloads (MediaStore, API29+)",&G.cfg.publishDownloads);
  ImGui::Checkbox("Native crash handler",&G.cfg.crashHandler);
  ImGui::TextWrapped("Files dir: %s",G.cfg.dir.c_str());
  ImGui::TextWrapped("Package: %s  SDK: %d",G.cfg.pkg.c_str(),G.cfg.sdk);
  if(ImGui::Button("Save settings")){
    json s{{"fontScale",G.cfg.fontScale},{"opacity",G.cfg.opacity},{"theme",G.cfg.theme},
           {"logCap",G.cfg.logCap},{"publishDownloads",G.cfg.publishDownloads}};
    std::ofstream f(G.cfg.dir+"/settings.json"); f<<s.dump(2);
    bridge::post({{"t","settings"},{"publish",G.cfg.publishDownloads}});
    toast("saved");
  }
  if(ImGui::Button("Export logs to Downloads")){ tool_export_logs(); toast("exporting logs..."); }
  ImGui::TextWrapped("Diagnostics dir: %s", diag::dir().c_str());
  ImGui::Separator();
  ImGui::TextDisabled("libtool phase 1 - frida-gum embedded, no root/server/gadget.");
  ImGui::TextDisabled("Smali live edit: phase 2 (see README roadmap).");
}
static void tabPatches(){
  std::vector<std::string> del;
  std::map<std::string,std::vector<std::string>> byCls;
  for(auto& [id,p]:G.patches) byCls[p.cls].push_back(id);
  for(auto& [cls,ids]:byCls){
    if(!ImGui::TreeNode(cls.c_str())) continue;
    for(auto& id:ids){
      PatchRec p=G.patches[id];
      ImGui::PushID(id.c_str());
      bool en=p.enabled;
      if(ImGui::Checkbox(("##en"+id).c_str(),&en)){ p.enabled=en; G.patches[id]=p;
        bridge::post({{"t","patch"},{"rec",patchJson(p)}}); }
      ImGui::SameLine();
      bool open=ImGui::TreeNode((p.member+(p.isField?" [field]":" [method]")).c_str());
      ImGui::SameLine(); if(ImGui::SmallButton("delete")){
        bridge::post({{"t","patchDel"},{"id",id}}); del.push_back(id); if(open) ImGui::TreePop(); ImGui::PopID(); continue; }
      if(open){
        if(p.isField){
          std::string v=p.ret.value("v","");
          if(ImGui::InputText("value",&v)) p.ret["v"]=v;
          ImGui::Checkbox("lock (re-apply 1s poll)",&p.lock);
          if(ImGui::Button("Apply")){ p.ret["k"]=ArgEditor::isText(p.type)?"prim":"str";
            G.patches[id]=p; bridge::post({{"t","patch"},{"rec",patchJson(p)}}); }
        }else{
          ArgEditor ed; ed.types=p.params; ed.vals=p.args;
          argEditor(ed,"pp",0,id,"patch",true);
          p.args=ed.vals;
          bool ron=p.ret.value("k","skip")!="skip";
          if(ImGui::Checkbox("patch return",&ron)) p.ret["k"]=ron?"prim":"skip";
          if(ron){ std::string v=p.ret.value("v",""); ImGui::SetNextItemWidth(-1);
            if(ImGui::InputText("ret",&v)){ p.ret["v"]=v; p.ret["k"]=ArgEditor::isText(p.retType)?"prim":"str"; } }
          if(ImGui::Button("Apply")){ G.patches[id]=p; bridge::post({{"t","patch"},{"rec",patchJson(p)}}); }
        }
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    ImGui::TreePop();
  }
  for(auto& d:del) G.patches.erase(d);
  if(G.patches.empty()) ImGui::TextDisabled("No patches. Register from Classes tab.");
}
static json patchJson(const PatchRec& p){
  return json{{"id",p.id},{"cls",p.cls},{"member",p.member},{"isField",p.isField},{"enabled",p.enabled},
              {"ov",p.ov},{"args",p.args},{"ret",p.ret},{"lock",p.lock},{"type",p.type},{"retType",p.retType}};
}
static json hookJson(const HookRec& h){
  return json{{"id",h.id},{"cls",h.cls},{"name",h.name},{"ov",h.ov},{"mode",h.mode},
              {"code",h.code},{"enabled",h.enabled},{"ret",h.ret}};
}
static void tabHooks(){
  std::vector<std::string> del;
  for(auto& [id,h0]:G.hooks){
    HookRec h=h0; ImGui::PushID(id.c_str());
    bool en=h.enabled;
    if(ImGui::Checkbox("##en",&en)){ h.enabled=en; G.hooks[id]=h; bridge::post({{"t","hook"},{"rec",hookJson(h)}}); }
    ImGui::SameLine();
    bool open=ImGui::TreeNode((h.cls+"::"+h.name).c_str());
    ImGui::SameLine(); if(ImGui::SmallButton("delete")){
      bridge::post({{"t","hookDel"},{"id",id}}); del.push_back(id); if(open)ImGui::TreePop(); ImGui::PopID(); continue; }
    if(open){
      int mode=h.mode=="java"?0:(h.mode=="smali"?1:2);
      ImGui::Combo("mode",&mode,"Java (JS)\0Smali (phase 2)\0Noop\0");
      h.mode=mode==0?"java":(mode==1?"smali":"noop");
      if(h.mode=="java"){
        ImGui::InputTextMultiline("##code",&h.code,ImVec2(-1,220));
      }else if(h.mode=="smali"){
        ImGui::TextWrapped("Smali live editing needs the embedded assembler module (phase 2). Use Java (JS) or Noop for now.");
      }else ImGui::TextDisabled("Returns default value for return type; body skipped.");
      if(ImGui::Button("Apply")){ G.hooks[id]=h; bridge::post({{"t","hook"},{"rec",hookJson(h)}}); }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  for(auto& d:del) G.hooks.erase(d);
  if(G.hooks.empty()) ImGui::TextDisabled("No hooks. Register from Classes tab.");
}
static void tabTrackers(){
  std::vector<std::string> del;
  for(auto& [id,t0]:G.tracks){
    TrackRec t=t0; ImGui::PushID(id.c_str());
    bool en=t.enabled;
    if(ImGui::Checkbox("##en",&en)){ t.enabled=en; G.tracks[id]=t;
      bridge::post({{"t","track"},{"id",id},{"cls",t.cls},{"member",t.member},{"isField",t.isField},{"on",en}}); }
    ImGui::SameLine();
    bool open=ImGui::TreeNode((t.cls+"::"+t.member).c_str());
    ImGui::SameLine(); if(ImGui::SmallButton("clear")) G.tracks[id].log.clear();
    ImGui::SameLine(); if(ImGui::SmallButton("delete")){
      bridge::post({{"t","trackDel"},{"id",id},{"cls",t.cls},{"member",t.member},{"isField",t.isField}});
      del.push_back(id); if(open)ImGui::TreePop(); ImGui::PopID(); continue; }
    if(open){
      if(t.isField) ImGui::TextDisabled("field track = value-change watch (1s poll)");
      ImGui::BeginChild("##tl",ImVec2(-1,240),true);
      for(auto& e:G.tracks[id].log){ ImGui::TextWrapped("%s",e.text.c_str()); drawStack(e.stack); }
      ImGui::EndChild(); ImGui::TreePop();
    }
    ImGui::PopID();
  }
  for(auto& d:del) G.tracks.erase(d);
  if(G.tracks.empty()) ImGui::TextDisabled("No trackers. Register from Classes tab.");
}
static void tabDumper(){
  if(ImGui::Button("Scan memory & dump all loaded DEX")) bridge::post({{"t","dump"}});
  ImGui::TextWrapped("Dumps go to: %s (and Downloads if enabled in Settings)",G.cfg.dir.c_str());
  for(auto& [p,n]:G.dumped) ImGui::TextWrapped("%s  (%lld bytes)",p.c_str(),n);
}
static void tabRecorder(){
  ImGui::SetNextItemWidth(-1); ImGui::InputText("scope prefix",G.recScope,sizeof G.recScope);
  if(!G.recorderOn){ if(ImGui::Button("Start")){ G.recorderOn=true;
    bridge::post({{"t","record"},{"on",true},{"scope",G.recScope}}); } }
  else { if(ImGui::Button("Stop")){ G.recorderOn=false; bridge::post({{"t","record"},{"on",false}}); } }
  ImGui::SameLine(); if(ImGui::Button("Clear")) G.recordLog.clear();
  ImGui::TextDisabled("Hooks all methods of classes under scope (capped). Fields not recorded in phase 1.");
  drawLog(G.recordLog,true);
}
static void tabMonitor(){
  if(!G.monitorOn){ if(ImGui::Button("Start")){ G.monitorOn=true; bridge::post({{"t","monitor"},{"on",true}}); } }
  else { if(ImGui::Button("Stop")){ G.monitorOn=false; bridge::post({{"t","monitor"},{"on",false}}); } }
  ImGui::SameLine(); if(ImGui::Button("Clear")) G.monitorLog.clear();
  ImGui::TextDisabled("SSL_read/SSL_write plaintext + URL opens, with Java caller stack when available.");
  ImGui::BeginChild("##ml",ImVec2(-1,260),true);
  for(int i=0;i<(int)G.monitorLog.size();i++){
    auto& e=G.monitorLog[i];
    if(ImGui::Selectable(e.text.c_str())) G.monitorDetail=i;
  }
  ImGui::EndChild();
  if(G.monitorDetail>=0&&G.monitorDetail<(int)G.monitorLog.size()&&ImGui::BeginPopupModal("Traffic detail",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
    auto& e=G.monitorLog[G.monitorDetail];
    ImGui::TextWrapped("%s",e.raw.value("preview","").c_str());
    ImGui::Separator(); drawStack(e.stack);
    if(ImGui::Button("Close")){ ImGui::CloseCurrentPopup(); G.monitorDetail=-1; }
    ImGui::EndPopup();
  }
  if(G.monitorDetail>=0) ImGui::OpenPopup("Traffic detail");
}
static void tabScript(){
  ImGui::InputTextMultiline("##src",G.scriptBuf,sizeof G.scriptBuf,ImVec2(-1,ImGui::GetContentRegionAvail().y*0.5f));
  if(ImGui::Button("Run")) bridge::post({{"t","eval"},{"code",G.scriptBuf}});
  ImGui::SameLine(); if(ImGui::Button("Clear log")) G.scriptLog.clear();
  ImGui::TextDisabled("Frida JS (Java, Interceptor, Memory...). console.log below.");
  drawLog(G.scriptLog,false);
}
// ---------------- classes tab ----------------
static void memberPanels(ClassWindow& w,const MethodInfo& m,const std::string& key){
  MemberUI& mu=w.mu[key];
  auto toggle=[&](const char* l,bool& b){ if(ImGui::SmallButton(l)) b=!b; ImGui::SameLine(); };
  toggle("Call",mu.call); toggle("Patch",mu.patch); toggle("Track",mu.track); toggle("Hook",mu.hook);
  ImGui::NewLine();
  if(mu.call){
    if(mu.callArgs.types.empty()) mu.callArgs.init(m.p,false);
    argEditor(mu.callArgs,"ca",w.id,key,"call",false);
    if(ImGui::Button("Call")){
      bridge::post({{"t","call"},{"id",req()},{"cls",w.cls},{"name",m.n},{"ov",m.ov},
        {"h",w.handle},{"args",mu.callArgs.vals},
        {"ctx",{{"w",w.id},{"k","call"},{"m",key}}}});
    }
    ImGui::TextWrapped("%s",mu.callResult.c_str());
    for(auto& e:mu.callLog) ImGui::TextDisabled("%s",e.text.c_str());
  }
  if(mu.patch){
    if(mu.patchArgs.types.empty()) mu.patchArgs.init(m.p,true);
    argEditor(mu.patchArgs,"pa",w.id,key,"patch",true);
    bool ron=mu.patchRet.value("k","skip")!="skip";
    if(ImGui::Checkbox("patch return",&ron)) mu.patchRet["k"]=ron?"prim":"skip";
    if(ron){ std::string v=mu.patchRet.value("v",""); ImGui::SetNextItemWidth(-1);
      if(ImGui::InputText("ret",&v)){ mu.patchRet["v"]=v;
        mu.patchRet["k"]=ArgEditor::isText(m.ret)?"prim":"str"; } }
    if(ImGui::Button("Register patch")){
      PatchRec p; p.id=key; p.cls=w.cls; p.member=m.n; p.ov=m.ov; p.params=m.p;
      p.args=mu.patchArgs.vals; p.ret=mu.patchRet; p.retType=m.ret;
      G.patches[key]=p; bridge::post({{"t","patch"},{"rec",patchJson(p)}}); toast("patch registered");
    }
  }
  if(mu.track){
    bool on=G.tracks.count(key)&&G.tracks[key].enabled;
    if(ImGui::Checkbox("track on/off",&on)){
      if(on){ TrackRec t; t.id=key; t.cls=w.cls; t.member=m.n; G.tracks[key]=t; }
      bridge::post({{"t","track"},{"id",key},{"cls",w.cls},{"member",m.n},{"ov",m.ov},{"isField",false},{"on",on}});
      if(!on) G.tracks.erase(key);
    }
    ImGui::TextDisabled("Logs appear in Trackers tab.");
  }
  if(mu.hook){
    if(mu.hookCode.empty()) mu.hookCode=hookTemplate(m);
    int mode=mu.hookMode=="java"?0:(mu.hookMode=="smali"?1:2);
    ImGui::Combo("mode",&mode,"Java (JS)\0Smali (phase 2)\0Noop\0");
    mu.hookMode=mode==0?"java":(mode==1?"smali":"noop");
    if(mu.hookMode=="java") ImGui::InputTextMultiline("##hc",&mu.hookCode,ImVec2(-1,180));
    if(mu.hookMode=="smali") ImGui::TextWrapped("Phase 2: embedded smali assembler. Use Java (JS)/Noop.");
    if(ImGui::Button("Apply hook")){
      HookRec h; h.id=key; h.cls=w.cls; h.name=m.n; h.ov=m.ov; h.mode=mu.hookMode;
      h.code=mu.hookCode; h.ret=m.ret; h.params=m.p;
      G.hooks[key]=h; bridge::post({{"t","hook"},{"rec",hookJson(h)}}); toast("hook applied");
    }
  }
}
static void fieldPanel(ClassWindow& w,const FieldInfo& f,const std::string& key){
  MemberUI& mu=w.mu[key];
  std::string cur=(w.handle||f.st)?(mu.fieldVal.empty()?f.v:mu.fieldVal):"(needs instance)";
  ImGui::Text("current: %s",cur.c_str());
  if((w.handle||f.st)&&ImGui::Button("Read"))
    bridge::post({{"t","fieldGet"},{"cls",w.cls},{"name",f.n},{"h",w.handle},{"type",f.t},
      {"ctx",{{"w",w.id},{"k","field"},{"m",key}}}});
  ImGui::SetNextItemWidth(-1); ImGui::InputText("value",&mu.fieldVal);
  if(ImGui::Button("Set"))
    bridge::post({{"t","fieldSet"},{"cls",w.cls},{"name",f.n},{"h",w.handle},{"type",f.t},{"val",mu.fieldVal}});
  ImGui::SameLine();
  if(ImGui::Button("Patch")){
    PatchRec p; p.id=key; p.cls=w.cls; p.member=f.n; p.isField=true; p.type=f.t;
    p.ret=json{{"k","prim"},{"v",mu.fieldVal}}; p.lock=mu.fieldLock; p.enabled=true;
    G.patches[key]=p; bridge::post({{"t","patch"},{"rec",patchJson(p)}});
  }
  ImGui::Checkbox("lock (poll)",&mu.fieldLock);
  bool tr=G.tracks.count(key)&&G.tracks[key].enabled;
  if(ImGui::Checkbox("track changes",&tr)){
    if(tr){ TrackRec t; t.id=key; t.cls=w.cls; t.member=f.n; t.isField=true; G.tracks[key]=t; }
    else G.tracks.erase(key);
    bridge::post({{"t","track"},{"id",key},{"cls",w.cls},{"member",f.n},{"isField",true},{"on",tr}});
  }
}
static void classWindow(ClassWindow& w){
  if(!w.loaded){ ImGui::TextDisabled("loading..."); return; }
  ImGui::SetNextItemWidth(-1); ImGui::InputText("filter members",w.mfilter,sizeof w.mfilter);
  if(ImGui::Button("Instances")) w.instPanel=!w.instPanel;
  ImGui::SameLine(); if(ImGui::SmallButton("close window")) w.open=false;
  if(w.instPanel){
    ImGui::BeginChild("##inst",ImVec2(-1,230),true);
    if(ImGui::Button("Find")) bridge::post({{"t","inst"},{"id",req()},{"cls",w.cls},
      {"ctx",{{"w",w.id},{"k","find"}}}});
    ImGui::SameLine(); if(ImGui::Button("Create")) w.creating=!w.creating;
    if(w.creating&&!w.ctors.empty()){
      std::vector<std::string> labels;
      for(auto& c:w.ctors){ std::string s="("; for(auto& t:c.get<std::vector<std::string>>()) s+=t+" "; s+=")"; labels.push_back(s); }
      std::vector<const char*> ptrs; for(auto& l:labels) ptrs.push_back(l.c_str());
      if(ImGui::Combo("constructor",&w.ctorSel,ptrs.data(),(int)ptrs.size())){
        w.ctorArgs.init(w.ctors[w.ctorSel].get<std::vector<std::string>>(),false); }
      argEditor(w.ctorArgs,"ct",w.id,"","ctor",false);
      if(ImGui::Button("Create instance"))
        bridge::post({{"t","create"},{"id",req()},{"cls",w.cls},{"sel",w.ctorSel},{"args",w.ctorArgs.vals},
          {"ctx",{{"w",w.id},{"k","create"}}}});
    }
    for(auto& in:w.instances){
      std::string l=in.label+" ##"+std::to_string(in.h);
      if(ImGui::Selectable(l.c_str())) openClass(w.cls,in.h);
    }
    ImGui::EndChild();
  }
  std::string fl=w.mfilter;
  if(ImGui::CollapsingHeader("Fields",ImGuiTreeNodeFlags_DefaultOpen)){
    for(auto& f:w.fields){
      if(!fl.empty()&&f.n.find(fl)==std::string::npos) continue;
      std::string key=w.cls+"::"+f.n;
      ImGui::PushID(key.c_str());
      ImGui::TextColored(ImColor(typeCol(f.t)),"%s",f.t.c_str()); ImGui::SameLine();
      bool open=ImGui::TreeNode((f.mods+" "+f.n).c_str());
      if(open){ fieldPanel(w,f,key); ImGui::TreePop(); }
      ImGui::PopID();
    }
  }
  if(ImGui::CollapsingHeader("Methods",ImGuiTreeNodeFlags_DefaultOpen)){
    for(auto& m:w.methods){
      if(!fl.empty()&&m.n.find(fl)==std::string::npos) continue;
      std::string key=w.cls+"::"+m.n+"#"+std::to_string(m.ov);
      ImGui::PushID(key.c_str());
      ImGui::TextColored(ImColor(typeCol(m.ret)),"%s",m.ret.c_str()); ImGui::SameLine();
      bool open=ImGui::TreeNode(sigOf(m).c_str());
      if(open){ ImGui::TextDisabled("%s",m.mods.c_str()); memberPanels(w,m,key); ImGui::TreePop(); }
      ImGui::PopID();
    }
  }
}
static void listWindow(ClassWindow& w){
  ImGui::SetNextItemWidth(-1); ImGui::InputText("filter",w.filter,sizeof w.filter);
  ImGui::Combo("mode",&w.mode,"Class\0Method\0Field\0");
  if(w.mode==1) ImGui::Combo("by",&w.sub,"Name\0Return type\0Param type\0");
  if(w.mode==2) ImGui::Combo("by",&w.sub,"Name\0Type\0");
  if(ImGui::Button("Search")){ w.searching=true;
    bridge::post({{"t","enum"},{"id",req()},{"filter",w.filter},{"mode",w.mode},{"sub",w.sub},
      {"ctx",{{"w",w.id},{"k","list"}}}}); }
  ImGui::SameLine(); if(ImGui::SmallButton("close window")) w.open=false;
  if(w.searching) ImGui::TextDisabled("searching...");
  ImGui::Text("%zu results",(size_t)w.results.size());
  ImGui::BeginChild("##res",ImVec2(-1,-1),true);
  for(auto& r:w.results) if(ImGui::Selectable(r.c_str())) openClass(r);
  ImGui::EndChild();
}
static void tabClasses(){
  ImGui::BeginTabBar("##cw");
  if(ImGui::TabItemButton("+")){ ClassWindow w; w.id=G.nextWin++; G.wins.push_back(w); G.activeWin=w.id; }
  for(auto& w:G.wins){
    if(!w.open) continue;
    std::string lab=w.title+"###w"+std::to_string(w.id);
    if(ImGui::BeginTabItem(lab.c_str())){
      G.activeWin=w.id;
      if(w.isList) listWindow(w); else classWindow(w);
      ImGui::EndTabItem();
    }
  }
  ImGui::EndTabBar();
  G.wins.erase(std::remove_if(G.wins.begin(),G.wins.end(),[](const ClassWindow& w){return !w.open;}),G.wins.end());
  if(G.wins.empty()){ ClassWindow w; w.id=G.nextWin++; G.wins.push_back(w); }
}
// ---------------- frame ----------------
void draw(){
  std::lock_guard<std::mutex> lk(G.mtx);
  flushOpen();
  ImGuiIO& io=ImGui::GetIO();
  io.FontGlobalScale=G.cfg.fontScale;
  static ImVec2 tpos(12,160);
  ImGui::SetNextWindowPos(tpos,ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
  ImGui::Begin("##toggle",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoNav);
  ImVec2 sz(72,72);
  ImGui::InvisibleButton("##tgl",sz);
  static bool dragging=false; static float moved=0;
  if(ImGui::IsItemActivated()){ dragging=true; moved=0; }
  if(dragging&&ImGui::IsMouseDown(0)){ ImVec2 d=io.MouseDelta; moved+=fabsf(d.x)+fabsf(d.y); tpos.x+=d.x; tpos.y+=d.y; }
  if(dragging&&ImGui::IsMouseReleased(0)){ dragging=false; if(moved<6.0f) G.menuOpen=!G.menuOpen; }
  ImDrawList* dl=ImGui::GetWindowDrawList();
  ImVec2 p=ImGui::GetItemRectMin();
  ImVec2 c(p.x+sz.x*0.5f,p.y+sz.y*0.5f);
  float t=(float)ImGui::GetTime();
  float pulse=0.5f+0.5f*sinf(t*2.5f);
  float r=30.0f;
  ImVec2 a(c.x,c.y-r), b(c.x-r*0.87f,c.y+r*0.5f), cc(c.x+r*0.87f,c.y+r*0.5f);
  dl->AddTriangle(a,b,cc,IM_COL32(0,255,150,(int)(70+120*pulse)),3.0f+2.0f*pulse);
  dl->AddTriangle(a,b,cc,IM_COL32(0,168,107,255),1.5f);
  float br=14.0f+2.0f*pulse;
  dl->AddCircleFilled(c,br+4.0f,IM_COL32(200,80,255,(int)(90+150*pulse)),32);
  dl->AddCircleFilled(c,br,IM_COL32(160,32,240,255),32);
  dl->AddCircle(c,br,IM_COL32(255,255,255,200),32,1.5f);
  dl->AddText(ImVec2(c.x-5,c.y-7),IM_COL32(255,255,255,255),"T");
  ImGui::End(); ImGui::PopStyleVar();
  if(G.menuOpen){
    ImGui::SetNextWindowSize(ImVec2(std::min(520.0f,io.DisplaySize.x-16),std::min(700.0f,io.DisplaySize.y-60)),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(8,180),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(G.cfg.opacity);
    ImGui::Begin("libtool",&G.menuOpen);
    if(ImGui::BeginTabBar("##main")){
      if(ImGui::BeginTabItem("Settings")){ tabSettings(); ImGui::EndTabItem(); }
      if(ImGui::BeginTabItem("Patches")){ tabPatches(); ImGui::EndTabItem(); }
      if(ImGui::BeginTabItem("Hooks")){ tabHooks(); ImGui::EndTabItem(); }
      if(ImGui::BeginTabItem("Trackers")){ tabTrackers(); ImGui::EndTabItem(); }
      if(ImGui::BeginTabItem("Dumper")){ tabDumper(); ImGui::EndTabItem(); }
      if(ImGui::BeginTabItem("Recorder")){ tabRecorder(); ImGui::EndTabItem(); }
      if(ImGui::BeginTabItem("Monitor")){ tabMonitor(); ImGui::EndTabItem(); }
      if(ImGui::BeginTabItem("Script")){ tabScript(); ImGui::EndTabItem(); }
      if(ImGui::BeginTabItem("Classes")){ tabClasses(); ImGui::EndTabItem(); }
      ImGui::EndTabBar();
    }
    ImGui::End();
  }
  if(g_detailOpen){ ImGui::OpenPopup("Call stack"); g_detailOpen=false; }
  if(ImGui::BeginPopupModal("Call stack",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
    for(auto& f:g_detail.stack){ if(ImGui::Selectable(f.s.c_str())){ openClass(f.c); ImGui::CloseCurrentPopup(); } }
    if(ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  float y=io.DisplaySize.y-30;
  for(auto it=G.toasts.begin();it!=G.toasts.end();){
    it->second-=io.DeltaTime;
    if(it->second<=0){ it=G.toasts.erase(it); continue; }
    ImGui::SetNextWindowPos(ImVec2(8,y-28)); y-=32;
    ImGui::Begin(("##toast"+std::to_string((size_t)&*it)).c_str(),nullptr,
      ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoNav);
    ImGui::TextUnformatted(it->first.c_str()); ImGui::End(); ++it;
  }
}
} // namespace ui
