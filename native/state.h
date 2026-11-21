#pragma once
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// ---- wire value encoding: {k:"skip"|"null"|"prim"|"str"|"obj", v?, h?}
struct MethodInfo { std::string n, mods, ret; std::vector<std::string> p; bool st=false; int ov=-1; };
struct FieldInfo  { std::string n, mods, t, v; bool st=false; };
struct InstanceInfo { long long h=0; std::string label; };
struct StackFrame { std::string c, s; };
struct LogEntry { std::string text, cls; std::vector<StackFrame> stack; json raw; };
struct PatchRec { std::string id, cls, member, type, retType; bool isField=false, enabled=true, lock=false;
                  int ov=-1; std::vector<std::string> params; std::vector<json> args; json ret; };
struct HookRec  { std::string id, cls, name, mode="java", code, ret; bool enabled=true; int ov=-1;
                  std::vector<std::string> params; };
struct TrackRec { std::string id, cls, member; bool isField=false, enabled=true; std::deque<LogEntry> log; };

struct ArgEditor {
  std::vector<std::string> types; std::vector<json> vals;
  int popupFor=-1; std::vector<InstanceInfo> results; bool busy=false;
  void init(const std::vector<std::string>& t, bool patchMode){
    types=t; vals.assign(t.size(), json{{"k", patchMode?"skip":"prim"},{"v",""}});
    for(size_t i=0;i<t.size();i++) if(!isText(t[i])) vals[i]=json{{"k","null"}};
  }
  static bool isText(const std::string& t){
    return t=="int"||t=="long"||t=="short"||t=="byte"||t=="float"||t=="double"
        ||t=="boolean"||t=="char"||t=="java.lang.String"||t=="java.lang.CharSequence";
  }
};
struct MemberUI {
  bool call=false, patch=false, track=false, hook=false;
  ArgEditor callArgs, patchArgs; json patchRet=json{{"k","skip"},{"v",""}}; bool patchRetOn=false;
  std::string hookMode="java", hookCode, callResult, fieldVal; bool fieldLock=false, fieldTrack=false;
  std::deque<LogEntry> callLog;
};
struct ClassWindow {
  int id=0; bool isList=true, open=true, loaded=false, searching=false;
  std::string title="Classes", cls; long long handle=0;
  char filter[256]={0}; int mode=0, sub=0;
  std::vector<std::string> results;
  char mfilter[256]={0};
  std::vector<MethodInfo> methods; std::vector<FieldInfo> fields; json ctors=json::array();
  bool instPanel=false, creating=false; int ctorSel=0; ArgEditor ctorArgs;
  std::vector<InstanceInfo> instances;
  std::map<std::string, MemberUI> mu;
};
struct Settings {
  float fontScale=1.0f, opacity=0.95f; int theme=0, logCap=2000;
  bool publishDownloads=true, crashHandler=true;
  std::string dir, pkg; int sdk=0;
};
struct AppState {
  std::mutex mtx;
  std::vector<ClassWindow> wins; int activeWin=-1, nextWin=1, nextReq=1;
  std::map<std::string, PatchRec> patches;
  std::map<std::string, HookRec> hooks;
  std::map<std::string, TrackRec> tracks;
  std::deque<LogEntry> recordLog, monitorLog, scriptLog;
  std::vector<std::pair<std::string,long long>> dumped;
  std::deque<std::pair<std::string,float>> toasts;
  Settings cfg; bool menuOpen=false, recorderOn=false, monitorOn=false;
  char scriptBuf[65536]={0}; char recScope[256]={0};
  int monitorDetail=-1;
};
inline AppState G;

namespace ui { void draw(); void openClass(const std::string& cls, long long h=0);
               void toast(const std::string& s); void route(const json& j); }
namespace bridge { void post(const json& j); }
