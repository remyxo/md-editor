find_package(Qt6 REQUIRED COMPONENTS Test Gui)

add_executable(test_editorbridge test_editorbridge.cpp ../src/core/EditorBridge.cpp)
target_include_directories(test_editorbridge PRIVATE ../src)
target_link_libraries(test_editorbridge PRIVATE Qt6::Test)
add_test(NAME test_editorbridge COMMAND test_editorbridge)

add_executable(test_filemanager test_filemanager.cpp ../src/core/FileManager.cpp)
target_include_directories(test_filemanager PRIVATE ../src)
target_link_libraries(test_filemanager PRIVATE Qt6::Test)
add_test(NAME test_filemanager COMMAND test_filemanager)

add_executable(test_markdownhandler test_markdownhandler.cpp ../src/core/MarkdownHandler.cpp)
target_include_directories(test_markdownhandler PRIVATE ../src)
target_link_libraries(test_markdownhandler PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME test_markdownhandler COMMAND test_markdownhandler)
