# Two builds of the same sources, and they cannot share one configure because they use different
# compilers. Configure each into its own tree:
#
#   cmake -S . -B build/host   -G Ninja
#   cmake -S . -B build/device -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ndl-arm926ej-s.cmake
#   cmake -S . -B build/san    -G Ninja -DNPS_SANITIZE=ON
#
# The host tree is what to use while iterating. The device tree is what PERF-010's budgets have to be
# measured against, because an isolated host benchmark cannot qualify anything (PERF-011). The tree
# paths are not arbitrary: build/host/nps_host and build/device/*.tns are the paths the notes, the
# deploy step and the emulator loop all name.
#

cmake_minimum_required(VERSION 3.20)
project(nps CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NPS_LINK_OPTIMIZE "Enable cross-module optimization and dead-section removal" ${CMAKE_CROSSCOMPILING})
set(NPS_OPTIMIZED_LINK_FLAGS "")
if(NPS_LINK_OPTIMIZE)
    if(CMAKE_CROSSCOMPILING AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-ffunction-sections -fdata-sections -flto=auto -fno-fat-lto-objects)
        set(NPS_OPTIMIZED_LINK_FLAGS -flto=auto -Os -Wl,-O2,--gc-sections)
    elseif(NOT CMAKE_CROSSCOMPILING)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT nps_ipo_supported OUTPUT nps_ipo_error LANGUAGES CXX)
        if(NOT nps_ipo_supported)
            message(FATAL_ERROR "Link optimization is unavailable: ${nps_ipo_error}")
        endif()
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        if(APPLE)
            add_link_options(-Wl,-dead_strip)
        elseif(UNIX)
            add_compile_options(-ffunction-sections -fdata-sections)
            add_link_options(-Wl,-O2,--gc-sections)
        endif()
    else()
        message(FATAL_ERROR "Link optimization has not been qualified for this cross compiler")
    endif()
endif()

# The layout is the one section 28 of the pack sketches: headers under include/nps by area, sources
# under src by the same areas, tests split by kind. Every project include is spelled nps/<area>/x.h,
# which is what include/ exists to make possible.
set(CORE_SOURCES
    src/ui/canvas.cc
    src/ui/bitmap.cc
    src/core/ast.cc
    src/core/task.cc
    src/core/parser.cc
    src/core/print.cc
    src/core/canonical.cc
    src/core/context.cc
    src/core/evaluate.cc
    src/steps/derivation.cc
    src/steps/solve_task.cc
    src/steps/schema.cc
    src/steps/numeric_mode.cc
    src/steps/linear.cc
    src/steps/quadratic.cc
    src/steps/rearrange.cc
    src/steps/rewrite.cc
    src/steps/command.cc
    src/steps/integer.cc
    src/steps/matrix_row.cc
    src/steps/matrix_form.cc
    src/steps/matrix.cc
    src/steps/differentiate.cc
    src/steps/integrate.cc
    src/steps/calculus.cc
    src/physics/catch_up.cc
    src/physics/density.cc
    src/physics/modern.cc
    src/physics/relativity.cc
    src/physics/kinematics.cc
    src/physics/planar_kinematics.cc
    src/physics/position_motion.cc
    src/physics/graph_integration.cc
    src/physics/optics.cc
    src/physics/relative_motion.cc
    src/physics/unit_conversion.cc
    src/physics/vector_addition.cc
    src/physics/vector_components.cc
    src/physics/vector_cross.cc
    src/physics/scalar_product.cc
    src/physics/forces.cc
    src/physics/relation.cc
    src/physics/ranking.cc
    src/physics/gravitation.cc
    src/physics/oscillation.cc
    src/physics/circular_motion.cc
    src/physics/work.cc
    src/units/units.cc
    src/platform/nspire/device_identity.cc
    src/cas/giac/giac_adapter.cc)

set(TEST_SOURCES
    src/platform/nspire/integrity.cc
    src/platform/nspire/native_menu_policy.cc
    tests/unit/run_tests.cc
    tests/unit/native_menu_tests.cc
    tests/unit/adapter_tests.cc
    tests/unit/canonical_tests.cc
    tests/unit/task_tests.cc
    tests/unit/solve_task_tests.cc
    tests/unit/derivation_tests.cc
    tests/unit/linear_tests.cc
    tests/unit/quadratic_tests.cc
    tests/unit/rearrange_tests.cc
    tests/unit/rewrite_tests.cc
    tests/unit/command_tests.cc
    tests/unit/integer_tests.cc
    tests/unit/matrix_row_tests.cc
    tests/unit/matrix_form_tests.cc
    tests/unit/matrix_tests.cc
    tests/unit/differentiate_tests.cc
    tests/unit/integrate_tests.cc
    tests/unit/calculus_tests.cc
    tests/unit/ui_canvas_tests.cc
    tests/unit/integrity_tests.cc
    tests/unit/units_tests.cc
    tests/unit/catch_up_tests.cc
    tests/unit/density_tests.cc
    tests/unit/modern_tests.cc
    tests/unit/relativity_tests.cc
    tests/unit/gravitation_tests.cc
    tests/unit/oscillation_tests.cc
    tests/unit/circular_motion_tests.cc
    tests/unit/kinematics_tests.cc
    tests/unit/planar_kinematics_tests.cc
    tests/unit/position_motion_tests.cc
    tests/unit/graph_integration_tests.cc
    tests/unit/optics_tests.cc
    tests/unit/relative_motion_tests.cc
    tests/unit/unit_conversion_tests.cc
    tests/unit/vector_addition_tests.cc
    tests/unit/vector_components_tests.cc
    tests/unit/vector_cross_tests.cc
    tests/unit/scalar_product_tests.cc
    tests/unit/forces_tests.cc
    tests/unit/ranking_tests.cc
    tests/unit/work_tests.cc
    tests/unit/context_tests.cc
    tests/unit/golden_tests.cc
    tests/property/fuzz.cc
    tests/golden/golden.cc)

# Diagnostic instrumentation in the Lua module (raw-memory peek, watchdog pokes, flash tracing) is
# off by default. Configure with -DNPS_DIAG=1 to compile it back in for chasing a fault. Ninja
# tracks the compile command line, so changing this rebuilds what read it with no stamp file.
if(CMAKE_CROSSCOMPILING)
    set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_CXX_COMPILER_LAUNCHER})
endif()
enable_language(C)
option(NPS_QUALIFY_RETAINED_UI "Build LVGL qualification targets without enabling product controls" OFF)
option(NPS_RETAINED_UI "Enable retained controls in the application module" OFF)
option(NPS_RETAINED_FAULT_INJECTION "Expose retained renderer failure controls in diagnostic builds" OFF)
if(NPS_RETAINED_FAULT_INJECTION AND NOT NPS_RETAINED_UI)
    message(FATAL_ERROR "Retained failure injection requires NPS_RETAINED_UI")
