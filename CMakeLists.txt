cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project("Rig SCP")

# Set C99 mode
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")

# Libuv compilation work-around https://github.com/joyent/libuv/issues/551
if ( CMAKE_COMPILER_IS_GNUCC )
	add_definitions("-D_GNU_SOURCE")
endif ( CMAKE_COMPILER_IS_GNUCC )

# Enable compiler warnings under GCC
if ( CMAKE_COMPILER_IS_GNUCC )
	# XXX: add_definitions isn't strictly meant for doing this...
	add_definitions("-Wall")
	add_definitions("-Werror")
	add_definitions("-pedantic")
endif ( CMAKE_COMPILER_IS_GNUCC )

# Compile the library
add_subdirectory(src)

# Compile the tests
add_subdirectory(tests)

# XXX: Example executable
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
add_executable(hello hello.c)
target_link_libraries(hello rig_scp)
