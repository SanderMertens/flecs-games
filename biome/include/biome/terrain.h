#ifndef FLECS_TERRAIN_H
#define FLECS_TERRAIN_H

#undef ECS_META_IMPL
#ifndef BIOME_TERRAIN_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define TerrainCellSize (5)         /* meters */

#define TerrainSlopeIndex (0)       /* flecs_vec2_t (height gradient of tile) */
#define TerrainSoilIndex (1)        /* TerrainSoil */
#define TerrainGroundIndex (2)
#define TerrainOccupancyIndex (3)
#define TerrainPowerIndex (4)

ECS_ENUM(TerrainSampleKind, {
    TerrainSampleKindNearest,
    TerrainSampleKindLinear,
});

ECS_STRUCT(TerrainSoil, {
    float sedimentFactor;          /* 0: solid rock, 1: fine sand */
    float fertility;               /* 0: no nutrients, 1: maximum nutrients */
});

ECS_STRUCT(TerrainGround, {
    float moisture;
});

ECS_STRUCT(TerrainOccupancy, {
    uint64_t buildings;
});

extern ECS_COMPONENT_DECLARE(TerrainScatterAssets);
typedef ecs_vec_t TerrainScatterAssets;

/* Scatter prefab across terrain in unoccupied cells */
ECS_STRUCT(TerrainScatter,  {
    TerrainScatterAssets prefab;
    int32_t count;
    float min_height;
    flecs_vec2_t position_variance; /* +/- x/z offset; 0 disables per axis */
    flecs_vec2_t rotation_variance; /* +/- x/y angle; 0 disables per axis */
    flecs_vec3_t scale_variance;    /* +/- prefab scale; 0 disables per axis */
    float cluster;                  /* 0: random, 1: fully clustered */
    float cluster_scale;            /* approximate cluster size in cells */
});

ECS_STRUCT(TerrainColors, {
    flecs_rgba_t initial;
    flecs_rgba_t rock;
    flecs_rgba_t sand;
    flecs_rgba_t wet_soil;
    flecs_rgba_t vegetation;
    flecs_rgba_t dry_ice;
    flecs_rgba_t ice;
});

ECS_STRUCT(Terrain, {
    int16_t width;
    int16_t height;
    TerrainSampleKind sample_kind;
    int16_t color_update_frames;
    TerrainColors colors;
    float scale;
    int16_t octaves;
    int16_t warp_octaves;
    float ridge_freq;
    float ridge_min;
    float ridge_octaves;
    float ridge_mix;
    float height_offset;
    float height_gain;
    float height_base;
    float max_height;
    float warp;
    float river_freq;
    int16_t river_octaves;
    float river_width;
    float river_depth;
    float ocean_scale;
    float ocean_amount;
    float ocean_shore_width;
    float ocean_depth;
    float water_level;
    float ground_moisture;
    float water_moisture_range_tiles;
    float fertility_decay;
});

float biomeHash2(int32_t x, int32_t z);
float biomeNoise2(float x, float z);
float biomeFbm2(float x, float z, int32_t octaves);

void biomeTerrainImport(ecs_world_t *world);

#endif
