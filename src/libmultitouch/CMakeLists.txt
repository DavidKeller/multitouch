include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(
    multitouch
    SHARED

    event.c
    packet.c
    chain.c
    input.c
    output.c
    device.c
)

target_link_libraries(
    multitouch

    pthread
    peach
)

install(
    TARGETS

    multitouch

    LIBRARY DESTINATION lib
)
