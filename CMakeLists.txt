cmake_minimum_required(VERSION 3.20)
project(BorderWatch LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/third_party
)

set(SOURCES
    cpp/third_party/yyjson.c
    cpp/src/Config.cpp
    cpp/src/Watchlist.cpp
    cpp/src/AudioPlayer.cpp
    cpp/src/LogScanner.cpp
    cpp/src/ToastManager.cpp
    cpp/src/DetectedWindow.cpp
    cpp/src/StartupManager.cpp
    cpp/src/HotkeyManager.cpp
    cpp/src/TrayIcon.cpp
    cpp/src/ModernControls.cpp
    cpp/src/MainWindow.cpp
    cpp/src/Main.cpp
    cpp/res/BorderWatch.rc
)

add_executable(BorderWatch WIN32 ${SOURCES})

target_link_libraries(BorderWatch PRIVATE
    user32
    gdi32
    shell32
    ole32
    comctl32
    comdlg32
    winhttp
    gdiplus
    advapi32
    uxtheme
)
