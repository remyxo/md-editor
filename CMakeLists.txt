cmake_minimum_required(VERSION 3.20)
project(md-editor VERSION 0.3.0 LANGUAGES CXX)
add_compile_definitions(MD_EDITOR_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets WebEngineWidgets WebChannel)

add_subdirectory(src)

option(BUILD_TESTS "构建测试" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ===== CPack DEB 打包 =====
set(CPACK_PACKAGE_NAME "md-editor")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "md-editor")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Markdown WYSIWYG Editor based on Qt6 + TipTap")
set(CPACK_PACKAGE_CONTACT "daijd")
set(CPACK_GENERATOR "DEB")

# DEB 特定设置
set(CPACK_DEBIAN_PACKAGE_NAME "md-editor")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "daijd")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "基于 Qt6 + TipTap 的 Markdown 所见即所得编辑器。
支持标准 Markdown 语法、文件树管理、多标签页、
PDF 导出和亮色/暗色主题。")
set(CPACK_DEBIAN_PACKAGE_SECTION "editors")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
# 运行时依赖 (去掉 -dev 后缀，只保留运行时库)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6widgets6 (>= 6.4), libqt6webenginewidgets6 (>= 6.4), libqt6webchannel6 (>= 6.4), libqt6gui6 (>= 6.4), libqt6core6 (>= 6.4), qt6-qpa-plugins")
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
