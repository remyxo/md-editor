cmake_minimum_required(VERSION 3.20)
project(md-editor VERSION 0.1.0 LANGUAGES CXX)
add_compile_definitions(MD_EDITOR_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets WebEngineWidgets WebChannel)

add_subdirectory(src)

option(BUILD_TESTS "构建测试" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
