#include "biome.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *frame_output_path;
    int32_t frame_output_nth;
    const char *scene_path;
    int32_t width;
    int32_t height;
} BiomeAppOptions;

static int biomeParseArgs(
    int argc,
    char *argv[],
    BiomeAppOptions *options)
{
    for (int i = 1; i < argc; i ++) {
        const char *arg = argv[i];

        if (!strcmp(arg, "--scene") && i + 1 < argc) {
            options->scene_path = argv[++ i];
            continue;
        }
        if (!strcmp(arg, "--frame-out") && i + 1 < argc) {
            options->frame_output_path = argv[++ i];
            continue;
        }
        if (!strcmp(arg, "--frame-nth") && i + 1 < argc) {
            options->frame_output_nth = atoi(argv[++ i]);
            continue;
        }
        if (!strcmp(arg, "--width") && i + 1 < argc) {
            options->width = atoi(argv[++ i]);
            continue;
        }
        if (!strcmp(arg, "--height") && i + 1 < argc) {
            options->height = atoi(argv[++ i]);
            continue;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    BiomeAppOptions options = {
        .width = 1600,
        .height = 1000
    };

    biomeParseArgs(argc, argv, &options);

    setvbuf(stdout, NULL, _IOLBF, 0);

    flecsEngine_initTracy();

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsStats);
    ECS_IMPORT(world, FlecsScriptMath);
    ECS_IMPORT(world, FlecsEngine);

    ECS_IMPORT(world, biomeTerrain);
    ECS_IMPORT(world, biomeWeather);
    ECS_IMPORT(world, biomeWater);
    ECS_IMPORT(world, biomeBehavior);
    ECS_IMPORT(world, biomeResources);
    ECS_IMPORT(world, biomeLogistics);
    ECS_IMPORT(world, biomeFactory);
    ECS_IMPORT(world, biomePower);
    ECS_IMPORT(world, biomeBuildings);
    ECS_IMPORT(world, biomeBuilding_rule);
    ECS_IMPORT(world, biomeGhost);
    ECS_IMPORT(world, biomeTerrainItemIndex);
    ECS_IMPORT(world, biomeMiner);
    ECS_IMPORT(world, biomePlant);
    ECS_IMPORT(world, biomeTool);
    ECS_IMPORT(world, biomeUi);

    ecs_log_set_level(0);

    ecs_entity_t surface = ecs_entity(world, { .name = "surface" });
    ecs_set(world, surface, FlecsSurface, {
        .title = "Biome",
        .width = options.width,
        .height = options.height,
        .resolution_scale = 1,
        .vsync = true,
        .msaa = FlecsMsaa4x,
        .write_to_file = options.frame_output_path,
        .write_nth = options.frame_output_nth,
    });

    const char *scene = options.scene_path ?
        options.scene_path : "etc/scenes/biome.flecs";
    if (!ecs_script(world, { .filename = scene })) {
        ecs_err("failed to load scene %s", scene);
    }
    biomeWaterConfigureRenderer(world);

    if (!options.frame_output_path) {
        ecs_singleton_set(world, EcsRest, {0});
        // ecs_entity_t dequeue_rest = ecs_lookup(
        //     world, "flecs.rest.DequeueRest");
        // ecs_assert(dequeue_rest != 0, ECS_INTERNAL_ERROR, NULL);
        // ecs_remove_pair(world, dequeue_rest, EcsDependsOn, EcsPostFrame);
        // ecs_add_pair(world, dequeue_rest, EcsDependsOn, EcsPreFrame);
    }

    ecs_set_target_fps(world, 60);

    int32_t frame = 0;
    while (ecs_progress(world, 0.033)) { }

    ecs_log_set_level(-1);
    return ecs_fini(world);
}
