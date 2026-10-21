# ============================================================
# Engine-core tests, built with qmake from Qt (no CMake needed).
#
# WHERE THINGS LIVE (see BlastPioneer.vcxproj for the full layout):
#   <repo>\BlastPioneer\            project: sources + resources
#     game\ core\ ui\ screens\      source groups
#     tests\                        this .pro + the test sources  <- you are here
#   <repo>\x64\                     build output
#     obj\<Config>\                 MSBuild intermediates
#     tests\                        qmake shadow build for these tests
#     Debug\ Release\               runnable folders (exe + Qt runtime + content)
#
# BUILD (from a NORMAL prompt, the script sets up MSVC itself):
#     cd /d <repo>\BlastPioneer\tests
#     run_tests.bat
#
# Or manually, from "x64 Native Tools Command Prompt for VS 2022":
#     "%QTDIR%\bin\qmake.exe" tests.pro
#     nmake
#     release\bp_tests.exe            (exit code 0 = all passed)
#
# Keep this file ASCII-only.
# ============================================================

# core: 引擎与测试本身
# gui : 只因为 KeyDispatch.h 用到 QKeyEvent（按键分发测试需要），
#       注意这里**没有** QtWidgets —— 引擎依然完全不依赖界面
# network: 只因为 NetworkProtocol.h 里 ClientEndpoint 用到 QHostAddress。
#       联机的"线上格式"是两端唯一的接口，所以它也要能脱机单测
QT += core gui network

CONFIG += console c++20
CONFIG -= app_bundle

TARGET = bp_tests
TEMPLATE = app

# 引擎源码在 ../game。这里用 $$clean_path($$PWD/...) 拼成绝对路径：
# qmake 在 Windows 上检查"相对 ../ 路径"是否存在时会出错（阶段三踩过这个坑），
# 而 $$PWD 本身已是绝对路径，过一遍 clean_path 把 .. 折掉就彻底没这个问题。
GAME_DIR = $$clean_path($$PWD/../src/game)

# core 只为了 NetworkProtocol.h 与崩溃保护（LaunchGuard）：联机的"线上格式"是两端唯一的接口，
# 也就是引擎契约的一部分；LaunchGuard 是纯逻辑状态机，同样必须能脱机测试。
# 它用 FileManager 落盘、用 DirectoryLayout 找路径，所以这两个也要一起编。
CORE_DIR = $$clean_path($$PWD/../src/core)

# 原生模组 C ABI 的头文件：测试会静态断言"0.1.0 承诺过的偏移量一个都没变"
SDK_DIR = $$clean_path($$PWD/../sdk)

INCLUDEPATH += $$GAME_DIR $$CORE_DIR $$SDK_DIR

HEADERS += \
    $$GAME_DIR/GameTypes.h \
    $$GAME_DIR/GameConfig.h \
    $$GAME_DIR/GameBoard.h \
    $$GAME_DIR/MapGenerator.h \
    $$GAME_DIR/LevelData.h \
    $$GAME_DIR/ModApi.h \
    $$GAME_DIR/GameplayPatch.h \
    $$GAME_DIR/GameEngine.h \
    $$GAME_DIR/GameLoop.h \
    $$GAME_DIR/SingleGameEngine.h \
    $$GAME_DIR/MultiGameEngine.h \
    $$GAME_DIR/DemoGameEngine.h \
    $$GAME_DIR/ItemEffects.h \
    $$GAME_DIR/KeyDispatch.h \
    $$CORE_DIR/NetworkProtocol.h \
    $$CORE_DIR/UiTokens.h \
    $$CORE_DIR/LaunchGuard.h \
    $$CORE_DIR/DirectoryLayout.h \
    $$CORE_DIR/FileManager.h \
    $$SDK_DIR/bp_mod_api.h \
    test_key_dispatch.h

SOURCES += \
    test_game_core.cpp \
    $$GAME_DIR/GameBoard.cpp \
    $$GAME_DIR/MapGenerator.cpp \
    $$GAME_DIR/LevelData.cpp \
    $$GAME_DIR/ModApi.cpp \
    $$GAME_DIR/GameplayPatch.cpp \
    $$GAME_DIR/GameEngine.cpp \
    $$GAME_DIR/GameLoop.cpp \
    $$GAME_DIR/SingleGameEngine.cpp \
    $$GAME_DIR/MultiGameEngine.cpp \
    $$GAME_DIR/DemoGameEngine.cpp \
    $$GAME_DIR/ItemEffects.cpp \
    $$CORE_DIR/UiTokens.cpp \
    $$CORE_DIR/LaunchGuard.cpp \
    $$CORE_DIR/DirectoryLayout.cpp \
    $$CORE_DIR/FileManager.cpp
