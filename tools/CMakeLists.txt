add_executable(encoder encoder.cpp)
target_link_libraries(encoder PUBLIC vcdenc)

add_executable(vcdiff-partial vcdiff-partial.c)
target_link_libraries(vcdiff-partial PUBLIC vcdiff_incremental)

add_executable(vcdiff-fuse vcdiff-fuse.c)
target_link_libraries(vcdiff-fuse PUBLIC vcdiff_incremental fuse)
target_compile_definitions(vcdiff-fuse PUBLIC _FILE_OFFSET_BITS=64)
