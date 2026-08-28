#include "ore_else.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void oreParseArgs(int argc, char *argv[]) {
    for (int i = 1; i < argc; i ++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--stresstest")) {
            oreStressTestSet(30);
        } else if (!strncmp(arg, "--stresstest=", 13)) {
            float minutes = (float)atof(arg + 13);
            oreStressTestSet(minutes > 0 ? minutes : 30);
        }
    }
}

int main(int argc, char *argv[]) {
    oreParseArgs(argc, argv);

    ecs_world_t *world = ecs_init();

    ECS_IMPORT(world, FlecsScriptMath);
    ECS_IMPORT(world, FlecsEngine);
    ECS_IMPORT(world, Ore_else);

    oreSeedMap(world);

    if (!ecs_script(world, { .filename = "etc/ore_else.flecs" })) {
        ecs_err("failed to load etc/ore_else.flecs");
        return -1;
    }

    if (oreStressTestEnabled()) {
        oreStressTestApply(world);
    } else {
        oreSetState(world, OreGameStateTitle);
    }

    const char *rest_port = getenv("ORE_REST_PORT");
    const char *fixed_dt = getenv("ORE_FIXED_DT");

    if (fixed_dt) {
        ecs_set_target_fps(world, 0);
    }

    return ecs_app_run(world, &(ecs_app_desc_t) {
        .enable_rest = true,
        .enable_stats = true,
        .port = rest_port ? (uint16_t)atoi(rest_port) : 0,
        .delta_time = fixed_dt ? (ecs_ftime_t)atof(fixed_dt) : 0
    });
}
