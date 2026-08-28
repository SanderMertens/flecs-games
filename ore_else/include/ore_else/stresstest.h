#ifndef ORE_ELSE_STRESSTEST_H
#define ORE_ELSE_STRESSTEST_H

#include <flecs.h>
#include <flecs_engine.h>

void oreStressTestSet(float minutes);

bool oreStressTestEnabled(void);

void oreStressTestApply(ecs_world_t *world);

#endif
