cmake_minimum_required(VERSION 3.16)

project(EarthView VERSION 0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appEarthView
    main.cpp
    EarthView.cpp
    EarthView.h
    OrbitFeed.cpp
    OrbitFeed.h
)

qt_add_qml_module(appEarthView
    URI EarthView
    VERSION 1.0
    RESOURCE_PREFIX "/"
    QML_FILES
        Main.qml
    RESOURCES
        assets/earth/earth-landmask-2048.png
    SOURCES
        EarthView.cpp
        EarthView.h
        OrbitFeed.cpp
        OrbitFeed.h
)

set(NATS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NATS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NATS_BUILD_STREAMING OFF CACHE BOOL "" FORCE)
set(NATS_BUILD_LIB_SHARED OFF CACHE BOOL "" FORCE)
set(NATS_BUILD_WITH_TLS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/nats.c)

target_link_libraries(appEarthView
    PRIVATE Qt6::Quick
            Qt6::QuickControls2
            nats_static
)

target_include_directories(appEarthView
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/external/nats.c/src
)

include(GNUInstallDirs)
install(TARGETS appEarthView
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
