find_library(APPKIT AppKit REQUIRED)

set(bundle_docs "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/docs/support.md")
set_source_files_properties(${bundle_docs} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/docs)
add_executable(ContextActions MACOSX_BUNDLE main.mm ContextActions.icns ${bundle_docs})
set_source_files_properties(ContextActions.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_target_properties(ContextActions PROPERTIES
  OBJCXX_STANDARD 20
  OBJCXX_STANDARD_REQUIRED YES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.shortkedr.context-actions"
  MACOSX_BUNDLE_BUNDLE_NAME "Context Actions"
  MACOSX_BUNDLE_ICON_FILE "ContextActions.icns"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
target_compile_options(ContextActions PRIVATE -fobjc-arc)
target_link_libraries(ContextActions PRIVATE actions_core "${APPKIT}")
install(TARGETS ContextActions BUNDLE DESTINATION .)
