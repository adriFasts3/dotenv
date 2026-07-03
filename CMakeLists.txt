# CMake build for dotenv
#
# Options (override with -D on the command line, e.g. -DDOTENV_TESTS=OFF):
#   DOTENV_DISTRO_INSTALL=ON  install shared/static libs, header, pkg-config
#   DOTENV_EXAMPLES=ON        build the example programs
#   DOTENV_TESTS=ON           build and register the test suite
#
# Source tarball (written to build/dist/):
#   cpack --config build/CPackSourceConfig.cmake   (or `make dist`)
#
# Layout: public header in include/, sources in src/, examples in src/examples/.

cmake_minimum_required(VERSION 3.13)

project(dotenv
    VERSION "0.1.0"
    DESCRIPTION "minimal, dependency-free .env parser"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

#### options ####
option(DOTENV_DISTRO_INSTALL "install shared/static libs, header and pkg-config" ON)
option(DOTENV_EXAMPLES "build the example programs" ON)
option(DOTENV_TESTS "build and register the test suite" ON)

#### dotenv ####
# Build both a shared and a static library sharing the base name, so we get
# libdotenv.so/.dylib and libdotenv.a.
add_library(dotenv SHARED src/dotenv.c)
add_library(dotenv_static STATIC src/dotenv.c)
add_library(dotenv::dotenv ALIAS dotenv)
add_library(dotenv::dotenv_static ALIAS dotenv_static)

set_target_properties(dotenv PROPERTIES SOVERSION 0)
set_target_properties(dotenv_static PROPERTIES OUTPUT_NAME dotenv)

foreach(target dotenv dotenv_static)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    set_target_properties(${target} PROPERTIES
        C_VISIBILITY_PRESET hidden
        POSITION_INDEPENDENT_CODE ON  # allow linking the static lib into shared objects
    )
endforeach()

if(DOTENV_DISTRO_INSTALL)
    include(GNUInstallDirs)

    install(TARGETS dotenv dotenv_static
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(FILES include/dotenv.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    configure_file(dotenv.pc.in dotenv.pc @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dotenv.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )
endif()

if(DOTENV_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(DOTENV_EXAMPLES)
    add_subdirectory(src/examples)
endif()

#### source tarball (`make dist` / `cpack --config build/CPackSourceConfig.cmake`) ####
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}")
# Emit the tarball (and CPack's staging dir) under the build tree, so `make
# clean` removes it and it never clutters the source root.
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/"
    "/build/"                 # CMake build dir
    "CMakeCache\\\\.txt"      # stray CMake build dirs
    "/_CPack_Packages/"       # CPack staging
    "\\\\.sw[po]$"            # editor swap files
    "\\\\.o$"
    "\\\\.d$"
    "\\\\.tar\\\\.gz$"
    "CLAUDE\\\\.md"           # dev-only agent/design notes, not shipped
    "AGENTS\\\\.md"
    "TASK\\\\.md"
    "dotenv\\\\.txt"
)
include(CPack)

# Convenience `make dist` target that produces the source tarball.
add_custom_target(dist
    COMMAND ${CMAKE_CPACK_COMMAND} --config ${CMAKE_BINARY_DIR}/CPackSourceConfig.cmake
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building source tarball"
)
