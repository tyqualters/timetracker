cmake_minimum_required(VERSION 3.28)
project(TimeTracker C CXX)

set(CMAKE_CXX_STANDARD 20)

add_executable(TimeTracker main.cpp
        raygui.h cyber/style_cyber.h
)

if(Unix AND CMAKE_BUILD_TYPE EQUAL Debug)
        message("Adding address sanitation flags")
        add_compile_options(-fsanitize=address)
        add_link_options(-fsanitize=address)
endif()

find_package(raylib REQUIRED) # system
find_package(CURL REQUIRED) # vcpkg
find_package(jsoncpp CONFIG REQUIRED) # vcpkg

target_link_libraries(TimeTracker PRIVATE raylib CURL::libcurl JsonCpp::JsonCpp)
