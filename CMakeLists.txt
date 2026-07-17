cmake_minimum_required(VERSION 3.20)
project(wslpm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Win32 GUI application (no console)
add_executable(wslpm WIN32
    src/main.cpp
    src/app.cpp
    src/wsl_bridge.cpp
    src/operations.cpp
    res/app.rc
)

target_include_directories(wslpm PRIVATE src)

target_compile_definitions(wslpm PRIVATE UNICODE _UNICODE)

target_link_libraries(wslpm PRIVATE
    user32 gdi32 shell32 dwmapi comctl32 uxtheme
    ole32 oleaut32 crypt32 shlwapi comdlg32
    advapi32 uuid
)

# UTF-8 source code support
if(MSVC)
    target_compile_options(wslpm PRIVATE /utf-8 /W3)
    # app.rc embeds its own manifest; stop the linker from adding a second one
    target_link_options(wslpm PRIVATE /MANIFEST:NO)
endif()
