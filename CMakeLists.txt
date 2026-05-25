cmake_minimum_required(VERSION 3.20)
project(WSLMountManager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Win32 GUI application (no console)
add_executable(WSLMountManager WIN32
    src/main.cpp
    src/app.cpp
    src/wsl_bridge.cpp
    src/operations.cpp
    res/app.rc
)

target_include_directories(WSLMountManager PRIVATE src)

target_link_libraries(WSLMountManager PRIVATE
    user32 gdi32 shell32 dwmapi comctl32
    ole32 oleaut32 crypt32 shlwapi comdlg32
    advapi32 uuid
)

# UTF-8 source code support
if(MSVC)
    target_compile_options(WSLMountManager PRIVATE /utf-8 /W3)
endif()
