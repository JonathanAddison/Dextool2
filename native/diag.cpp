#include "diag.h"
#include <android/log.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <csignal>
#include <unwind.h>
#include <dlfcn.h>
#include <thread>
#include <mutex>
#include <vector>
#define TAG "libtool"
static JavaVM* g_vm=nullptr;
static char g_dir[512]={0}, g_pkg[256]={0};
static int g_fd=-1;
static char g_ring[64][256]; static int g_ringN=0;
static std::mutex g_mtx;
static bool g_diagPub=false;
static bool g_pubDone=false;
static struct sigaction g_old[64];
static void mkdirs(const char* p){ char t[512]; strncpy(t,p,511); t[511]=0;
  for(char* q=t+1;*q;q++){ if(*q=='/'){ *q=0; mkdir(t,0771); *q='/'; } } mkdir(t,0771); }
static void detect(){
  FILE* f=fopen("/proc/self/cmdline","r");
  if(f){ size_t n=fread(g_pkg,1,255,f); fclose(f); g_pkg[n]=0;
    for(char* q=g_pkg;*q;q++) if(*q==':'||*q=='\n'||*q==' ') *q=0; }
  if(!g_pkg[0]) strcpy(g_pkg,"unknown");
  snprintf(g_dir,sizeof g_dir,"/data/data/%s/files/tool",g_pkg);
  mkdirs(g_dir);
  char pend[600]; snprintf(pend,sizeof pend,"%s/pending",g_dir); mkdirs(pend);
  char lp[600]; snprintf(lp,sizeof lp,"%s/diag.log",g_dir);
  g_fd=open(lp,O_CREAT|O_APPEND|O_WRONLY,0660);
}
namespace diag {
std::string dir(){ return g_dir; }
void setDir(const std::string& d){ if(!d.empty()) mkdirs(d.c_str()); }
void log(const char* fmt,...){
  char buf[256]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);
  char line[320]; snprintf(line,sizeof line,"[%ld] %s\n",(long)time(nullptr),buf);
  { std::lock_guard<std::mutex> lk(g_mtx);
    strncpy(g_ring[g_ringN%64],line,255); g_ring[g_ringN%64][255]=0; g_ringN++;
    if(g_fd>=0) write(g_fd,line,strlen(line)); }
  __android_log_print(ANDROID_LOG_INFO,TAG,"%s",buf);
}
}
struct BtState{ void** cur; void** end; };
static _Unwind_Reason_Code btCb(_Unwind_Context* c,void* p){
  BtState* s=(BtState*)p; void* pc=(void*)_Unwind_GetIP(c);
  if(s->cur<s->end)*s->cur++=pc; return _URC_NO_REASON; }
