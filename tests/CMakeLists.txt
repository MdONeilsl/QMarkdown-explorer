# -----------------------------------------------------------------------------
#  Test suite for QMarkdown-explorer.
# -----------------------------------------------------------------------------

set(MD_GENERATED_INCLUDE ${CMAKE_BINARY_DIR}/generated)
set(MD_TEST_LIBS Qt6::Test Qt6::Widgets)

# -----------------------------------------------------------------------------
#  add_test_qt_runtime(<target>)
#
#  Copies the Qt runtime DLLs and every platform plugin next to <target>
#  on Windows.  No-op on other platforms.
#
#  The platform plugin directory is copied in full (windows, offscreen,
#  minimal, …) rather than just qwindows[d].dll.  The tests set
#  QT_QPA_PLATFORM=offscreen to run on headless CI runners, and the
#  offscreen plugin must therefore be present — copying only qwindows
#  leaves the tests unable to construct a QApplication:
#
#      "Could not find the Qt platform plugin "offscreen"."
#
#  Copying the whole directory is simpler than tracking individual
#  plugin names, and the size cost (a few hundred KB per plugin) is
#  irrelevant for a test build.
# -----------------------------------------------------------------------------
function(add_test_qt_runtime target)
    if(NOT WIN32)
        return()
    endif()
    if(NOT TARGET Qt6::Test)
        return()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt6::Core>
            $<TARGET_FILE:Qt6::Gui>
            $<TARGET_FILE:Qt6::Widgets>
            $<TARGET_FILE:Qt6::Test>
            $<TARGET_FILE_DIR:${target}>
        COMMENT "Copying Qt runtime DLLs next to ${target}"
        VERBATIM
    )

    if(DEFINED QT6_INSTALL_PREFIX
       AND EXISTS "${QT6_INSTALL_PREFIX}/plugins/platforms")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${target}>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${QT6_INSTALL_PREFIX}/plugins/platforms"
                "$<TARGET_FILE_DIR:${target}>/platforms"
            COMMENT "Copying Qt platform plugins next to ${target}"
            VERBATIM
        )
    else()
        message(WARNING
            "Cannot locate the Qt platform plugin directory for target "
            "'${target}'.  '${QT6_INSTALL_PREFIX}/plugins/platforms' does "
            "not exist.  Tests that create a QApplication will fail to "
            "start.")
    endif()
endfunction()

