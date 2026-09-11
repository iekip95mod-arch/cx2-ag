# The TI-Nspire CX II cross toolchain, as section 5.2 of the agent pack asks for it: the compiler is
# nspire-g++ and packaging goes out through nspire-ld and genzehn, so what gets exercised is the
# supported ndl path rather than a hand-rolled equivalent of it.
#
# Configure a device tree with:
#   cmake -S stepcas -B stepcas/build/device -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ndl-arm926ej-s.cmake

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm926ej-s)

if(NOT NDL_SDK)
    get_filename_component(NDL_SDK "${CMAKE_CURRENT_LIST_DIR}/../../../vendor/ndl-src/ndl-sdk" ABSOLUTE)
endif()
if(NOT EXISTS "${NDL_SDK}/bin/nspire-g++")
    message(FATAL_ERROR "no ndl SDK at ${NDL_SDK}. Pass -DNDL_SDK=<path>.")
endif()

# try_compile re-reads this file in a fresh process carrying only the variables named here, so
# without this the documented -DNDL_SDK passes configure and then fails the compiler test with the
# default path in the message. It only worked from a checkout with ndl-src beside it.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES NDL_SDK)

set(NDL_SDK_BIN "${NDL_SDK}/bin")
set(NDL_TOOLCHAIN "${NDL_SDK}/toolchain/install")

# nspire-g++ is a shell wrapper that runs nspire-tools to find the SDK, so nspire-tools has to
# be on PATH whenever the compiler runs. Two processes need that and neither fix covers the other:
# the ENV line is this configure process and the try_compile children it spawns, the launcher is
# ninja, which is started later and inherits nothing set here.
#
# The two get different values on purpose. Configure time keeps the caller's PATH, because CMake also
# searches it for ninja. The launcher does not, because its arguments become part of every compile
# command ninja records, and folding $ENV{PATH} in there rebuilds the whole tree whenever a shell
# with a different PATH runs the build. The two SDK directories plus the standard base is everything
# the wrapper chain reaches for: nspire-tools is a bash script using uname, dirname, readlink and tr.
set(ENV{PATH} "${NDL_SDK_BIN}:${NDL_TOOLCHAIN}/bin:$ENV{PATH}")
set(NDL_PATH "${NDL_SDK_BIN}:${NDL_TOOLCHAIN}/bin:/usr/bin:/bin:/usr/sbin:/sbin")
set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env "PATH=${NDL_PATH}")

set(CMAKE_C_COMPILER "${NDL_SDK_BIN}/nspire-gcc")
set(CMAKE_CXX_COMPILER "${NDL_SDK_BIN}/nspire-g++")

# The binutils names are prefixed, so CMake's search for a bare ar finds the host's: measured, it
# caches /usr/bin/ar and /usr/bin/ranlib. Nothing archives in this build today, every target is an
# object library or a custom command, so this bites nobody yet. It is here so that a static library
# added later is archived by the cross ar rather than silently by the host's.
set(CMAKE_AR "${NDL_TOOLCHAIN}/bin/arm-none-eabi-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${NDL_TOOLCHAIN}/bin/arm-none-eabi-ranlib" CACHE FILEPATH "")

set(CMAKE_CXX_FLAGS_INIT "-Os -marm -fno-exceptions -fno-rtti")

set(CMAKE_FIND_ROOT_PATH "${NDL_TOOLCHAIN}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
