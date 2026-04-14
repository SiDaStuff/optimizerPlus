cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(optimizerPlus VERSION 1.0.2 LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "OptimizerPlus currently supports Windows builds only.")
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode installation directory")
else()
    message(STATUS "Found Geode SDK: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} geode)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

setup_geode_mod(${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} winmm)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /O2 /Oi)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -O3)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    OPTIMIZER_VERSION="1.0.2"
    OPTIMIZER_AUTHOR="sidastuff"
    NOMINMAX
)

message(STATUS "OptimizerPlus ${PROJECT_VERSION} configured successfully")

