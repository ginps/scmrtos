#*******************************************************************************
#
#     scmRTOS kernel tests on GoogleTest / GoogleMock
#
#     Builds one test program per kernel configuration:
#         context switch scheme 0/1 x priority order 0/1 x process restart 0/1
#     and registers every test of every program with CTest.
#
#     cmake -S test/gtest -B build/gtest
#     cmake --build build/gtest -j
#     ctest --test-dir build/gtest -j8
#
#     GoogleTest comes from (first that works):
#       -DSCMTEST_GTEST_DIR=<googletest source tree>   (offline builds)
#       find_package(GTest)                             (installed GTest with GMock)
#       FetchContent from GitHub                        (SCMTEST_GTEST_TAG, default v1.15.2)
#
#     The tree under test: -DSCMRTOS_DIR=<scmRTOS tree> (default: this repository).
#
#*******************************************************************************
cmake_minimum_required(VERSION 3.22)
project(scmrtos_gtest CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "the tests run on the Linux test port of scmRTOS: Linux host only")
endif()

get_filename_component(_repo "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SCMRTOS_DIR      "${_repo}"  CACHE PATH   "scmRTOS tree under test")
set(SCMRTOS_PORT_DIR ""          CACHE PATH   "Linux port directory (default: <SCMRTOS_DIR>/port/linux/gcc, else this repository's)")
set(SCMTEST_GTEST_DIR ""         CACHE PATH   "GoogleTest source tree (offline build)")
set(SCMTEST_GTEST_TAG "v1.15.2"  CACHE STRING "GoogleTest tag for FetchContent")
option(SCMTEST_SANITIZE "build with -fsanitize=address,undefined" OFF)
option(SCMTEST_COVERAGE "build with --coverage"                   OFF)

if(NOT SCMRTOS_PORT_DIR)
    if(EXISTS "${SCMRTOS_DIR}/port/linux/gcc/os_target.h")
        set(SCMRTOS_PORT_DIR "${SCMRTOS_DIR}/port/linux/gcc")
    else()
        set(SCMRTOS_PORT_DIR "${_repo}/port/linux/gcc")
    endif()
endif()

#-------------------------------------------------------------------------------
#   GoogleTest / GoogleMock
#
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK   ON  CACHE BOOL "" FORCE)
if(SCMTEST_GTEST_DIR)
    add_subdirectory("${SCMTEST_GTEST_DIR}" googletest EXCLUDE_FROM_ALL)
else()
    find_package(GTest CONFIG QUIET)
    if(NOT TARGET GTest::gmock)
        include(FetchContent)
        FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        ${SCMTEST_GTEST_TAG}
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(googletest)
    endif()
endif()
if(TARGET gmock_main AND NOT TARGET GTest::gmock_main)
    add_library(GTest::gmock_main ALIAS gmock_main)
endif()

include(GoogleTest)
enable_testing()

#-------------------------------------------------------------------------------
#   Sources
#
set(KERNEL_SOURCES
    "${SCMRTOS_DIR}/core/os_kernel.cpp"
    "${SCMRTOS_DIR}/core/os_services.cpp"
    "${SCMRTOS_PORT_DIR}/os_target.cpp")
if(EXISTS "${SCMRTOS_DIR}/core/usrlib.cpp")
    list(APPEND KERNEL_SOURCES "${SCMRTOS_DIR}/core/usrlib.cpp")
endif()

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
set(HARNESS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/harness/rtos_test.cpp")

set(COMMON_FLAGS -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
if(SCMTEST_SANITIZE)
    list(APPEND COMMON_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined)
    set(COMMON_LINK -fsanitize=address,undefined)
endif()
if(SCMTEST_COVERAGE)
    list(APPEND COMMON_FLAGS --coverage)
    list(APPEND COMMON_LINK --coverage)
endif()

#-------------------------------------------------------------------------------
#   One program per configuration
#
#   Known defects: the suite KnownDefect checks the correct behaviour where a
#   known defect of the tree breaks it. Its tests are registered with WILL_FAIL:
#   CTest reports them as passed while the defect is there, and as failed when
#   the defect is gone (then move the test to its regular suite).
#
set(SCMTEST_TARGETS "")
foreach(scheme 0 1)
    foreach(order 0 1)
        foreach(restart 0 1)
            set(cfg "s${scheme}_o${order}_r${restart}")
            set(tgt "scmrtos_gtest_${cfg}")
            add_executable(${tgt} ${HARNESS_SOURCES} ${TEST_SOURCES} ${KERNEL_SOURCES})
            target_include_directories(${tgt} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/config"
                "${CMAKE_CURRENT_SOURCE_DIR}/harness"
                "${SCMRTOS_DIR}/core"
                "${SCMRTOS_PORT_DIR}")
            target_compile_definitions(${tgt} PRIVATE
                scmRTOS_CONTEXT_SWITCH_SCHEME=${scheme}
                scmRTOS_PRIORITY_ORDER=${order}
                scmRTOS_PROCESS_RESTART_ENABLE=${restart})
            target_compile_options(${tgt} PRIVATE ${COMMON_FLAGS})
            target_link_options(${tgt} PRIVATE ${COMMON_LINK})
            target_link_libraries(${tgt} PRIVATE GTest::gmock_main)

            set(_props TIMEOUT 60)
            if(SCMTEST_SANITIZE)
                list(APPEND _props ENVIRONMENT "ASAN_OPTIONS=detect_stack_use_after_return=0:detect_leaks=0")
            endif()
            gtest_discover_tests(${tgt}
                TEST_PREFIX "${cfg}."
                TEST_FILTER "-KnownDefect*"
                PROPERTIES LABELS "${cfg}" ${_props}
                DISCOVERY_TIMEOUT 30)
            gtest_discover_tests(${tgt}
                TEST_PREFIX "${cfg}."
                TEST_FILTER "KnownDefect*"
                PROPERTIES LABELS known-defect WILL_FAIL TRUE ${_props}
                DISCOVERY_TIMEOUT 30)
            list(APPEND SCMTEST_TARGETS ${tgt})
        endforeach()
    endforeach()
endforeach()

add_custom_target(scmrtos_gtest_all DEPENDS ${SCMTEST_TARGETS})
