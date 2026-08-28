add_executable(lettuce_pray
  src/main.c
  src/game.c
)

target_include_directories(lettuce_pray PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(lettuce_pray PRIVATE flecs_engine)

if(EMSCRIPTEN)
  target_link_options(lettuce_pray PRIVATE
    -sALLOW_MEMORY_GROWTH=1
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=65536
    -sSTACK_SIZE=1048576
    "-sASYNCIFY_IMPORTS=['emscripten_sleep']"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/projects/lettuce_pray/etc@projects/lettuce_pray/etc"
    "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/shell.html"
  )

  set_target_properties(lettuce_pray PROPERTIES
    SUFFIX ".html"
  )
endif()
