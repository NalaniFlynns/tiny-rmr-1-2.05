cmake_minimum_required(VERSION 3.16)
project(rmrbuildtool VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_subdirectory(../fwsec fwsec)

qt_add_executable(rmrbuildtool WIN32
    main.cpp
    mainwindow.cpp mainwindow.h
    buildrunner.cpp buildrunner.h
    encryptworker.cpp encryptworker.h
    keystore.cpp keystore.h
    pwgen.cpp pwgen.h
)

target_link_libraries(rmrbuildtool PRIVATE fwsec Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_definitions(rmrbuildtool PRIVATE _CRT_SECURE_NO_WARNINGS)