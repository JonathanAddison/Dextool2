#pragma once
#include <string>
#include <jni.h>
namespace diag {
  void init(JavaVM* vm);
  void log(const char* fmt,...);
  void setDir(const std::string& d);
  std::string dir();
  void publishAll();
}
extern "C" void tool_export_logs();
