find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_editorbridge test_editorbridge.cpp ../src/core/EditorBridge.cpp)
target_include_directories(test_editorbridge PRIVATE ../src)
target_link_libraries(test_editorbridge PRIVATE Qt6::Test)
add_test(NAME test_editorbridge COMMAND test_editorbridge)
