# vim: ts=2 shiftwidth=2 filetype=CMAKE

#
# Sample command lines:
#
#     cd make
#     mkdir build
#     cd build
#     CMake .. -G "Visual Studio 14 Win64" -DR3_OS_ID=0.3.40
#          -DR3_EXTERNAL_FFI=yes -DR3_CPP=no
#

cmake_minimum_required (VERSION 2.8)

set (CMAKE_OSX_DEPLOYMENT_TARGET 10.7)

project (Rebol3 C CXX)


# Simplify appending flags to the C build settings, C++ settings, or both

macro(add_cxx_flags flags)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flags}")
endmacro()

macro(add_c_flags flags)
    set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} ${flags}")
endmacro()

macro(add_c_and_cxx_flags flags)
    set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} ${flags}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flags}")
endmacro()



set (TOP_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set (CORE_DIR "${TOP_SRC_DIR}/core")
set (OS_DIR "${TOP_SRC_DIR}/os")
set (AGG_DIR "${TOP_SRC_DIR}/agg")
set (TOOLS_DIR "${TOP_SRC_DIR}/tools")
set (EXTERNAL_DIR "${TOP_SRC_DIR}/../external")
set (FFI_DIR "${EXTERNAL_DIR}/libffi")
if(CMAKE_HOST_WIN32)
    set (REBOL "${CMAKE_CURRENT_BINARY_DIR}/r3-make.exe")
else()
    set (REBOL "${CMAKE_CURRENT_BINARY_DIR}/r3-make")
endif()

option(R3_EXTERNAL_FFI "Build with external FFI" OFF)
option(R3_CPP "Build C files as C++" OFF)

if (NOT EXISTS ${REBOL})
    message(FATAL_ERROR "${REBOL} doesn't exist, an executable r3 is required")
endif()

if (NOT DEFINED R3_OS_ID)
    message("Trying to obtain OS_ID from r3-make, as R3_OS_ID is not defined ...")
    execute_process(COMMAND ${REBOL} --do "print rejoin [\"0.\" system/version/4 \".\" system/version/5]" OUTPUT_VARIABLE R3_OS_ID)
    string(STRIP ${R3_OS_ID} R3_OS_ID)
endif()

if (NOT DEFINED R3_OS_ID)
    message(FATAL_ERROR "R3_OS_ID is not defined")
endif()
message("Building Rebol 3 for OS_ID: ${R3_OS_ID}")

set(COMMON_MACROS REB_EXE)
if (WIN32)
    list (APPEND COMMON_MACROS
        WIN32
        TO_WINDOWS
        ENDIAN_LITTLE
        UNICODE
        _UNICODE
        )
    set(LIBS wsock32 comdlg32)
elseif (APPLE)
    list (APPEND COMMON_MACROS
        ENDIAN_LITTLE
        HAS_LL_CONSTS
        )
elseif (UNIX)
    if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
        list (APPEND COMMON_MACROS
            TO_LINUX
            TO_LINUX_X86
            ENDIAN_LITTLE
            HAS_LL_CONSTS
            )
        set(LIBS m dl)
    else ()
        message(FATAL_ERROR "Unsupported Unix system: ${CMAKE_SYSTEM_NAME}")
    endif ()
else ()
    message(FATAL_ERROR "Unsupported system")
endif ()

if (R3_OS_ID STREQUAL "0.2.5")
    list (APPEND COMMON_MACROS
        TO_OSX_X86
        )
    set(CMAKE_OSX_ARCHITECTURE i386)
elseif (R3_OS_ID STREQUAL "0.2.40")
    list (APPEND COMMON_MACROS
        TO_OSX_X64
        __LP64__
        )
    set(CMAKE_OSX_ARCHITECTURE x86_64)
elseif (R3_OS_ID STREQUAL "0.3.1")
    list (APPEND COMMON_MACROS
        TO_WINDOWS_X86
        )
elseif (R3_OS_ID STREQUAL "0.3.40")
    list (APPEND COMMON_MACROS
        TO_WINDOWS_X64
        __LLP64__
        )
elseif (R3_OS_ID STREQUAL "0.4.4")
    list (APPEND COMMON_MACROS
        TO_LINUX_X86
        )
elseif (R3_OS_ID STREQUAL "0.4.20") #linux arm
    list (APPEND COMMON_MACROS
        TO_LINUX_ARM
        )
elseif (R3_OS_ID STREQUAL "0.4.40") #linux x86_64
    list (APPEND COMMON_MACROS
        TO_LINUX_X64
        __LLP64__
        )
endif ()

if (MSVC)
    set (LINK_FLAGS /STACK:4194304)

    # !!! At the moment, there are many places where in the 64-bit build, a
    # 64-bit integer is used in places where a 32-bit integer is expected.
    # Ren-C intends to use 64-bit series indices in 64-bit builds, but that
    # just hasn't been done yet.
    #
    # (Note: /WD is "Warning Disable")
    #
    add_c_and_cxx_flags(/wd4244) # possible data loss in argument conversion
    add_c_and_cxx_flags(/wd4267) # possible data loss in initialization

    # MSVC complains if you use old-style functions like `strcpy` instead of
    # `strcpy_s`.  There should be a review of these cases, but for now they
    # are allowed as-is
    #
    add_c_and_cxx_flags(-D_CRT_SECURE_NO_WARNINGS)
endif()

if (WIN32 OR CYGWIN)
    set (CORE_PLATFORM_SOURCE
        ${OS_DIR}/windows/host-lib.c
        ${OS_DIR}/windows/dev-stdio.c
        ${OS_DIR}/windows/dev-file.c
        ${OS_DIR}/windows/dev-serial.c
        ${OS_DIR}/windows/dev-event.c
        ${OS_DIR}/windows/dev-clipboard.c
        )
else ()
    set (CORE_PLATFORM_SOURCE
        ${OS_DIR}/generic/host-memory.c
        ${OS_DIR}/generic/host-locale.c
        ${OS_DIR}/generic/iso-639.c
        ${OS_DIR}/generic/iso-3166.c
        ${OS_DIR}/generic/host-gob.c
        ${OS_DIR}/posix/host-readline.c
        ${OS_DIR}/posix/dev-stdio.c
        ${OS_DIR}/posix/dev-event.c
        ${OS_DIR}/posix/dev-file.c
        ${OS_DIR}/posix/host-browse.c
        ${OS_DIR}/posix/host-config.c
        ${OS_DIR}/posix/host-error.c
        ${OS_DIR}/posix/host-library.c
        ${OS_DIR}/posix/host-process.c
        ${OS_DIR}/posix/host-thread.c
        ${OS_DIR}/posix/host-time.c
        )
    if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
        set (CORE_PLATFORM_SOURCE ${CORE_PLATFORM_SOURCE}
            # Linux has support for ELF format encapping
            ${OS_DIR}/linux/host-encap.c

            # There is a Linux serial device
            ${OS_DIR}/linux/dev-serial.c

            # Linux supports siginfo_t-style signals
            ${OS_DIR}/linux/dev-signal.c
            )
    else ()
        list (APPEND CORE_PLATFORM_SOURCE
            ${OS_DIR}/stub/host-encap.c
            )
    endif ()
endif ()

if (R3_EXTERNAL_FFI)
    find_package(PkgConfig)
    pkg_search_module(FFI libffi)
    if (${FFI_FOUND})
        set (COMMON_MACROS ${COMMON_MACROS} HAVE_LIBFFI_AVAILABLE)
    else ()
        message(WARNING "FFI is not found, External Library Access will not work!")
    endif ()
else ()
    if (XCODE) #Xcode has problem with EXCLUDE_FROM_ALL
        add_subdirectory(${FFI_DIR} ${CMAKE_CURRENT_BINARY_DIR}/ffi)
    else ()
        add_subdirectory(${FFI_DIR} ${CMAKE_CURRENT_BINARY_DIR}/ffi EXCLUDE_FROM_ALL)
    endif ()
    set (COMMON_MACROS ${COMMON_MACROS} HAVE_LIBFFI_AVAILABLE FFI_BUILDING)
    set (FFI_LIBRARIES ffi_s)
endif ()

#CORE
set (CORE_SOURCE
    ${CORE_DIR}/a-constants.c
    ${CORE_DIR}/a-globals.c
    ${CORE_DIR}/a-lib.c
    ${CORE_DIR}/a-stubs.c
    ${CORE_DIR}/b-boot.c
    ${CORE_DIR}/b-init.c
    ${CORE_DIR}/c-bind.c
    ${CORE_DIR}/c-do.c
    ${CORE_DIR}/c-eval.c
    ${CORE_DIR}/c-error.c
    ${CORE_DIR}/c-frame.c
    ${CORE_DIR}/c-function.c
    ${CORE_DIR}/c-path.c
    ${CORE_DIR}/c-port.c
    ${CORE_DIR}/c-signal.c
    ${CORE_DIR}/c-task.c
    ${CORE_DIR}/c-word.c
    ${CORE_DIR}/c-value.c
    ${CORE_DIR}/d-break.c
    ${CORE_DIR}/d-crash.c
    ${CORE_DIR}/d-dump.c
    ${CORE_DIR}/d-eval.c
    ${CORE_DIR}/d-legacy.c
    ${CORE_DIR}/d-print.c
    ${CORE_DIR}/d-stack.c
    ${CORE_DIR}/d-trace.c
    ${CORE_DIR}/f-blocks.c
    ${CORE_DIR}/f-deci.c
    ${CORE_DIR}/f-dtoa.c
    ${CORE_DIR}/f-enbase.c
    ${CORE_DIR}/f-extension.c
    ${CORE_DIR}/f-int.c
    ${CORE_DIR}/f-math.c
    ${CORE_DIR}/f-modify.c
    ${CORE_DIR}/f-qsort.c
    ${CORE_DIR}/f-random.c
    ${CORE_DIR}/f-round.c
    ${CORE_DIR}/f-series.c
    ${CORE_DIR}/f-stubs.c
    ${CORE_DIR}/l-scan.c
    ${CORE_DIR}/l-types.c
    ${CORE_DIR}/m-gc.c
    ${CORE_DIR}/m-pools.c
    ${CORE_DIR}/m-series.c
    ${CORE_DIR}/m-stacks.c
    ${CORE_DIR}/n-control.c
    ${CORE_DIR}/n-data.c
    ${CORE_DIR}/n-io.c
    ${CORE_DIR}/n-loop.c
    ${CORE_DIR}/n-math.c
    ${CORE_DIR}/n-reduce.c
    ${CORE_DIR}/n-sets.c
    ${CORE_DIR}/n-strings.c
    ${CORE_DIR}/n-system.c
    ${CORE_DIR}/p-clipboard.c
    ${CORE_DIR}/p-console.c
    ${CORE_DIR}/p-dir.c
    ${CORE_DIR}/p-dns.c
    ${CORE_DIR}/p-event.c
    ${CORE_DIR}/p-file.c
    ${CORE_DIR}/p-net.c
    ${CORE_DIR}/p-serial.c
    ${CORE_DIR}/p-signal.c
    ${CORE_DIR}/s-cases.c
    ${CORE_DIR}/s-crc.c
    ${CORE_DIR}/s-file.c
    ${CORE_DIR}/s-find.c
    ${CORE_DIR}/s-make.c
    ${CORE_DIR}/s-mold.c
    ${CORE_DIR}/s-ops.c
    ${CORE_DIR}/s-trim.c
    ${CORE_DIR}/s-unicode.c
    ${CORE_DIR}/t-bitset.c
    ${CORE_DIR}/t-block.c
    ${CORE_DIR}/t-char.c
    ${CORE_DIR}/t-datatype.c
    ${CORE_DIR}/t-date.c
    ${CORE_DIR}/t-decimal.c
    ${CORE_DIR}/t-event.c
    ${CORE_DIR}/t-function.c
    ${CORE_DIR}/t-gob.c
    ${CORE_DIR}/t-image.c
    ${CORE_DIR}/t-integer.c
    ${CORE_DIR}/t-library.c
    ${CORE_DIR}/t-logic.c
    ${CORE_DIR}/t-map.c
    ${CORE_DIR}/t-money.c
    ${CORE_DIR}/t-none.c
    ${CORE_DIR}/t-object.c
    ${CORE_DIR}/t-pair.c
    ${CORE_DIR}/t-port.c
    ${CORE_DIR}/t-routine.c
    ${CORE_DIR}/t-string.c
    ${CORE_DIR}/t-struct.c
    ${CORE_DIR}/t-time.c
    ${CORE_DIR}/t-tuple.c
    ${CORE_DIR}/t-typeset.c
    ${CORE_DIR}/t-utype.c
    ${CORE_DIR}/t-varargs.c
    ${CORE_DIR}/t-vector.c
    ${CORE_DIR}/t-word.c
    ${CORE_DIR}/u-bmp.c
    ${CORE_DIR}/u-compress.c
    ${CORE_DIR}/u-dialect.c
    ${CORE_DIR}/u-gif.c
    ${CORE_DIR}/u-jpg.c
    ${CORE_DIR}/u-md5.c
    ${CORE_DIR}/u-parse.c
    ${CORE_DIR}/u-png.c
    ${CORE_DIR}/u-sha1.c
    ${CORE_DIR}/u-zlib.c
    ${CORE_DIR}/../codecs/aes/aes.c
    ${CORE_DIR}/../codecs/bigint/bigint.c
    ${CORE_DIR}/../codecs/dh/dh.c
    ${CORE_DIR}/../codecs/png/lodepng.c
    ${CORE_DIR}/../codecs/rc4/rc4.c
    ${CORE_DIR}/../codecs/rsa/rsa.c
    )

file(GLOB CORE_C_FILES ${CORE_DIR}/*.c)
list(REMOVE_ITEM CORE_C_FILES ${CORE_DIR}/a-lib2.c) #see make-headers.r
#message("CORE_C_FILES: ${CORE_C_FILES}")

set(NATIVE_OUTPUT
    ${TOP_SRC_DIR}/boot/tmp-natives.r
    ${TOP_SRC_DIR}/boot/tmp-actions.r
    )
set(NATIVE_DEPENDS "")

add_custom_command(OUTPUT
    ${NATIVE_OUTPUT}
    COMMAND ${REBOL} ${TOOLS_DIR}/make-natives.r
    DEPENDS
    ${REBOL} ${TOOLS_DIR}/make-natives.r
    ${NTIVE_DEPENDS}
)

set(HEADER_OUTPUT
    ${TOP_SRC_DIR}/include/tmp-funcs.h
    ${TOP_SRC_DIR}/include/tmp-funcargs.h
    ${TOP_SRC_DIR}/include/tmp-strings.h
    )
set(HEADER_DEPENDS
    ${TOOLS_DIR}/common.r
    ${TOOLS_DIR}/common-parsers.r
    ${TOOLS_DIR}/form-header.r
    ${CORE_C_FILES}
    ${NATIVE_OUTPUT}
    )

add_custom_command(OUTPUT
    ${HEADER_OUTPUT}
    COMMAND ${REBOL} ${TOOLS_DIR}/make-headers.r
    DEPENDS
    ${TOOLS_DIR}/make-headers.r
    ${HEADER_DEPENDS}
    )

set(BOOT_OUTPUT
    ${TOP_SRC_DIR}/include/tmp-evaltypes.inc
    ${TOP_SRC_DIR}/include/tmp-maketypes.h
    ${TOP_SRC_DIR}/include/tmp-comptypes.h
    ${TOP_SRC_DIR}/include/reb-types.h
    ${TOP_SRC_DIR}/include/ext-types.h
    ${TOP_SRC_DIR}/include/tmp-exttypes.h
    ${TOP_SRC_DIR}/include/tmp-bootdefs.h
    ${TOP_SRC_DIR}/include/tmp-sysobj.h
    ${TOP_SRC_DIR}/include/reb-dialect.h
    ${TOP_SRC_DIR}/include/reb-evtypes.h
    ${TOP_SRC_DIR}/include/tmp-errnums.h
    ${TOP_SRC_DIR}/include/tmp-portmodes.h
    ${TOP_SRC_DIR}/include/tmp-sysctx.h
    ${TOP_SRC_DIR}/include/tmp-boot.h
    ${TOP_SRC_DIR}/core/b-boot.c
    )
set(BOOT_DEPENDS
    # all of the files loaded by make-boot.r
    ${TOP_SRC_DIR}/boot/version.r
    ${TOP_SRC_DIR}/boot/types.r
    ${TOP_SRC_DIR}/boot/types-ext.r
    ${TOP_SRC_DIR}/boot/strings.r
    ${TOP_SRC_DIR}/boot/errors.r
    ${TOP_SRC_DIR}/boot/words.r
    ${TOP_SRC_DIR}/boot/modes.r
    ${TOP_SRC_DIR}/boot/tmp-actions.r
    ${TOP_SRC_DIR}/boot/sysobj.r
    ${TOP_SRC_DIR}/boot/platforms.r

    ${TOP_SRC_DIR}/mezz/boot-files.r

    ${TOP_SRC_DIR}/boot/booters.r
    ${TOP_SRC_DIR}/boot/tmp-natives.r
    ${TOP_SRC_DIR}/boot/typespec.r
    ${TOP_SRC_DIR}/boot/root.r
    ${TOP_SRC_DIR}/boot/task.r
    ${TOP_SRC_DIR}/boot/ops.r

    # all of the files in boot-files.r
    ${TOP_SRC_DIR}/mezz/base-constants.r
    ${TOP_SRC_DIR}/mezz/base-funcs.r
    ${TOP_SRC_DIR}/mezz/base-infix.r
    ${TOP_SRC_DIR}/mezz/base-series.r
    ${TOP_SRC_DIR}/mezz/base-files.r
    ${TOP_SRC_DIR}/mezz/base-debug.r
    ${TOP_SRC_DIR}/mezz/base-defs.r

    ${TOP_SRC_DIR}/mezz/sys-base.r
    ${TOP_SRC_DIR}/mezz/sys-ports.r
    ${TOP_SRC_DIR}/mezz/sys-codec.r # export to lib!
    ${TOP_SRC_DIR}/mezz/sys-load.r
    ${TOP_SRC_DIR}/mezz/sys-start.r

    ${TOP_SRC_DIR}/mezz/mezz-types.r
    ${TOP_SRC_DIR}/mezz/mezz-func.r
    ${TOP_SRC_DIR}/mezz/mezz-debug.r
    ${TOP_SRC_DIR}/mezz/mezz-control.r
    ${TOP_SRC_DIR}/mezz/mezz-save.r
    ${TOP_SRC_DIR}/mezz/mezz-series.r
    ${TOP_SRC_DIR}/mezz/mezz-files.r
    ${TOP_SRC_DIR}/mezz/mezz-shell.r
    ${TOP_SRC_DIR}/mezz/mezz-math.r
    ${TOP_SRC_DIR}/mezz/mezz-help.r # move dump-obj!
    ${TOP_SRC_DIR}/mezz/mezz-banner.r
    ${TOP_SRC_DIR}/mezz/mezz-colors.r
    ${TOP_SRC_DIR}/mezz/mezz-tail.r
    ${TOP_SRC_DIR}/mezz/mezz-legacy.r

    ${TOP_SRC_DIR}/mezz/prot-http.r
    ${TOP_SRC_DIR}/mezz/prot-tls.r
    )

add_custom_command(OUTPUT
    ${BOOT_OUTPUT}
    COMMAND ${REBOL} ${TOOLS_DIR}/make-boot.r ${R3_OS_ID}
    DEPENDS
    ${TOOLS_DIR}/make-boot.r
    ${BOOT_DEPENDS}
    )

set(EXT_CORE_OUTPUT
    ${TOP_SRC_DIR}/include/host-ext-core.h
    )

set(EXT_CORE_DEPENDS
    ${TOP_SRC_DIR}/boot/core.r
    )

add_custom_command(OUTPUT
    ${EXT_CORE_OUTPUT}
    COMMAND ${REBOL} ${TOOLS_DIR}/core-ext.r
    DEPENDS
    ${TOOLS_DIR}/core-ext.r
    ${EXT_CORE_DEPENDS}
    )

set(REB_LIB_OUTPUT
    ${TOP_SRC_DIR}/include/reb-lib.h
    ${TOP_SRC_DIR}/include/reb-lib-lib.h
    )

set(REB_LIB_DEPENDS
    ${TOOLS_DIR}/common.r
    ${TOOLS_DIR}/common-parsers.r
    ${TOOLS_DIR}/form-header.r
    ${TOP_SRC_DIR}/boot/version.r
    ${CORE_DIR}/a-lib.c
    ${CORE_DIR}/f-extension.c
    )

add_custom_command(OUTPUT
    ${REB_LIB_OUTPUT}
    COMMAND ${REBOL} ${TOOLS_DIR}/make-reb-lib.r
    DEPENDS
    ${TOOLS_DIR}/make-reb-lib.r
    ${REB_LIB_DEPENDS}
    )

set (GENERATED_CORE_SOURCE
    ${NATIVE_OUTPUT}
    ${HEADER_OUTPUT}
    ${BOOT_OUTPUT}
    ${EXT_CORE_OUTPUT}
    ${REB_LIB_OUTPUT}
    )

set_source_files_properties(
    ${TOP_SRC_DIR}/include/host-table.inc
    PROPERTIES
    HEADER_FILE_ONLY TRUE
    )

#HOST
set(EXT_OS_OUTPUT
    ${TOP_SRC_DIR}/include/host-lib.h
    ${TOP_SRC_DIR}/include/host-table.inc
    )

set(EXT_OS_DEPENDS
    ${TOP_SRC_DIR}/boot/version.r
    ${TOOLS_DIR}/common.r
    ${TOOLS_DIR}/common-parsers.r
    ${TOOLS_DIR}/form-header.r
    ${TOOLS_DIR}/file-base.r
    ${OS_SOURCE}
    )

if (BUILD_VIEW)
    add_custom_command(OUTPUT
        ${EXT_OS_OUTPUT}
        COMMAND ${REBOL} ${TOOLS_DIR}/make-os-ext.r ${R3_OS_ID} gfx
        DEPENDS
        ${TOOLS_DIR}/make-os-ext.r
        ${EXT_OS_DEPENDS}
        )
else ()
    add_custom_command(OUTPUT
        ${EXT_OS_OUTPUT}
        COMMAND ${REBOL} ${TOOLS_DIR}/make-os-ext.r ${R3_OS_ID}
        DEPENDS
        ${TOOLS_DIR}/make-os-ext.r
        ${EXT_OS_DEPENDS}
        )
endif ()

set (OS_SOURCE
    ${OS_DIR}/host-main.c
    ${OS_DIR}/host-args.c
    ${OS_DIR}/host-device.c
    ${OS_DIR}/host-stdio.c
    ${OS_DIR}/host-core.c
    ${OS_DIR}/host-table.c
    ${OS_DIR}/dev-net.c
    ${OS_DIR}/dev-dns.c
    ${OS_DIR}/generic/host-memory.c
    ${CORE_PLATFORM_SOURCE}
    )

set (GENERATED_OS_SOURCE
    ${EXT_OS_OUTPUT}
    )

if(NOT (MSVC_IDE OR XCODE))
    add_custom_target(clean-generated COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/clean-generated.cmake)
    add_custom_target(clean-all
        COMMAND ${CMAKE_BUILD_TOOL} clean
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/clean-generated.cmake
        )
endif()
add_library(libr3_core_core OBJECT ${CORE_SOURCE} ${GENERATED_CORE_SOURCE} ${GENERATED_OS_SOURCE})
add_executable(r3-core $<TARGET_OBJECTS:libr3_core_core> ${OS_SOURCE})
target_include_directories(libr3_core_core PUBLIC ${TOP_SRC_DIR}/include ${TOP_SRC_DIR}/codecs ${FFI_INCLUDE_DIRS})
target_include_directories(r3-core PUBLIC ${TOP_SRC_DIR}/include ${TOP_SRC_DIR}/codecs)
target_compile_definitions(libr3_core_core PUBLIC REB_API REB_CORE ${COMMON_MACROS})
if (WIN32)
    #WINSYS_WIN32 is needed for dev-event.c
    target_compile_definitions(r3-core PUBLIC REB_CORE REB_EXE WINSYS_WIN32 ${COMMON_MACROS})
else ()
    target_compile_definitions(r3-core PUBLIC REB_CORE REB_EXE ${COMMON_MACROS})
endif ()

# Build as C by default, but there are extra checks if you build as C++
# NOTE: The library code will not be binary compatible in -debug- builds as
# C++, so a C++ debug client cannot be used with a C built debug Rebol
#
if (R3_CPP)
    set_property(SOURCE ${CORE_SOURCE} PROPERTY LANGUAGE CXX)
    set_property(SOURCE ${OS_SOURCE} PROPERTY LANGUAGE CXX)
    set_property(SOURCE {GENERATED_OS_SOURCE} PROPERTY LANGUAGE CXX)
    set_target_properties(r3-core PROPERTIES LINKER_LANGUAGE CXX)
endif()

if (DEFINED LINK_FLAGS)
    set_target_properties(r3-core PROPERTIES LINK_FLAGS ${LINK_FLAGS})
endif()

target_link_libraries(r3-core ${LIBS} ${FFI_LIBRARIES})

if (NOT R3_EXTERNAL_FFI)
    add_dependencies(r3-core ffi_s)
endif()