static void crashHandler(int sig,siginfo_t* si,void*){
  char path[700]; snprintf(path,sizeof path,"%s/pending/crash_%ld_%d.txt",g_dir,(long)time(nullptr),sig);
  int fd=open(path,O_CREAT|O_WRONLY|O_TRUNC,0660);
  if(fd>=0){
    char b[512]; int n;
    n=snprintf(b,sizeof b,"signal %d addr %p pid %d tid %d pkg %s\n",sig,si->si_addr,getpid(),gettid(),g_pkg); write(fd,b,n);
    char fp[92]; __system_property_get("ro.build.fingerprint",fp);
    n=snprintf(b,sizeof b,"build %s\n",fp); write(fd,b,n);
    int count=g_ringN<64?g_ringN:64, start=g_ringN-count;
    for(int i=0;i<count;i++){ const char* l=g_ring[(start+i)%64]; write(fd,l,strlen(l)); }
    void* bt[64]; BtState s{bt,bt+64}; _Unwind_Backtrace(btCb,&s);
    for(void** p=bt;p<s.cur;p++){ Dl_info di;
      if(dladdr(*p,&di)) n=snprintf(b,sizeof b,"%s (%s+0x%lx)\n",di.dli_fname,di.dli_sname?di.dli_sname:"?",(unsigned long)((char*)*p-(char*)di.dli_saddr));
      else n=snprintf(b,sizeof b,"%p\n",*p); write(fd,b,n); }
    int m=open("/proc/self/maps",O_RDONLY);
    if(m>=0){ char mb[4096]; ssize_t r; while((r=read(m,mb,sizeof mb))>0) write(fd,mb,r); close(m); }
    fsync(fd); close(fd);
  }
  __android_log_print(ANDROID_LOG_ERROR,TAG,"crash %d -> %s",sig,path);
  sigaction(sig,&g_old[sig],nullptr); raise(sig);
}
static void installCrash(){
  struct sigaction sa{}; sa.sa_sigaction=crashHandler; sa.sa_flags=SA_SIGINFO|SA_RESETHAND;
  for(int s:{SIGSEGV,SIGABRT,SIGBUS,SIGFPE,SIGILL,SIGSYS}) sigaction(s,&sa,&g_old[s]);
}
static bool getCtx(JNIEnv* env,jobject* out){
  jclass at=env->FindClass("android/app/ActivityThread");
  if(!at){ env->ExceptionClear(); return false; }
  jmethodID m=env->GetStaticMethodID(at,"currentApplication","()Landroid/app/Application;");
  jobject app=env->CallStaticObjectMethod(at,m);
  if(env->ExceptionCheck()) env->ExceptionClear();
  if(!app){ env->DeleteLocalRef(at); return false; }
  jclass cc=env->FindClass("android/content/Context");
  jmethodID gc=env->GetMethodID(cc,"getApplicationContext","()Landroid/content/Context;");
  jobject ctx=env->CallObjectMethod(app,gc);
  env->DeleteLocalRef(app); env->DeleteLocalRef(at); env->DeleteLocalRef(cc);
  if(!ctx) return false; *out=ctx; return true;
}
static void publishOne(JNIEnv* env,jobject resolver,jobject dUri,jclass cvCls,jmethodID cvInit,
    jmethodID put,jmethodID insert,jmethodID openOS,jmethodID write,jmethodID close,
    const std::string& path,bool renameAfter){
  FILE* f=fopen(path.c_str(),"rb"); if(!f) return;
  fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
  if(sz<=0||sz>8*1024*1024){ fclose(f); return; }
  std::vector<char> buf(sz); fread(buf.data(),1,sz,f); fclose(f);
  std::string base=path.substr(path.find_last_of('/')+1);
  std::string name="libtool_"+base;
  jobject cv=env->NewObject(cvCls,cvInit);
  jstring jn=env->NewStringUTF(name.c_str()), jm=env->NewStringUTF("text/plain");
  jstring k1=env->NewStringUTF("_display_name"), k2=env->NewStringUTF("mime_type");
  env->CallVoidMethod(cv,put,k1,jn); env->CallVoidMethod(cv,put,k2,jm);
  jobject outUri=env->CallObjectMethod(resolver,insert,dUri,cv);
  if(env->ExceptionCheck()) env->ExceptionClear();
  bool ok=false;
  if(outUri){
    jobject os=env->CallObjectMethod(resolver,openOS,outUri);
    if(env->ExceptionCheck()) env->ExceptionClear();
    if(os){
      jbyteArray arr=env->NewByteArray(sz);
      env->SetByteArrayRegion(arr,0,sz,(jbyte*)buf.data());
      env->CallVoidMethod(os,write,arr,0,(jint)sz);
      env->CallVoidMethod(os,close);
      if(env->ExceptionCheck()){ diag::log("publish write exception"); env->ExceptionClear(); } else { ok=true; diag::log("published %s",name.c_str()); }
      env->DeleteLocalRef(arr); env->DeleteLocalRef(os);
    }
    env->DeleteLocalRef(outUri);
  }
  env->DeleteLocalRef(cv);env->DeleteLocalRef(jn);env->DeleteLocalRef(jm);
  env->DeleteLocalRef(k1);env->DeleteLocalRef(k2);
  if(ok&&renameAfter){ std::string done=path+".published"; rename(path.c_str(),done.c_str()); }
}
static void mirrorToExternal(){
  std::string dir="/sdcard/Android/data/"+std::string(g_pkg)+"/files/tool";
  mkdirs(dir.c_str());
  std::vector<std::string> files;
  char pend[600]; snprintf(pend,sizeof pend,"%s/pending",g_dir);
  DIR* d=opendir(pend); if(d){ dirent* e; while((e=readdir(d))){ std::string nm=e->d_name; if(nm[0]=='.') continue; files.push_back(std::string(pend)+"/"+nm);} closedir(d); }
  files.push_back(std::string(g_dir)+"/diag.log");
  for(const auto& p:files){ FILE* in=fopen(p.c_str(),"rb"); if(!in) continue;
    std::string base=p.substr(p.find_last_of('/')+1);
    std::string out=dir+"/libtool_"+base;
    FILE* o=fopen(out.c_str(),"wb"); if(!o){ fclose(in); continue; }
    char b[8192]; size_t r; while((r=fread(b,1,sizeof b,in))>0) fwrite(b,1,r,o);
    fclose(in); fclose(o); diag::log("mirrored %s",out.c_str()); }
}
static void publishAll(){
  diag::log("publish start");
  if(!g_vm) return;
  JNIEnv* env=nullptr;
  if(g_vm->AttachCurrentThread(&env,nullptr)!=0) return;
  jobject ctx=nullptr;
  for(int i=0;i<40&&!ctx;i++){ if(getCtx(env,&ctx)) break; usleep(250000); }
  if(!ctx){ diag::log("publish: no context"); g_vm->DetachCurrentThread(); return; }
  diag::log("publish: ctx ok");
  mirrorToExternal();
  jclass bv=env->FindClass("android/os/Build$VERSION");
  int sdk=env->GetStaticIntField(bv,env->GetStaticFieldID(bv,"SDK_INT","I"));
  diag::log("publish sdk=%d",sdk);
  jclass cc=env->FindClass("android/content/Context");
  jobject resolver=env->CallObjectMethod(ctx,env->GetMethodID(cc,"getContentResolver","()Landroid/content/ContentResolver;"));
  if(sdk>=29&&resolver){
    jclass ms=env->FindClass("android/provider/MediaStore$Downloads");
    jobject dUri=env->GetStaticObjectField(ms,env->GetStaticFieldID(ms,"EXTERNAL_CONTENT_URI","Landroid/net/Uri;"));
    jclass cvCls=env->FindClass("android/content/ContentValues");
    jmethodID cvInit=env->GetMethodID(cvCls,"<init>","()V");
    jmethodID put=env->GetMethodID(cvCls,"put","(Ljava/lang/String;Ljava/lang/String;)V");
    jclass cr=env->FindClass("android/content/ContentResolver");
    jmethodID insert=env->GetMethodID(cr,"insert","(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;");
    jmethodID openOS=env->GetMethodID(cr,"openOutputStream","(Landroid/net/Uri;)Ljava/io/OutputStream;");
    jclass osCls=env->FindClass("java/io/OutputStream");
    jmethodID write=env->GetMethodID(osCls,"write","([BII)V");
    jmethodID close=env->GetMethodID(osCls,"close","()V");
    std::vector<std::string> files;
    char pend[600]; snprintf(pend,sizeof pend,"%s/pending",g_dir);
    DIR* d=opendir(pend);
    if(d){ dirent* e; while((e=readdir(d))){ std::string nm=e->d_name;
      if(nm[0]=='.'||nm.size()>10&&nm.substr(nm.size()-10)==".published") continue;
      files.push_back(std::string(pend)+"/"+nm); } closedir(d); }
    for(auto& p:files) publishOne(env,resolver,dUri,cvCls,cvInit,put,insert,openOS,write,close,p,true);
    std::string diagp=std::string(g_dir)+"/diag.log";
    if(!g_diagPub){ publishOne(env,resolver,dUri,cvCls,cvInit,put,insert,openOS,write,close,diagp,false); g_diagPub=true; g_pubDone=true; }
    env->DeleteLocalRef(ms);env->DeleteLocalRef(dUri);env->DeleteLocalRef(cvCls);
    env->DeleteLocalRef(cr);env->DeleteLocalRef(osCls);
  }
    if(sdk<29){
      std::vector<std::string> files;
      char pend[600]; snprintf(pend,sizeof pend,"%s/pending",g_dir);
      DIR* d=opendir(pend); if(d){ dirent* e; while((e=readdir(d))){ std::string nm=e->d_name; if(nm[0]=='.') continue; files.push_back(std::string(pend)+"/"+nm);} closedir(d); }
      files.push_back(std::string(g_dir)+"/diag.log");
      for(auto& p:files){ std::string bb=p.substr(p.find_last_of('/')+1); if(g_diagPub&&bb=="diag.log") continue; FILE* in=fopen(p.c_str(),"rb"); if(!in) continue;
        std::string base=p.substr(p.find_last_of('/')+1);
        std::string out="/sdcard/Download/libtool_"+base;
        FILE* o=fopen(out.c_str(),"wb"); if(!o){ diag::log("fallback open fail %s",out.c_str()); fclose(in); continue; }
        char b[8192]; size_t r; while((r=fread(b,1,sizeof b,in))>0) fwrite(b,1,r,o);
        fclose(in); fclose(o); diag::log("fallback published %s",out.c_str()); if(base=="diag.log"){ g_diagPub=true; g_pubDone=true; } }
    }
    if(resolver) env->DeleteLocalRef(resolver);
  env->DeleteLocalRef(ctx); env->DeleteLocalRef(cc); env->DeleteLocalRef(bv);
  g_vm->DetachCurrentThread();
}
namespace diag { void publishAll(){ ::publishAll(); } }
extern "C" void tool_export_logs(){ std::thread([]{ publishAll(); }).detach(); }
namespace diag {
void init(JavaVM* vm){
  g_vm=vm; detect(); installCrash();
  log("diag init dir=%s pkg=%s",g_dir,g_pkg);
  std::thread([]{ for(int i=0;i<8 && !g_pubDone;i++){ usleep(i==0?1500000:5000000); publishAll(); } }).detach();
}
}
