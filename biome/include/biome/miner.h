#ifndef FLECS_MINER_H
#define FLECS_MINER_H

#undef ECS_META_IMPL
#ifndef BIOME_MINER_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* Resource deposit (must be mined into storage). */
ECS_STRUCT(BiomeResourceDeposit, {
    ecs_entity_t resource;
    int32_t amount;
});

/* Item that mines resource from a deposit */
ECS_STRUCT(BiomeResourceMiner, {
    ecs_entity_t deposit;       /* Entity with ResourceDeposit. */
});

void biomeMinerImport(ecs_world_t *world);

#endif
