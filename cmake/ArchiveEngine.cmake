include(FetchContent)
option(CONTEXT_ACTIONS_OFFLINE "Build using only cached archive engine sources" OFF)

function(archive_dependency name filename sha256)
  set(downloads "${CMAKE_BINARY_DIR}/archive-upstream")
  set(urls ${ARGN})
  if(EXISTS "${downloads}/${filename}")
    set(cached "${downloads}/${filename}")
    file(SHA256 "${cached}" actual)
    if(NOT actual STREQUAL sha256)
      message(FATAL_ERROR "SHA-256 mismatch: ${cached}. Remove the invalid archive and retry.")
    endif()
    set(urls "${cached}")
  elseif(CONTEXT_ACTIONS_OFFLINE)
    message(FATAL_ERROR "Offline build: missing archive engine source ${downloads}/${filename}")
  endif()
  FetchContent_Declare(${name}
    URL ${urls}
    URL_HASH "SHA256=${sha256}"
    DOWNLOAD_DIR "${downloads}"
    DOWNLOAD_NAME "${filename}"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    # Populate without adding the dependency's install rules.
    SOURCE_SUBDIR context-actions-populate-only)
endfunction()

function(install_libarchive_notice source destination)
  file(READ "${source}/COPYING" upstream)
  file(GLOB sources "${source}/libarchive/*.c" "${source}/libarchive/*.h")
  set(credits)
  set(licenses)
  foreach(path IN LISTS sources)
    file(STRINGS "${path}" notices REGEX "^[ /*#\t]*Copyright [(]?[cC0-9]")
    list(APPEND credits ${notices})
    # Preserve full notices where upstream incorporates separately attributed code.
    file(READ "${path}" content)
    foreach(marker IN ITEMS "The Regents of the University of California" "The NetBSD Foundation"
                            "David Mazieres" "Yann Collet" "Samuel Neves"
                            "Copyright (C) The XZ Embedded authors"
                            "Copyright (C) The XZ Utils authors")
      string(FIND "${content}" "${marker}" position)
      if(position GREATER_EQUAL 0)
        string(SUBSTRING "${content}" 0 "${position}" before)
        string(FIND "${before}" "/*" start REVERSE)
        string(SUBSTRING "${content}" "${start}" -1 comment)
        string(FIND "${comment}" "*/" end)
        math(EXPR length "${end} + 2")
        string(SUBSTRING "${comment}" 0 "${length}" alternate_license)
        get_filename_component(filename "${path}" NAME)
        string(APPEND licenses "\n${filename}\n\n${alternate_license}\n")
      endif()
    endforeach()
  endforeach()
  list(TRANSFORM credits STRIP)
  list(TRANSFORM credits REPLACE "^[ /*#\t]+" "")
  list(TRANSFORM credits STRIP)
  list(REMOVE_DUPLICATES credits)
  list(SORT credits)
  list(JOIN credits "\n" attribution)
  set(notice "${CMAKE_CURRENT_BINARY_DIR}/archive-libarchive.txt")
  file(CONFIGURE OUTPUT "${notice}" CONTENT
    "${upstream}\nLibarchive source copyright notices\n\n${attribution}\n\nAdditional license texts\n${licenses}" @ONLY)
  install(FILES "${notice}" DESTINATION "${destination}" RENAME libarchive.txt)
endfunction()

function(add_archive_engine)
  archive_dependency(zlib zlib-1.4.tar.gz
    02dd159783677a749c04adbdd02b55b8a2bcc7e054f51b76b549199a916ad584
    https://zlib.net/zlib-1.4.tar.gz https://zlib.net/fossils/zlib-1.4.tar.gz)
  archive_dependency(liblzma xz-5.8.4.tar.xz
    4ce24038fd4221e0d13bc1a2de7a4db56e90b92b3bf75321f6c14be73f65de4b
    https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.xz)
  archive_dependency(libarchive libarchive-3.8.11.tar.xz
    a3dc0872b1b39da34dc18ef1b944f0f11ffd37425d70ae04be679279f1cf43d2
    https://www.libarchive.org/downloads/libarchive-3.8.11.tar.xz)

  set(BUILD_SHARED_LIBS OFF)
  set(BUILD_TESTING OFF)
  set(ZLIB_BUILD_SHARED OFF)
  set(ZLIB_BUILD_STATIC ON)
  set(ZLIB_BUILD_TESTING OFF)
  set(ZLIB_INSTALL OFF)
  set(XZ_DOC OFF)
  set(XZ_NLS OFF)
  set(XZ_EXTERNAL_SHA256 OFF)
  set(XZ_TOOL_XZ OFF)
  set(XZ_TOOL_XZDEC OFF)
  set(XZ_TOOL_LZMADEC OFF)
  set(XZ_TOOL_LZMAINFO OFF)
  set(XZ_TOOL_SCRIPTS OFF)
  if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(MSVC_USE_STATIC_CRT ON)
  endif()
  FetchContent_MakeAvailable(zlib liblzma)
  add_subdirectory("${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}" EXCLUDE_FROM_ALL)
  add_subdirectory("${liblzma_SOURCE_DIR}" "${liblzma_BINARY_DIR}" EXCLUDE_FROM_ALL)

  # Libarchive's CMake still uses FindZLIB/FindLibLZMA variables. Redirect
  # discovery to the two static targets in this build, including its probes.
  file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/zlib-config.cmake" [=[
set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}")
set(ZLIB_LIBRARIES ZLIB::ZLIBSTATIC)
set(ZLIB_WINAPI FALSE)
set(ZLIB_DLL FALSE)
set(WITHOUT_ZLIB_DLL TRUE)
]=])
  file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/zlib-config-version.cmake" [=[
