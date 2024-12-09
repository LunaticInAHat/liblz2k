cmake_minimum_required(VERSION 3.11)

project(lz2k VERSION 0.1)

add_library(lz2k STATIC)
add_executable(lz2k1demo)
add_dependencies(lz2k1demo lz2k)

set_property(TARGET lz2k PROPERTY POSITION_INDEPENDENT_CODE true)
target_compile_features(lz2k PUBLIC cxx_std_17)
target_compile_options(lz2k PUBLIC -Wall -Werror -Wextra)
target_include_directories(lz2k PUBLIC include demo)

target_link_libraries(lz2k1demo lz2k)
target_compile_features(lz2k1demo PUBLIC cxx_std_17)
target_compile_options(lz2k1demo PUBLIC -Wall -Werror -Wextra)

add_subdirectory(src)

target_sources(lz2k1demo PRIVATE ${PROJECT_SOURCE_DIR}/demo/demo.cpp)
