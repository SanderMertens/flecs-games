add_executable(ore_else
  src/main.c
  src/game.c
  src/resources.c
  src/player.c
  src/buildings.c
  src/placement.c
  src/rules.c
  src/combat.c
  src/rocket.c
  src/health_bars.c
  src/drones.c
  src/quality.c
  src/stresstest.c
  src/craft.c
  src/power.c
  src/ui.c
)

target_include_directories(ore_else PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ore_else PRIVATE flecs_engine)

if(EMSCRIPTEN)
  target_link_options(ore_else PRIVATE
    -sALLOW_MEMORY_GROWTH=1
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=65536
    -sSTACK_SIZE=1048576
    "-sASYNCIFY_IMPORTS=['emscripten_sleep']"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/projects/ore_else/etc@projects/ore_else/etc"
    "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/shell.html"
  )

  set_target_properties(ore_else PROPERTIES
    SUFFIX ".html"
  )
endif()