set(PACKAGE_VERSION 1.4)
if(PACKAGE_FIND_VERSION VERSION_LESS_EQUAL PACKAGE_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
endif()
]=])
  file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/liblzma-config.cmake" [=[
set(LIBLZMA_FOUND TRUE)
set(LIBLZMA_INCLUDE_DIRS "${liblzma_SOURCE_DIR}/src/liblzma/api")
set(LIBLZMA_INCLUDE_DIR "${LIBLZMA_INCLUDE_DIRS}")
set(LIBLZMA_LIBRARIES liblzma)
# liblzma supplies its static definition on its interface target.
set(WITHOUT_LZMA_API_STATIC TRUE)
set(LZMA_API_STATIC FALSE)
# The feature probe cannot link a library built in the same build tree.
# The repacker does not use the optional threaded XZ stream writer.
set(HAVE_LZMA_STREAM_ENCODER_MT FALSE)
]=])

  foreach(feature IN ITEMS BZip2 LIBB2 LZ4 LZO ZSTD LIBXML2 BSDXML EXPAT WIN32_XMLLITE
                           OPENSSL MBEDTLS NETTLE MD CNG PCREPOSIX PCRE2POSIX ACL XATTR
                           TAR CPIO CAT UNZIP TEST INSTALL WERROR)
    set(ENABLE_${feature} OFF)
  endforeach()
  set(ENABLE_LZMA ON)
  set(ENABLE_ZLIB ON)
  FetchContent_MakeAvailable(libarchive)
  add_subdirectory("${libarchive_SOURCE_DIR}" "${libarchive_BINARY_DIR}" EXCLUDE_FROM_ALL)
  target_compile_definitions(archive_static INTERFACE LIBARCHIVE_STATIC)
  add_library(ContextActions::Archive ALIAS archive_static)

  add_executable(ContextActionsArchive src/archive_engine.cpp)
  if(WIN32)
    target_sources(ContextActionsArchive PRIVATE src/windows/archive_platform.cpp)
  elseif(APPLE)
    target_sources(ContextActionsArchive PRIVATE src/posix/archive_platform.cpp src/macos/archive_main.cpp)
  else()
    target_sources(ContextActionsArchive PRIVATE src/posix/archive_platform.cpp src/linux/archive_main.cpp)
  endif()
  target_link_libraries(ContextActionsArchive PRIVATE ContextActions::Archive ZLIB::ZLIBSTATIC)
  if(MSVC)
    target_compile_options(ContextActionsArchive PRIVATE /W4 /utf-8 /permissive- /EHsc
      "/d1trimfile:${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_options(ContextActionsArchive PRIVATE /PDBALTPATH:ContextActionsArchive.pdb)
  else()
    target_compile_options(ContextActionsArchive PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  if(WIN32)
    set(engine_dir .)
    set(license_dir licenses/archive)
  elseif(APPLE)
    set(engine_dir ContextActions.app/Contents/MacOS)
    set(license_dir ContextActions.app/Contents/Resources/licenses/archive)
  else()
    include(GNUInstallDirs)
    set(engine_dir "${CMAKE_INSTALL_BINDIR}")
    set(license_dir "${CMAKE_INSTALL_DATADIR}/doc/context-actions/licenses/archive")
  endif()
  install(TARGETS ContextActionsArchive RUNTIME DESTINATION "${engine_dir}")
  install_libarchive_notice("${libarchive_SOURCE_DIR}" "${license_dir}")
  install(FILES "${zlib_SOURCE_DIR}/LICENSE" DESTINATION "${license_dir}" RENAME zlib.txt)
  install(FILES "${liblzma_SOURCE_DIR}/COPYING.0BSD" DESTINATION "${license_dir}" RENAME liblzma.txt)
endfunction()
