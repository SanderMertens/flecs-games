#include "ship_happens.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    ecs_world_t *world = ecs_init();

    ECS_IMPORT(world, FlecsScriptMath);
    ECS_IMPORT(world, FlecsEngine);
    ECS_IMPORT(world, Ship_happens);

    if (!ecs_script(world, { .filename = "etc/ship_happens.flecs" })) {
        ecs_err("failed to load etc/ship_happens.flecs");
        return -1;
    }

    return ecs_app_run(world, &(ecs_app_desc_t) {
        .enable_rest = true,
        .enable_stats = true
    });
}
