cmake_minimum_required(VERSION 3.14)

project(patchfs
        DESCRIPTION "Fuse filesystem dynamically patching files via vcdiff"
        LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_CXX_EXTENSIONS off)
set(CMAKE_CXX_STANDARD 20)

set(EXTRA_WARNINGS "-ggdb -fdiagnostics-color=always -fconcepts-diagnostics-depth=2 -Wlogical-op -Wstrict-null-sentinel -Wall -Wextra -pedantic -Wcast-align -Wcast-qual -Wctor-dtor-privacy -Wdisabled-optimization -Wformat=2 -Winit-self -Wmissing-include-dirs -Wold-style-cast -Woverloaded-virtual -Wredundant-decls -Wsign-promo -Wundef -fdiagnostics-show-option -Wconversion")

set(CMAKE_CXX_FLAGS_DEBUG "${EXTRA_WARNINGS} -fsanitize=address -fsanitize=undefined")
set(CMAKE_CXX_FLAGS_RELEASE "${EXTRA_WARNINGS} -march=native")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${EXTRA_WARNINGS} -march=native")

find_package(PkgConfig REQUIRED)

pkg_check_modules(FUSE REQUIRED fuse3)

add_subdirectory(third_party)
add_subdirectory(src)
add_subdirectory(tools)
