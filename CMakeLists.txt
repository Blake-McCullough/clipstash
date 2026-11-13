cmake_minimum_required(VERSION 3.19)
# Changing this version on main publishes a release (see .github/workflows/release.yml).
project(clipstash VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network DBus OpenGL OpenGLWidgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

add_library(clipstash_core STATIC
  src/core/Settings.cpp src/core/Settings.h
  src/core/Clip.cpp src/core/Clip.h
  src/core/Proc.cpp src/core/Proc.h
  src/core/Recorder.cpp src/core/Recorder.h
  src/core/MediaTools.cpp src/core/MediaTools.h
  src/core/ClipLibrary.cpp src/core/ClipLibrary.h
  src/core/Exporter.cpp src/core/Exporter.h
  src/core/Uploader.cpp src/core/Uploader.h
  src/core/ClipJobs.cpp src/core/ClipJobs.h
  src/core/Updater.cpp src/core/Updater.h
)
target_include_directories(clipstash_core PUBLIC src)
target_link_libraries(clipstash_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network)
target_compile_definitions(clipstash_core PUBLIC CLIPSTASH_VERSION="${PROJECT_VERSION}")

add_executable(clipstash
  src/main.cpp
  src/hotkeys/HotkeyManager.cpp src/hotkeys/HotkeyManager.h
  src/hotkeys/PortalHotkeys.cpp src/hotkeys/PortalHotkeys.h
  src/hotkeys/EvdevHotkeys.cpp src/hotkeys/EvdevHotkeys.h
  src/ui/Theme.cpp src/ui/Theme.h
  src/ui/MainWindow.cpp src/ui/MainWindow.h
  src/ui/LibraryPage.cpp src/ui/LibraryPage.h
  src/ui/ClipDelegate.cpp src/ui/ClipDelegate.h
  src/ui/EditorPage.cpp src/ui/EditorPage.h
  src/ui/MpvWidget.cpp src/ui/MpvWidget.h
  src/ui/ClipSound.cpp src/ui/ClipSound.h
  src/ui/Timeline.cpp src/ui/Timeline.h
  src/ui/SettingsPage.cpp src/ui/SettingsPage.h
  src/ui/HotkeyEdit.cpp src/ui/HotkeyEdit.h
)
target_link_libraries(clipstash PRIVATE clipstash_core
  Qt6::Widgets Qt6::DBus Qt6::OpenGL Qt6::OpenGLWidgets PkgConfig::MPV)
target_compile_options(clipstash PRIVATE -Wall -Wextra)

include(GNUInstallDirs)
install(TARGETS clipstash RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/io.github.blake_mccullough.clipstash.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/io.github.blake_mccullough.clipstash.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

option(CLIPSTASH_TESTS "Build tests" OFF)
if(CLIPSTASH_TESTS)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  enable_testing()
  add_executable(test_core tests/test_core.cpp)
  target_link_libraries(test_core PRIVATE clipstash_core Qt6::Test)
  add_test(NAME core COMMAND test_core)
endif()
