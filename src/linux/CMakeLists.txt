include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.6)

add_executable(ContextActions main.cpp)
target_link_libraries(ContextActions PRIVATE actions_core PkgConfig::GTK4)
target_compile_options(ContextActions PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(ContextActions PRIVATE
    GDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_6
    GDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_6)
set_target_properties(ContextActions PROPERTIES OUTPUT_NAME context-actions)

install(TARGETS ContextActions RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES io.github.ShortKedr.ContextActions.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES context_actions.py
    DESTINATION ${CMAKE_INSTALL_DATADIR}/nautilus-python/extensions)
install(PROGRAMS io.github.ShortKedr.ContextActions.servicemenu.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/kio/servicemenus)
install(FILES icon.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME io.github.ShortKedr.ContextActions.png)
