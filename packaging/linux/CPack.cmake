set(CPACK_PACKAGE_NAME "context-actions")
set(CPACK_PACKAGE_VENDOR "Context Actions")
set(CPACK_PACKAGE_CONTACT "Context Actions <ShortKedr@users.noreply.github.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/ShortKedr/context-actions")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Batch media conversion, PDF assembly, and file renaming")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_GENERATOR "TGZ")
set(CPACK_SYSTEM_NAME "linux-${CMAKE_SYSTEM_PROCESSOR}")

set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/CPackLinux.cmake")
configure_file("${CMAKE_CURRENT_LIST_DIR}/archive-config.cmake.in" "${CPACK_PROJECT_CONFIG_FILE}" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/portable-install.sh"
    "${CMAKE_CURRENT_BINARY_DIR}/portable-installer/install.sh" COPYONLY
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/portable-installer/VERSION" CONTENT "${PROJECT_VERSION}\n")

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION utils)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgtk-4-1 (>= 4.6), ffmpeg, qpdf, python3-nautilus")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libreoffice-writer, libreoffice-calc")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_LIST_DIR}/preinst")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)

set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "MIT AND BSD-2-Clause AND BSD-3-Clause AND CC0-1.0 AND ISC AND Zlib AND 0BSD")
set(CPACK_RPM_USER_FILELIST
    "%license /usr/share/doc/context-actions/LICENSE"
    "%license /usr/share/doc/context-actions/licenses/archive/libarchive.txt"
    "%license /usr/share/doc/context-actions/licenses/archive/zlib.txt"
    "%license /usr/share/doc/context-actions/licenses/archive/liblzma.txt")
set(CPACK_RPM_PACKAGE_REQUIRES "gtk4 >= 4.6, ffmpeg, qpdf, nautilus-python")
set(CPACK_RPM_PACKAGE_REQUIRES_PRE "rpm >= 4.16")
set(CPACK_RPM_PACKAGE_SUGGESTS "libreoffice-writer, libreoffice-calc")
set(CPACK_RPM_PRE_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_LIST_DIR}/rpm-preinstall.sh")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/applications
    /usr/share/kio
    /usr/share/kio/servicemenus
    /usr/share/nautilus-python
    /usr/share/nautilus-python/extensions
    /usr/share/icons
    /usr/share/icons/hicolor
    /usr/share/icons/hicolor/256x256
    /usr/share/icons/hicolor/256x256/apps)

include(CPack)
