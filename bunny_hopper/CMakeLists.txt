add_executable(bunny_hopper
  src/main.c
  src/game.c
)

target_include_directories(bunny_hopper PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(bunny_hopper PRIVATE flecs_engine)

if(EMSCRIPTEN)
  target_link_options(bunny_hopper PRIVATE
    -sALLOW_MEMORY_GROWTH=1
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=65536
    -sSTACK_SIZE=1048576
    "-sASYNCIFY_IMPORTS=['emscripten_sleep']"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/projects/bunny_hopper/etc@projects/bunny_hopper/etc"
    "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/shell.html"
  )

  set_target_properties(bunny_hopper PROPERTIES
    SUFFIX ".html"
  )
endif()
