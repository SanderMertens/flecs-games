#ifndef ORE_ELSE_GAME_H
#define ORE_ELSE_GAME_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef ORE_ELSE_GAME_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define ORE_ROWS (48)
#define ORE_COLS (48)
#define ORE_STOCK_SLOTS (15)

ECS_ENUM(OreGameState, {
    OreGameStatePlaying,
    OreGameStateWon,
    OreGameStateLost,
    OreGameStatePaused,
    OreGameStateTitle
});

ECS_STRUCT(OreMap, {
    float x0;
    float z0;
    float tile;
});

ECS_STRUCT(OreClock, {
    float time_elapsed;
});

ECS_STRUCT(OreLuminiteProgress, {
    int32_t luminite_held;
    int32_t luminite_shipped;
});

ECS_STRUCT(OreGame, {
    ecs_entity_t camera;
    ecs_entity_t player;
    ecs_entity_t deposits;
    ecs_entity_t buildings;
    ecs_entity_t critters;
    ecs_entity_t projectiles;
    ecs_entity_t connectors;
    ecs_entity_t fx_pool;
    ecs_entity_t glow_pool;
    ecs_entity_t mine_burst;
    ecs_entity_t poof_burst;
    ecs_entity_t marker_prefab;
    ecs_entity_t marker_bad;
    ecs_entity_t drill;
    ecs_entity_t selected;

ECS_PRIVATE
    ecs_entity_t deposit_grid[ORE_ROWS * ORE_COLS];
    ecs_entity_t building_grid[ORE_ROWS * ORE_COLS];
    ecs_entity_t flora_grid[ORE_ROWS * ORE_COLS];
    ecs_entity_t flora_root;
    int32_t hover_row;
    int32_t hover_col;
    float hover_fx;
    float hover_fz;
});

typedef struct OreCell {
    int32_t row;
    int32_t col;
    float fx;
    float fz;
} OreCell;

float oreRandf(void);

bool oreCellAt(
    ecs_world_t *world,
    const OreGame *game,
    float nx,
    float ny,
    OreCell *out);

bool oreTextSet(char **dst, const char *src);

float oreTileX(const OreMap *map, int32_t col);

float oreTileZ(const OreMap *map, int32_t row);

int32_t oreColAt(const OreMap *map, float x);

int32_t oreRowAt(const OreMap *map, float z);

bool oreOnGrid(int32_t row, int32_t col);

ecs_entity_t* oreDepositCell(OreGame *game, int32_t row, int32_t col);

bool oreDepositMine(
    ecs_world_t *world,
    OreGame *game,
    ecs_entity_t deposit);

void oreExplosion(
    ecs_world_t *world,
    OreGame *game,
    ecs_entity_t burst,
    float x,
    float y,
    float z,
    float scale);

void oreBurst(
    ecs_world_t *world,
    ecs_entity_t pool,
    ecs_entity_t burst,
    float x,
    float y,
    float z);

void oreCombust(
    ecs_world_t *world,
    OreGame *game,
    ecs_entity_t root,
    float x,
    float y,
    float z,
    float r);

void oreSetState(ecs_world_t *world, OreGameState state);

bool orePlaying(ecs_world_t *world);

bool oreTitle(ecs_world_t *world);

void oreResolve(ecs_script_future_t *future, bool value);

const char* oreDocText(
    const char *src,
    char *dst,
    int32_t size);

const char* oreItemName(
    ecs_world_t *world,
    ecs_entity_t item,
    char *dst,
    int32_t size);

ecs_entity_t oreChild(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

void oreSeedMap(ecs_world_t *world);

void oreGameImport(ecs_world_t *world);

void Ore_elseImport(ecs_world_t *world);

#endif
