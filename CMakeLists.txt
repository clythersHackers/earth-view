cmake_minimum_required(VERSION 3.16)

project(EarthView VERSION 0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.8)

option(EARTH_VIEW_BUILD_DEMO "Build EarthView demo app" ON)

qt_add_qml_module(earth-view
    URI EarthView
    VERSION 1.0
    RESOURCE_PREFIX "/"
    CLASS_NAME EarthViewPlugin
    QML_FILES
        Main.qml
    RESOURCES
        assets/earth/earth-landmask-2048.png
    SOURCES
        EarthView.cpp
        EarthView.h
)

target_link_libraries(earth-view
    PRIVATE Qt6::Quick
            Qt6::QuickControls2
)

if (EARTH_VIEW_BUILD_DEMO)
    qt_add_executable(appEarthView
        main.cpp
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
                earth-view
    )

    target_include_directories(appEarthView
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/external/nats.c/src
    )
endif()

include(GNUInstallDirs)
install(TARGETS earth-view
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if (EARTH_VIEW_BUILD_DEMO)
    install(TARGETS appEarthView
        BUNDLE DESTINATION .
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
