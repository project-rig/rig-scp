include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(rig_scp SHARED rs.c
                           rs__queue.c
                           rs__process_queue.c
                           rs__process_response.c
                           rs__cancel.c
                           rs__transport.c
                           rs__queue.c
                           rs__scp.c)
target_link_libraries(rig_scp uv)
