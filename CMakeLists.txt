cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set architecture for different platforms
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# OptimizerPlus - Advanced Performance Optimization Mod for Geometry Dash
# 30+ optimizations for graphics and processing
project(optimizerPlus VERSION 1.0.1 LANGUAGES CXX)

# Verify Geode SDK is available
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode installation directory")
else()
    message(STATUS "Found Geode SDK: $ENV{GEODE_SDK}")
endif()

# Load Geode SDK first
add_subdirectory($ENV{GEODE_SDK} geode)

# Collect all source files
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

# Create the shared library (mod binary)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

# Configure the mod with Geode (must be after add_library)
setup_geode_mod(${PROJECT_NAME})

# Compiler optimizations
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /O2 /Oi)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -O3)
endif()

# Version information
target_compile_definitions(${PROJECT_NAME} PRIVATE
    OPTIMIZER_VERSION="1.0.1"
    OPTIMIZER_AUTHOR="sidastuff"
)

message(STATUS "OptimizerPlus ${PROJECT_VERSION} configured successfully")

