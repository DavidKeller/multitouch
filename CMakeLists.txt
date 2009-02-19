cmake_minimum_required(VERSION 2.6)

project(multitouch)

include(CheckLibraryExists)

check_library_exists(
    peach

    peach_hash_init

    ""

    HAVE_PEACH
)

set(
    CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall"
)

add_subdirectory(src/)
add_subdirectory(include/)
