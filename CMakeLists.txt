cmake_minimum_required(VERSION 3.20)
project(md-editor VERSION 0.12.2 LANGUAGES CXX C)
add_compile_definitions(MD_EDITOR_VERSION="${PROJECT_VERSION}")

# macOS 最低系统版本：Qt 6.8 官方最低支持 macOS 12。
# 必须显式设置——不设时 CMake 默认取 SDK 版本，macOS 26 runner 上会把
# Info.plist 的 LSMinimumSystemVersion 写成 26，导致 Sequoia(15) 等
# 旧系统弹「应用程序的这个版本不能与此版本的macOS配合使用」拒绝启动。
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets WebEngineWidgets WebChannel LinguistTools)

add_subdirectory(src)

option(BUILD_TESTS "构建测试" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ===== CPack 打包 =====
# 仅 Linux 使用 CPack 打 DEB；Windows/macOS 由 scripts/package-*.{ps1,sh} 打包
if(UNIX AND NOT APPLE)
    set(CPACK_PACKAGE_NAME "md-editor")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_VENDOR "md-editor")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Markdown WYSIWYG Editor based on Qt6 + TipTap")
    set(CPACK_PACKAGE_CONTACT "daijd")
    set(CPACK_GENERATOR "DEB")

    # DEB 特定设置
    set(CPACK_DEBIAN_PACKAGE_NAME "md-editor")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "remyxo")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "基于 Qt6 + TipTap 的 Markdown 所见即所得编辑器。
支持标准 Markdown 语法、HTML标签、文件树管理、多标签页、
PDF/DOCX 导出和亮色/暗色主题。")
    set(CPACK_DEBIAN_PACKAGE_SECTION "editors")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    # 运行时依赖 (去掉 -dev 后缀，只保留运行时库)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt6widgets6 (>= 6.8), libqt6webenginewidgets6 (>= 6.8), libqt6webchannel6 (>= 6.8), libqt6gui6 (>= 6.8), libqt6dbus6 (>= 6.8), libqt6core6 (>= 6.8), qt6-qpa-plugins")
    # 推荐安装
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-translations-l10n")

    # 安装到 /usr/local
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")

    # 安装规则
    install(TARGETS md-editor RUNTIME DESTINATION bin)
    install(FILES src/resources/icons/md-editor.svg
        DESTINATION share/icons/hicolor/scalable/apps
        RENAME md-editor.svg)
    install(FILES src/resources/md-editor.desktop
        DESTINATION share/applications)

    include(CPack)
endif()
