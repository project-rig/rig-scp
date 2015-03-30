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

# Compile/install the library
add_subdirectory(lib)

# Compile the tests
add_subdirectory(tests)

# Compile examples
add_subdirectory(examples)
