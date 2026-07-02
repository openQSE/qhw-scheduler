cmake_minimum_required(VERSION 3.20)

project(qhw_scheduler C)

include(GNUInstallDirs)

option(QHW_SCHED_BUILD_SHARED "Build shared qhw-scheduler library" ON)
option(QHW_SCHED_BUILD_STATIC "Build static qhw-scheduler library" OFF)
option(QHW_SCHED_BUILD_TESTS "Build qhw-scheduler C tests" ON)
option(QHW_SCHED_BUILD_PLUGINS "Build scheduler policy plugins" ON)
option(QHW_SCHED_BUILD_PYTHON "Build Python bindings" ON)
option(QHW_SCHED_BUILD_SWIG "Build SWIG Python extension" ON)
option(QHW_SCHED_INSTALL_PYTHON_ARTIFACTS
    "Install native artifacts into the Python package"
    OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(QHW_SCHED_BUILD_PYTHON AND NOT QHW_SCHED_BUILD_SHARED)
    message(FATAL_ERROR
        "QHW_SCHED_BUILD_PYTHON requires QHW_SCHED_BUILD_SHARED=ON")
endif()

if(QHW_SCHED_BUILD_PYTHON AND NOT QHW_SCHED_BUILD_SWIG)
    message(FATAL_ERROR
        "QHW_SCHED_BUILD_PYTHON requires QHW_SCHED_BUILD_SWIG=ON")
endif()

set(QHW_SCHED_CORE_SOURCES
    src/qhw_allocator.c
    src/qhw_error.c
    src/qhw_plugin.c
    src/qhw_qpu.c
    src/qhw_scheduler.c
    src/qhw_split.c
    src/qhw_stats.c
    src/qhw_task.c
    src/qhw_thread.c
    src/policy/qhw_deadline_boost.c
    src/policy/qhw_ready_queue.c
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

set_target_properties(qhw_scheduler PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

set(QHW_SCHED_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/qhw_scheduler/qhw_scheduler.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/qhw_scheduler/qhw_scheduler_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/qhw_scheduler/qhw_scheduler_plugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/qhw_scheduler/qhw_scheduler_abi.h)

if(QHW_SCHED_BUILD_TESTS)
    enable_testing()

    add_executable(test_core tests/c/test_core.c)
    target_link_libraries(test_core PRIVATE qhw_scheduler)
    add_test(NAME test_core COMMAND test_core)

    add_executable(test_deadline_boost tests/c/test_deadline_boost.c)
    target_include_directories(test_deadline_boost
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_deadline_boost PRIVATE qhw_scheduler)
    add_test(NAME test_deadline_boost COMMAND test_deadline_boost)
endif()

install(
    TARGETS qhw_scheduler
    EXPORT qhw_schedulerTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
    DIRECTORY include/qhw_scheduler
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
    EXPORT qhw_schedulerTargets
    NAMESPACE qhw_scheduler::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qhw_scheduler)

if(QHW_SCHED_INSTALL_PYTHON_ARTIFACTS)
    install(
        TARGETS qhw_scheduler
        LIBRARY DESTINATION qhw_scheduler/native
        ARCHIVE DESTINATION qhw_scheduler/native
        RUNTIME DESTINATION qhw_scheduler/native)
endif()

if(QHW_SCHED_BUILD_PLUGINS)
    add_library(qhw_sched_fifo MODULE src/plugins/fifo.c)
    target_include_directories(qhw_sched_fifo
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(qhw_sched_fifo PRIVATE qhw_scheduler)
    set_target_properties(qhw_sched_fifo PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        INSTALL_RPATH "$ORIGIN/../native:$ORIGIN/../..")

    add_library(qhw_sched_priority MODULE src/plugins/priority.c)
    target_include_directories(qhw_sched_priority
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(qhw_sched_priority PRIVATE qhw_scheduler)
    set_target_properties(qhw_sched_priority PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        INSTALL_RPATH "$ORIGIN/../native:$ORIGIN/../..")

    add_library(qhw_sched_round_robin MODULE src/plugins/round_robin.c)
    target_include_directories(qhw_sched_round_robin
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(qhw_sched_round_robin PRIVATE qhw_scheduler)
    set_target_properties(qhw_sched_round_robin PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        INSTALL_RPATH "$ORIGIN/../native:$ORIGIN/../..")

    if(QHW_SCHED_BUILD_TESTS)
        add_library(qhw_sched_bad_plugin MODULE tests/c/bad_plugin.c)
        target_include_directories(qhw_sched_bad_plugin
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include)
        set_target_properties(qhw_sched_bad_plugin PROPERTIES
            PREFIX ""
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

        add_library(qhw_sched_fail_finish MODULE
            tests/c/fail_finish_plugin.c)
        target_include_directories(qhw_sched_fail_finish
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(qhw_sched_fail_finish PRIVATE qhw_scheduler)
        set_target_properties(qhw_sched_fail_finish PROPERTIES
            PREFIX ""
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

        add_executable(test_fifo tests/c/test_fifo.c)
        target_link_libraries(test_fifo PRIVATE qhw_scheduler)
        target_compile_definitions(test_fifo PRIVATE
            QHW_FIFO_PLUGIN_PATH="$<TARGET_FILE:qhw_sched_fifo>"
            QHW_BAD_PLUGIN_PATH="$<TARGET_FILE:qhw_sched_bad_plugin>")
        add_dependencies(test_fifo qhw_sched_fifo qhw_sched_bad_plugin)
        add_test(NAME test_fifo COMMAND test_fifo)

        add_executable(test_priority tests/c/test_priority.c)
        target_link_libraries(test_priority PRIVATE qhw_scheduler)
        target_compile_definitions(test_priority PRIVATE
            QHW_PRIORITY_PLUGIN_PATH="$<TARGET_FILE:qhw_sched_priority>")
        add_dependencies(test_priority qhw_sched_priority)
        add_test(NAME test_priority COMMAND test_priority)

        add_executable(test_round_robin tests/c/test_round_robin.c)
        target_link_libraries(test_round_robin PRIVATE qhw_scheduler)
        set(QHW_ROUND_ROBIN_PLUGIN_TARGET
            "$<TARGET_FILE:qhw_sched_round_robin>")
        set(QHW_ROUND_ROBIN_PLUGIN_DEFINE
            "QHW_ROUND_ROBIN_PLUGIN_PATH=\"${QHW_ROUND_ROBIN_PLUGIN_TARGET}\"")
        target_compile_definitions(test_round_robin PRIVATE
            ${QHW_ROUND_ROBIN_PLUGIN_DEFINE})
        add_dependencies(test_round_robin qhw_sched_round_robin)
        add_test(NAME test_round_robin COMMAND test_round_robin)

        add_executable(test_split tests/c/test_split.c)
        target_link_libraries(test_split PRIVATE qhw_scheduler)
        target_compile_definitions(test_split PRIVATE
            QHW_FIFO_PLUGIN_PATH="$<TARGET_FILE:qhw_sched_fifo>"
            QHW_FAIL_FINISH_PLUGIN_PATH="$<TARGET_FILE:qhw_sched_fail_finish>")
        add_dependencies(test_split
            qhw_sched_fifo
            qhw_sched_fail_finish)
        add_test(NAME test_split COMMAND test_split)
    endif()

    install(
        TARGETS qhw_sched_fifo
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/qhw_scheduler/plugins)

    install(
        TARGETS qhw_sched_priority
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/qhw_scheduler/plugins)

    install(
        TARGETS qhw_sched_round_robin
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/qhw_scheduler/plugins)

    if(QHW_SCHED_INSTALL_PYTHON_ARTIFACTS)
        install(
            TARGETS qhw_sched_fifo
            LIBRARY DESTINATION qhw_scheduler/plugins)
        install(
            TARGETS qhw_sched_priority
            LIBRARY DESTINATION qhw_scheduler/plugins)
        install(
            TARGETS qhw_sched_round_robin
            LIBRARY DESTINATION qhw_scheduler/plugins)
    endif()
endif()

if(QHW_SCHED_BUILD_PYTHON)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_package(SWIG REQUIRED)
    find_package(Python3 COMPONENTS Development.Module REQUIRED)
    set(QHW_SCHED_PYTHON_BUILD_DIR
        ${CMAKE_CURRENT_BINARY_DIR}/python/qhw_scheduler)
    set(QHW_SCHED_PYTHON_TREE_COMMANDS
        COMMAND
            ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/python/qhw_scheduler
            ${QHW_SCHED_PYTHON_BUILD_DIR}
        COMMAND
            ${CMAKE_COMMAND} -E make_directory
            ${QHW_SCHED_PYTHON_BUILD_DIR}/native
        COMMAND
            ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:qhw_scheduler>
            ${QHW_SCHED_PYTHON_BUILD_DIR}/native)
    set(QHW_SCHED_PYTHON_TREE_DEPENDS qhw_scheduler)

    if(QHW_SCHED_BUILD_PLUGINS)
        list(APPEND QHW_SCHED_PYTHON_TREE_COMMANDS
            COMMAND
                ${CMAKE_COMMAND} -E make_directory
                ${QHW_SCHED_PYTHON_BUILD_DIR}/plugins
            COMMAND
                ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE:qhw_sched_fifo>
                ${QHW_SCHED_PYTHON_BUILD_DIR}/plugins
            COMMAND
                ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE:qhw_sched_priority>
                ${QHW_SCHED_PYTHON_BUILD_DIR}/plugins
            COMMAND
                ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE:qhw_sched_round_robin>
                ${QHW_SCHED_PYTHON_BUILD_DIR}/plugins)
        list(APPEND QHW_SCHED_PYTHON_TREE_DEPENDS
            qhw_sched_fifo
            qhw_sched_priority
            qhw_sched_round_robin)
    endif()

    add_custom_target(qhw_scheduler_python_tree ALL
        ${QHW_SCHED_PYTHON_TREE_COMMANDS}
        DEPENDS ${QHW_SCHED_PYTHON_TREE_DEPENDS})

    set(QHW_SCHED_SWIG_DIR ${CMAKE_CURRENT_BINARY_DIR}/swig)
    set(QHW_SCHED_SWIG_C
        ${QHW_SCHED_SWIG_DIR}/qhw_scheduler_wrap.c)
    set(QHW_SCHED_SWIG_PY
        ${QHW_SCHED_PYTHON_BUILD_DIR}/_qhw_scheduler.py)

    add_custom_command(
        OUTPUT ${QHW_SCHED_SWIG_C} ${QHW_SCHED_SWIG_PY}
        COMMAND
            ${CMAKE_COMMAND} -E make_directory ${QHW_SCHED_SWIG_DIR}
        COMMAND
            ${CMAKE_COMMAND} -E make_directory
            ${QHW_SCHED_PYTHON_BUILD_DIR}
        COMMAND
            ${SWIG_EXECUTABLE}
            -python
            -module _qhw_scheduler
            -interface _qhw_scheduler_c
            -outdir ${QHW_SCHED_PYTHON_BUILD_DIR}
            -o ${QHW_SCHED_SWIG_C}
            -I${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/swg/qhw_scheduler.i
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/swg/qhw_scheduler.i
            ${CMAKE_CURRENT_SOURCE_DIR}/swg/qhw_scheduler_typemaps.i
            ${QHW_SCHED_PUBLIC_HEADERS}
        VERBATIM)

    add_library(_qhw_scheduler_c MODULE ${QHW_SCHED_SWIG_C})
    target_include_directories(_qhw_scheduler_c
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${Python3_INCLUDE_DIRS})
    target_link_libraries(_qhw_scheduler_c
        PRIVATE
            qhw_scheduler
            Python3::Module)
    set_target_properties(_qhw_scheduler_c PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${QHW_SCHED_PYTHON_BUILD_DIR}
        INSTALL_RPATH "$ORIGIN/native:$ORIGIN")
    add_dependencies(_qhw_scheduler_c qhw_scheduler_python_tree)

    if(QHW_SCHED_INSTALL_PYTHON_ARTIFACTS)
        install(
            TARGETS _qhw_scheduler_c
            LIBRARY DESTINATION qhw_scheduler)
        install(
            FILES ${QHW_SCHED_SWIG_PY}
            DESTINATION qhw_scheduler)
    endif()
endif()

if(QHW_SCHED_BUILD_PYTHON AND QHW_SCHED_BUILD_TESTS
    AND QHW_SCHED_BUILD_PLUGINS)
    add_test(
        NAME test_python_fifo
        COMMAND
            ${CMAKE_COMMAND} -E env
            PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python
            ${Python3_EXECUTABLE}
            -S
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_fifo.py)

    add_test(
        NAME test_python_fifo_detailed
        COMMAND
            ${CMAKE_COMMAND} -E env
            PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python
            ${Python3_EXECUTABLE}
            -S
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_fifo_detailed.py)

    add_test(
        NAME test_python_priority
        COMMAND
            ${CMAKE_COMMAND} -E env
            PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python
            ${Python3_EXECUTABLE}
            -S
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_priority.py)

    add_test(
        NAME test_python_round_robin
        COMMAND
            ${CMAKE_COMMAND} -E env
            PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python
            ${Python3_EXECUTABLE}
            -S
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_round_robin.py)

    add_test(
        NAME test_python_split
        COMMAND
            ${CMAKE_COMMAND} -E env
            PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python
            ${Python3_EXECUTABLE}
            -S
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_split.py)

    add_test(
        NAME test_python_private_import
        COMMAND
            ${CMAKE_COMMAND} -E env
            PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python
            ${Python3_EXECUTABLE}
            -S
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_private_import.py)
endif()
