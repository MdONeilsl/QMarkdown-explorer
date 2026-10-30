# -----------------------------------------------------------------------------
#  QMarkdown-explorer — top-level build configuration
#
#  Sections, in file order:
#    * build settings — C++20, AUTOMOC / AUTORCC
#    * dependencies — Qt6 Widgets + LinguistTools
#    * generated config.h — CMake project() values reachable from C++
#    * application target — sources, resources, link
#    * Windows executable-file icon — opt-in via resources/icons/app.ico
#    * translations — .ts → .qm at build time
#    * install rules — executable, styles/, translations/
#    * build-tree asset copies — the running binary never needs
#      'cmake --install'
#    * Windows runtime deployment — explicit four-file copy, no
#      windeployqt
#
#  Files the user must supply before compiling:
#    * resources/icons/app.png     — required (QRC; window icon)
#    * resources/icons/github.png  — required (QRC; status-bar button)
#    * resources/icons/qt.png      — optional (QRC; Qt-button override;
#                                    QStyle fallback when absent)
#    * resources/icons/app.ico     — optional (exe-file icon; the build
#                                    skips this block if absent)
# -----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.21)

# Change HOMEPAGE_URL to your own repository — it is the top entry of
# the GitHub dropdown at runtime (see RepoList::applicationRepoUrl).
project(QMarkdown-explorer
    VERSION      0.5.1
    DESCRIPTION  "A desktop Markdown explorer built with Qt 6 Widgets"
    HOMEPAGE_URL "https://github.com/your-username/QMarkdown-explorer"
    LANGUAGES    CXX)

