# Mini 1 - Part A (Phase 1): serial baseline, single project.
# Spec: no threads in this phase.
#
#   include/      headers
#   src/          sources; src/main.cpp is the program entry point
#   tests/        one test program per .cpp (run with ctest)
cmake_minimum_required(VERSION 3.20)
project(mini1_part_a LANGUAGES CXX)

include(${CMAKE_SOURCE_DIR}/../cmake/Mini1Common.cmake)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
set(MAIN_SOURCE ${CMAKE_SOURCE_DIR}/src/main.cpp)
list(REMOVE_ITEM SOURCES ${MAIN_SOURCE})

# Everything except main.cpp, compiled once and shared by the program and tests.
if(SOURCES)
  add_library(mini1_a_core OBJECT ${SOURCES})
  target_include_directories(mini1_a_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
else()
  add_library(mini1_a_core INTERFACE)
  target_include_directories(mini1_a_core INTERFACE ${CMAKE_SOURCE_DIR}/include)
endif()

if(EXISTS ${MAIN_SOURCE})
  add_executable(mini1_a ${MAIN_SOURCE})
  target_link_libraries(mini1_a PRIVATE mini1_a_core)
else()
  message(WARNING "part-a: src/main.cpp not found - program not built")
endif()

mini1_add_tests(${CMAKE_SOURCE_DIR}/tests mini1_a_core)
