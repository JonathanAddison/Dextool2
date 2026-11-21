cmake_minimum_required(VERSION 3.22)
project(tool C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_library(tool SHARED
  native/main.cpp native/renderer.cpp native/ui.cpp native/diag.cpp
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
  ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp)
target_include_directories(tool PRIVATE
  native ${GUM_DIR} ${IMGUI_DIR} ${IMGUI_DIR}/backends
  ${IMGUI_DIR}/misc/cpp ${JSON_DIR} ${GEN_DIR})
target_compile_definitions(tool PRIVATE IMGUI_IMPL_OPENGL_ES3)
target_compile_options(tool PRIVATE -ffunction-sections -fdata-sections -fvisibility=hidden -O2)
target_link_options(tool PRIVATE
  -Wl,--gc-sections -Wl,--exclude-libs,ALL -s
  -Wl,--version-script=${CMAKE_SOURCE_DIR}/native/exports.map)
target_link_libraries(tool PRIVATE
  ${GUM_DIR}/libfrida-gumjs.a EGL GLESv3 android log dl m atomic)
