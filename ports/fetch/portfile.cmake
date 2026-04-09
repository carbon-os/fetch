vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL             https://github.com/carbon-os/fetch
    REF             0ffc15023bd7d79ff49886c6d3d85a190de56f14
    HEAD_REF        main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(
    CONFIG_PATH lib/cmake/fetch
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
