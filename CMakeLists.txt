cmake_minimum_required(VERSION 3.1)
project(AmigaAdf LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_VERBOSE_MAKEFILE ON)

add_subdirectory(firmware)

add_executable(adf main.cpp)
target_link_libraries(adf robotcontrol)

include(CTest)
enable_testing()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
