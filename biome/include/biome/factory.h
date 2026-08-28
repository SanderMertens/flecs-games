#ifndef FLECS_FACTORY_H
#define FLECS_FACTORY_H

#undef ECS_META_IMPL
#ifndef BIOME_FACTORY_IMPL
#define ECS_META_IMPL EXTERN
#endif

extern ECS_COMPONENT_DECLARE(biome_factory_outputMode_t);
extern ECS_COMPONENT_DECLARE(biome_factory_inputMode_t);

typedef enum {
    BiomeFactoryOutputVent,
    BiomeFactoryOutputStore
} biome_factory_outputMode_t;

typedef enum {
    BiomeFactoryInputSink,
    BiomeFactoryInputCapture
} biome_factory_inputMode_t;

ECS_STRUCT(BiomeFactory, {
    ecs_entity_t recipe;
    biome_factory_outputMode_t output_mode;
    biome_factory_inputMode_t input_mode;
});

bool biome_factory_isActive(
    const ecs_world_t *world,
    ecs_entity_t entity);

int32_t biome_factory_canAfford(
    const ecs_world_t *world, 
    ecs_entity_t item);

bool biome_factory_purchase(
    const ecs_world_t *world,
    ecs_entity_t item,
    int32_t count);

void biome_factory_refund(
    const ecs_world_t *world,
    ecs_entity_t item,
    int32_t count);

void biomeFactoryImport(ecs_world_t *world);

#endif
