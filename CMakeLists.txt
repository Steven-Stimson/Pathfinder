cmake_minimum_required(VERSION 3.28)
project(PathfinderPP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Test Concurrent Network)

# Qt 6.7.x links the deprecated AGL framework on macOS, which was removed in
# Xcode 26 / macOS 26 SDK. Strip it from the OpenGL target if present.
if(APPLE)
    # Strip the deprecated AGL framework from all Qt/OpenGL targets.
    # macOS 26 / Xcode 26 removed AGL, so linking against it fails.
    foreach(_agl_target OpenGL::GL Qt6::Gui Qt6::Widgets Qt6::Svg)
        if(TARGET ${_agl_target})
            get_target_property(_agl_libs ${_agl_target} INTERFACE_LINK_LIBRARIES)
            if(_agl_libs)
                list(FILTER _agl_libs EXCLUDE REGEX "[Aa][Gg][Ll]")
                set_target_properties(${_agl_target} PROPERTIES INTERFACE_LINK_LIBRARIES "${_agl_libs}")
            endif()
        endif()
    endforeach()
endif()

set(LIB_SOURCES
    graphsearch/hit.cpp
    graphsearch/queries.cpp
    graphsearch/query.cpp
    graphsearch/querypath.cpp
    graphsearch/blast/blastsearch.cpp
    graphsearch/minimap2/minimap2search.cpp
    graphsearch/hmmer/hmmersearch.cpp
    graph/assemblygraphbuilder.cpp
    graph/assemblygraph.cpp
    graph/annotationsmanager.cpp
    graph/debruijnedge.cpp
    graph/debruijnnode.cpp
    graph/graphicsitemedge.cpp
    graph/graphicsitemlink.cpp
    graph/graphicsitemnode.cpp
    graph/movenodescommand.cpp
    graph/graphcommand.cpp
    graph/viewstate.cpp
    graph/graphlocation.cpp
    graph/path.cpp
    program/globals.cpp
    program/memory.cpp
    program/scinot.cpp
    program/settings.cpp
    program/colormap.cpp
    ui/dialogs/aboutdialog.cpp
    ui/annotationswidget.cpp
    ui/bedwidget.cpp
    ui/dialogs/hitfiltersdialog.cpp
    ui/dialogs/graphsearchdialog.cpp
    ui/dialogs/changenodedepthdialog.cpp
    ui/dialogs/changenodenamedialog.cpp
    ui/widgets/colourbutton.cpp
    ui/widgets/collapsebutton.h
    ui/dialogs/enteronequerydialog.cpp
    ui/dialogs/pathlistdialog.cpp
    ui/dialogs/walklistdialog.cpp
    ui/graphicsviewzoom.cpp
    ui/dialogs/graphinfodialog.cpp
    ui/dialogs/tttdialog.cpp
    ui/widgets/infotextwidget.cpp
    ui/mainwindow.cpp
    ui/bandagegraphicsscene.cpp
    ui/bandagegraphicsview.cpp
    ui/dialogs/myprogressdialog.cpp
    ui/nodewidthvisualaid.cpp
    ui/dialogs/pathspecifydialog.cpp
    ui/dialogs/querypathsdialog.cpp
    ui/dialogs/settingsdialog.cpp
    ui/widgets/verticallabel.cpp
    ui/widgets/verticalscrollarea.cpp
    ui/dialogs/logandownloaddialog.cpp
    graph/nodecolorer.cpp
    graph/sequenceutils.cpp
    graph/annotation.cpp
    graph/gfawriter.cpp
    graph/fastawriter.cpp
    graph/io.cpp
    graph/graphscope.cpp
    graphsearch/graphsearch.cpp)

set(FORMS
    ui/dialogs/aboutdialog.ui
    ui/dialogs/hitfiltersdialog.ui
    ui/dialogs/graphsearchdialog.ui
    ui/dialogs/changenodedepthdialog.ui
    ui/dialogs/changenodenamedialog.ui
    ui/dialogs/enteronequerydialog.ui
    ui/dialogs/graphinfodialog.ui
    ui/dialogs/tttdialog.ui
    ui/mainwindow.ui
    ui/dialogs/myprogressdialog.ui
    ui/dialogs/pathspecifydialog.ui
    ui/dialogs/querypathsdialog.ui
    ui/dialogs/settingsdialog.ui
    ui/dialogs/logandownloaddialog.ui)

set(CLI_SOURCES
    command_line/commoncommandlinefunctions.cpp
    command_line/image.cpp
    command_line/info.cpp
    command_line/layout.cpp
    command_line/load.cpp
    command_line/querypaths.cpp
    command_line/reduce.cpp
    command_line/settings.cpp)

set(RESOURCES images/images.qrc images/application.icns images/application.rc)