# -----------------------------------------------------------------------------
#  Build settings
#
#  AUTOUIC is on for forward compatibility: there are no .ui files in the
#  current tree, but adding one later requires no CMake change.  AUTORCC
#  picks up resources/resources.qrc; AUTOMOC covers the Q_OBJECT-bearing
#  headers.
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD           20)
set(CMAKE_CXX_STANDARD_REQUIRED  ON)
set(CMAKE_CXX_EXTENSIONS         OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# -----------------------------------------------------------------------------
#  Dependencies
# -----------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)

qt_standard_project_setup()

# -----------------------------------------------------------------------------
#  Generated configuration header
#
#  Substitutes project() fields into src/config.h.in and places the
#  result where every translation unit can #include "config.h" from —
#  the include directory is added to the target further below.
# -----------------------------------------------------------------------------
configure_file(
    ${CMAKE_SOURCE_DIR}/src/config.h.in
    ${CMAKE_BINARY_DIR}/generated/config.h
    @ONLY
)

# -----------------------------------------------------------------------------
#  Application target
#
#  WIN32 switches the PE subsystem to WINDOWS on Windows: no console
#  pops up at launch.  qt_add_executable links Qt6::EntryPointPrivate
#  automatically for WIN32 targets, so ordinary int main(int, char**)
#  still works.  Non-Windows platforms ignore the keyword.
# -----------------------------------------------------------------------------
qt_add_executable(QMarkdown-explorer WIN32
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/MarkdownView.h
    src/MarkdownView.cpp
    src/MarkdownDocument.h
    src/MarkdownFilterProxyModel.h
    src/MarkdownFilterProxyModel.cpp
    src/ImageResolver.h
    src/ImageResolver.cpp
    src/StyleLoader.h
    src/StyleLoader.cpp
    src/AppIcon.h
    src/AppIcon.cpp
    src/StatusBarIcons.h
    src/StatusBarIcons.cpp
    src/RepoList.h
    src/RepoList.cpp
    resources/resources.qrc
)

# config.h lives here, not in src/.
target_include_directories(QMarkdown-explorer PRIVATE
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(QMarkdown-explorer PRIVATE Qt6::Widgets)

# -----------------------------------------------------------------------------
#  Windows executable-file icon
#
#  A .rc resource script is the only way to make Windows Explorer show a
#  custom icon for QMarkdown-explorer.exe.  The script is generated from
#  a template so its metadata mirrors project() — same trick as config.h.
#
#  Guarded by the existence of resources/icons/app.ico:
#    * the PE resource compiler aborts the whole build on a missing file
#      reference, unlike C++ #include which fails per-TU;
#    * the .ico is not derivable from the .png at build time without an
#      external image tool;
#    * the runtime window icon works without it — the .ico only affects
#      Explorer's display of the .exe file itself.
#
#  Add resources/icons/app.ico (multi-resolution recommended) and
#  rebuild: no CMake change is needed.
# -----------------------------------------------------------------------------
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/resources/icons/app.ico")
    set(APP_ICON_ICO_PATH
        "${CMAKE_SOURCE_DIR}/resources/icons/app.ico"
        CACHE INTERNAL "Absolute path to the Windows .ico for the exe resource")

    configure_file(
        ${CMAKE_SOURCE_DIR}/resources/app.rc.in
        ${CMAKE_BINARY_DIR}/generated/app.rc
        @ONLY
    )

    target_sources(QMarkdown-explorer PRIVATE
        ${CMAKE_BINARY_DIR}/generated/app.rc
    )

    message(STATUS "Windows exe icon: ${APP_ICON_ICO_PATH}")
elseif(WIN32)
    message(STATUS
        "Windows exe icon: not embedded "
        "(resources/icons/app.ico not found — build continues with the "
        "default Windows application icon)")
endif()

# -----------------------------------------------------------------------------
#  Translations
#
#  Every translations/*.ts compiles to a matching .qm during the build;
#  QM_FILES captures the produced files for the copy and install rules
#  below.
# -----------------------------------------------------------------------------
file(GLOB TS_FILES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/translations/*.ts
)

qt_add_translations(QMarkdown-explorer
    TS_FILES                 ${TS_FILES}
    QM_FILES_OUTPUT_VARIABLE QM_FILES
)

# -----------------------------------------------------------------------------
#  Install rules
#
#  Layout produced by 'cmake --install <prefix>':
#    <prefix>/QMarkdown-explorer[.exe]
#    <prefix>/styles/*.qss
#    <prefix>/translations/*.qm
#  Qt runtime DLLs are not installed — see the Windows deployment block
#  below for the runtime-side reasoning.
# -----------------------------------------------------------------------------
install(TARGETS QMarkdown-explorer
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION .
)

install(FILES ${QM_FILES} DESTINATION translations)

if(DEFINED QT6_INSTALL_PREFIX AND EXISTS "${QT6_INSTALL_PREFIX}/translations")
    install(DIRECTORY "${QT6_INSTALL_PREFIX}/translations/"
        DESTINATION translations
        FILES_MATCHING PATTERN "qtbase_*.qm")
endif()

install(DIRECTORY ${CMAKE_SOURCE_DIR}/styles/ DESTINATION styles)

# -----------------------------------------------------------------------------
#  Build-tree asset copies
#
#  Mirrors the install rules into the build directory, so the binary is
#  runnable from build/Debug/ (or build/) without an install step.  The
#  copies are idempotent — copy_if_different avoids re-stamping the
#  timestamp when nothing changed.
# -----------------------------------------------------------------------------
add_custom_command(TARGET QMarkdown-explorer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/styles
        $<TARGET_FILE_DIR:QMarkdown-explorer>/styles
    COMMENT "Copying QSS styles next to the executable"
    VERBATIM
)

if(QM_FILES)
    add_custom_command(TARGET QMarkdown-explorer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:QMarkdown-explorer>/translations
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${QM_FILES}
            $<TARGET_FILE_DIR:QMarkdown-explorer>/translations
        COMMENT "Copying compiled translations next to the executable"
        VERBATIM
    )
endif()

# -----------------------------------------------------------------------------
#  Windows runtime deployment
#
#  The exact runtime closure of the executable is four files:
#
#    QMarkdown-explorer.exe
#    Qt6Core[d].dll
#    Qt6Gui[d].dll
#    Qt6Widgets[d].dll
#    platforms/qwindows[d].dll
#
#  'windeployqt' is deliberately not used: it also copies styles
#  plugins, image-format plugins, VC redistributable bits, and the
#  translation catalogues we already install ourselves — none of which
#  this application needs, and all of which add up to tens of megabytes
#  of clutter in a development build folder.  The four-file copy below
#  is the entire runtime dependency.
#
#  $<TARGET_FILE:Qt6::Xxx> resolves to the correct .dll for the active
#  configuration, so Debug picks the d-suffixed variants and
#  Release / RelWithDebInfo / MinSizeRel pick the plain ones — no
#  per-config branching.
#
#  $<IF:$<CONFIG:Debug>,d,> does the same for the platform plugin, which
#  has no imported target to resolve from.
#
#  If Qt is ever added with a module that ships plugins (QtSvg for SVG
#  icons, QtImageFormats for WebP/TIFF, QtPrintSupport, etc.), the
#  corresponding plugin folder must be copied here as well — otherwise
#  the application builds and launches but silently fails to decode the
#  format the new module provides.
# -----------------------------------------------------------------------------
if(WIN32)
    if(NOT DEFINED QT6_INSTALL_PREFIX OR
       NOT EXISTS "${QT6_INSTALL_PREFIX}/plugins/platforms")
        message(FATAL_ERROR
            "Qt platform plugin directory not found under "
            "'${QT6_INSTALL_PREFIX}/plugins/platforms'.  "
            "Set CMAKE_PREFIX_PATH to a Qt 6 installation that contains "
            "the qwindows platform plugin.")
    endif()

    add_custom_command(TARGET QMarkdown-explorer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt6::Core>
            $<TARGET_FILE:Qt6::Gui>
            $<TARGET_FILE:Qt6::Widgets>
            $<TARGET_FILE_DIR:QMarkdown-explorer>
        COMMENT "Copying Qt runtime DLLs next to the executable"
        VERBATIM
    )

    add_custom_command(TARGET QMarkdown-explorer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:QMarkdown-explorer>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT6_INSTALL_PREFIX}/plugins/platforms/qwindows$<IF:$<CONFIG:Debug>,d,>.dll"
            "$<TARGET_FILE_DIR:QMarkdown-explorer>/platforms/"
        COMMENT "Copying Qt platform plugin next to the executable"
        VERBATIM
    )
endif()

# -----------------------------------------------------------------------------
#  Test suite
#
#  BUILD_TESTING is the CMake-standard cache variable.  When it is OFF,
#  Qt6::Test is not required and the tests/ directory is not configured —
#  so `cmake -B build -DBUILD_TESTING=OFF` works without Qt Test installed.
#
#  Each test binary is standalone: it compiles only the sources it
#  exercises.  This keeps `ctest` fast and lets a single test run in
#  isolation when debugging.
# -----------------------------------------------------------------------------
option(BUILD_TESTING "Build the test suite" ON)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()
