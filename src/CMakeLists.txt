set(SOURCES
    main.cpp
    app/Application.cpp
    app/ThemeManager.cpp
    core/EditorBridge.cpp
    ui/EditorTab.cpp
)

set(HEADERS
    app/Application.h
    app/ThemeManager.h
    core/EditorBridge.h
    ui/EditorTab.h
)

qt_add_resources(RESOURCES resources/app.qrc)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::WebChannel
)
