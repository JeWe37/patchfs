add_subdirectory(open-vcdiff EXCLUDE_FROM_ALL)
target_include_directories(vcdenc_STATIC INTERFACE "open-vcdiff/src")

add_custom_target(build-tiny-vcdiff ALL
  COMMAND make
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tiny-vcdiff
)

add_library(tiny-vcdiff STATIC IMPORTED GLOBAL)
set_target_properties(tiny-vcdiff PROPERTIES
  IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/tiny-vcdiff/libvcdiff.a
)
target_include_directories(tiny-vcdiff SYSTEM INTERFACE tiny-vcdiff/include)
add_dependencies(tiny-vcdiff build-tiny-vcdiff)
