# Compile the library
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(rigscp SHARED rs.c
                          rs__queue.c
                          rs__process_queue.c
                          rs__process_response.c
                          rs__cancel.c
                          rs__transport.c
                          rs__queue.c
                          rs__scp.c)
target_link_libraries(rigscp uv)

# Install into the system
install(TARGETS rigscp
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib/static)
INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../include/rs.h
        DESTINATION include)