endif()
if(NPS_QUALIFY_RETAINED_UI OR NPS_RETAINED_UI)
    if(CMAKE_CROSSCOMPILING AND NPS_LINK_OPTIMIZE)
        # Use compiler-aware archivers for slim LTO objects.
        foreach(language C CXX)
            set(CMAKE_${language}_ARCHIVE_CREATE "${NDL_TOOLCHAIN}/bin/arm-none-eabi-gcc-ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
            set(CMAKE_${language}_ARCHIVE_APPEND "${NDL_TOOLCHAIN}/bin/arm-none-eabi-gcc-ar q <TARGET> <LINK_FLAGS> <OBJECTS>")
            set(CMAKE_${language}_ARCHIVE_FINISH "${NDL_TOOLCHAIN}/bin/arm-none-eabi-gcc-ranlib <TARGET>")
        endforeach()
    endif()
    set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/include/nps/ui/lv_conf.h" CACHE PATH "" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/lvgl EXCLUDE_FROM_ALL)
    get_target_property(nps_lvgl_sources lvgl SOURCES)
    list(FILTER nps_lvgl_sources EXCLUDE REGEX "\\.S$")
    set_property(TARGET lvgl PROPERTY SOURCES ${nps_lvgl_sources})
    target_compile_options(lvgl PRIVATE -Os)
    target_sources(lvgl PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/retained_menu.c")
    target_include_directories(lvgl PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/src/stdlib/builtin/lv_mem_core_builtin.c"
        TARGET_DIRECTORY lvgl PROPERTIES COMPILE_DEFINITIONS
        "lv_malloc_core=nps_lv_builtin_malloc_core;lv_realloc_core=nps_lv_builtin_realloc_core")
    add_library(nps_retained_ui STATIC src/ui/retained_surface.cc src/ui/retained_menu.cc)
    target_include_directories(nps_retained_ui PUBLIC include)
    target_link_libraries(nps_retained_ui PUBLIC lvgl::lvgl)
    if(NPS_RETAINED_UI)
        set(NPS_RETAINED_LINK_TARGETS nps_retained_ui lvgl)
        set(NPS_RETAINED_LINK_FLAGS $<TARGET_FILE:nps_retained_ui> $<TARGET_FILE:lvgl>)
    endif()
    if(NOT CMAKE_CROSSCOMPILING OR NPS_RETAINED_FAULT_INJECTION)
        target_compile_definitions(lvgl PRIVATE NPS_UI_TESTING=1)
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        add_executable(nps_retained_ui_test tests/ui/retained_surface_tests.cc)
        target_link_libraries(nps_retained_ui_test PRIVATE nps_retained_ui)
        add_executable(nps_retained_menu_test tests/ui/retained_menu_tests.cc)
        target_compile_definitions(nps_retained_menu_test PRIVATE NPS_UI_TESTING=1)
        target_link_libraries(nps_retained_menu_test PRIVATE nps_retained_ui)
        enable_testing()
        add_test(NAME retained_ui COMMAND nps_retained_ui_test)
        add_test(NAME retained_menu COMMAND nps_retained_menu_test)
        add_test(NAME retained_menu_assert COMMAND nps_retained_menu_test assert)
        add_test(NAME retained_menu_faults COMMAND "${CMAKE_COMMAND}"
            "-DPROBE=$<TARGET_FILE:nps_retained_menu_test>" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/retained_menu_faults.cmake")
    endif()
endif()
add_library(nps_ui_containers INTERFACE)
target_include_directories(nps_ui_containers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/etl/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/EASTL/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/EABase/include/Common")
target_compile_definitions(nps_ui_containers INTERFACE ETL_NO_PROFILE_HEADER
    EASTL_ASSERT_ENABLED=0 EASTL_EXCEPTIONS_ENABLED=0 EASTL_NAME_ENABLED=0 EA_HAVE_CPP11_INITIALIZER_LIST=1)
if(CMAKE_CROSSCOMPILING)
    target_compile_definitions(nps_ui_containers INTERFACE EA_PLATFORM_NSPIRE=1)
endif()
set(NPS_RGB_SOURCES
    third_party/nRGBlib/src/nRGBlib/drawLine.c
    third_party/nRGBlib/src/nRGBlib/drawCircle.c)
list(APPEND CORE_SOURCES ${NPS_RGB_SOURCES})
set_source_files_properties(${NPS_RGB_SOURCES} PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/nrgblib;${CMAKE_CURRENT_SOURCE_DIR}/third_party/nRGBlib/src/include"
    COMPILE_DEFINITIONS "setPixel_s=nps_rgb_pixel;drawLine_=nps_rgb_line;drawCircle_=nps_rgb_circle;drawLine=nps_rgb_line_shape;drawCircle=nps_rgb_circle_shape")

set(NPS_DIAG 0 CACHE STRING "compile the Lua module's diagnostic instrumentation back in")
set(NPS_RESOURCE_PROFILE 0 CACHE STRING "instrument unified allocation and render-ready resources")
if(NOT NPS_RESOURCE_PROFILE MATCHES "^[01]$")
    message(FATAL_ERROR "NPS_RESOURCE_PROFILE must be 0 or 1")
endif()
if(NPS_RESOURCE_PROFILE AND NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "NPS_RESOURCE_PROFILE is available only in a device build")
endif()

# <stem> from <stem>.tns, spelled with string arithmetic rather than a pattern.
function(nps_package_stem output package_name)
    string(LENGTH "${package_name}" package_length)
    math(EXPR stem_length "${package_length} - 4")
    string(SUBSTRING "${package_name}" 0 ${stem_length} stem)
    set(${output} "${stem}" PARENT_SCOPE)
endfunction()

# <stem>.sha256.tns from <stem>.tns.
function(nps_sidecar_name output package_name)
    nps_package_stem(stem "${package_name}")
    set(${output} "${stem}.sha256.tns" PARENT_SCOPE)
endfunction()

# Every packaged name, once. A manifest row that names a sidecar and the packaging step that writes
# it are one fact, and spelling it twice let them drift: renaming a package left the manifest
# publishing a sidecar nothing produced. Sidecars come from the rule above rather than a second copy
# of it, and these sit outside the host and device branches because both read them.
set(NPS_PROBE_PACKAGE nps_m0_probe.tns)
set(NPS_SPLIT_PACKAGE nps_split.luax.tns)
set(NPS_UNIFIED_PACKAGE nps_nspire.luax.tns)
set(NPS_V2_DOCUMENT nps_v2.tns)
set(NPS_V3_DOCUMENT nps_v3.tns)
set(NPS_V4_DOCUMENT nps_v4.tns)
nps_sidecar_name(NPS_PROBE_SIDECAR "${NPS_PROBE_PACKAGE}")
nps_sidecar_name(NPS_SPLIT_SIDECAR "${NPS_SPLIT_PACKAGE}")
nps_sidecar_name(NPS_UNIFIED_SIDECAR "${NPS_UNIFIED_PACKAGE}")
nps_sidecar_name(NPS_V3_SIDECAR "${NPS_V3_DOCUMENT}")
nps_sidecar_name(NPS_V4_SIDECAR "${NPS_V4_DOCUMENT}")

function(nps_hash_inputs output)
    set(inputs ${ARGN})
    list(REMOVE_DUPLICATES inputs)
    list(SORT inputs)
    set(material "")
    foreach(input IN LISTS inputs)
        if(NOT EXISTS "${input}")
            message(FATAL_ERROR "manifest input does not exist: ${input}")
        endif()
        file(SHA256 "${input}" digest)
        file(RELATIVE_PATH label "${CMAKE_CURRENT_SOURCE_DIR}" "${input}")
        string(APPEND material "${label}\n${digest}\n")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
        set_property(GLOBAL APPEND PROPERTY nps_manifest_inputs "${input}")
    endforeach()
    string(SHA256 combined "${material}")
    set(${output} "${combined}" PARENT_SCOPE)
endfunction()

# A source can reach a packaged image without reaching the hash that identifies it, and then editing
# it changes the packaged bytes while the manifest id stays put. Three sources have arrived that way
# already, so the two lists are compared here rather than kept in step by hand. Takes an add_tns name
# and the name of that artifact's input list, and reads the link's own object list, so adding an
# object library to a link needs no second edit here for the check to see it.
function(nps_check_hash_covers artifact inputs)
    set(covered "")
    foreach(input IN LISTS ${inputs})
        get_filename_component(input "${input}" ABSOLUTE)
        list(APPEND covered "${input}")
    endforeach()
    get_property(objects GLOBAL PROPERTY nps_tns_objects_${artifact})
    if(NOT objects)
        message(FATAL_ERROR "no link recorded its objects under ${artifact}, so nothing was checked")
    endif()
    set(prefix "$<TARGET_OBJECTS:")
    string(LENGTH "${prefix}" prefix_length)
    foreach(object IN LISTS objects)
        string(FIND "${object}" "${prefix}" position)
        if(NOT position EQUAL 0)
            # Giac's prebuilt objects are covered by NPS_GIAC_HASH rather than by an input list, and
            # are not targets here. Anything else arriving as a bare path is covered by nothing.
            if(NOT "${object}" IN_LIST GIAC_OBJECTS)
                message(FATAL_ERROR
                        "the ${artifact} image links ${object}, which is neither an object library "
                        "nor one of Giac's prebuilt objects, so no hash covers it.")
            endif()
            continue()
        endif()
        string(LENGTH "${object}" object_length)
        math(EXPR name_length "${object_length} - ${prefix_length} - 1")
        string(SUBSTRING "${object}" ${prefix_length} ${name_length} target)
        get_target_property(sources ${target} SOURCES)
        get_target_property(directory ${target} SOURCE_DIR)
        foreach(source IN LISTS sources)
            if(NOT IS_ABSOLUTE "${source}")
                set(source "${directory}/${source}")
            endif()
            get_filename_component(source "${source}" ABSOLUTE)
            if(NOT "${source}" IN_LIST covered)
                message(FATAL_ERROR
                        "the ${artifact} image links ${source}, which its capability manifest hash "
                        "does not cover. Add it to the artifact's build inputs, or the packaged "
                        "bytes will change while the manifest id does not.")
            endif()
        endforeach()
    endforeach()
endfunction()

if(DEFINED NDL_SDK AND NOT "${NDL_SDK}" STREQUAL "")
    get_filename_component(NPS_NDL_SDK "${NDL_SDK}" ABSOLUTE)
else()
    get_filename_component(NPS_NDL_SDK
                           "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/ndl-src/ndl-sdk" ABSOLUTE)
endif()
get_filename_component(NPS_NDL_ROOT "${NPS_NDL_SDK}/.." ABSOLUTE)
if(DEFINED GIAC_ROOT AND NOT "${GIAC_ROOT}" STREQUAL "")
    get_filename_component(NPS_GIAC_ROOT "${GIAC_ROOT}" ABSOLUTE)
else()
    get_filename_component(NPS_GIAC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/khi-src" ABSOLUTE)
endif()
set(NPS_GIAC_SRC "${NPS_GIAC_ROOT}/src")

# The integrity check hashes the packaged artifact with Giac's own SHA-256 rather than a second copy
# of the algorithm, so both trees compile khi-src's sha256.c and the device links the very object it
# already ships. Named here because the host build now needs this one file from the Giac fork, which
# it did not before.
if(NOT EXISTS "${NPS_GIAC_SRC}/sha256.c" OR NOT EXISTS "${NPS_GIAC_SRC}/sha256.h")
    message(FATAL_ERROR "the integrity check needs khi-src/src/sha256.c and sha256.h")
endif()
# This project enables CXX only, and sha256.h carries its own extern "C", so the one C file we borrow
# is compiled by the C++ front end rather than widening the project's languages for it.
set_source_files_properties("${NPS_GIAC_SRC}/sha256.c" PROPERTIES LANGUAGE CXX)

add_library(nps_gmp INTERFACE)
if(CMAKE_CROSSCOMPILING)
    set(NPS_GMP_INCLUDE_DIR "${NPS_NDL_SDK}/toolchain/install/include")
    set(NPS_GMP_LIBRARY "${NPS_NDL_SDK}/toolchain/install/lib/libgmp.a")
    if(NOT EXISTS "${NPS_GMP_INCLUDE_DIR}/gmp.h" OR NOT EXISTS "${NPS_GMP_LIBRARY}")
        message(FATAL_ERROR "the ndl GMP 6.3.0 installation is incomplete")
    endif()
    target_include_directories(nps_gmp INTERFACE "${NPS_GMP_INCLUDE_DIR}")
    target_link_libraries(nps_gmp INTERFACE "${NPS_GMP_LIBRARY}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NPS_GMP REQUIRED IMPORTED_TARGET "gmp=6.3.0")
    target_link_libraries(nps_gmp INTERFACE PkgConfig::NPS_GMP)
endif()

# Inputs that #222 owns: built SDK and toolchain artifacts a manifest hashes because the packaged
# bytes depend on them. The check below cannot hold those to the tracked-source rule until #222 moves
# those hashes onto sources, so the call that hashed one says so here. Naming the call rather than a
# path pattern keeps the exemption from covering a tracked source that happens to sit beside one.
function(nps_defer_manifest_inputs)
    foreach(input IN LISTS ARGN)
        set_property(GLOBAL APPEND PROPERTY nps_manifest_inputs_deferred "${input}")
    endforeach()
endfunction()

function(nps_hash_ndl_inputs output)
    set(sdk "${NPS_NDL_SDK}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E env
                            "PATH=${sdk}/bin:${sdk}/toolchain/install/bin:$ENV{PATH}"
                            make -s -C "${sdk}/system" all COMMAND_ERROR_IS_FATAL ANY)
    file(GLOB_RECURSE headers CONFIGURE_DEPENDS LIST_DIRECTORIES false "${sdk}/include/*")
    file(GLOB_RECURSE toolchain_headers CONFIGURE_DEPENDS LIST_DIRECTORIES false
         "${sdk}/toolchain/install/arm-none-eabi/include/*"
         "${sdk}/toolchain/install/lib/gcc/arm-none-eabi/*/include/*"
         "${sdk}/toolchain/install/lib/gcc/arm-none-eabi/*/include-fixed/*")
    file(GLOB platform_archives CONFIGURE_DEPENDS LIST_DIRECTORIES false "${sdk}/lib/*.a")
    file(GLOB system_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
         "${sdk}/system/*.S"
         "${sdk}/system/*.c"
         "${sdk}/system/Makefile"
         "${sdk}/system/ldscript")
    file(GLOB toolchain_specs CONFIGURE_DEPENDS LIST_DIRECTORIES false
         "${sdk}/toolchain/install/arm-none-eabi/lib/*.specs"
         "${sdk}/toolchain/install/lib/gcc/arm-none-eabi/*/specs")
    file(GLOB toolchain_libgcc CONFIGURE_DEPENDS LIST_DIRECTORIES false
         "${sdk}/toolchain/install/lib/gcc/arm-none-eabi/*/libgcc.a")
    file(GLOB toolchain_programs CONFIGURE_DEPENDS LIST_DIRECTORIES false
         "${sdk}/toolchain/install/libexec/gcc/arm-none-eabi/*/cc1"
         "${sdk}/toolchain/install/libexec/gcc/arm-none-eabi/*/cc1plus"
         "${sdk}/toolchain/install/libexec/gcc/arm-none-eabi/*/collect2"
         "${sdk}/toolchain/install/libexec/gcc/arm-none-eabi/*/lto-wrapper")
    set(source_inputs "")
    if(EXISTS "${NPS_NDL_ROOT}/ndl/src/tools/MakeSyscalls")
        file(GLOB_RECURSE syscall_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
             "${NPS_NDL_ROOT}/ndl/src/tools/MakeSyscalls/*")
        list(APPEND source_inputs ${syscall_sources})
    endif()
    if(EXISTS "${NPS_NDL_ROOT}/ndl/src/resources/install.c")
        list(APPEND source_inputs "${NPS_NDL_ROOT}/ndl/src/resources/install.c")
    endif()
    set(user_inputs "")
    if(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/.ndl")
        file(GLOB_RECURSE user_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false
             "$ENV{HOME}/.ndl/include/*" "$ENV{HOME}/.ndl/lib/*")
    endif()
    set(inputs
        ${headers}
        ${toolchain_headers}
        ${platform_archives}
        ${system_sources}
        ${toolchain_specs}
        ${toolchain_libgcc}
        ${toolchain_programs}
        ${source_inputs}
        ${user_inputs}
        "${sdk}/bin/arm-none-eabi-ld.gold"
        "${sdk}/bin/genzehn"
        "${sdk}/bin/nspire-g++"
        "${sdk}/bin/nspire-gcc"
        "${sdk}/bin/nspire-ld"
        "${sdk}/bin/nspire-tools"
        "${sdk}/lib/libndls.a"
        "${sdk}/libndls/config.c"
        "${sdk}/libndls/file_each.c"
        "${sdk}/system/crt0.o"
        "${sdk}/system/crti.o"
        "${sdk}/system/crtn.o"
        "${sdk}/toolchain/build_toolchain.sh"
        "${sdk}/toolchain/install/arm-none-eabi/lib/libc.a"
        "${sdk}/toolchain/install/arm-none-eabi/lib/libm.a"
        "${sdk}/toolchain/install/arm-none-eabi/lib/libstdc++.a"
        "${sdk}/toolchain/install/bin/arm-none-eabi-gcc"
        "${sdk}/toolchain/install/bin/arm-none-eabi-g++"
        "${sdk}/toolchain/install/bin/arm-none-eabi-as"
        "${sdk}/toolchain/install/bin/arm-none-eabi-ld"
        "${sdk}/toolchain/install/include/gmp.h"
        "${sdk}/toolchain/install/lib/libgmp.a")
    # Every one of these is produced by bootstrapping the SDK and the cross toolchain, which is #222.
    nps_defer_manifest_inputs(${inputs})
    nps_hash_inputs(hash ${inputs})
    set(${output} "${hash}" PARENT_SCOPE)
endfunction()

function(nps_hash_giac_inputs output)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
         "${NPS_GIAC_SRC}/*.c"
         "${NPS_GIAC_SRC}/*.cc"
         "${NPS_GIAC_SRC}/*.cxx"
         "${NPS_GIAC_SRC}/*.h"
         "${NPS_GIAC_SRC}/*.hh"
         "${NPS_GIAC_SRC}/*.lex"
         "${NPS_GIAC_SRC}/*.y")
    set(inputs
        ${sources}
        "${NPS_GIAC_ROOT}/configure"
        "${NPS_GIAC_ROOT}/configure.ac"
        "${NPS_GIAC_SRC}/Makefile.ki"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/giac-objs.mk"
        "${NPS_NDL_SDK}/toolchain/install/lib/libgmp.a"
        "${NPS_NDL_SDK}/toolchain/install/lib/libmpfi.a"
        "${NPS_NDL_SDK}/toolchain/install/lib/libmpfr.a")
    # The Giac sources are tracked. The three toolchain archives are bootstrap output, also #222.
    nps_defer_manifest_inputs(
        "${NPS_NDL_SDK}/toolchain/install/lib/libgmp.a"
        "${NPS_NDL_SDK}/toolchain/install/lib/libmpfi.a"
        "${NPS_NDL_SDK}/toolchain/install/lib/libmpfr.a")
    nps_hash_inputs(hash ${inputs})
    set(${output} "${hash}" PARENT_SCOPE)
endfunction()

if(CMAKE_CROSSCOMPILING)
    list(APPEND CORE_SOURCES src/platform/nspire/native_canvas.cc)
endif()
file(GLOB_RECURSE NPS_UI_DEPENDENCY_HEADERS CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/etl/include/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/EASTL/include/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/EABase/include/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nRGBlib/src/include/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/nrgblib/*")
file(GLOB_RECURSE NPS_PUBLIC_HEADERS CONFIGURE_DEPENDS LIST_DIRECTORIES false
     "${CMAKE_CURRENT_SOURCE_DIR}/include/nps/*.h")
set(NPS_COMMON_MANIFEST_INPUTS
    ${NPS_PUBLIC_HEADERS}
    ${NPS_UI_DEPENDENCY_HEADERS}
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/capability_manifest.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/write-sha256.cmake")
if(NPS_RETAINED_UI)
    file(GLOB_RECURSE NPS_RETAINED_DEPENDENCY_INPUTS CONFIGURE_DEPENDS LIST_DIRECTORIES false
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/src/*")
    list(APPEND NPS_COMMON_MANIFEST_INPUTS ${NPS_RETAINED_DEPENDENCY_INPUTS}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/retained_menu.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/retained_menu.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/retained_surface.cc")
endif()
foreach(source IN LISTS CORE_SOURCES)
    list(APPEND NPS_COMMON_MANIFEST_INPUTS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
endforeach()

# The packager reaches the packaged bytes through what it was compiled from, so its tracked sources
# answer the same question the compiled luna answered and exist in a checkout nobody has built yet.
# Hashing the binary made the manifest id depend on whether somebody had already run bootstrap, and
# a fresh worktree could not configure the host build at all.
file(GLOB NPS_LUNA_MANIFEST_INPUTS CONFIGURE_DEPENDS LIST_DIRECTORIES false
     "${NPS_NDL_SDK}/tools/luna/Makefile"
     "${NPS_NDL_SDK}/tools/luna/*.c"
     "${NPS_NDL_SDK}/tools/luna/*.h"
     "${NPS_NDL_SDK}/tools/luna/minizip-1.1/*.c"
     "${NPS_NDL_SDK}/tools/luna/minizip-1.1/*.h")
if(NOT NPS_LUNA_MANIFEST_INPUTS)
    message(FATAL_ERROR "no luna packager sources under ${NPS_NDL_SDK}/tools/luna")
endif()
# Hashed here rather than only inside the branches that package, so an input only a warm tree owns
# fails the first configure of every checkout instead of the first configure that finds LuaJIT.
nps_hash_inputs(NPS_LUNA_HASH ${NPS_LUNA_MANIFEST_INPUTS})

function(nps_add_manifest target artifact release_targets diagnostic_only backend_name
         backend_version backend_interface backend_deployment step_hash ndl_hash giac_hash
         sidecar ui_sidecar)
    # Keep the verification claim tied to the artifact behavior rather than individual call sites.
    if(artifact STREQUAL "probe")
        set(sidecar_scheme "external-sha256-sidecar")
    elseif(artifact STREQUAL "split" OR artifact STREQUAL "unified")
        set(sidecar_scheme "runtime-sha256-sidecar")
    else()
        set(sidecar_scheme "")
    endif()
    if(ndl_hash)
        set(NPS_CFG_HAS_NDL 1)
    else()
        set(NPS_CFG_HAS_NDL 0)
    endif()
    if(giac_hash)
        set(NPS_CFG_HAS_GIAC 1)
    else()
        set(NPS_CFG_HAS_GIAC 0)
    endif()
    if(sidecar)
        set(NPS_CFG_HAS_SIDECAR 1)
    else()
        set(NPS_CFG_HAS_SIDECAR 0)
    endif()
    if(ui_sidecar)
        set(NPS_CFG_HAS_UI_SIDECAR 1)
    else()
        set(NPS_CFG_HAS_UI_SIDECAR 0)
    endif()
    if(CMAKE_CROSSCOMPILING)
        if(CMAKE_CONFIGURATION_TYPES)
            message(FATAL_ERROR "device manifests require a single-configuration generator")
        endif()
        string(TOUPPER "${CMAKE_BUILD_TYPE}" build_type_key)
        set(config_c_flags "")
        set(config_cxx_flags "")
        set(config_exe_linker_flags "")
        set(config_module_linker_flags "")
        set(config_shared_linker_flags "")
        if(NOT build_type_key STREQUAL "")
            set(config_c_flags "${CMAKE_C_FLAGS_${build_type_key}}")
            set(config_cxx_flags "${CMAKE_CXX_FLAGS_${build_type_key}}")
            set(config_exe_linker_flags "${CMAKE_EXE_LINKER_FLAGS_${build_type_key}}")
            set(config_module_linker_flags "${CMAKE_MODULE_LINKER_FLAGS_${build_type_key}}")
            set(config_shared_linker_flags "${CMAKE_SHARED_LINKER_FLAGS_${build_type_key}}")
        endif()
        set(build_profile
            "generator=${CMAKE_GENERATOR}|build_type=${CMAKE_BUILD_TYPE}|compiler=${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}|compiler_path=${CMAKE_CXX_COMPILER}|c_flags=${CMAKE_C_FLAGS}|config_c_flags=${config_c_flags}|cxx_flags=${CMAKE_CXX_FLAGS}|config_cxx_flags=${config_cxx_flags}|exe_linker_flags=${CMAKE_EXE_LINKER_FLAGS}|config_exe_linker_flags=${config_exe_linker_flags}|module_linker_flags=${CMAKE_MODULE_LINKER_FLAGS}|config_module_linker_flags=${config_module_linker_flags}|shared_linker_flags=${CMAKE_SHARED_LINKER_FLAGS}|config_shared_linker_flags=${config_shared_linker_flags}|diag=${NPS_DIAG}|resource_profile=${NPS_RESOURCE_PROFILE}|link_optimize=${NPS_LINK_OPTIMIZE}|optimized_link_flags=${NPS_OPTIMIZED_LINK_FLAGS}")
    else()
        set(build_profile "host-semantic-test")
    endif()
    string(APPEND build_profile "|retained_ui=${NPS_RETAINED_UI}")
    string(APPEND build_profile "|retained_fault_injection=${NPS_RETAINED_FAULT_INJECTION}")
    string(SHA256 identity
           "schema=2\nartifact=${artifact}\ntargets=${release_targets}\ndiagnostic=${diagnostic_only}\nprofile=${build_profile}\nstepcas=${step_hash}\nndl=${ndl_hash}\ngiac=${giac_hash}\nbackend=${backend_name}\nversion=${backend_version}\ninterface=${backend_interface}\ndeployment=${backend_deployment}\nsidecar=${sidecar}\nsidecar_scheme=${sidecar_scheme}\nui_sidecar=${ui_sidecar}\n")
    if(artifact STREQUAL "host")
        set(identity_kind "semantic-inputs-sha256")
    else()
        set(identity_kind "inputs-sha256")
    endif()
    set(NPS_CFG_ID "stepcas.${artifact}.${identity_kind}.${identity}")
    set(NPS_CFG_ARTIFACT "${artifact}")
    set(NPS_CFG_RELEASE_TARGETS "${release_targets}")
    set(NPS_CFG_DIAGNOSTIC_ONLY "${diagnostic_only}")
    set(NPS_CFG_BACKEND_NAME "${backend_name}")
    set(NPS_CFG_BACKEND_VERSION "${backend_version}")
    set(NPS_CFG_BACKEND_INTERFACE "${backend_interface}")
    set(NPS_CFG_BACKEND_DEPLOYMENT "${backend_deployment}")
    set(NPS_CFG_STEP_HASH "${step_hash}")
    set(NPS_CFG_NDL_HASH "${ndl_hash}")
    set(NPS_CFG_GIAC_HASH "${giac_hash}")
    set(NPS_CFG_SIDECAR "${sidecar}")
    set(NPS_CFG_SIDECAR_SCHEME "${sidecar_scheme}")
    set(NPS_CFG_UI_SIDECAR "${ui_sidecar}")
    set(config_dir "${CMAKE_CURRENT_BINARY_DIR}/manifest/${artifact}")
    file(MAKE_DIRECTORY "${config_dir}")
    file(CONFIGURE OUTPUT "${config_dir}/nps_manifest_config.h" CONTENT [=[
#ifndef NPS_MANIFEST_CONFIG_H
#define NPS_MANIFEST_CONFIG_H
#define NPS_MANIFEST_ID "@NPS_CFG_ID@"
#define NPS_MANIFEST_ARTIFACT "@NPS_CFG_ARTIFACT@"
#define NPS_MANIFEST_RELEASE_TARGETS @NPS_CFG_RELEASE_TARGETS@
#define NPS_MANIFEST_DIAGNOSTIC_ONLY @NPS_CFG_DIAGNOSTIC_ONLY@
#define NPS_MANIFEST_BACKEND_NAME "@NPS_CFG_BACKEND_NAME@"
#define NPS_MANIFEST_BACKEND_VERSION "@NPS_CFG_BACKEND_VERSION@"
#define NPS_MANIFEST_BACKEND_INTERFACE "@NPS_CFG_BACKEND_INTERFACE@"
#define NPS_MANIFEST_BACKEND_DEPLOYMENT "@NPS_CFG_BACKEND_DEPLOYMENT@"
#define NPS_MANIFEST_STEP_HASH "@NPS_CFG_STEP_HASH@"
#define NPS_MANIFEST_HAS_NDL @NPS_CFG_HAS_NDL@
#define NPS_MANIFEST_NDL_HASH "@NPS_CFG_NDL_HASH@"
#define NPS_MANIFEST_HAS_GIAC @NPS_CFG_HAS_GIAC@
#define NPS_MANIFEST_GIAC_HASH "@NPS_CFG_GIAC_HASH@"
#define NPS_MANIFEST_HAS_SIDECAR @NPS_CFG_HAS_SIDECAR@
#define NPS_MANIFEST_SIDECAR "@NPS_CFG_SIDECAR@"
#define NPS_MANIFEST_SIDECAR_SCHEME "@NPS_CFG_SIDECAR_SCHEME@"
#define NPS_MANIFEST_HAS_UI_SIDECAR @NPS_CFG_HAS_UI_SIDECAR@
#define NPS_MANIFEST_UI_SIDECAR "@NPS_CFG_UI_SIDECAR@"
#endif
]=] @ONLY)
    add_library(${target} OBJECT src/core/capability_manifest.cc)
    target_include_directories(${target} PRIVATE include "${config_dir}")
    set(${target}_MANIFEST_ID "${NPS_CFG_ID}" PARENT_SCOPE)
endfunction()

if(CMAKE_CROSSCOMPILING)

    file(STRINGS "${NPS_GIAC_SRC}/config.h" NPS_GIAC_VERSION_LINE
         REGEX "^#define PACKAGE_VERSION \"[^\"]+\"$")
    if(NOT NPS_GIAC_VERSION_LINE MATCHES "\"([^\"]+)\"")
        message(FATAL_ERROR "cannot read the bundled Giac version")
    endif()
    set(NPS_GIAC_VERSION "${CMAKE_MATCH_1}")

    # All three device images link the integrity check and Giac's SHA-256 alongside it, so both are
    # inputs of every device artifact rather than of whichever list happened to name them.
    set(NPS_DEVICE_BUILD_INPUTS
        ${NPS_COMMON_MANIFEST_INPUTS}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchains/ndl-arm926ej-s.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/nspire/integrity.cc"
        "${NPS_GIAC_SRC}/sha256.c")
    set(NPS_PROBE_BUILD_INPUTS
        ${NPS_DEVICE_BUILD_INPUTS}
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/device_probe.cc")
    nps_hash_inputs(NPS_PROBE_STEP_HASH ${NPS_PROBE_BUILD_INPUTS})
    set(NPS_SPLIT_BUILD_INPUTS
        ${NPS_DEVICE_BUILD_INPUTS}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/nspire/lua_module.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/lua/nps_v3.lua"
        ${NPS_LUNA_MANIFEST_INPUTS})
    nps_hash_inputs(NPS_SPLIT_STEP_HASH ${NPS_SPLIT_BUILD_INPUTS})
    set(NPS_UNIFIED_BUILD_INPUTS
        ${NPS_DEVICE_BUILD_INPUTS}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/nspire/lua_module.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/nspire/native_menu.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/nspire/native_menu_policy.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cas/giac/giac_typed.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/lua/nps_v4.lua"
        ${NPS_LUNA_MANIFEST_INPUTS})
    if(NPS_RESOURCE_PROFILE)
        list(APPEND NPS_UNIFIED_BUILD_INPUTS
             "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/nspire/allocation_probe.cc")
    endif()
    nps_hash_inputs(NPS_UNIFIED_STEP_HASH ${NPS_UNIFIED_BUILD_INPUTS})
    nps_hash_ndl_inputs(NPS_NDL_HASH)
    nps_hash_giac_inputs(NPS_GIAC_HASH)

    nps_add_manifest(nps_manifest_probe probe 1 1 none not-installed none absent
                     "${NPS_PROBE_STEP_HASH}" "${NPS_NDL_HASH}" ""
                     "${NPS_PROBE_SIDECAR}" "")
    nps_add_manifest(nps_manifest_split split 1 0 Giac runtime-unverified
                     lua5.1.luagiac.caseval-v1 external-required-unvalidated
                     "${NPS_SPLIT_STEP_HASH}" "${NPS_NDL_HASH}" ""
                     "${NPS_SPLIT_SIDECAR}" "${NPS_V3_SIDECAR}")
    nps_add_manifest(nps_manifest_unified unified 1 ${NPS_RESOURCE_PROFILE} Giac "${NPS_GIAC_VERSION}"
                      nps.giac.typed-v1 bundled-static
                      "${NPS_UNIFIED_STEP_HASH}" "${NPS_NDL_HASH}" "${NPS_GIAC_HASH}"
                      "${NPS_UNIFIED_SIDECAR}" "${NPS_V4_SIDECAR}")
    if(NPS_RETAINED_FAULT_INJECTION)
        target_compile_definitions(nps_manifest_unified PRIVATE NPS_RETAINED_FAULT_INJECTION=1)
    endif()
    if(nps_manifest_probe_MANIFEST_ID STREQUAL nps_manifest_split_MANIFEST_ID OR
       nps_manifest_probe_MANIFEST_ID STREQUAL nps_manifest_unified_MANIFEST_ID OR
       nps_manifest_split_MANIFEST_ID STREQUAL nps_manifest_unified_MANIFEST_ID)
        message(FATAL_ERROR "artifact capability manifest ids must differ")
    endif()
    foreach(manifest nps_manifest_probe nps_manifest_split nps_manifest_unified)
        target_compile_options(${manifest} PRIVATE -Wall -Wextra)
    endforeach()

    # ------------------------------------------------------------------------------------------
    # The calculator
    # ------------------------------------------------------------------------------------------

    add_library(nps_core OBJECT ${CORE_SOURCES})
    target_compile_options(nps_core PRIVATE -Wall -Wextra)
    target_include_directories(nps_core PRIVATE include "${NDL_TOOLCHAIN}/include")
    target_link_libraries(nps_core PRIVATE nps_gmp)
    target_link_libraries(nps_core PUBLIC nps_ui_containers)

    # The two entry points are objects of their own so that the device target compiles them too. Leaving
    # them to the link targets alone meant device_probe.cc went a whole session uncompiled while the
    # build kept reporting success.
    add_library(nps_bench_main OBJECT benchmarks/device_probe.cc)
    add_library(nps_split_module OBJECT src/platform/nspire/lua_module.cc)
    foreach(entry nps_bench_main nps_split_module)
        target_compile_options(${entry} PRIVATE -Wall -Wextra)
        target_link_libraries(${entry} PRIVATE nps_ui_containers)
        target_include_directories(${entry} PRIVATE include "${NDL_TOOLCHAIN}/include")
    endforeach()
    # The Lua module is the only file that reads it, so it is defined only where it is read. The old
    # build put it on every device object, which is why a stamp file had to force the rebuild.
    target_compile_definitions(nps_split_module PRIVATE NPS_DIAG=${NPS_DIAG} NPS_RELEASE_MANIFEST=1)

    add_custom_target(device DEPENDS nps_core nps_bench_main nps_split_module
                                     nps_manifest_probe nps_manifest_split nps_manifest_unified)

    set(NSPIRE_LD ${CMAKE_COMMAND} -E env "PATH=${NDL_PATH}" nspire-ld)
    set(GENZEHN ${CMAKE_COMMAND} -E env "PATH=${NDL_PATH}" genzehn)

    # nspire-ld then genzehn, which is the packaging path R13 supports. genzehn reads no stdin, so
    # the </dev/null the old Makefile carried is gone rather than reproduced.
    function(add_tns name)
        cmake_parse_arguments(TNS "COMPRESS" "OUTPUT" "OBJECTS;LDFLAGS;DEPENDS" ${ARGN})
        # nps_check_hash_covers reads this rather than a second copy of the list, so an object
        # library reaching a link cannot escape the hash by someone forgetting the second edit.
        set_property(GLOBAL PROPERTY nps_tns_objects_${name} "${TNS_OBJECTS}")
        set(elf "${CMAKE_BINARY_DIR}/${name}.elf")
        set(package "${CMAKE_BINARY_DIR}/${TNS_OUTPUT}")
        # A sidecar has to reach the calculator, and a name not ending in .tns cannot: the emulator
        # link refuses it with "Didn't get 04" and the handheld file service with "write: Invalid
        # input", measured on both against a control of the same bytes under a .tns name. So the
        # digest suffix goes before the extension, for every artifact rather than one.
        nps_sidecar_name(sidecar "${TNS_OUTPUT}")
        set(sidecar "${CMAKE_BINARY_DIR}/${sidecar}")
        set(record "${CMAKE_BINARY_DIR}/${name}.offline-audit.txt")
        set(compress "")
        if(TNS_COMPRESS)
            set(compress --compress)
        endif()
        # The objects are inputs as well as arguments, and both spellings are needed. Naming only
        # the object library targets makes them order-only edges in ninja, which say when to build
        # but not when to rebuild: editing a source recompiled its object and left the packaged .tns
        # untouched, ready to be deployed stale. Naming the object files themselves is what makes
        # the link depend on their contents.
        add_custom_command(
            OUTPUT "${package}" "${sidecar}" "${record}"
            BYPRODUCTS "${elf}" "${elf}.map"
            COMMAND ${NSPIRE_LD} ${NPS_OPTIMIZED_LINK_FLAGS} ${TNS_OBJECTS} -o "${elf}" ${TNS_LDFLAGS} "-Wl,-Map,${elf}.map"
            # Before genzehn, not after. genzehn writes the .tns, so an audit that runs later fails
            # the edge but leaves a packaged image on disk with a sidecar beside it, which is exactly
            # what a deploy step looks for. Auditing the linked ELF first means a host-reaching image
            # is never packaged at all.
            COMMAND ${CMAKE_COMMAND} -E env "NPS_NDL_SDK=${NPS_NDL_SDK}"
                    "NPS_GIAC_SRC=${NPS_GIAC_SRC}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/offline-audit.sh"
                    "${CMAKE_BINARY_DIR}/offline-audit" "${elf}"
            COMMAND ${GENZEHN} --input "${elf}" --output "${package}"
                    --name ${name} ${compress} --240x320-support true
            COMMAND ${CMAKE_COMMAND} "-DINPUT=${package}" "-DOUTPUT=${sidecar}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/write-sha256.cmake"
            # Read a second time to write the record, so the digest it carries is of the package that
            # was actually produced and the verdict is recomputed against the ELF that produced it.
            COMMAND ${CMAKE_COMMAND} -E env "NPS_NDL_SDK=${NPS_NDL_SDK}"
                    "NPS_GIAC_SRC=${NPS_GIAC_SRC}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/offline-audit.sh"
                    "${CMAKE_BINARY_DIR}/offline-audit" --record "${record}"
                    --package "${package}" "${elf}"
            DEPENDS ${TNS_OBJECTS} ${TNS_DEPENDS}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/write-sha256.cmake"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/offline_audit.cc"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/offline-audit.sh"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/testdata/offline_dirty.cc"
            COMMENT "linking and packaging ${TNS_OUTPUT}"
            VERBATIM COMMAND_EXPAND_LISTS)
    endfunction()

    add_library(nps_runtime_inventory_main OBJECT tests/target/runtime_inventory.cc)
    target_include_directories(nps_runtime_inventory_main PRIVATE include "${NPS_NDL_SDK}/include")
    add_tns(nps_runtime_inventory
        OUTPUT nps_runtime_inventory.tns
        OBJECTS $<TARGET_OBJECTS:nps_runtime_inventory_main> $<TARGET_OBJECTS:nps_nspire_integrity>
                $<TARGET_OBJECTS:nps_nspire_sha256>
        DEPENDS nps_runtime_inventory_main nps_nspire_integrity nps_nspire_sha256)
    add_custom_target(runtime_inventory DEPENDS "${CMAKE_BINARY_DIR}/nps_runtime_inventory.tns")
    add_library(nps_container_probe_main OBJECT tests/target/container_probe.cc tests/ui/container_workload.cc)
    target_include_directories(nps_container_probe_main PRIVATE include tests "${NPS_NDL_SDK}/include")
    target_link_libraries(nps_container_probe_main PRIVATE nps_ui_containers)
    add_tns(nps_container_probe
        OUTPUT nps_container_probe.luax.tns
        COMPRESS
        OBJECTS $<TARGET_OBJECTS:nps_container_probe_main> $<TARGET_OBJECTS:nps_nspire_integrity>
                $<TARGET_OBJECTS:nps_nspire_sha256>
        LDFLAGS -Wl,--gc-sections
        DEPENDS nps_container_probe_main nps_nspire_integrity nps_nspire_sha256)
    add_custom_target(container_probe DEPENDS "${CMAKE_BINARY_DIR}/nps_container_probe.luax.tns")
    if(NPS_QUALIFY_RETAINED_UI)
        add_library(nps_retained_probe_main OBJECT tests/target/retained_probe.cc src/ui/bitmap.cc)
        target_include_directories(nps_retained_probe_main PRIVATE include tests "${NPS_NDL_SDK}/include")
        target_link_libraries(nps_retained_probe_main PRIVATE nps_retained_ui nps_ui_containers)
        add_tns(nps_retained_probe
            OUTPUT nps_retained_probe.luax.tns
            COMPRESS
            OBJECTS $<TARGET_OBJECTS:nps_retained_probe_main> $<TARGET_OBJECTS:nps_nspire_integrity>
                    $<TARGET_OBJECTS:nps_nspire_sha256>
            LDFLAGS $<TARGET_FILE:nps_retained_ui> $<TARGET_FILE:lvgl> -Wl,--gc-sections
            DEPENDS nps_retained_probe_main nps_nspire_integrity nps_nspire_sha256 nps_retained_ui lvgl
                    $<TARGET_FILE:nps_retained_ui> $<TARGET_FILE:lvgl>)
        add_custom_target(retained_probe DEPENDS "${CMAKE_BINARY_DIR}/nps_retained_probe.luax.tns")
    endif()

    # Section 5.2's item 6, a benchmark and smoke-test application for device profiling: a standalone
    # ndl program that runs the core on the calculator and writes what it found. No Giac, so it
    # cannot take a Lua session down with it, and it can be launched with exec. It is the one device
    # artefact that is not a Lua module, so it needs no .luax infix.
    add_tns(nps_m0_probe
        OUTPUT ${NPS_PROBE_PACKAGE}
        OBJECTS $<TARGET_OBJECTS:nps_core> $<TARGET_OBJECTS:nps_manifest_probe>
                $<TARGET_OBJECTS:nps_bench_main> $<TARGET_OBJECTS:nps_nspire_integrity>
                $<TARGET_OBJECTS:nps_nspire_sha256>
        LDFLAGS -Wl,--gc-sections -lSDL "-L${NDL_TOOLCHAIN}/lib" -lgmp
        DEPENDS nps_core nps_manifest_probe nps_bench_main nps_nspire_integrity
                nps_nspire_sha256 resident_exit_lint)
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/nps_bench.elf" "${CMAKE_BINARY_DIR}/nps_bench.tns"
               "${CMAKE_BINARY_DIR}/nps_bench.sha256.tns"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/nps_m0_probe.elf" "${CMAKE_BINARY_DIR}/nps_bench.elf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/${NPS_PROBE_PACKAGE}" "${CMAKE_BINARY_DIR}/nps_bench.tns"
        COMMAND ${CMAKE_COMMAND} "-DINPUT=${CMAKE_BINARY_DIR}/nps_bench.tns"
                "-DOUTPUT=${CMAKE_BINARY_DIR}/nps_bench.sha256.tns"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/write-sha256.cmake"
        DEPENDS "${CMAKE_BINARY_DIR}/${NPS_PROBE_PACKAGE}"
                "${CMAKE_BINARY_DIR}/${NPS_PROBE_SIDECAR}"
        COMMENT "copying the legacy probe artifact names"
        VERBATIM)
    add_custom_target(probe DEPENDS "${CMAKE_BINARY_DIR}/${NPS_PROBE_PACKAGE}"
                                    "${CMAKE_BINARY_DIR}/${NPS_PROBE_SIDECAR}"
                                    "${CMAKE_BINARY_DIR}/nps_bench.elf"
                                    "${CMAKE_BINARY_DIR}/nps_bench.tns"
                                    "${CMAKE_BINARY_DIR}/nps_bench.sha256.tns")

    # The core as a Lua module, for the two-image arrangement where Giac is a separate luagiac image.
    # nrequire("nps_split") searches the documents folder for nps_split.luax.tns and ld_execs it, and
    # main() registers the table into the OS's own Lua state. The .luax infix is not decoration:
    # ndl-sdk luaext.c:53 builds the filename as name .. ".luax.tns", so a module cannot be named
    # plain .tns and still be findable by nrequire.
    add_tns(nps_split
        OUTPUT ${NPS_SPLIT_PACKAGE}
        COMPRESS
        OBJECTS $<TARGET_OBJECTS:nps_core> $<TARGET_OBJECTS:nps_manifest_split>
                $<TARGET_OBJECTS:nps_split_module> $<TARGET_OBJECTS:nps_nspire_integrity>
                $<TARGET_OBJECTS:nps_nspire_sha256>
        LDFLAGS -Wl,--gc-sections "-L${NDL_TOOLCHAIN}/lib" -lgmp
        DEPENDS nps_core nps_manifest_split nps_split_module nps_nspire_integrity
                nps_nspire_sha256 resident_exit_lint)
    add_custom_target(luax DEPENDS "${CMAKE_BINARY_DIR}/${NPS_SPLIT_PACKAGE}"
                                   "${CMAKE_BINARY_DIR}/${NPS_SPLIT_SIDECAR}")

    # --------------------------------------------------------------------------------------------
    # Ki V4: one image with Giac linked into it
    # --------------------------------------------------------------------------------------------
    #
    # The Lua round trip through luagiac.caseval was never the design, only what was reachable while
    # the two were separate images. This makes a backend call an ordinary call: one artefact, one set
    # of static constructors, and no re-entering the interpreter while Giac runs. The luax target
    # above is untouched, so Ki V3 keeps building.

    set(GIAC_SRC "${NPS_GIAC_SRC}")
    # Shareable between build directories, because these 112 objects come from khi-src rather than
    # from this repository and cost about eighty megabytes and several minutes to make. Without that,
    # a device build of a commit rather than of the working tree is expensive enough that nobody
    # produces one, which is why the installed size in benchmarks/BUDGETS.md has never been measured
    # against a hash. Defaults to the build directory, so an ordinary build is unchanged.
    set(NPS_GIAC_OBJECT_DIR "" CACHE PATH "Directory holding the compiled Giac objects")
    if(NPS_GIAC_OBJECT_DIR STREQUAL "")
        set(GIAC_OBJECT_DIR "${CMAKE_CURRENT_BINARY_DIR}/giac-objects")
    else()
        get_filename_component(GIAC_OBJECT_DIR "${NPS_GIAC_OBJECT_DIR}" ABSOLUTE)
    endif()
    set(GIAC_QUERY ${CMAKE_COMMAND} -E env "PATH=${NDL_PATH}" make -s -C "${GIAC_SRC}"
        -f Makefile.ki -f "${CMAKE_CURRENT_SOURCE_DIR}/tools/giac-objs.mk"
        "SDK=${NPS_NDL_SDK}" "OBJDIR=${GIAC_OBJECT_DIR}")

    # Giac's object list and its configuration defines are asked for rather than copied, so a source
    # added or a flag changed over there cannot leave this linking a stale set or reading Giac's
    # headers differently from how Giac read them. Several of those defines change the layout of gen,
    # and a mismatch there is not a compile error, it is a value that arrives wrong. Makefile.ki is a
    # configure dependency so that editing it re-runs these queries.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GIAC_SRC}/Makefile.ki")
    execute_process(COMMAND ${GIAC_QUERY} print-objs
                    OUTPUT_VARIABLE GIAC_OBJ_NAMES OUTPUT_STRIP_TRAILING_WHITESPACE
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${GIAC_QUERY} print-defines
                    OUTPUT_VARIABLE GIAC_DEFINES OUTPUT_STRIP_TRAILING_WHITESPACE
                    COMMAND_ERROR_IS_FATAL ANY)
    separate_arguments(GIAC_OBJ_NAMES NATIVE_COMMAND "${GIAC_OBJ_NAMES}")
    separate_arguments(GIAC_DEFINES NATIVE_COMMAND "${GIAC_DEFINES}")

    # luabridge.o is added on top of the queried list, and luagiac.o is left out deliberately: it has
    # its own main and registers its own caseval, and this build supplies both.
    list(APPEND GIAC_OBJ_NAMES luabridge.o)
    set(GIAC_OBJECTS "")
    foreach(obj IN LISTS GIAC_OBJ_NAMES)
        list(APPEND GIAC_OBJECTS "${GIAC_OBJECT_DIR}/${obj}")
    endforeach()

    # lua_module.cc is compiled twice, once per build, because the two differ by a define and sharing
    # one object would silently give whichever was built last.
    add_library(nps_nspire_module OBJECT src/platform/nspire/lua_module.cc)
    target_compile_options(nps_nspire_module PRIVATE -Wall -Wextra)
    target_link_libraries(nps_nspire_module PRIVATE nps_ui_containers)
    if(NPS_RETAINED_UI)
        target_compile_definitions(nps_nspire_module PRIVATE NPS_RETAINED_UI=1)
        target_link_libraries(nps_nspire_module PRIVATE nps_retained_ui)
        if(NPS_RETAINED_FAULT_INJECTION)
            target_compile_definitions(nps_nspire_module PRIVATE NPS_UI_TESTING=1 NPS_RETAINED_FAULT_INJECTION=1)
        endif()
    endif()
    target_compile_definitions(nps_nspire_module PRIVATE NPS_GIAC=1 NPS_DIAG=${NPS_DIAG}
                                                         NPS_RESOURCE_PROFILE=${NPS_RESOURCE_PROFILE}
                                                         NPS_RELEASE_MANIFEST=1)
    target_include_directories(nps_nspire_module PRIVATE include "${NDL_TOOLCHAIN}/include" "${GIAC_SRC}")

    add_library(nps_nspire_integrity OBJECT src/platform/nspire/integrity.cc)

    # The probe and the split module carry no Giac, so they need Giac's sha256 object of their own.
    # The unified build must not link this one: it already gets the same object from Giac's own list,
    # and linking both would be a duplicate symbol.
    # kdisplay.h drags in the whole of Giac and redefines the COLOR_ names, so the menu wrapper is a
    # translation unit of its own rather than something lua_module.cc includes. Unified only: the
    # probe and split builds carry no Giac and so have no doMenu to call.
    add_library(nps_nspire_menu OBJECT src/platform/nspire/native_menu.cc
                                       src/platform/nspire/native_menu_policy.cc)
    target_compile_options(nps_nspire_menu PRIVATE -Wall -Wextra ${GIAC_DEFINES})
    target_include_directories(nps_nspire_menu PRIVATE
        include "${NDL_TOOLCHAIN}/include" "${GIAC_SRC}")

    add_library(nps_nspire_sha256 OBJECT "${NPS_GIAC_SRC}/sha256.c")
    target_compile_options(nps_nspire_sha256 PRIVATE -Wall -Wextra)
    target_include_directories(nps_nspire_sha256 PRIVATE "${NPS_GIAC_SRC}")
    target_compile_options(nps_nspire_integrity PRIVATE -Wall -Wextra ${GIAC_DEFINES})
    target_include_directories(nps_nspire_integrity PRIVATE
        include "${NDL_TOOLCHAIN}/include" "${GIAC_SRC}")

    set(NPS_RESOURCE_PROFILE_OBJECTS "")
    set(NPS_RESOURCE_PROFILE_TARGETS "")
    set(NPS_RESOURCE_PROFILE_LDFLAGS "")
    if(NPS_RESOURCE_PROFILE)
        add_library(nps_nspire_allocation_probe OBJECT src/platform/nspire/allocation_probe.cc)
        target_compile_options(nps_nspire_allocation_probe PRIVATE -Wall -Wextra)
        target_compile_definitions(nps_nspire_allocation_probe PRIVATE NPS_RESOURCE_PROFILE=1)
        target_include_directories(nps_nspire_allocation_probe PRIVATE
            include "${NDL_TOOLCHAIN}/include")
        add_dependencies(device nps_nspire_allocation_probe)
        list(APPEND NPS_RESOURCE_PROFILE_OBJECTS
             $<TARGET_OBJECTS:nps_nspire_allocation_probe>)
        list(APPEND NPS_RESOURCE_PROFILE_TARGETS nps_nspire_allocation_probe)
        list(APPEND NPS_RESOURCE_PROFILE_LDFLAGS
             -Wl,--wrap=malloc
             -Wl,--wrap=free
             -Wl,--wrap=realloc
             -Wl,--wrap=calloc
             -Wl,--wrap=_malloc_r
             -Wl,--wrap=_free_r
             -Wl,--wrap=_realloc_r
             -Wl,--wrap=_calloc_r)
    endif()

    # The typed adapter path. Only this build has it, because it is the only one with Giac in the
    # same image, and it is the only object here that sees Giac's headers.
    add_library(nps_giac_typed OBJECT src/cas/giac/giac_typed.cc)
    target_compile_options(nps_giac_typed PRIVATE -Wall -Wextra -fno-strict-aliasing)
    target_compile_definitions(nps_giac_typed PRIVATE NPS_GIAC=1)
    target_compile_options(nps_giac_typed PRIVATE ${GIAC_DEFINES})
    target_include_directories(nps_giac_typed PRIVATE
        include "${NDL_TOOLCHAIN}/include" "${GIAC_SRC}" "${GIAC_SRC}/..")

    # Giac's own objects are built by its own Makefile. Always out of date on purpose: that make is
    # the only thing that knows whether they are current.
    add_custom_target(giac_objects
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${NDL_PATH}" make -C "${GIAC_SRC}"
                -f Makefile.ki -f "${CMAKE_CURRENT_SOURCE_DIR}/tools/giac-objs.mk"
                "SDK=${NPS_NDL_SDK}" "OBJDIR=${GIAC_OBJECT_DIR}" ${GIAC_OBJECTS}
        BYPRODUCTS ${GIAC_OBJECTS}
        COMMENT "building Giac's objects through Makefile.ki"
        VERBATIM)

    # The objects are named as inputs of the link as well as being built above, which closes a hole
    # the old build had: there, a rebuilt Giac object relinked nothing, because the packaged .tns
    # depended only on this project's objects and the sub-make ran inside a recipe that a current
    # .tns kept from running at all. They have to exist for ninja to resolve the graph, so a
    # khi-src cleaned to nothing has to be built once with its own Makefile.ki first.

    # Section 5.2's item 3, the ndl application: the core with Giac in the same image.
    add_tns(nps_nspire
        OUTPUT ${NPS_UNIFIED_PACKAGE}
        COMPRESS
        OBJECTS $<TARGET_OBJECTS:nps_core> $<TARGET_OBJECTS:nps_manifest_unified>
                $<TARGET_OBJECTS:nps_nspire_module> $<TARGET_OBJECTS:nps_nspire_integrity>
                $<TARGET_OBJECTS:nps_giac_typed> $<TARGET_OBJECTS:nps_nspire_menu>
                ${NPS_RESOURCE_PROFILE_OBJECTS} ${GIAC_OBJECTS}
        LDFLAGS -Wl,--nspireio,--gc-sections ${NPS_RESOURCE_PROFILE_LDFLAGS}
                ${NPS_RETAINED_LINK_FLAGS}
                "-L${NDL_TOOLCHAIN}/lib" -lmpfi -lmpfr -lgmp
        DEPENDS nps_core nps_manifest_unified nps_nspire_module nps_nspire_integrity nps_giac_typed
                nps_nspire_menu ${NPS_RESOURCE_PROFILE_TARGETS} ${NPS_RETAINED_LINK_TARGETS} giac_objects ${GIAC_OBJECTS}
                resident_exit_lint)
    add_custom_target(unified DEPENDS "${CMAKE_BINARY_DIR}/${NPS_UNIFIED_PACKAGE}"
                                      "${CMAKE_BINARY_DIR}/${NPS_UNIFIED_SIDECAR}")

    # Every object each of the three links names, read off the link itself and checked against the
    # hash that artifact publishes.
    nps_check_hash_covers(nps_m0_probe NPS_PROBE_BUILD_INPUTS)
    nps_check_hash_covers(nps_split NPS_SPLIT_BUILD_INPUTS)
    nps_check_hash_covers(nps_nspire NPS_UNIFIED_BUILD_INPUTS)

    # --------------------------------------------------------------------------------------------
    # The documents that get opened on the calculator
    # --------------------------------------------------------------------------------------------

    set(LUNA "${NPS_NDL_SDK}/tools/luna/luna")
    set(DOCUMENTS "")
    foreach(doc "${NPS_V2_DOCUMENT}" "${NPS_V3_DOCUMENT}" "${NPS_V4_DOCUMENT}")
        # The stem names the Lua source and the target, the package name is what reaches the
        # calculator, and the sidecar comes from the one rule rather than a fifth spelling of it.
        nps_package_stem(stem "${doc}")
        nps_sidecar_name(doc_sidecar "${doc}")
        add_custom_command(
            OUTPUT "${CMAKE_BINARY_DIR}/${doc}" "${CMAKE_BINARY_DIR}/${doc_sidecar}"
            COMMAND "${LUNA}" "${CMAKE_CURRENT_SOURCE_DIR}/lua/${stem}.lua" "${CMAKE_BINARY_DIR}/${doc}"
            COMMAND ${CMAKE_COMMAND} "-DINPUT=${CMAKE_BINARY_DIR}/${doc}"
                    "-DOUTPUT=${CMAKE_BINARY_DIR}/${doc_sidecar}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/write-sha256.cmake"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lua/${stem}.lua"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/write-sha256.cmake"
            COMMENT "luna ${doc}"
            VERBATIM)
        add_custom_target(${stem}_document DEPENDS "${CMAKE_BINARY_DIR}/${doc}"
                                                   "${CMAKE_BINARY_DIR}/${doc_sidecar}")
        list(APPEND DOCUMENTS "${CMAKE_BINARY_DIR}/${doc}"
                              "${CMAKE_BINARY_DIR}/${doc_sidecar}")
    endforeach()
    add_custom_target(document DEPENDS ${DOCUMENTS})
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/d2_callbacks.tns" "${CMAKE_BINARY_DIR}/d2_callbacks.sha256.tns"
        COMMAND "${LUNA}" "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/d2_callbacks.lua"
                "${CMAKE_BINARY_DIR}/d2_callbacks.tns"
        COMMAND ${CMAKE_COMMAND} "-DINPUT=${CMAKE_BINARY_DIR}/d2_callbacks.tns"
                "-DOUTPUT=${CMAKE_BINARY_DIR}/d2_callbacks.sha256.tns"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/write-sha256.cmake"
        DEPENDS benchmarks/d2_callbacks.lua tools/write-sha256.cmake
        VERBATIM)
    add_custom_target(d2_callbacks_document DEPENDS "${CMAKE_BINARY_DIR}/d2_callbacks.tns"
        "${CMAKE_BINARY_DIR}/d2_callbacks.sha256.tns")
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/ui_canvas_probe.tns"
        COMMAND "${LUNA}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/target/ui_canvas_probe.lua"
                "${CMAKE_BINARY_DIR}/ui_canvas_probe.tns"
        DEPENDS tests/target/ui_canvas_probe.lua
        VERBATIM)
    add_custom_target(ui_canvas_probe_document DEPENDS "${CMAKE_BINARY_DIR}/ui_canvas_probe.tns")
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/ui_preview.tns"
        COMMAND ${CMAKE_COMMAND} "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DOUTPUT=${CMAKE_BINARY_DIR}/ui_preview.lua"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/ui_preview_document.cmake"
        COMMAND "${LUNA}" "${CMAKE_BINARY_DIR}/ui_preview.lua"
                "${CMAKE_BINARY_DIR}/ui_preview.tns"
        DEPENDS lua/nps_v4.lua tests/target/ui_preview_device.lua tests/ui/ui_preview_document.cmake
        VERBATIM)
    add_custom_target(ui_preview_document DEPENDS "${CMAKE_BINARY_DIR}/ui_preview.tns")
    if(NPS_RETAINED_UI)
        add_custom_command(
            OUTPUT "${CMAKE_BINARY_DIR}/retained_app_probe.tns"
            COMMAND "${LUNA}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/target/retained_app_probe.lua"
                    "${CMAKE_BINARY_DIR}/retained_app_probe.tns"
            DEPENDS tests/target/retained_app_probe.lua
            VERBATIM)
        add_custom_target(retained_app_probe_document DEPENDS "${CMAKE_BINARY_DIR}/retained_app_probe.tns")
        if(NPS_RETAINED_FAULT_INJECTION)
            add_custom_command(
                OUTPUT "${CMAKE_BINARY_DIR}/retained_failure.tns"
                COMMAND ${CMAKE_COMMAND} "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
                        "-DOUTPUT=${CMAKE_BINARY_DIR}/retained_failure.lua"
                        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/retained_failure_document.cmake"
                COMMAND "${LUNA}" "${CMAKE_BINARY_DIR}/retained_failure.lua"
                        "${CMAKE_BINARY_DIR}/retained_failure.tns"
                DEPENDS lua/nps_v4.lua tests/target/retained_failure.lua tests/ui/retained_failure_document.cmake
                VERBATIM)
            add_custom_target(retained_failure_document DEPENDS "${CMAKE_BINARY_DIR}/retained_failure.tns")
        endif()
    endif()
    if(NPS_QUALIFY_RETAINED_UI)
        add_custom_command(
            OUTPUT "${CMAKE_BINARY_DIR}/retained_probe.tns"
            COMMAND "${LUNA}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/target/retained_probe.lua"
                    "${CMAKE_BINARY_DIR}/retained_probe.tns"
            DEPENDS tests/target/retained_probe.lua
            VERBATIM)
        add_custom_target(retained_probe_document DEPENDS "${CMAKE_BINARY_DIR}/retained_probe.tns")
    endif()
    add_dependencies(luax nps_v3_document)
    add_dependencies(unified nps_v4_document)

else()

    # ------------------------------------------------------------------------------------------
    # The host
    # ------------------------------------------------------------------------------------------

    option(NPS_SANITIZE "build with ASan and UBSan" OFF)

    add_library(nps_flags INTERFACE)
    target_link_libraries(nps_flags INTERFACE nps_ui_containers)
    target_compile_options(nps_flags INTERFACE -O2 -g -Wall -Wextra -Wshadow)
    if(NPS_SANITIZE)
        # Two defect classes so far were invisible at -O2 and correct at -O0, so a reading pass would
        # have passed both. This is the build that sees them.
        set(SAN_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined
                      -fno-omit-frame-pointer)
        target_compile_options(nps_flags INTERFACE ${SAN_FLAGS})
        target_link_options(nps_flags INTERFACE ${SAN_FLAGS})
        if(NPS_QUALIFY_RETAINED_UI OR NPS_RETAINED_UI)
            target_compile_options(lvgl PRIVATE ${SAN_FLAGS})
            target_compile_options(nps_retained_ui PRIVATE ${SAN_FLAGS})
            target_compile_options(nps_retained_ui_test PRIVATE ${SAN_FLAGS})
            target_link_options(nps_retained_ui_test PRIVATE ${SAN_FLAGS})
            target_compile_options(nps_retained_menu_test PRIVATE ${SAN_FLAGS})
            target_link_options(nps_retained_menu_test PRIVATE ${SAN_FLAGS})
        endif()
    endif()

    nps_hash_inputs(NPS_HOST_STEP_HASH ${NPS_COMMON_MANIFEST_INPUTS})
    nps_add_manifest(nps_manifest_host host 0 0 none not-installed none absent
                     "${NPS_HOST_STEP_HASH}" "" "" "" "")
    target_link_libraries(nps_manifest_host PRIVATE nps_flags)

    nps_add_manifest(nps_manifest_probe_host probe 1 1 none not-installed none absent
                     "${NPS_HOST_STEP_HASH}" "" "" "${NPS_PROBE_SIDECAR}" "")
    target_link_libraries(nps_manifest_probe_host PRIVATE nps_flags)
    add_executable(nps_manifest_probe_test tests/unit/manifest_scheme_tests.cc
                   $<TARGET_OBJECTS:nps_manifest_probe_host>)
    target_compile_definitions(nps_manifest_probe_test PRIVATE
                               NPS_EXPECTED_ARTIFACT="probe"
                               NPS_EXPECTED_SIDECAR_SCHEME="external-sha256-sidecar")
    target_include_directories(nps_manifest_probe_test PRIVATE include)
    target_link_libraries(nps_manifest_probe_test PRIVATE nps_flags)

    nps_hash_inputs(NPS_SPLIT_HOST_STEP_HASH
        ${NPS_COMMON_MANIFEST_INPUTS}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchains/ndl-arm926ej-s.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/nspire/lua_module.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/lua/nps_v3.lua"
        ${NPS_LUNA_MANIFEST_INPUTS})
    nps_add_manifest(nps_manifest_split_host split 1 0 Giac runtime-unverified
                     lua5.1.luagiac.caseval-v1 external-required-unvalidated
                     "${NPS_SPLIT_HOST_STEP_HASH}" "" "" "${NPS_SPLIT_SIDECAR}" "${NPS_V3_SIDECAR}")
    target_link_libraries(nps_manifest_split_host PRIVATE nps_flags)
    if(NPS_RETAINED_FAULT_INJECTION)
        target_compile_definitions(nps_manifest_split_host PRIVATE NPS_RETAINED_FAULT_INJECTION=1)
    endif()
    set_target_properties(nps_manifest_split_host PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(nps_manifest_host_MANIFEST_ID STREQUAL nps_manifest_split_host_MANIFEST_ID)
        message(FATAL_ERROR "host and split capability manifest ids must differ")
    endif()
    add_executable(nps_manifest_split_test tests/unit/manifest_scheme_tests.cc
                   $<TARGET_OBJECTS:nps_manifest_split_host>)
    target_compile_definitions(nps_manifest_split_test PRIVATE
                               NPS_EXPECTED_ARTIFACT="split"
                               NPS_EXPECTED_SIDECAR_SCHEME="runtime-sha256-sidecar")
    target_include_directories(nps_manifest_split_test PRIVATE include)
    target_link_libraries(nps_manifest_split_test PRIVATE nps_flags)

    nps_add_manifest(nps_manifest_unified_host unified 1 0 Giac bundled-static
                     nps.giac.typed-v1 bundled-static
                     "${NPS_HOST_STEP_HASH}" "" "" "${NPS_UNIFIED_SIDECAR}" "${NPS_V4_SIDECAR}")
    target_link_libraries(nps_manifest_unified_host PRIVATE nps_flags)
    add_executable(nps_manifest_unified_test tests/unit/manifest_scheme_tests.cc
                   $<TARGET_OBJECTS:nps_manifest_unified_host>)
    target_compile_definitions(nps_manifest_unified_test PRIVATE
                               NPS_EXPECTED_ARTIFACT="unified"
                               NPS_EXPECTED_SIDECAR_SCHEME="runtime-sha256-sidecar")
    target_include_directories(nps_manifest_unified_test PRIVATE include)
    target_link_libraries(nps_manifest_unified_test PRIVATE nps_flags)

    add_library(nps_core OBJECT ${CORE_SOURCES})
    target_link_libraries(nps_core PUBLIC nps_flags nps_gmp)
    target_include_directories(nps_core PUBLIC include)
    # The family census the coverage join reads. Host only, so the calculator build carries neither
    # the storage nor the call.
    target_compile_definitions(nps_core PUBLIC NPS_FAMILY_CENSUS=1)

    # Section 5.2's item 2, the host test harness.
    add_executable(nps_host ${TEST_SOURCES} "${NPS_GIAC_SRC}/sha256.c")
    target_link_libraries(nps_host PRIVATE nps_core nps_manifest_host nps_flags)
    if(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(nps_host PRIVATE Threads::Threads)
    endif()
    target_include_directories(nps_host PRIVATE tests "${NPS_GIAC_SRC}")

    add_executable(nps_canvas_performance tests/ui/canvas_performance.cc src/ui/canvas.cc ${NPS_RGB_SOURCES})
    target_include_directories(nps_canvas_performance PRIVATE include)
    target_link_libraries(nps_canvas_performance PRIVATE nps_flags)
    add_test(NAME canvas_performance COMMAND nps_canvas_performance)

    add_executable(nps_container_workload_test tests/ui/container_workload_tests.cc tests/ui/container_workload.cc)
    target_include_directories(nps_container_workload_test PRIVATE include)
    target_link_libraries(nps_container_workload_test PRIVATE nps_flags)
    add_test(NAME container_workload COMMAND nps_container_workload_test)

    add_executable(nps_fuzz tests/property/fuzz.cc tests/property/fuzz_main.cc)
    target_link_libraries(nps_fuzz PRIVATE nps_core nps_manifest_host nps_flags)
    target_include_directories(nps_fuzz PRIVATE tests)

    # PRD section 19.9 and MVP criterion 15. Reads the PRD and the evidence the unit run wrote, and
    # reports which requirements have a passing check behind them. It links none of the core and
    # takes nps_flags for the warnings and the sanitizers, since it is all hand-rolled parsing.
    add_executable(nps_traceability tools/traceability.cc)
    target_link_libraries(nps_traceability PRIVATE nps_flags)

    # PRD section 27. Checks the coverage catalog against what the golden fixtures record, so a
    # family cannot claim a rule the corpus never runs.
    # It takes the schema table as well, because VER-016 asks every rule to declare one and the
    # catalog is the only inventory that names the rules a host run cannot reach.
    add_executable(nps_coverage tools/coverage.cc src/steps/schema.cc)
    target_include_directories(nps_coverage PRIVATE include)
    target_link_libraries(nps_coverage PRIVATE nps_flags)

    # A device run has no TestSink, so what it found reaches the evidence file through here. The
    # record names the packaged artifact and the digest it was read from, and this refuses to turn
    # any of it into evidence unless that artifact is in the tree and still hashes to the same thing.
    # Without that clause a transcript is a text file anyone can type.
    # It also judges the schema 2 records a handheld run leaves, which carry readings rather than
    # verdicts, so every threshold and the requirement sentences are here. device_identity.cc comes
    # with it, so PLAT-001's model and build come from the table the module itself reads them with.
    add_executable(nps_device_evidence tools/device_evidence.cc
                   src/platform/nspire/device_identity.cc "${NPS_GIAC_SRC}/sha256.c")
    target_link_libraries(nps_device_evidence PRIVATE nps_flags)
    target_include_directories(nps_device_evidence PRIVATE include "${NPS_GIAC_SRC}")

    # Writes down what a handheld run came back with, and nothing else. No threshold and no verdict
    # live here, because a reader cannot tell a verdict a writer computed from one a person typed.
    add_executable(nps_device_run tools/device_run.cc "${NPS_GIAC_SRC}/sha256.c")
    target_link_libraries(nps_device_run PRIVATE nps_flags)
    target_include_directories(nps_device_run PRIVATE "${NPS_GIAC_SRC}")

    # PRD section 22.1's acceptance corpus, and MVP criteria 2, 10 and 11 with it. Runs every corpus
    # case through the engine its family names and gates on distinct semantic cases rather than on
    # how many records the files hold. Named apart from nps_corpus_audit, which is the EPUB
    # reference-textbook audit and a different artifact entirely.
    add_executable(nps_acceptance_corpus tools/acceptance_corpus.cc)
    target_link_libraries(nps_acceptance_corpus PRIVATE nps_core nps_manifest_host nps_flags)

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(NPS_CORPUS_AUDIT "${CMAKE_BINARY_DIR}/bin/nps_corpus_audit")
    configure_file(tools/corpus_audit.py "${NPS_CORPUS_AUDIT}" COPYONLY
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    add_custom_target(nps_corpus_audit DEPENDS "${NPS_CORPUS_AUDIT}")

    # Reproduces on the host, under a sanitizer, exactly the sequence a cross-checked device call
    # runs. It has its own main and lived in the tree uncompiled, which is the same way
    # device_probe.cc once rotted, so it is built here rather than trusted to still work.
    add_executable(nps_crosspath tests/target/crosspath.cc)
    target_link_libraries(nps_crosspath PRIVATE nps_core nps_manifest_host nps_flags)
    target_include_directories(nps_crosspath PRIVATE tests)

    add_executable(nps_nspire_measurement_test tests/tools/nspire_measurement_test.cc)
    target_link_libraries(nps_nspire_measurement_test PRIVATE nps_flags nps_gmp)
    target_include_directories(nps_nspire_measurement_test PRIVATE include)

    add_executable(nps_nspire_allocation_probe_test
                   tests/tools/nspire_allocation_probe_test.cc
                   src/platform/nspire/allocation_probe.cc)
    target_compile_definitions(nps_nspire_allocation_probe_test PRIVATE
                               NPS_RESOURCE_PROFILE=1 NPS_RESOURCE_PROFILE_TESTING=1)
    target_link_libraries(nps_nspire_allocation_probe_test PRIVATE nps_flags)
    target_include_directories(nps_nspire_allocation_probe_test PRIVATE include)

    # PRD 20.2 criterion 12 wants at least 10,000 generated expressions through the parser and the
    # rewrite path. The check below runs a few hundred of the same cases so the ordinary loop stays
    # fast, and this target is the full run.
    set(FUZZ_CASES 10000 CACHE STRING "cases for the standalone fuzz run")
    set(FUZZ_SEED 1 CACHE STRING "seed for the standalone fuzz run")
    add_custom_target(fuzz_run COMMAND nps_fuzz ${FUZZ_CASES} ${FUZZ_SEED} USES_TERMINAL)

    # Rewrites every golden fixture from what the code produces now. It fails on purpose: a run that
    # writes the answer and then passes on it has checked nothing, so read the diff and then run the
    # checks to confirm.
    # nps_host exits non-zero in regold mode, on purpose, and that must not stop the reminder from
    # printing. The shell is here for the trailing or-true, which a COMMAND list cannot express.
    add_custom_target(regold
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/fixtures"
        COMMAND sh -c "NPS_REGOLD=1 '$<TARGET_FILE:nps_host>' || true"
        COMMAND ${CMAKE_COMMAND} -E echo "fixtures rewritten, review the diff and then run the checks"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS nps_host
        USES_TERMINAL VERBATIM)

    # The Lua bridge itself, built as a host shared library against LuaJIT's 5.1 headers and driven
    # with a scripted Giac under the same sanitizers. On the calculator a defect here is a reset with
    # nothing to read. The sanitizer runtime has to be in the interpreter before the library loads,
    # which is what the inserted dylib does. Skipped without luajit or its headers.
    find_program(LUAJIT_EXECUTABLE luajit)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LUAJIT QUIET luajit)
    endif()
    # Built wherever luajit is, rather than only under the sanitizer. The sanitizer answers a
    # different question, whether the language edge faults, and gating the build on it left the entry
    # points untested by anything ctest runs: the unit tests reach under them and the UI smoke
    # replaces the module with a Lua table, so nothing ran integrate_into at all.
    if(LUAJIT_EXECUTABLE AND LUAJIT_FOUND)
        add_library(nps_luax SHARED tests/target/luax_host.cc
                    src/platform/nspire/integrity.cc "${NPS_GIAC_SRC}/sha256.c"
                    ${CORE_SOURCES})
        set_target_properties(nps_luax PROPERTIES OUTPUT_NAME nps_split PREFIX "" SUFFIX .so)
        target_compile_definitions(nps_luax PRIVATE NPS_INTEGRITY_TESTING=1
                                                    NPS_RELEASE_MANIFEST=1)
        target_link_libraries(nps_luax PRIVATE nps_manifest_split_host nps_flags nps_gmp)
        target_include_directories(nps_luax PRIVATE include tests tests/target/host_os
                                   "${NPS_GIAC_SRC}" ${LUAJIT_INCLUDE_DIRS})
        target_link_options(nps_luax PRIVATE $<$<PLATFORM_ID:Darwin>:-Wl,-undefined,dynamic_lookup>)

        add_library(nps_container_probe_host SHARED tests/target/container_probe_host.cc tests/ui/container_workload.cc
                    src/platform/nspire/integrity.cc "${NPS_GIAC_SRC}/sha256.c")
        set_target_properties(nps_container_probe_host PROPERTIES PREFIX "" SUFFIX .so)
        target_include_directories(nps_container_probe_host PRIVATE include tests tests/target/host_os
                                   "${NPS_GIAC_SRC}" ${LUAJIT_INCLUDE_DIRS})
        target_link_libraries(nps_container_probe_host PRIVATE nps_flags)
        target_link_options(nps_container_probe_host PRIVATE $<$<PLATFORM_ID:Darwin>:-Wl,-undefined,dynamic_lookup>)

        add_library(nps_luax_profile SHARED tests/target/profile_host.cc
                    src/platform/nspire/integrity.cc "${NPS_GIAC_SRC}/sha256.c"
                    ${CORE_SOURCES})
        set_target_properties(nps_luax_profile PROPERTIES OUTPUT_NAME nps_profile PREFIX "" SUFFIX .so)
        target_compile_definitions(nps_luax_profile PRIVATE NPS_INTEGRITY_TESTING=1
                                    NPS_RELEASE_MANIFEST=1 NPS_RESOURCE_PROFILE=1)
        target_link_libraries(nps_luax_profile PRIVATE nps_manifest_split_host nps_flags nps_gmp)
        target_include_directories(nps_luax_profile PRIVATE include tests tests/target/host_os
                                   "${NPS_GIAC_SRC}" ${LUAJIT_INCLUDE_DIRS})
        target_link_options(nps_luax_profile PRIVATE $<$<PLATFORM_ID:Darwin>:-Wl,-undefined,dynamic_lookup>)
        if(NPS_RETAINED_UI)
            foreach(module nps_luax nps_luax_profile)
                target_compile_definitions(${module} PRIVATE NPS_RETAINED_UI=1)
                target_link_libraries(${module} PRIVATE nps_retained_ui)
                if(NPS_RETAINED_FAULT_INJECTION)
                    target_compile_definitions(${module} PRIVATE NPS_UI_TESTING=1 NPS_RETAINED_FAULT_INJECTION=1)
                endif()
            endforeach()
        endif()

        if(NPS_SANITIZE)
            execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libclang_rt.asan_osx_dynamic.dylib
                            OUTPUT_VARIABLE ASAN_DYLIB OUTPUT_STRIP_TRAILING_WHITESPACE)
            add_custom_target(luaxhost
                COMMAND ${CMAKE_COMMAND} -E env "DYLD_INSERT_LIBRARIES=${ASAN_DYLIB}"
                        ${LUAJIT_EXECUTABLE} tests/target/luax_host.lua $<TARGET_FILE:nps_luax>
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                DEPENDS nps_luax
                USES_TERMINAL)
        else()
            add_custom_target(luaxhost
                COMMAND ${LUAJIT_EXECUTABLE} tests/target/luax_host.lua $<TARGET_FILE:nps_luax>
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                DEPENDS nps_luax
                USES_TERMINAL)
        endif()

        # CTest covers the bridge contract. The luaxhost target also injects ASan when enabled.
        add_test(NAME luax_host
                 COMMAND ${LUAJIT_EXECUTABLE} tests/target/luax_host.lua $<TARGET_FILE:nps_luax>
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        set(NPS_PROFILE_HOST_ENV --unset=NPS_EVIDENCE)
        if(NPS_SANITIZE)
            list(APPEND NPS_PROFILE_HOST_ENV "DYLD_INSERT_LIBRARIES=${ASAN_DYLIB}")
        endif()
        add_test(NAME container_probe
                 COMMAND ${CMAKE_COMMAND} -E env ${NPS_PROFILE_HOST_ENV}
                         ${LUAJIT_EXECUTABLE} tests/target/container_probe_host.lua $<TARGET_FILE:nps_container_probe_host>
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME luax_profile
                 COMMAND ${CMAKE_COMMAND} -E env ${NPS_PROFILE_HOST_ENV}
                         ${LUAJIT_EXECUTABLE} tests/target/luax_host.lua $<TARGET_FILE:nps_luax_profile>
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        if(NPS_RETAINED_UI)
            if(NPS_RETAINED_FAULT_INJECTION)
                set(retained_fault_modes resource assert)
            else()
                set(retained_fault_modes disabled)
            endif()
            foreach(mode IN LISTS retained_fault_modes)
                add_test(NAME retained_fault_${mode}
                    COMMAND ${CMAKE_COMMAND} -E env ${NPS_PROFILE_HOST_ENV}
                        ${LUAJIT_EXECUTABLE} tests/target/retained_fault_host.lua $<TARGET_FILE:nps_luax> ${mode}
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            endforeach()
            add_test(NAME retained_identity COMMAND ${CMAKE_COMMAND}
                "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/retained-identity"
                "-DC_COMPILER=${CMAKE_C_COMPILER}"
                "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/retained_identity.cmake")
            add_test(NAME retained_performance_probe
                COMMAND ${LUAJIT_EXECUTABLE} tests/ui/retained_performance.lua
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            foreach(mode normal resource assert)
                if(mode STREQUAL "normal")
                    set(menu_arguments)
                else()
                    set(menu_arguments "${mode}")
                endif()
                add_test(NAME retained_lua_${mode}
                    COMMAND ${CMAKE_COMMAND} -E env ${NPS_PROFILE_HOST_ENV}
                        ${LUAJIT_EXECUTABLE} tests/target/retained_menu_host.lua $<TARGET_FILE:nps_luax> ${menu_arguments}
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            endforeach()
        endif()
        # It reads the only manifest any host test sees that carries the release target list, so it
        # is the run that can settle PLAT-009, and it appends what it settles to the file unit wrote.
        # Chained between unit and the UI run because three appenders to one file must not overlap.
        # When luajit is absent this test does not exist, and a required fixture with no setup test
        # does not block its dependents, so the rest of the chain is unaffected.
        set_tests_properties(luax_host PROPERTIES
            ENVIRONMENT "NPS_EVIDENCE=${CMAKE_BINARY_DIR}/evidence.txt"
            FIXTURES_REQUIRED nps_evidence_unit
            FIXTURES_SETUP nps_evidence_luax)
        if(NPS_SANITIZE)
            set_property(TEST luax_host APPEND PROPERTY ENVIRONMENT
                "DYLD_INSERT_LIBRARIES=${ASAN_DYLIB}")
        endif()
    else()
        add_custom_target(luaxhost
            COMMAND ${CMAKE_COMMAND} -E echo "luax host: skipped, needs luajit with headers")
    endif()

endif()

# ----------------------------------------------------------------------------------------------
# Checks, in both trees
# ----------------------------------------------------------------------------------------------

include(CTest)

# Catches the resident-module teardown bug at the language edge: a .luax main that returns instead of
# calling _exit runs static destructors on an image that stays loaded. The wrapper is a clang-query
# AST matcher and skips when clang-query is absent, the same way the UI smoke skips without a host
# lua.
#
# It is named in each packaging step's own dependencies rather than beside the .tns in the wrapper,
# which is what makes it a gate: a target named there runs to completion before the edge that links,
# so a main that returns cannot leave a packaged .tns on disk for someone to deploy. Being always out
# of date does not force a relink, because ninja reruns the edge on its file inputs, not on this.
#
# The lint cannot judge the module without the SDK headers, and a custom target has no ENVIRONMENT
# property, so the path the configure already knows is handed over through env the way the packaging
# steps hand over theirs.
set(NPS_UI_LINT_CPATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/etl/include")
add_custom_target(resident_exit_lint
    COMMAND ${CMAKE_COMMAND} -E env "NDL_SDK=${NPS_NDL_SDK}" "CPATH=${NPS_UI_LINT_CPATH}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/lint-resident-exit.sh"
    COMMENT "resident-exit lint"
    USES_TERMINAL)

# The curated clang-tidy pass over the shipped algorithmic core. Config lives in .clang-tidy, the
# file list and the gate in tools/tidy.sh. Skips when clang-tidy is absent, like the checks above.
add_custom_target(tidy
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/tidy.sh"
    COMMENT "clang-tidy"
    USES_TERMINAL)

if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME unit COMMAND nps_host)
    add_test(NAME manifest_probe COMMAND nps_manifest_probe_test)
    add_test(NAME manifest_split COMMAND nps_manifest_split_test)
    add_test(NAME manifest_unified COMMAND nps_manifest_unified_test)
    set_tests_properties(unit PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "NPS_EVIDENCE=${CMAKE_BINARY_DIR}/evidence.txt"
        FIXTURES_SETUP nps_evidence_unit)
    add_test(NAME nspire_measurement COMMAND nps_nspire_measurement_test)
    add_test(NAME nspire_allocation_probe COMMAND nps_nspire_allocation_probe_test)

    # Runs where there is no emulator and no flash image, which is the half of tools/emu worth
    # checking on a runner: whether a frame is a screen, and whether a missing image reports as a
    # skip rather than as a passing run.
    add_test(NAME emu_screen
             COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/emu_screen_test.py")

    # The UI half, driven on the host against stubbed calculator globals. Its defects used to be
    # found by opening the document on the calculator, and each one reset the device.
    find_program(LUA_HOST NAMES luajit lua)
    foreach(smoke ui_smoke ui_smoke_v3 ui_smoke_v4
                  ti_info_paint ti_info_toc ti_info_search ti_info_cards
                  ti_info_widths ti_info_numbers ti_voc_walk ti_voc_defs)
        if(LUA_HOST)
            add_test(NAME ${smoke} COMMAND "${LUA_HOST}" tests/target/${smoke}.lua)
        else()
            # An echo exits zero, so all three of these used to report Passed on a machine with no
            # interpreter, which is a suite that cannot fail reading as coverage. The property below
            # is the one thing that turns the same echo into a Skipped, and it is a plain literal
            # rather than a pattern.
            add_test(NAME ${smoke} COMMAND ${CMAKE_COMMAND} -E echo "nps ui: skipped, no host lua found")
            set_tests_properties(${smoke} PROPERTIES SKIP_REGULAR_EXPRESSION "no host lua found")
        endif()
        set_tests_properties(${smoke} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()
    # Both fixtures, not just the luax one: without luajit that test does not exist, and requiring
    # only a fixture nothing sets up would let this run before unit and append to no file.
    # The names come from the packaging variables rather than a copy of them, so the smoke test is
    # told what this build calls its unified package and its V4 document even though the host
    # configure builds neither.
    set_tests_properties(ui_smoke_v4 PROPERTIES
        ENVIRONMENT "NPS_EVIDENCE=${CMAKE_BINARY_DIR}/evidence.txt;NPS_UNIFIED_SIDECAR=${NPS_UNIFIED_SIDECAR};NPS_V4_SIDECAR=${NPS_V4_SIDECAR}"
        FIXTURES_REQUIRED "nps_evidence_unit;nps_evidence_luax"
        FIXTURES_SETUP nps_evidence_ui)
    if(TARGET nps_luax)
        add_test(NAME canvas_bridge_performance COMMAND "${LUA_HOST}"
                 "${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/canvas_bridge_performance.lua" "$<TARGET_FILE:nps_luax>")
        if(NPS_SANITIZE)
            set_property(TEST canvas_bridge_performance APPEND PROPERTY ENVIRONMENT
                "DYLD_INSERT_LIBRARIES=${ASAN_DYLIB}")
        endif()
        set_property(TEST ui_smoke_v4 APPEND PROPERTY ENVIRONMENT
            "NPS_COMMAND_MODULE=$<TARGET_FILE:nps_luax>")
        if(NPS_SANITIZE)
            set_property(TEST ui_smoke_v4 APPEND PROPERTY ENVIRONMENT
                "DYLD_INSERT_LIBRARIES=${ASAN_DYLIB}")
        endif()
    endif()

    # The lint needs the SDK headers to resolve or it cannot judge the module at all, and the
    # configure already knows where they are, so it is told rather than left to guess from the
    # source layout.
    add_test(NAME resident_exit COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/lint-resident-exit.sh")
    set_tests_properties(resident_exit PROPERTIES ENVIRONMENT "NDL_SDK=${NPS_NDL_SDK};CPATH=${NPS_UI_LINT_CPATH}")
    add_test(NAME clang_tidy COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/tidy.sh")

    # What this configure hashed into its manifests, so the check below reads the real list rather
    # than a second copy of it that could drift from the lists above.
    get_property(NPS_MANIFEST_INPUTS GLOBAL PROPERTY nps_manifest_inputs)
    list(REMOVE_DUPLICATES NPS_MANIFEST_INPUTS)
    list(SORT NPS_MANIFEST_INPUTS)
    string(REPLACE ";" "\n" NPS_MANIFEST_INPUT_LINES "${NPS_MANIFEST_INPUTS}")
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/manifest-inputs.txt"
         CONTENT "${NPS_MANIFEST_INPUT_LINES}\n")
    get_property(NPS_MANIFEST_DEFERRED GLOBAL PROPERTY nps_manifest_inputs_deferred)
    list(REMOVE_DUPLICATES NPS_MANIFEST_DEFERRED)
    list(SORT NPS_MANIFEST_DEFERRED)
    string(REPLACE ";" "\n" NPS_MANIFEST_DEFERRED_LINES "${NPS_MANIFEST_DEFERRED}")
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/manifest-inputs-deferred.txt"
         CONTENT "${NPS_MANIFEST_DEFERRED_LINES}\n")
    add_test(NAME manifest_inputs COMMAND "${CMAKE_COMMAND}"
             "-DINPUTS=${CMAKE_BINARY_DIR}/manifest-inputs.txt"
             "-DDEFERRED=${CMAKE_BINARY_DIR}/manifest-inputs-deferred.txt"
             "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
             -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-manifest-inputs.cmake")
    add_test(NAME manifest_inputs_check COMMAND "${CMAKE_COMMAND}"
             "-DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/tools/check-manifest-inputs.cmake"
             "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
             "-DWORK_DIR=${CMAKE_BINARY_DIR}/manifest-inputs-check"
             -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/manifest_inputs_check_test.cmake")
    add_test(NAME lua_bridge_prologue COMMAND "${Python3_EXECUTABLE}"
             "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/lua_bridge_prologue_test.py"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/nspire/lua_module.cc")
    add_test(NAME corpus_audit COMMAND "${Python3_EXECUTABLE}"
             "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/corpus_audit_test.py" "${NPS_CORPUS_AUDIT}")

    # A device build is a separate configure with a separate compiler, so the host suite cannot run
    # the offline audit itself. It reads what that build left behind instead, and nps_device_evidence
    # refuses a record whose package no longer hashes to the digest written beside it.
    set(NPS_DEVICE_BUILD_DIR "${CMAKE_BINARY_DIR}/../device" CACHE PATH
        "the device build tree whose offline audit records this suite ingests")
    # One test that reads the directory when it runs, rather than one test per record registered from
    # a glob. A glob only re-runs when cmake does, so a record written after the last configure went
    # unread and its requirements reported unmet: 83 of 102 with two valid records sitting on disk,
    # 84 once cmake had run again. That is an evidence gate that lies by omission.
    add_test(NAME giac_runtime_names
             COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/giac_runtime_names_test.py"
                     "${NPS_GIAC_SRC}/kdisplay.cc" --compiler "${CMAKE_CXX_COMPILER}")

    add_test(NAME device_evidence COMMAND nps_device_evidence --sweep "${NPS_DEVICE_BUILD_DIR}")
    # It appends down the same chain the host appenders use, because they all append to one file and
    # a fixture is what keeps two of them out of it at once. One test means one link rather than one
    # per record, so the chain no longer changes shape when a record appears.
    set_tests_properties(device_evidence PROPERTIES
        ENVIRONMENT "NPS_EVIDENCE=${CMAKE_BINARY_DIR}/evidence.txt"
        FIXTURES_REQUIRED nps_evidence_coverage
        FIXTURES_SETUP nps_evidence_device)
    set(NPS_EVIDENCE_TAIL nps_evidence_device)

    # These two arrived with their scripts at 12621ad and nothing has ever referenced them, so
    # neither has run. Each needs something a host build does not have, so each is discovered the way
    # the device records above are, and an absence is named rather than passed over.
    if(EXISTS "${NPS_NDL_SDK}")
        add_test(NAME bootstrap_scripts
                 COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/bootstrap_scripts_test.sh")
        set_tests_properties(bootstrap_scripts PROPERTIES
                             ENVIRONMENT "NDL_SDK=${NPS_NDL_SDK}")
    else()
        message(STATUS "nps: no ndl SDK at ${NPS_NDL_SDK}, so bootstrap-host.sh and "
                       "bootstrap-ndl.sh go untested here")
    endif()

    # Makefile.ki names the parser and lexer generators the package build shells out to, and a name
    # that is wrong for this host does not surface until the whole Giac tree has already compiled.
    # Generating the parser costs a fraction of a second, so the answer is had here instead.
    if(EXISTS "${NPS_GIAC_SRC}/Makefile.ki")
        add_test(NAME giac_parser_tools
                 COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/giac_parser_tools_test.sh"
                 "${NPS_GIAC_SRC}" "${CMAKE_CURRENT_BINARY_DIR}/giac-parser-tools")
    else()
        message(STATUS "nps: no Makefile.ki under ${NPS_GIAC_SRC}, so its generator selection goes "
                       "untested here")
    endif()

    # Globbed rather than tested with EXISTS, so a device build done after this configure brings the
    # check with it instead of waiting for an unrelated reconfigure.
    file(GLOB NPS_DEVICE_IMAGE CONFIGURE_DEPENDS "${NPS_DEVICE_BUILD_DIR}/nps_nspire.elf")
    if(NPS_DEVICE_IMAGE AND EXISTS "${NPS_NDL_SDK}")
        add_test(NAME report_size
                 COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/report_size_test.sh"
                 "${NPS_DEVICE_IMAGE}")
        set_tests_properties(report_size PROPERTIES ENVIRONMENT "NDL_SDK=${NPS_NDL_SDK}")
    else()
        message(STATUS "nps: no ARM image under ${NPS_DEVICE_BUILD_DIR}, so report-size.sh goes "
                       "untested here")
    endif()

    # Unit replaces the evidence, V4 appends to it, then traceability reads it.
    add_test(NAME traceability COMMAND nps_traceability
             "${CMAKE_CURRENT_SOURCE_DIR}/../docs/StepCAS_Product_Requirements_Document.md"
             "${CMAKE_BINARY_DIR}/evidence.txt" "${CMAKE_CURRENT_SOURCE_DIR}/catalog/families.md"
             "${CMAKE_BINARY_DIR}/traceability.md")
    set_tests_properties(traceability PROPERTIES FIXTURES_REQUIRED ${NPS_EVIDENCE_TAIL})

    add_test(NAME coverage COMMAND nps_coverage "${CMAKE_CURRENT_SOURCE_DIR}/catalog/families.md"
             "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/fixtures"
             "${CMAKE_BINARY_DIR}/coverage.md")
    # The catalog is where a family declares its envelope, so MATH-013 is answered here and appended
    # after the corpus, which is the last group written before the report reads the file.
    set_tests_properties(coverage PROPERTIES
        ENVIRONMENT "NPS_EVIDENCE=${CMAKE_BINARY_DIR}/evidence.txt"
        FIXTURES_REQUIRED nps_evidence_corpus
        FIXTURES_SETUP nps_evidence_coverage)

    # Nine refusals, each made to fire on a record built to trip it, before the one acceptance is
    # believed. A gate that has only ever accepted is indistinguishable from no gate.
    add_test(NAME device_evidence_selftest COMMAND nps_device_evidence --selftest)

    # Every threshold driven past on both sides, and the refusals with them. A writer that has only
    # ever emitted pass cannot be told from one that emits pass whatever the readings say.
    add_test(NAME device_run_selftest COMMAND nps_device_run --selftest)

    # The catalog's none convention, on a staged catalog rather than on the one in the tree, because
    # no family relies on it for obligations today and a reader with no live user is untested unless
    # something stages one.
    add_test(NAME coverage_selftest COMMAND nps_coverage --selftest)

    # Staged PRDs and catalogs, because the ones in the tree list each requirement once and carry a
    # single universal module scope. Every arm of that scope reader is driven from here: the domain
    # it reads, the two shapes it refuses as malformed, the row it declines as not module-scoped and
    # the domain no catalog family matches.
    add_test(NAME traceability_selftest COMMAND nps_traceability --selftest)

    # Section 19.9 again: evidence that nothing compares is not evidence. Each fixture loses its
    # claim line in turn and the suite has to fail while it is gone.
    add_test(NAME evidence_mutation
             COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/evidence-mutation.sh"
             "$<TARGET_FILE:nps_host>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/fixtures")

    # Section 22.1's corpus, run case by case. This one fails on a case whose declared outcome or
    # answer the engine no longer produces, which is a regression.
    add_test(NAME acceptance_corpus COMMAND nps_acceptance_corpus
             "${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus"
             "${CMAKE_BINARY_DIR}/acceptance_corpus.md")
    # It also writes what the corpus evidences, which is why it sits between the UI run and the
    # traceability report. The minimums run below cannot do this: it fails on purpose, and a fixture
    # that fails takes every test depending on it out of the run.
    set_tests_properties(acceptance_corpus PROPERTIES
        ENVIRONMENT "NPS_EVIDENCE=${CMAKE_BINARY_DIR}/evidence.txt"
        FIXTURES_REQUIRED nps_evidence_ui
        FIXTURES_SETUP nps_evidence_corpus)

    # The same run, gating on the section 22.1 minimums instead. It is a test of its own so a
    # shortfall against those minimums cannot be read as a regression or the other way round.
    add_test(NAME acceptance_corpus_minimums COMMAND nps_acceptance_corpus
             "${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus"
             "${CMAKE_BINARY_DIR}/acceptance_corpus_minimums.md" --gate)

    # The five section 20.2 invariants and the two PRD requirements checked the same way, each
    # broken in turn on a record built for it, so none is a check that has only ever passed.
    add_test(NAME acceptance_corpus_selftest COMMAND nps_acceptance_corpus --selftest)

    # And the corpus held to the same standard as the fixtures: a declared outcome is corrupted, a
    # declared answer is corrupted, a family is removed, the corpus is padded with renumbered copies
    # of a case it already holds, and cases it already holds are rewritten under another letter. The
    # audit has to notice the first three and leave the distinct count alone for the last two.
    add_test(NAME corpus_mutation
             COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/corpus-mutation.sh"
             "$<TARGET_FILE:nps_acceptance_corpus>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus")

    # check is the everyday target. Verbose rather than --output-on-failure because each of these
    # prints its own count, and watching those numbers move is most of what the loop is for.
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} -V --test-dir "${CMAKE_BINARY_DIR}"
        DEPENDS nps_host nps_canvas_performance nps_container_workload_test nps_nspire_measurement_test nps_nspire_allocation_probe_test
                nps_manifest_probe_test nps_manifest_split_test nps_manifest_unified_test nps_traceability nps_coverage nps_corpus_audit nps_device_evidence
                nps_acceptance_corpus nps_device_run
        USES_TERMINAL)
    if(TARGET nps_retained_ui_test)
        add_dependencies(check nps_retained_ui_test nps_retained_menu_test)
    endif()
    # The luax run is the first appender to the evidence file, so a tree where its module was never
    # built loses that test and every test chained behind it.
    if(TARGET nps_luax)
        add_dependencies(check nps_luax nps_luax_profile nps_container_probe_host)
    endif()
    set(GIAC_HOST_ROOT "" CACHE PATH "Configured host Giac source directory for runtime tests")
    if(GIAC_HOST_ROOT)
        add_subdirectory(tests/giac)
add_dependencies(check matrix_capture guard_cleanup typed_differential matrix_cancellation determinant_capture lua_matrix value_layout)
        set_property(TEST ui_smoke_v4 APPEND PROPERTY FIXTURES_REQUIRED determinant_records)
        set_property(TEST ui_smoke_v4 APPEND PROPERTY ENVIRONMENT
            "NPS_DETERMINANT_RECORDS=${CMAKE_BINARY_DIR}/determinant-records.lua")
    endif()
endif()
