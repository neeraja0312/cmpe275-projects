# Mini 1 - Part B (Phase 2). Two builds, one per step of the spec:
#
# Step 1 - Phase 1 code with OpenMP applied (compare against part-a):
#   omp/include/  headers
#   omp/src/      sources; omp/src/main.cpp is the entry point  -> mini1_b_omp
#
# Step 2 - data classes rewritten as a library linked to the app:
#   lib/include/  public headers  - the only thing the app can see
#   lib/src/      library internals (private headers + .cpp)    -> libaqdata
#   app/          main / benchmark driver, links the library    -> mini1_b
#
#   tests/        one test program per .cpp, linked to the library (ctest)
cmake_minimum_required(VERSION 3.20)
project(mini1_part_b LANGUAGES CXX)

include(${CMAKE_SOURCE_DIR}/../cmake/Mini1Common.cmake)

find_package(OpenMP REQUIRED)

# ---- Step 1: OpenMP applied to the Phase 1 design -------------------------
file(GLOB_RECURSE OMP_ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/omp/src/*.cpp)
set(OMP_MAIN_SOURCE ${CMAKE_SOURCE_DIR}/omp/src/main.cpp)
set(OMP_CORE_SOURCES ${OMP_ALL_SOURCES})
list(REMOVE_ITEM OMP_CORE_SOURCES ${OMP_MAIN_SOURCE})

if(OMP_CORE_SOURCES)
  add_library(mini1_b_omp_core OBJECT ${OMP_CORE_SOURCES})
  target_include_directories(mini1_b_omp_core PUBLIC ${CMAKE_SOURCE_DIR}/omp/include)
  target_link_libraries(mini1_b_omp_core PUBLIC OpenMP::OpenMP_CXX)

  if(EXISTS ${OMP_MAIN_SOURCE})
    add_executable(mini1_b_omp ${OMP_MAIN_SOURCE})
    target_link_libraries(mini1_b_omp PRIVATE mini1_b_omp_core)
  endif()

  file(GLOB OMP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/omp/tests/*.cpp)
  foreach(src IN LISTS OMP_TEST_SOURCES)
    get_filename_component(name ${src} NAME_WE)
    set(target omp_${name})
    add_executable(${target} ${src})
    target_link_libraries(${target} PRIVATE mini1_b_omp_core)
    add_test(NAME ${target} COMMAND ${target} ${MINI1_DATA_DIR})
  endforeach()
else()
  message(WARNING "part-b: no .cpp files in omp/src/ yet - mini1_b_omp not built")
endif()

# ---- Step 2: library + app -------------------------------------------------
# Static by default; pass -DBUILD_SHARED_LIBS=ON to compare a shared library.
option(BUILD_SHARED_LIBS "Build the data library as a shared library" OFF)

file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/lib/src/*.cpp)
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/app/*.cpp)

if(LIB_SOURCES)
  add_library(aqdata ${LIB_SOURCES})
  target_include_directories(aqdata
    PUBLIC  ${CMAKE_SOURCE_DIR}/lib/include
    PRIVATE ${CMAKE_SOURCE_DIR}/lib/src)
  target_link_libraries(aqdata PUBLIC OpenMP::OpenMP_CXX)
  # Only names marked AQ_API (lib/include/aqdata/Export.hpp) are visible outside
  # the library, so the internals stay private in the shared build too.
  set_target_properties(aqdata PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

  if(APP_SOURCES)
    add_executable(mini1_b ${APP_SOURCES})
    target_link_libraries(mini1_b PRIVATE aqdata)
  else()
    message(WARNING "part-b: no .cpp files in app/ yet - mini1_b not built")
  endif()

  mini1_add_tests(${CMAKE_SOURCE_DIR}/tests aqdata)
  # The tokenizer is intentionally private to the library; its focused test
  # may include that private header without exposing it to the application.
  if(TARGET test_tokenizer)
    target_include_directories(test_tokenizer PRIVATE ${CMAKE_SOURCE_DIR}/lib/src)
  endif()
else()
  message(WARNING "part-b: no .cpp files in lib/src/ yet - library, app and tests not built")
endif()
