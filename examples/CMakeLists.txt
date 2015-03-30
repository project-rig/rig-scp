include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_executable(hello hello.c)
target_link_libraries(hello rigscp)

