cmake_minimum_required(VERSION 3.20)

project(qhw_scheduler C)

option(QHW_SCHED_BUILD_SHARED "Build shared qhw-scheduler library" ON)
option(QHW_SCHED_BUILD_STATIC "Build static qhw-scheduler library" OFF)
option(QHW_SCHED_BUILD_TESTS "Build qhw-scheduler C tests" ON)
option(QHW_SCHED_BUILD_PLUGINS "Build scheduler policy plugins" ON)
option(QHW_SCHED_BUILD_PYTHON "Build Python bindings" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(QHW_SCHED_CORE_SOURCES
    src/qhw_allocator.c
    src/qhw_error.c
    src/qhw_plugin.c
    src/qhw_qpu.c
    src/qhw_scheduler.c
    src/qhw_stats.c
    src/qhw_task.c
    src/qhw_thread.c
    src/util/qhw_hash_table.c
    src/util/qhw_heap.c
    src/util/qhw_list.c
    src/util/qhw_rb_tree.c
    src/util/qhw_ring.c)

if(QHW_SCHED_BUILD_SHARED)
    add_library(qhw_scheduler SHARED ${QHW_SCHED_CORE_SOURCES})
else()
    add_library(qhw_scheduler STATIC ${QHW_SCHED_CORE_SOURCES})
endif()

target_include_directories(qhw_scheduler
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(qhw_scheduler PRIVATE ${CMAKE_DL_LIBS})

if(UNIX)
    target_link_libraries(qhw_scheduler PRIVATE pthread)
endif()

target_compile_options(qhw_scheduler PRIVATE
    -Wall
    -Wextra
    -Werror)

if(QHW_SCHED_BUILD_TESTS)
    enable_testing()

    add_executable(test_core tests/c/test_core.c)
    target_link_libraries(test_core PRIVATE qhw_scheduler)
    add_test(NAME test_core COMMAND test_core)
endif()
