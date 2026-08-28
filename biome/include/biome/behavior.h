#ifndef BIOME_BEHAVIOR_H
#define BIOME_BEHAVIOR_H

extern ECS_COMPONENT_DECLARE(BiomeBehaviors);

typedef ecs_vec_t BiomeBehaviors;

void biomeBehaviorImport(ecs_world_t *world);

#endif
