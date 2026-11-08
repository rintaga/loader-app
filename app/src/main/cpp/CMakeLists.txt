cmake_minimum_required(VERSION 3.22)
project(customloader CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(shadowhook REQUIRED CONFIG)
set(IMGUI ${CMAKE_CURRENT_SOURCE_DIR}/imgui)   # git clone ocornut/imgui ke sini (lihat workflow)
add_library(customloader SHARED loader.cpp
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_opengl3.cpp)
target_include_directories(customloader PRIVATE ${IMGUI} ${IMGUI}/backends)
target_compile_definitions(customloader PRIVATE IMGUI_IMPL_OPENGL_ES3)
target_link_libraries(customloader shadowhook::shadowhook log EGL GLESv3 android dl)
