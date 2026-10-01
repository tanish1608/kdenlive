# Copyright (C) 2026 Montaz contributors
# SPDX-License-Identifier: BSD-3-Clause
# Standalone build of Qt's pinned Cocoa plugin against the existing Craft SDK.
cmake_minimum_required(VERSION 3.25)
project(MontazCocoa LANGUAGES CXX OBJCXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core Gui CorePrivate GuiPrivate)
qt_add_plugin(MontazCocoa SHARED PLUGIN_TYPE platforms CLASS_NAME QCocoaIntegrationPlugin)
set_target_properties(MontazCocoa PROPERTIES OUTPUT_NAME qcocoa
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/platforms")
file(GLOB cocoa_sources "${MONTAZ_QT_COCOA_SOURCE}/*.mm" "${MONTAZ_QT_COCOA_SOURCE}/*.cpp")
# qnsview.mm includes these implementation fragments itself.
list(FILTER cocoa_sources EXCLUDE REGEX "/qnsview_.+\\.mm$")
if(NOT QT_FEATURE_vulkan)
    list(REMOVE_ITEM cocoa_sources "${MONTAZ_QT_COCOA_SOURCE}/qcocoavulkaninstance.mm")
endif()
if(NOT QT_FEATURE_opengl)
    list(REMOVE_ITEM cocoa_sources "${MONTAZ_QT_COCOA_SOURCE}/qcocoaglcontext.mm")
endif()
if(NOT QT_FEATURE_sessionmanager)
    list(REMOVE_ITEM cocoa_sources "${MONTAZ_QT_COCOA_SOURCE}/qcocoasessionmanager.cpp")
endif()
if(NOT QT_FEATURE_accessibility)
    message(FATAL_ERROR "The Qt SDK must support accessibility")
endif()
target_sources(MontazCocoa PRIVATE ${cocoa_sources})
target_include_directories(MontazCocoa PRIVATE "${MONTAZ_QT_COCOA_SOURCE}")
target_compile_definitions(MontazCocoa PRIVATE QT_NO_FOREACH)
target_link_libraries(MontazCocoa PRIVATE Qt6::CorePrivate Qt6::GuiPrivate
    "-framework Foundation" "-framework AppKit" "-framework Carbon"
    "-framework CoreServices" "-framework CoreVideo" "-framework IOKit"
    "-framework IOSurface" "-framework Metal" "-framework QuartzCore"
    "-framework UniformTypeIdentifiers" "-framework OpenGL")
qt_add_resources(MontazCocoa "qcocoaresources" PREFIX "/qt-project.org/mac/cursors"
    BASE "${MONTAZ_QT_COCOA_SOURCE}" FILES
    "${MONTAZ_QT_COCOA_SOURCE}/images/sizeallcursor.png"
    "${MONTAZ_QT_COCOA_SOURCE}/images/spincursor.png"
    "${MONTAZ_QT_COCOA_SOURCE}/images/waitcursor.png")
