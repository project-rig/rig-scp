cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project("Rig SCP")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
add_subdirectory(src)

add_executable(hello hello.c)
target_link_libraries(hello rig_scp)
