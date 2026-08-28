add_executable(ship_happens
  src/main.c
  src/game.c
)

target_include_directories(ship_happens PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ship_happens PRIVATE flecs_engine)

if(EMSCRIPTEN)
  target_link_options(ship_happens PRIVATE
    -sALLOW_MEMORY_GROWTH=1
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=65536
    -sSTACK_SIZE=1048576
    "-sASYNCIFY_IMPORTS=['emscripten_sleep']"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/projects/ship_happens/etc@projects/ship_happens/etc"
    "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/shell.html"
  )

  set_target_properties(ship_happens PROPERTIES
    SUFFIX ".html"
  )
endif()
