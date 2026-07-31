cmake_minimum_required(VERSION 3.28)

project(aries VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/project_options.cmake)
include(cmake/dependencies.cmake)
include(CTest)

add_subdirectory(data/converter)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
