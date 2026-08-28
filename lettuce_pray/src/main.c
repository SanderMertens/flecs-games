#include "lettuce_pray.h"

int main(int argc, char *argv[]) {
    ecs_world_t *world = ecs_init();

    ECS_IMPORT(world, FlecsScriptMath);
    ECS_IMPORT(world, FlecsEngine);
    ECS_IMPORT(world, Lettuce_pray);

    if (!ecs_script(world, { .filename = "etc/lettuce_pray.flecs" })) {
        ecs_err("failed to load etc/lettuce_pray.flecs");
        return -1;
    }

    return ecs_app_run(world, &(ecs_app_desc_t) {
        .enable_rest = true,
        .enable_stats = true
    });
}
