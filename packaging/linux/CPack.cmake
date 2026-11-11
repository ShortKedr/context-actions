set(CPACK_PACKAGE_NAME "context-actions")
set(CPACK_PACKAGE_VENDOR "Context Actions")
set(CPACK_PACKAGE_CONTACT "Context Actions <ShortKedr@users.noreply.github.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/ShortKedr/context-actions")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Batch media conversion, PDF assembly, and file renaming")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_GENERATOR "TGZ")
set(CPACK_SYSTEM_NAME "linux-${CMAKE_SYSTEM_PROCESSOR}")

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION utils)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgtk-4-1 (>= 4.6), ffmpeg, qpdf")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libreoffice-writer, libreoffice-calc")

set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_USER_FILELIST "%license /usr/share/doc/context-actions/LICENSE")
set(CPACK_RPM_PACKAGE_REQUIRES "gtk4 >= 4.6, ffmpeg, qpdf")
set(CPACK_RPM_PACKAGE_SUGGESTS "libreoffice-writer, libreoffice-calc")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/applications
    /usr/share/icons
    /usr/share/icons/hicolor
    /usr/share/icons/hicolor/256x256
    /usr/share/icons/hicolor/256x256/apps)

include(CPack)
