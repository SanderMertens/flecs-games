cmake_minimum_required(VERSION 3.20)
project(biome C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(flecs-engine)

file(GLOB BIOME_MODULE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/modules/*.c"
)

add_library(biome_modules STATIC ${BIOME_MODULE_SOURCES})

target_include_directories(biome_modules PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(biome_modules PUBLIC flecs_engine)

add_executable(biome src/main.c)

target_include_directories(biome PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(biome PRIVATE biome_modules)
