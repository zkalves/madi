cmake_minimum_required(VERSION 3.25)

project(vector
  VERSION 0.1.0
  DESCRIPTION "Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing"
  LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)
find_package(SQLite3 QUIET)
find_package(pugixml QUIET)
find_package(Qt6 COMPONENTS Core Gui Widgets OpenGLWidgets Sql OPTIONAL_COMPONENTS Svg QUIET)

find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1
  )
  FetchContent_MakeAvailable(zlib)
  if(TARGET zlibstatic)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
  elseif(TARGET zlib)
    add_library(ZLIB::ZLIB ALIAS zlib)
  endif()
endif()

find_library(SKIA_LIBRARY NAMES skia)
find_path(SKIA_INCLUDE_DIR NAMES include/core/SkCanvas.h core/SkCanvas.h)

find_library(AVOID_LIBRARY NAMES avoid)
find_path(AVOID_INCLUDE_DIR NAMES libavoid/libavoid.h)

find_library(OGDF_LIBRARY NAMES OGDF)
find_path(OGDF_INCLUDE_DIR NAMES ogdf/basic/Graph.h)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "src/core/*.cpp")
add_library(vector-core STATIC ${CORE_SOURCES})
target_include_directories(vector-core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(vector-core PUBLIC
  Threads::Threads
)

file(GLOB_RECURSE ROUTING_SOURCES CONFIGURE_DEPENDS "src/routing/*.cpp")
add_library(vector-routing STATIC ${ROUTING_SOURCES})
target_include_directories(vector-routing PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(vector-routing PUBLIC
  vector-core
  Threads::Threads
)
if(AVOID_LIBRARY AND AVOID_INCLUDE_DIR)
  target_include_directories(vector-routing PUBLIC ${AVOID_INCLUDE_DIR})
  target_link_libraries(vector-routing PUBLIC ${AVOID_LIBRARY})
  target_compile_definitions(vector-routing PUBLIC VECTOR_HAS_LIBAVOID=1)
endif()
if(OGDF_LIBRARY AND OGDF_INCLUDE_DIR)
  target_include_directories(vector-routing PUBLIC ${OGDF_INCLUDE_DIR})
  target_link_libraries(vector-routing PUBLIC ${OGDF_LIBRARY})
  target_compile_definitions(vector-routing PUBLIC VECTOR_HAS_OGDF=1)
endif()

# --- Storage / IO ---
file(GLOB_RECURSE IO_SOURCES CONFIGURE_DEPENDS "src/io/*.cpp")
add_library(vector-io STATIC ${IO_SOURCES})
target_include_directories(vector-io PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(vector-io PUBLIC
  vector-core
  ZLIB::ZLIB
)
if(pugixml_FOUND)
  target_link_libraries(vector-io PUBLIC pugixml::pugixml)
endif()

# --- Command Line Interface ---
add_executable(vector-cli src/app/main_cli.cpp)
target_include_directories(vector-cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vector-cli PRIVATE
  vector-core
  vector-routing
  vector-io
)

if(Qt6_FOUND)
  message(STATUS "Qt6 found (${Qt6_VERSION}) - vector-gui target enabled")
  file(GLOB_RECURSE GUI_SOURCES CONFIGURE_DEPENDS
    "src/gui/*.cpp"
    "src/renderer/*.cpp"
    "src/grid/*.cpp"
    "include/vector/gui/*.hpp"
    "include/vector/renderer/*.hpp"
    "include/vector/grid/*.hpp"
  )
  set(GUI_EXTRA_SOURCES "")
  set(GUI_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/res/resources.qrc")
  if(EXISTS "${GUI_RESOURCES}")
    list(APPEND GUI_EXTRA_SOURCES "${GUI_RESOURCES}")
  endif()
  if(WIN32)
    set(WINDOWS_RC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/vector.rc")
    if(EXISTS "${WINDOWS_RC_FILE}")
      list(APPEND GUI_EXTRA_SOURCES "${WINDOWS_RC_FILE}")
    endif()
    add_executable(vector-gui WIN32 ${GUI_SOURCES} ${GUI_EXTRA_SOURCES})
  else()
    add_executable(vector-gui ${GUI_SOURCES} ${GUI_EXTRA_SOURCES})
  endif()
  target_compile_definitions(vector-gui PRIVATE VECTOR_ENABLE_QT=1)
  target_include_directories(vector-gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_link_libraries(vector-gui PRIVATE
    vector-core
    vector-routing
    vector-io
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Sql
  )
  if(TARGET Qt6::Svg)
    target_link_libraries(vector-gui PRIVATE Qt6::Svg)
  endif()
  if(SKIA_LIBRARY AND SKIA_INCLUDE_DIR)
    target_include_directories(vector-gui PRIVATE ${SKIA_INCLUDE_DIR})
    target_link_libraries(vector-gui PRIVATE ${SKIA_LIBRARY})
    target_compile_definitions(vector-gui PRIVATE VECTOR_HAS_SKIA=1)
  endif()
else()
  message(STATUS "Qt6 not found - vector-gui target disabled")
endif()

enable_testing()

add_executable(CoreSmokeTest tests/core/CoreSmokeTest.cpp)
target_link_libraries(CoreSmokeTest PRIVATE vector-core)
add_test(NAME CoreSmokeTest COMMAND CoreSmokeTest)

add_executable(RoutingSmokeTest tests/routing/RoutingSmokeTest.cpp)
target_link_libraries(RoutingSmokeTest PRIVATE vector-routing)
add_test(NAME RoutingSmokeTest COMMAND RoutingSmokeTest)

add_executable(IoSmokeTest tests/io/IoSmokeTest.cpp)
target_link_libraries(IoSmokeTest PRIVATE vector-io)
add_test(NAME IoSmokeTest COMMAND IoSmokeTest)

if(Qt6_FOUND)
  add_executable(ThemeSmokeTest
    tests/gui/ThemeSmokeTest.cpp
    src/gui/MainWindow.cpp
    src/gui/ThemeManager.cpp
    src/renderer/CanvasWidget.cpp
    src/renderer/CanvasRenderer.cpp
    src/grid/GridManager.cpp
    include/vector/gui/MainWindow.hpp
    include/vector/gui/StencilLibraryDock.hpp
    include/vector/gui/ThemeManager.hpp
    include/vector/gui/HierarchyTreeDock.hpp
    include/vector/gui/SnapGridSettingsDialog.hpp
    include/vector/gui/SymbolEditorDialog.hpp
    include/vector/gui/CommandConsoleDock.hpp
    include/vector/gui/KeyboardShortcutsDialog.hpp
    include/vector/renderer/CanvasWidget.hpp
    res/resources.qrc
  )
  target_compile_definitions(ThemeSmokeTest PRIVATE VECTOR_ENABLE_QT=1)
  target_include_directories(ThemeSmokeTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(ThemeSmokeTest PRIVATE
    vector-core
    vector-routing
    vector-io
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
  )
  add_test(NAME ThemeSmokeTest COMMAND ThemeSmokeTest)
endif()

# ==============================================================================
# Icon Generation & Installation
# ==============================================================================
include(GNUInstallDirs)

find_program(RSVG_CONVERT rsvg-convert)
find_program(IMAGE_CONVERT convert)
set(CANONICAL_ICON_SVG "${CMAKE_CURRENT_SOURCE_DIR}/res/icons/vector.svg")
if(NOT EXISTS "${CANONICAL_ICON_SVG}")
  set(CANONICAL_ICON_SVG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/vector.svg")
endif()
if(RSVG_CONVERT AND IMAGE_CONVERT)
  set(ICON_SIZES 16 32 48 64 128 256 512)
  set(GENERATED_PNGS "")
  foreach(sz ${ICON_SIZES})
    set(PNG_OUT "${CMAKE_CURRENT_BINARY_DIR}/icons/${sz}x${sz}/vector.png")
    list(APPEND GENERATED_PNGS "${PNG_OUT}")
    add_custom_command(
      OUTPUT "${PNG_OUT}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/icons/${sz}x${sz}"
      COMMAND ${RSVG_CONVERT} -w ${sz} -h ${sz} "${CANONICAL_ICON_SVG}" -o "${PNG_OUT}"
      DEPENDS "${CANONICAL_ICON_SVG}"
      COMMENT "Rendering vector.png (${sz}x${sz})"
    )
    install(FILES "${PNG_OUT}"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${sz}x${sz}/apps"
      OPTIONAL
    )
  endforeach()
  add_custom_target(generate_icons ALL DEPENDS ${GENERATED_PNGS})
endif()

# ==============================================================================
# Installation Rules
# ==============================================================================
install(TARGETS vector-cli
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(TARGET vector-gui)
  install(TARGETS vector-gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
  )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/res/themes/")
  install(DIRECTORY res/themes/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/vector/themes
    FILES_MATCHING PATTERN "*.json"
  )
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/themes/")
  install(DIRECTORY themes/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/vector/themes
    FILES_MATCHING PATTERN "*.json"
  )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/vector.desktop")
  install(FILES packaging/vector.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
  )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/vector.appdata.xml")
  install(FILES packaging/vector.appdata.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
  )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/res/icons/vector.svg")
  install(FILES res/icons/vector.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME vector.svg
  )
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/vector.svg")
  install(FILES packaging/icons/vector.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
  )
endif()

# ==============================================================================
# CPack Packaging Configuration (DEB, RPM, TGZ, NSIS, ZIP)
# ==============================================================================
set(CPACK_PACKAGE_NAME "vector")
set(CPACK_PACKAGE_VENDOR "VECTOR Contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CONTACT "vector-dev@example.org")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/vector/vector")

# Multi-platform generators
if(WIN32)
  set(CPACK_GENERATOR "ZIP;NSIS")
  set(CPACK_NSIS_DISPLAY_NAME "VECTOR Diagram Editor")
  set(CPACK_NSIS_PACKAGE_NAME "VECTOR")
  set(CPACK_NSIS_MODIFY_PATH ON)
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_MUI_FINISHPAGE_RUN "vector-gui.exe")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/vector.ico")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/vector.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/vector.ico")
  endif()
else()
  set(CPACK_GENERATOR "DEB;RPM;TGZ")
endif()

# Debian (.deb)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "VECTOR Team <vector-dev@example.org>")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland, xdg-utils")

# RPM (.rpm)
set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
set(CPACK_RPM_PACKAGE_LICENSE "GPLv3+")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)

include(CPack)