# -----------------------------------------------------------------------------
#  tst_imageresolver
# -----------------------------------------------------------------------------
qt_add_executable(tst_imageresolver
    tst_imageresolver.cpp
    ${CMAKE_SOURCE_DIR}/src/ImageResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/ImageResolver.h
)
target_include_directories(tst_imageresolver PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${MD_GENERATED_INCLUDE}
)
target_link_libraries(tst_imageresolver PRIVATE ${MD_TEST_LIBS})
add_test_qt_runtime(tst_imageresolver)
add_test(NAME tst_imageresolver COMMAND tst_imageresolver)
set_tests_properties(tst_imageresolver PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# -----------------------------------------------------------------------------
#  tst_repolist
# -----------------------------------------------------------------------------
qt_add_executable(tst_repolist
    tst_repolist.cpp
    ${CMAKE_SOURCE_DIR}/src/RepoList.cpp
    ${CMAKE_SOURCE_DIR}/src/RepoList.h
)
target_include_directories(tst_repolist PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${MD_GENERATED_INCLUDE}
)
target_link_libraries(tst_repolist PRIVATE ${MD_TEST_LIBS})
add_test_qt_runtime(tst_repolist)
add_test(NAME tst_repolist COMMAND tst_repolist)
set_tests_properties(tst_repolist PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# -----------------------------------------------------------------------------
#  tst_styleloader
# -----------------------------------------------------------------------------
qt_add_executable(tst_styleloader
    tst_styleloader.cpp
    ${CMAKE_SOURCE_DIR}/src/StyleLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/StyleLoader.h
)
target_include_directories(tst_styleloader PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${MD_GENERATED_INCLUDE}
)
target_link_libraries(tst_styleloader PRIVATE ${MD_TEST_LIBS})
add_test_qt_runtime(tst_styleloader)
add_test(NAME tst_styleloader COMMAND tst_styleloader)
set_tests_properties(tst_styleloader PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# -----------------------------------------------------------------------------
#  tst_markdownview
# -----------------------------------------------------------------------------
qt_add_executable(tst_markdownview
    tst_markdownview.cpp
    ${CMAKE_SOURCE_DIR}/src/MarkdownView.cpp
    ${CMAKE_SOURCE_DIR}/src/MarkdownView.h
    ${CMAKE_SOURCE_DIR}/src/MarkdownDocument.h
    ${CMAKE_SOURCE_DIR}/src/ImageResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/ImageResolver.h
)
target_include_directories(tst_markdownview PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${MD_GENERATED_INCLUDE}
)
target_link_libraries(tst_markdownview PRIVATE ${MD_TEST_LIBS})
add_test_qt_runtime(tst_markdownview)
add_test(NAME tst_markdownview COMMAND tst_markdownview)
set_tests_properties(tst_markdownview PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# -----------------------------------------------------------------------------
#  tst_markdownfilterproxymodel
# -----------------------------------------------------------------------------
qt_add_executable(tst_markdownfilterproxymodel
    tst_markdownfilterproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/MarkdownFilterProxyModel.cpp
    ${CMAKE_SOURCE_DIR}/src/MarkdownFilterProxyModel.h
)
target_include_directories(tst_markdownfilterproxymodel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${MD_GENERATED_INCLUDE}
)
target_link_libraries(tst_markdownfilterproxymodel PRIVATE ${MD_TEST_LIBS})
add_test_qt_runtime(tst_markdownfilterproxymodel)
add_test(NAME tst_markdownfilterproxymodel COMMAND tst_markdownfilterproxymodel)
set_tests_properties(tst_markdownfilterproxymodel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# -----------------------------------------------------------------------------
#  tst_mainwindow
# -----------------------------------------------------------------------------
qt_add_executable(tst_mainwindow
    tst_mainwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/MainWindow.h
    ${CMAKE_SOURCE_DIR}/src/MarkdownView.cpp
    ${CMAKE_SOURCE_DIR}/src/MarkdownView.h
    ${CMAKE_SOURCE_DIR}/src/MarkdownDocument.h
    ${CMAKE_SOURCE_DIR}/src/MarkdownFilterProxyModel.cpp
    ${CMAKE_SOURCE_DIR}/src/MarkdownFilterProxyModel.h
    ${CMAKE_SOURCE_DIR}/src/ImageResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/ImageResolver.h
    ${CMAKE_SOURCE_DIR}/src/StyleLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/StyleLoader.h
    ${CMAKE_SOURCE_DIR}/src/RepoList.cpp
    ${CMAKE_SOURCE_DIR}/src/RepoList.h
    ${CMAKE_SOURCE_DIR}/src/AppIcon.cpp
    ${CMAKE_SOURCE_DIR}/src/AppIcon.h
    ${CMAKE_SOURCE_DIR}/src/StatusBarIcons.cpp
    ${CMAKE_SOURCE_DIR}/src/StatusBarIcons.h
)
target_include_directories(tst_mainwindow PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${MD_GENERATED_INCLUDE}
)
target_link_libraries(tst_mainwindow PRIVATE ${MD_TEST_LIBS})
add_test_qt_runtime(tst_mainwindow)
add_test(NAME tst_mainwindow COMMAND tst_mainwindow)
set_tests_properties(tst_mainwindow PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# -----------------------------------------------------------------------------
#  tst_cli — end-to-end command-line tests via QProcess.
#
#  The application binary path is passed via an environment variable
#  (QMD_APP_PATH) rather than as argv[1].  QTEST_GUILESS_MAIN parses argv
#  looking for test function names; a positional argument like
#  "C:/…/QMarkdown-explorer.exe" is interpreted as a test function called
#  "C", and the run fails before any of the test slots execute:
#
#      Unknown test function: 'C'
#
#  Using ${CMAKE_COMMAND} -E env sidesteps argv entirely.
# -----------------------------------------------------------------------------
qt_add_executable(tst_cli
    tst_cli.cpp
)
target_link_libraries(tst_cli PRIVATE Qt6::Test Qt6::Core)
add_test_qt_runtime(tst_cli)
add_dependencies(tst_cli QMarkdown-explorer)
add_test(NAME tst_cli COMMAND
    ${CMAKE_COMMAND} -E env
    "QT_QPA_PLATFORM=offscreen"
    "QMD_APP_PATH=$<TARGET_FILE:QMarkdown-explorer>"
    $<TARGET_FILE:tst_cli>)


# -----------------------------------------------------------------------------
#  tst_markdownviewtheme — code-block styling tests.
# -----------------------------------------------------------------------------
qt_add_executable(tst_markdownviewtheme
    tst_markdownviewtheme.cpp
    ${CMAKE_SOURCE_DIR}/src/MarkdownView.cpp
    ${CMAKE_SOURCE_DIR}/src/MarkdownView.h
    ${CMAKE_SOURCE_DIR}/src/MarkdownDocument.h
    ${CMAKE_SOURCE_DIR}/src/ImageResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/ImageResolver.h
)
target_include_directories(tst_markdownviewtheme PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${MD_GENERATED_INCLUDE}
)
target_link_libraries(tst_markdownviewtheme PRIVATE ${MD_TEST_LIBS})
add_test_qt_runtime(tst_markdownviewtheme)
add_test(NAME tst_markdownviewtheme COMMAND tst_markdownviewtheme)
set_tests_properties(tst_markdownviewtheme PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")