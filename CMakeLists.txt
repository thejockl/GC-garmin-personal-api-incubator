cmake_minimum_required(VERSION 3.16)
project(garminPersonalAPI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_compile_definitions(QT_MESSAGELOGCONTEXT)
add_executable(garminPersonalAPI
    main.cpp
    GarminPersonalAPI.h
    GarminPersonalAPI.cpp
)

target_link_libraries(garminPersonalAPI PRIVATE Qt6::Core Qt6::Network)
