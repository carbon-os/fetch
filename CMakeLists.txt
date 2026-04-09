cmake_minimum_required(VERSION 3.25)
project(fetch VERSION 0.1.0 LANGUAGES CXX)

# ══════════════════════════════════════════════════════════════ Standard ═════

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ════════════════════════════════════════════════════════ vcpkg toolchain ════

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "Vcpkg toolchain file")
    endif()
endif()

# ═══════════════════════════════════════════════════════════ Dependencies ════

find_package(OpenSSL 3.0 REQUIRED)

# ═══════════════════════════════════════════════════════════════ Library ═════

add_library(fetch
    src/url.cpp
    src/socket.cpp
    src/tls.cpp
    src/http.cpp
    src/client.cpp
)

add_library(fetch::fetch ALIAS fetch)

target_include_directories(fetch
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(fetch
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
)

if(WIN32)
    target_link_libraries(fetch PUBLIC ws2_32)
endif()

# ══════════════════════════════════════════════════════ Compiler warnings ════

if(MSVC)
    target_compile_options(fetch PRIVATE /W4 /WX)
else()
    target_compile_options(fetch PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Werror
    )
endif()

# ═══════════════════════════════════════════════════════════ Build type ══════

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type set, defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# ══════════════════════════════════════════════════════════════ Tests ═════════

option(FETCH_BUILD_TESTS "Build the test suite" ON)

if(FETCH_BUILD_TESTS)
    add_executable(fetch_tests tests/main.cpp)

    target_link_libraries(fetch_tests PRIVATE fetch::fetch)

    if(MSVC)
        target_compile_options(fetch_tests PRIVATE /W4)
    else()
        target_compile_options(fetch_tests PRIVATE -Wall -Wextra)
    endif()

    enable_testing()
    add_test(NAME fetch_tests COMMAND fetch_tests)
    set_tests_properties(fetch_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")
endif()

# ══════════════════════════════════════════════════════════════ CLI tool ═════

option(FETCH_BUILD_CLI "Build the fetch CLI tool" ON)

if(FETCH_BUILD_CLI)
    add_executable(fetch_cli cli/fetch.cpp)

    target_link_libraries(fetch_cli PRIVATE fetch::fetch)

    # Output binary is just called "fetch" (or "fetch.exe" on Windows)
    set_target_properties(fetch_cli PROPERTIES OUTPUT_NAME "fetch")

    if(MSVC)
        target_compile_options(fetch_cli PRIVATE /W4)
    else()
        target_compile_options(fetch_cli PRIVATE -Wall -Wextra)
    endif()

    install(TARGETS fetch_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ══════════════════════════════════════════════════════════════ Install ══════

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS fetch
    EXPORT  fetch-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/fetch
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT fetch-targets
    FILE        fetch-targets.cmake
    NAMESPACE   fetch::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fetch
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/fetch-config-version.cmake"
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fetch-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/fetch-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fetch
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/fetch-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/fetch-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fetch
)