# Settings shared by part-a and part-b: language level, build type,
# compiler requirements from the spec, warnings, and optional sanitizers.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Spec: gcc/g++ >= 13 or Clang >= 16 (not Apple's Xcode clang).
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
  message(FATAL_ERROR "Apple clang is not allowed. Configure with "
                      "-DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
  message(FATAL_ERROR "Clang >= 16 required (found ${CMAKE_CXX_COMPILER_VERSION})")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
  message(FATAL_ERROR "g++ >= 13 required (found ${CMAKE_CXX_COMPILER_VERSION})")
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# Sanitizers (debug aid; do not benchmark with these on):
#   -DMINI1_SANITIZE=address   AddressSanitizer + UndefinedBehaviorSanitizer
#   -DMINI1_SANITIZE=thread    ThreadSanitizer (expect false positives inside
#                              libomp, which is not instrumented)
set(MINI1_SANITIZE "" CACHE STRING "Sanitizer to enable: address, thread, or empty")
set_property(CACHE MINI1_SANITIZE PROPERTY STRINGS "" address thread)

if(MINI1_SANITIZE STREQUAL "address")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
elseif(MINI1_SANITIZE STREQUAL "thread")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=thread)
elseif(NOT MINI1_SANITIZE STREQUAL "")
  message(FATAL_ERROR "MINI1_SANITIZE must be address, thread, or empty")
endif()

# Default data directory passed to tests (Minis/Dataset).
set(MINI1_DATA_DIR "${CMAKE_CURRENT_LIST_DIR}/../../Dataset"
    CACHE PATH "Directory containing ozone/ and no2/ CSV folders")

enable_testing()

# mini1_add_tests(<dir> <link-target>)
# Each <dir>/*.cpp becomes its own test executable, linked to <link-target>
# and registered with CTest. Tests receive MINI1_DATA_DIR as argv[1].
function(mini1_add_tests dir link_target)
  file(GLOB test_sources CONFIGURE_DEPENDS ${dir}/*.cpp)
  foreach(src IN LISTS test_sources)
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE ${link_target})
    add_test(NAME ${name} COMMAND ${name} ${MINI1_DATA_DIR})
  endforeach()
endfunction()