file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION.txt" VERSION_STRING LIMIT_COUNT 1)
string(REGEX MATCH "([0-9]+)\.([0-9]+)(.*)" ver "${VERSION_STRING}")
if (ver STREQUAL "")
  message(FATAL_ERROR "Error to parse version string from ${VERSION_STRING}")
endif()
set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
set(PROJECT_VERSION_TWEAK "${CMAKE_MATCH_3}")
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}${PROJECT_VERSION_TWEAK}")

message(STATUS "Building Pathfinder version ${PROJECT_VERSION}")
add_compile_definitions(APP_VERSION="${PROJECT_VERSION}")

add_subdirectory(thirdparty)
include_directories(thirdparty)

include(FetchContent)
# Lexy for GFA parser
FetchContent_Declare(lexy URL https://lexy.foonathan.net/download/lexy-src.zip)
FetchContent_MakeAvailable(lexy)
# CLI11 for cmdline parsing
FetchContent_Declare(cli11 GIT_REPOSITORY https://github.com/CLIUtils/CLI11 GIT_TAG v2.4.2)
FetchContent_MakeAvailable(cli11)

set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)
FetchContent_Declare(
    zstd
    URL "https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR build/cmake
)
FetchContent_MakeAvailable(zstd)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_library(PathfinderLayout STATIC layout/graphlayoutworker.cpp layout/io.cpp layout/graphlayout.cpp)
target_link_libraries(PathfinderLayout PRIVATE OGDF Qt6::Concurrent Qt6::Gui Qt6::Widgets)

add_library(PathfinderIo STATIC
        io/gfa.cpp
        io/bedloader.cpp
        io/fileutils.cpp
        io/cigar.cpp
        io/gaf.cpp
        io/filedownloader.cpp)
target_link_libraries(PathfinderIo PRIVATE Qt6::Gui Qt6::Widgets Qt6::Network foonathan::lexy)

# FIXME: Untagle this
include_directories(ui)
add_library(PathfinderLib STATIC ${LIB_SOURCES} ${FORMS} graphsearch/graphsearchers.cpp)
target_link_libraries(PathfinderLib PRIVATE PathfinderLayout PathfinderIo llvmSupport Qt6::Concurrent Qt6::Widgets Qt6::Svg Qt6::Network ${bandage_zlib})
target_include_directories(PathfinderLib INTERFACE ".")

target_link_libraries(PathfinderLib PRIVATE libzstd_static)
# On windows and macos this is needed
target_include_directories(PathfinderLib PRIVATE ${zstd_SOURCE_DIR}/lib)

add_library(PathfinderCLI STATIC ${CLI_SOURCES})
target_link_libraries(PathfinderCLI PRIVATE PathfinderLib CLI11::CLI11 Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)

add_executable(PathfinderPP program/main.cpp ${RESOURCES})
target_link_libraries(PathfinderPP PathfinderCLI PathfinderLib CLI11::CLI11 Qt6::Core Qt6::Widgets Qt6::Concurrent ${bandage_zlib})
set_target_properties(PathfinderPP PROPERTIES OUTPUT_NAME "Pathfinder")

# On macOS, make AGL a weak framework to prevent link failure when it's missing (Xcode 26+)
if(APPLE)
    target_link_options(PathfinderPP PRIVATE "LINKER:-weak_framework,AGL")
endif()

# Build as a Windows GUI application (no console window on startup).
# Qt provides a WinMain wrapper that calls our main(). CLI mode still works,
# but stdout/stderr are not attached to a console by default.
if (WIN32)
    set_target_properties(PathfinderPP PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if (APPLE)
  set_target_properties(PathfinderPP PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "Pathfinder"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.bandagepropp.app"
        MACOSX_BUNDLE_ICON_FILE "application.icns")
  set_source_files_properties(images/application.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
endif()

# Copy TTT binary, solver, and conversion script to build directory
if(WIN32)
    set(TTT_BINARY_NAME "ttt.exe")
    set(GLPSOL_BINARY_NAME "glpsol.exe")
else()
    set(TTT_BINARY_NAME "ttt")
    set(GLPSOL_BINARY_NAME "glpsol")
endif()
add_custom_command(TARGET PathfinderPP POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PathfinderPP>/../Pathfinder/TTT"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Pathfinder/TTT/${TTT_BINARY_NAME}"
        "$<TARGET_FILE_DIR:PathfinderPP>/../Pathfinder/TTT/${TTT_BINARY_NAME}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Pathfinder/TTT/${GLPSOL_BINARY_NAME}"
        "$<TARGET_FILE_DIR:PathfinderPP>/../Pathfinder/TTT/${GLPSOL_BINARY_NAME}"
)

add_subdirectory(resources)
install(TARGETS PathfinderPP
        DESTINATION bin
        COMPONENT main)

enable_testing(true)
add_subdirectory(tests)
