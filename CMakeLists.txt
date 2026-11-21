cmake_minimum_required(VERSION 3.25)

project(Ember
	VERSION 0.1.0
	DESCRIPTION "Ember - a simple, production-grade 3D game engine"
	LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Dist = optimized distribution build: editor tooling stripped, asserts off.
set(CMAKE_CXX_FLAGS_DIST "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_DIST "${CMAKE_C_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_DIST "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)

option(EMBER_BUILD_EDITOR "Build the Ember editor" ON)
option(EMBER_BUILD_TESTS "Build Ember unit tests" ON)
option(EMBER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors for Ember targets" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(EmberCompileOptions)
include(EmberDependencies)
include(EmberShaders)

if(EMBER_BUILD_TESTS)
	enable_testing()
endif()

add_subdirectory(Ember)

if(EMBER_BUILD_EDITOR)
	add_subdirectory(EmberEditor)
endif()

add_subdirectory(EmberRuntime)

if(EMBER_BUILD_TESTS)
	add_subdirectory(EmberTests)
endif()
