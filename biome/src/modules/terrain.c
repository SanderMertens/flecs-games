#define BIOME_TERRAIN_IMPL

#include "biome.h"

ECS_COMPONENT_DECLARE(TerrainScatterAssets);

typedef struct TerrainColorUpdateContext {
    int32_t frame;
} TerrainColorUpdateContext;

static const TerrainColors biomeDefaultTerrainColors = {
    .initial = {120, 96, 74, 230},
    .rock = {125, 125, 125, 255},
    .sand = {178, 142, 104, 255},
    .wet_soil = {58, 43, 31, 255},
    .vegetation = {50, 70, 15, 255},
    .dry_ice = {130, 130, 130, 255},
    .ice = {235, 240, 245, 255}
};

static ECS_CTOR(Terrain, ptr, {
    ecs_os_zeromem(ptr);
    ptr->colors = biomeDefaultTerrainColors;
})

static void biomeTerrainColorUpdateContextFree(void *ptr) {
    ecs_os_free(ptr);
}

float biomeHash2(int32_t x, int32_t z) {
    uint32_t h = (uint32_t)x * 0x27d4eb2du;
    h ^= h >> 15;
    h += (uint32_t)z * 0x165667b1u;
    h ^= h >> 13;
    h *= 0x9e3779b1u;
    h ^= h >> 16;
    return (float)(h % 100000) / 100000.0f;
}

float biomeNoise2(float x, float z) {
    int32_t x0 = (int32_t)floorf(x), z0 = (int32_t)floorf(z);
    float fx = x - (float)x0, fz = z - (float)z0;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);
    float a = biomeHash2(x0, z0);
    float b = biomeHash2(x0 + 1, z0);
    float c = biomeHash2(x0, z0 + 1);
    float d = biomeHash2(x0 + 1, z0 + 1);
    float ab = a + (b - a) * fx;
    float cd = c + (d - c) * fx;
    return ab + (cd - ab) * fz;
}

float biomeFbm2(float x, float z, int32_t octaves) {
    float sum = 0, amp = 1.0f, freq = 1.0f, norm = 0;
    for (int32_t i = 0; i < octaves; i ++) {
        sum += biomeNoise2(x * freq, z * freq) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

static float biomeSmoothstep(float lo, float hi, float v) {
    if (v <= lo) return 0;
    if (v >= hi) return 1;
    v = (v - lo) / (hi - lo);
    return v * v * (3.0f - 2.0f * v);
}

static float biomeRiverDistance(
    const Terrain *t,
    float x,
    float z)
{
    return fabsf(2.0f * biomeFbm2(
        x * t->river_freq + 113.7f,
        z * t->river_freq + 197.3f,
        t->river_octaves) - 1.0f);
}

float biomeTerrainHeightF(const Terrain *t, float fx, float fz) {
    float wx = fx / t->scale, wz = fz / t->scale;

    float qx = biomeFbm2(wx + 13.7f, wz + 71.3f, t->warp_octaves);
    float qz = biomeFbm2(wx + 47.2f, wz + 5.9f, t->warp_octaves);
    float dx = wx + t->warp * (qx - 0.5f);
    float dz = wz + t->warp * (qz - 0.5f);
    float h = biomeFbm2(dx, dz, t->octaves);

    float r = 1.0f - fabsf(2.0f * biomeFbm2(
        wx * t->ridge_freq + 29.0f, wz * t->ridge_freq + 83.0f,
        t->ridge_octaves) - 1.0f);
    h = h * (1.0f - t->ridge_mix) + r * r * t->ridge_mix;

    h = (h - t->height_offset) * t->height_gain + t->height_base;
    if (h < 0) h = 0;
    if (h > 1.0f) h = 1.0f;
    h = h * h * (3.0f - 2.0f * h);

    h *= t->max_height;

    if (t->river_depth > 0 && t->river_freq > 0 &&
        t->river_width > 0 && t->river_octaves > 0)
    {
        float river = biomeRiverDistance(t, dx, dz);
        float step = 1.0f / t->scale;
        float filter = fabsf(
            river - biomeRiverDistance(t, dx - step, dz));
        float sample = fabsf(
            river - biomeRiverDistance(t, dx + step, dz));
        if (sample > filter) filter = sample;
        sample = fabsf(
            river - biomeRiverDistance(t, dx, dz - step));
        if (sample > filter) filter = sample;
        sample = fabsf(
            river - biomeRiverDistance(t, dx, dz + step));
        if (sample > filter) filter = sample;
        float inner = t->river_width - filter;
        if (inner < 0) inner = 0;
        float river_mask = 1.0f - biomeSmoothstep(
            inner, t->river_width + filter, river);
        h -= river_mask * t->river_depth;
    }

    if (t->ocean_depth > 0 && t->ocean_scale > 0 &&
        t->ocean_amount > 0)
    {
        float ocean_amount = t->ocean_amount;
        if (ocean_amount > 1.0f) ocean_amount = 1.0f;
        float ocean_freq = t->scale / t->ocean_scale;
        float ocean = biomeFbm2(
            dx * ocean_freq + 251.9f,
            dz * ocean_freq + 337.1f,
            2);
        float shore = t->ocean_shore_width;
        float ocean_mask;
        if (ocean_amount == 1.0f) {
            ocean_mask = 1.0f;
        } else if (shore > 0) {
            ocean_mask = 1.0f - biomeSmoothstep(
                ocean_amount - shore,
                ocean_amount + shore,
                ocean);
        } else {
            ocean_mask = ocean < ocean_amount;
        }
        h -= ocean_mask * t->ocean_depth;
    }

    return h;
}

float biomeCornerHeight(const Terrain *t, int32_t x, int32_t z) {
    return biomeTerrainHeightF(t, (float)x - 0.5f, (float)z - 0.5f);
}

static void biomeTerrainInitializeMoisture(
    const FlecsTerrain *terrain,
    const Terrain *config,
    TerrainGround *ground);

void FlecsTerrainOnSet(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    FlecsTerrain *cfg = ecs_field(it, FlecsTerrain, 0);

    int32_t w = cfg->width, d = cfg->depth;
    if (w <= 0 || d <= 0) {
        return;
    }
    if (ecs_vec_count(&cfg->heights) != (w + 1) * (d + 1)) {
        return;
    }

    flecs_vec2_t *slope = flecsEngine_terrain_getLayer(world, e, TerrainSlopeIndex, flecs_vec2_t);
    if (!slope) {
        return;
    }

    flecsEngine_terrain_computeSlope(world, e, slope);

    TerrainSoil *soil = flecsEngine_terrain_getLayer(
        world, e, TerrainSoilIndex, TerrainSoil);
    const Terrain *terrain = ecs_get(world, e, Terrain);
    if (!soil || !terrain) {
        return;
    }

    float max_h = terrain->max_height > 0 ? terrain->max_height : 1.0f;
    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = z * w + x;
            float h = flecsEngine_terrainCellHeight(cfg, x, z);

            float n = biomeFbm2(
                (float)x * 0.06f + 11.3f, (float)z * 0.06f + 42.7f, 3);

            float altitude = h / max_h;
            if (altitude < 0) altitude = 0;
            if (altitude > 1.0f) altitude = 1.0f;

            float sand = (n - altitude) * 4.0f + 0.5f;
            if (sand < 0) sand = 0;
            if (sand > 1.0f) sand = 1.0f;

            soil[i].sedimentFactor = sand;
            soil[i].fertility = 0;
        }
    }
}

void TerrainOnSet(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    Terrain *cfg = ecs_field(it, Terrain, 0);
    Terrain config = *cfg;
    cfg = &config;
    bool was_deferred = ecs_is_deferred(world);
    if (was_deferred) {
        ecs_defer_suspend(world);
    }

    float ex = (float)(cfg->width / 2) * TerrainCellSize;
    float ez = (float)(cfg->height / 2) * TerrainCellSize;
    ecs_set(world, e, FlecsPosition3, { -ex, 0, -ez });

    FlecsTerrain *t = ecs_ensure(world, e, FlecsTerrain);
    t->width = cfg->width;
    t->depth = cfg->height;
    t->cell_size = TerrainCellSize;

    int32_t cw = cfg->width + 1, ch = cfg->height + 1;
    ecs_vec_set_count_t(NULL, &t->heights, float, cw * ch);
    float *h = ecs_vec_first_t(&t->heights, float);
    for (int32_t z = 0; z < ch; z ++) {
        for (int32_t x = 0; x < cw; x ++) {
            h[z * cw + x] = biomeCornerHeight(cfg, x, z);
        }
    }

    int32_t cells = cfg->width * cfg->height;
    ecs_vec_set_count_t(NULL, &t->colors, flecs_rgba_t, cells);
    flecs_rgba_t *colors = ecs_vec_first_t(&t->colors, flecs_rgba_t);
    for (int32_t i = 0; i < cells; i ++) {
        colors[i] = cfg->colors.initial;
    }

    ecs_vec_set_count_t(NULL, &t->layerTypes, ecs_entity_t, 5);
    ecs_entity_t *layerTypes = ecs_vec_first_t(&t->layerTypes, ecs_entity_t);
    layerTypes[TerrainSlopeIndex] = ecs_id(flecs_vec2_t);
    layerTypes[TerrainSoilIndex] = ecs_id(TerrainSoil);
    layerTypes[TerrainGroundIndex] = ecs_id(TerrainGround);
    layerTypes[TerrainOccupancyIndex] = ecs_id(TerrainOccupancy);
    layerTypes[TerrainPowerIndex] = ecs_id(TerrainPower);

    ecs_vec_set_count_t(NULL, &t->layer_scale, int8_t, 5);
    int8_t *layer_scale = ecs_vec_first_t(&t->layer_scale, int8_t);
    for (int32_t i = 0; i < 5; i ++) {
        layer_scale[i] = 1;
    }

    ecs_modified(world, e, FlecsTerrain);

    const FlecsTerrain *generated = ecs_get(world, e, FlecsTerrain);
    TerrainGround *ground = flecsEngine_terrain_getLayer(
        world, e, TerrainGroundIndex, TerrainGround);
    if (generated && ground) {
        biomeTerrainInitializeMoisture(generated, cfg, ground);
    }

    if (was_deferred) {
        ecs_defer_resume(world);
    }
}

static void biomeTerrainInitializeMoisture(
    const FlecsTerrain *terrain,
    const Terrain *config,
    TerrainGround *ground)
{
    int32_t width = terrain->width;
    int32_t depth = terrain->depth;
    int32_t cell_count = width * depth;
    bool *submerged = ecs_os_malloc_n(bool, cell_count);
    float baseline = config->ground_moisture;
    if (baseline < 0) baseline = 0;
    if (baseline > 1) baseline = 1;

    for (int32_t z = 0; z < depth; z ++) {
        for (int32_t x = 0; x < width; x ++) {
            int32_t i = z * width + x;
            submerged[i] = flecsEngine_terrainCellHeight(
                terrain, x, z) <= config->water_level;
            ground[i].moisture = submerged[i] ? 1.0f : baseline;
        }
    }

    float range = config->water_moisture_range_tiles;
    int32_t radius = (int32_t)ceilf(range);
    float range_squared = range * range;
    if (range > 0) {
        for (int32_t z = 0; z < depth; z ++) {
            for (int32_t x = 0; x < width; x ++) {
                int32_t i = z * width + x;
                if (submerged[i]) {
                    continue;
                }
                float nearest_squared = range_squared;
                int32_t min_z = z > radius ? z - radius : 0;
                int32_t max_z = z + radius < depth
                    ? z + radius : depth - 1;
                int32_t min_x = x > radius ? x - radius : 0;
                int32_t max_x = x + radius < width
                    ? x + radius : width - 1;
                for (int32_t water_z = min_z; water_z <= max_z; water_z ++) {
                    for (int32_t water_x = min_x;
                        water_x <= max_x; water_x ++)
                    {
                        if (!submerged[water_z * width + water_x]) {
                            continue;
                        }
                        float dx = (float)(water_x - x);
                        float dz = (float)(water_z - z);
                        float distance_squared = dx * dx + dz * dz;
                        if (distance_squared < nearest_squared) {
                            nearest_squared = distance_squared;
                        }
                    }
                }
                float moisture = 1.0f - sqrtf(nearest_squared) / range;
                if (moisture > ground[i].moisture) {
                    ground[i].moisture = moisture;
                }
            }
        }
    }

    ecs_os_free(submerged);
}

static float biomeClampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void biomeMixColor(float rgb[3], flecs_rgba_t target, float f) {
    rgb[0] += ((float)target.r - rgb[0]) * f;
    rgb[1] += ((float)target.g - rgb[1]) * f;
    rgb[2] += ((float)target.b - rgb[2]) * f;
}

static void biomeMixRgb(float rgb[3], const float target[3], float f) {
    rgb[0] += (target[0] - rgb[0]) * f;
    rgb[1] += (target[1] - rgb[1]) * f;
    rgb[2] += (target[2] - rgb[2]) * f;
}

static flecs_rgba_t biomeGroundColor(
    const TerrainColors *c,
    const TerrainGround *g,
    const TerrainSoil *s,
    float temperature)
{
    float sediment = biomeClampf(s->sedimentFactor, 0, 1.0f);
    float rgb[3] = {(float)c->rock.r, (float)c->rock.g, (float)c->rock.b};
    biomeMixColor(rgb, c->sand, sediment);

    float moisture = biomeClampf(g->moisture, 0, 1.0f);
    moisture = moisture * moisture * (3.0f - 2.0f * moisture);
    float wetness = moisture * sediment;
    biomeMixColor(rgb, c->wet_soil, wetness);
    biomeMixColor(rgb, c->vegetation, biomeClampf(s->fertility, 0, 1.0f));
    float dry_frozen = biomeClampf((-temperature - 5.0f) / 10.0f, 0, 1.0f);
    float wet_frozen = biomeClampf(-temperature / 10.0f, 0, 1.0f);
    float dry_rgb[3] = {rgb[0], rgb[1], rgb[2]};
    float wet_rgb[3] = {rgb[0], rgb[1], rgb[2]};
    biomeMixColor(dry_rgb, c->dry_ice, dry_frozen);
    biomeMixColor(wet_rgb, c->ice, wet_frozen);
    biomeMixRgb(dry_rgb, wet_rgb, wetness);
    rgb[0] = dry_rgb[0];
    rgb[1] = dry_rgb[1];
    rgb[2] = dry_rgb[2];
    float roughness = 250.0f - 100.0f * wetness;

    return (flecs_rgba_t){
        (uint8_t)biomeClampf(rgb[0], 0, 255.0f),
        (uint8_t)biomeClampf(rgb[1], 0, 255.0f),
        (uint8_t)biomeClampf(rgb[2], 0, 255.0f),
        (uint8_t)biomeClampf(roughness, 0, 255.0f)
    };
}

static float biomeLerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static TerrainGround biomeSampleGroundLinear(
    const TerrainGround *ground,
    int32_t width,
    int32_t depth,
    int32_t scale,
    int32_t x,
    int32_t z)
{
    /* Layer values represent samples at the centers of their cells. Convert
     * the terrain cell center to that coordinate space before interpolating. */
    float fx = ((float)x + 0.5f) / (float)scale - 0.5f;
    float fz = ((float)z + 0.5f) / (float)scale - 0.5f;
    fx = biomeClampf(fx, 0, (float)(width - 1));
    fz = biomeClampf(fz, 0, (float)(depth - 1));

    int32_t x0 = (int32_t)floorf(fx);
    int32_t z0 = (int32_t)floorf(fz);
    int32_t x1 = x0 + 1 < width ? x0 + 1 : x0;
    int32_t z1 = z0 + 1 < depth ? z0 + 1 : z0;
    float tx = fx - (float)x0;
    float tz = fz - (float)z0;

    const TerrainGround *g00 = &ground[z0 * width + x0];
    const TerrainGround *g10 = &ground[z0 * width + x1];
    const TerrainGround *g01 = &ground[z1 * width + x0];
    const TerrainGround *g11 = &ground[z1 * width + x1];

#define BIOME_SAMPLE_FIELD(field) biomeLerp( \
    biomeLerp(g00->field, g10->field, tx), \
    biomeLerp(g01->field, g11->field, tx), tz)

    TerrainGround result = {
        .moisture = BIOME_SAMPLE_FIELD(moisture)
    };

#undef BIOME_SAMPLE_FIELD

    return result;
}

void ApplyTerrainColors(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    const Terrain *cfg = ecs_get(world, e, Terrain);
    const Weather *weather = ecs_singleton_get(world, Weather);
    float temperature = weather ? weather->temperature : 0;

    int32_t cells = t->width * t->depth;
    if (cells <= 0 || ecs_vec_count(&t->colors) != cells) {
        return;
    }
    if (ecs_vec_count(&t->layerTypes) <= TerrainGroundIndex) {
        return;
    }

    const TerrainGround *ground = flecsEngine_terrain_getLayer(
        world, e, TerrainGroundIndex, TerrainGround);
    const TerrainSoil *soil = flecsEngine_terrain_getLayer(
        world, e, TerrainSoilIndex, TerrainSoil);
    if (!ground || !soil) {
        return;
    }

    flecs_rgba_t *colors = ecs_vec_first_t(&t->colors, flecs_rgba_t);
    int32_t ground_w, ground_d;
    flecsEngine_terrainLayerDimensions(
        t, TerrainGroundIndex, &ground_w, &ground_d);
    int32_t ground_scale = flecsEngine_terrainLayerScale(
        t, TerrainGroundIndex);
    if (ground_w <= 0 || ground_d <= 0 || ground_scale <= 0) {
        return;
    }

    TerrainSampleKind sample_kind = cfg
        ? cfg->sample_kind
        : TerrainSampleKindNearest;
    const TerrainColors *terrain_colors = cfg
        ? &cfg->colors
        : &biomeDefaultTerrainColors;
    int32_t update_frames = cfg && cfg->color_update_frames > 0
        ? cfg->color_update_frames
        : 60;
    TerrainColorUpdateContext *ctx = it->ctx;
    int32_t frame = ctx->frame % update_frames;
    int32_t begin = (int32_t)((int64_t)cells * frame / update_frames);
    int32_t end = (int32_t)((int64_t)cells * (frame + 1) / update_frames);
    ctx->frame = (frame + 1) % update_frames;

    for (int32_t i = begin; i < end; i ++) {
        int32_t x = i % t->width;
        int32_t z = i / t->width;
        if (sample_kind == TerrainSampleKindLinear) {
            TerrainGround sample = biomeSampleGroundLinear(
                ground, ground_w, ground_d, ground_scale, x, z);
            colors[i] = biomeGroundColor(
                terrain_colors, &sample, &soil[i], temperature);
        } else {
            int32_t ground_x = x / ground_scale;
            int32_t ground_z = z / ground_scale;
            if (ground_x >= ground_w) ground_x = ground_w - 1;
            if (ground_z >= ground_d) ground_z = ground_d - 1;
            const TerrainGround *sample = &ground[
                ground_z * ground_w + ground_x];
            colors[i] = biomeGroundColor(
                terrain_colors, sample, &soil[i], temperature);
        }
    }

    if (begin != end) {
        flecsEngine_terrainColorsModifiedRange(
            world, e, begin, end - begin);
    }
}

static uint32_t biomeScatterRandom(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float biomeScatterVariance(uint32_t *state, float variance) {
    if (variance <= 0) {
        return 0;
    }

    float unit = (float)biomeScatterRandom(state) / 4294967296.0f;
    return (unit * 2.0f - 1.0f) * variance;
}

typedef struct biome_scatter_candidate_t {
    int32_t cell;
    float score;
} biome_scatter_candidate_t;

static int biomeScatterCompareCandidate(const void *ptr_a, const void *ptr_b) {
    const biome_scatter_candidate_t *a = ptr_a;
    const biome_scatter_candidate_t *b = ptr_b;
    if (a->score < b->score) return -1;
    if (a->score > b->score) return 1;
    return (a->cell > b->cell) - (a->cell < b->cell);
}

static void biomeScatterClusterCells(
    int32_t *available,
    int32_t available_count,
    int32_t width,
    int32_t depth,
    float cluster,
    float cluster_scale,
    uint32_t *random)
{
    if (cluster <= 0 || available_count <= 1) {
        return;
    }
    if (cluster > 1) {
        cluster = 1;
    }
    if (!(cluster_scale > 0)) {
        cluster_scale = (float)(width > depth ? width : depth) / 8.0f;
        if (cluster_scale < 1) {
            cluster_scale = 1;
        }
    }

    float noise_x = (float)biomeScatterRandom(random) /
        4194304.0f;
    float noise_y = (float)biomeScatterRandom(random) /
        4194304.0f;
    float random_weight = 1.0f - cluster;

    biome_scatter_candidate_t *candidates = ecs_os_malloc_n(
        biome_scatter_candidate_t, available_count);
    for (int32_t i = 0; i < available_count; i ++) {
        int32_t cell = available[i];
        float x = ((float)(cell % width) + 0.5f) / cluster_scale;
        float y = ((float)(cell / width) + 0.5f) / cluster_scale;
        float noise_score = 1.0f - biomeFbm2(
            x + noise_x, y + noise_y, 3);
        float random_score = (float)biomeScatterRandom(random) /
            4294967296.0f;
        candidates[i] = (biome_scatter_candidate_t){
            cell,
            random_score * random_weight + noise_score * cluster
        };
    }

    qsort(candidates, available_count, sizeof(biome_scatter_candidate_t),
        biomeScatterCompareCandidate);
    for (int32_t i = 0; i < available_count; i ++) {
        available[i] = candidates[i].cell;
    }
    ecs_os_free(candidates);
}

static void TerrainScatterOnSet(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    TerrainScatter *scatter = ecs_field(it, TerrainScatter, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t terrain = it->entities[i];
        if (!ecs_has(world, terrain, FlecsTerrain)) {
            terrain = ecs_id(Terrain);
        }
        const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
        if (!t || t->width <= 0 || t->depth <= 0 ||
            scatter[i].count <= 0)
        {
            continue;
        }

        int32_t prefab_count = ecs_vec_count(&scatter[i].prefab);
        ecs_entity_t *prefabs = ecs_vec_first_t(
            &scatter[i].prefab, ecs_entity_t);
        ecs_entity_t *valid_prefabs = ecs_os_malloc_n(
            ecs_entity_t, prefab_count);
        int32_t valid_prefab_count = 0;
        for (int32_t p = 0; p < prefab_count; p ++) {
            if (prefabs[p] && ecs_is_alive(world, prefabs[p])) {
                valid_prefabs[valid_prefab_count ++] = prefabs[p];
            }
        }
        if (!valid_prefab_count) {
            ecs_os_free(valid_prefabs);
            continue;
        }

        int64_t cell_count_64 = (int64_t)t->width * t->depth;
        if (cell_count_64 > INT32_MAX ||
            ecs_vec_count(&t->layerTypes) <= TerrainOccupancyIndex)
        {
            ecs_os_free(valid_prefabs);
            continue;
        }

        const TerrainOccupancy *occupancy = flecsEngine_terrain_getLayer(
            world, terrain, TerrainOccupancyIndex, TerrainOccupancy);
        if (!occupancy) {
            ecs_os_free(valid_prefabs);
            continue;
        }

        int32_t cell_count = (int32_t)cell_count_64;
        int32_t *available = ecs_os_malloc_n(int32_t, cell_count);
        int32_t available_count = 0;
        for (int32_t cell = 0; cell < cell_count; cell ++) {
            int32_t x = cell % t->width;
            int32_t y = cell / t->width;
            if (!occupancy[cell].buildings &&
                flecsEngine_terrainCellHeight(t, x, y) >=
                    scatter[i].min_height)
            {
                available[available_count ++] = cell;
            }
        }

        int32_t spawn_count = scatter[i].count;
        if (spawn_count > available_count) {
            spawn_count = available_count;
        }

        /* Use a local PRNG so scattering is reproducible and does not perturb
         * random number generation in other systems. */
        uint32_t selection_random = 0;
        selection_random ^= (uint32_t)t->width * 0x85ebca6bu;
        selection_random ^= (uint32_t)t->depth * 0xc2b2ae35u;
        selection_random ^= (uint32_t)valid_prefabs[0];
        for (int32_t p = 1; p < valid_prefab_count; p ++) {
            selection_random ^= (uint32_t)valid_prefabs[p] *
                (0x85ebca6bu + (uint32_t)p * 0xc2b2ae35u);
        }
        if (!selection_random) {
            selection_random = 1;
        }
        uint32_t variance_random = selection_random ^ 0xa511e9b3u;
        uint32_t prefab_random = selection_random ^ 0x63d83595u;
        float cluster = scatter[i].cluster;
        if (!(cluster > 0)) {
            cluster = 0;
        }
        biomeScatterClusterCells(available, available_count,
            t->width, t->depth, cluster, scatter[i].cluster_scale,
            &selection_random);

        bool was_deferred = ecs_is_deferred(world);
        if (was_deferred) {
            ecs_defer_suspend(world);
        }

        for (int32_t n = 0; n < spawn_count; n ++) {
            int32_t cell;
            if (cluster > 0) {
                cell = available[n];
            } else {
                uint32_t remaining = (uint32_t)(available_count - n);
                int32_t selected = n + (int32_t)(
                    biomeScatterRandom(&selection_random) % remaining);
                cell = available[selected];
                available[selected] = available[n];
            }

            int32_t x = cell % t->width;
            int32_t y = cell / t->width;
            float offset_x = biomeScatterVariance(
                &variance_random, scatter[i].position_variance.x);
            float offset_z = biomeScatterVariance(
                &variance_random, scatter[i].position_variance.y);
            float rotation_x = biomeScatterVariance(
                &variance_random, scatter[i].rotation_variance.x);
            float rotation_y = biomeScatterVariance(
                &variance_random, scatter[i].rotation_variance.y);
            float scale_x = biomeScatterVariance(
                &variance_random, scatter[i].scale_variance.x);
            float scale_y = biomeScatterVariance(
                &variance_random, scatter[i].scale_variance.y);
            float scale_z = biomeScatterVariance(
                &variance_random, scatter[i].scale_variance.z);

            ecs_entity_t prefab = valid_prefabs[
                biomeScatterRandom(&prefab_random) %
                    (uint32_t)valid_prefab_count];
            ecs_entity_t instance = ecs_new_w_pair(
                world, EcsIsA, prefab);
            ecs_add_pair(world, instance, EcsChildOf, terrain);
            ecs_set(world, instance, FlecsTerrainPosition, {
                .terrain = terrain,
                .x = x,
                .y = y,
                .yaw = rotation_y
            });

            if (offset_x || offset_z) {
                float px = ((float)x + 0.5f) * t->cell_size + offset_x;
                float pz = ((float)y + 0.5f) * t->cell_size + offset_z;
                ecs_set(world, instance, FlecsPosition3, {
                    px, flecsEngine_terrainSampleHeight(t, px, pz), pz
                });
            }
            if (rotation_x) {
                ecs_set(world, instance, FlecsRotation3, {
                    rotation_x, rotation_y, 0
                });
            }
            if (scale_x || scale_y || scale_z) {
                FlecsScale3 base_scale = {1, 1, 1};
                const FlecsScale3 *prefab_scale = ecs_get(
                    world, prefab, FlecsScale3);
                if (prefab_scale) {
                    base_scale = *prefab_scale;
                }
                ecs_set(world, instance, FlecsScale3, {
                    base_scale.x + scale_x,
                    base_scale.y + scale_y,
                    base_scale.z + scale_z
                });
            }
        }

        if (was_deferred) {
            ecs_defer_resume(world);
        }

        ecs_os_free(available);
        ecs_os_free(valid_prefabs);
    }
}

void biomeTerrainImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeTerrain);

    ecs_set_name_prefix(world, "Terrain");
    ECS_META_COMPONENT(world, TerrainSampleKind);
    ECS_META_COMPONENT(world, TerrainColors);

    ecs_set_name_prefix(world, NULL);
    ECS_META_COMPONENT(world, Terrain);

    ecs_set_name_prefix(world, "Terrain");

    ECS_META_COMPONENT(world, TerrainSoil);
    ECS_META_COMPONENT(world, TerrainGround);
    ECS_META_COMPONENT(world, TerrainOccupancy);
    ecs_id(TerrainScatterAssets) = ecs_vector(world, {
        .entity = ecs_entity(world, {
            .name = "ScatterAssets",
            .symbol = "TerrainScatterAssets"
        }),
        .type = ecs_id(ecs_entity_t)
    });
    ECS_META_COMPONENT(world, TerrainScatter);

    ecs_set_hooks(world, Terrain, {
        .ctor = ecs_ctor(Terrain),
        .on_set = TerrainOnSet
    });

    ecs_observer(world, {
        .query.terms = {{ ecs_id(FlecsTerrain) }},
        .events = { EcsOnSet },
        .callback = FlecsTerrainOnSet
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(TerrainScatter) }},
        .events = { EcsOnSet },
        .callback = TerrainScatterOnSet
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ApplyTerrainColors" }),
        .query.terms = {{ .id = ecs_id(FlecsTerrain) }},
        .phase = EcsPreStore,
        .callback = ApplyTerrainColors,
        .ctx = ecs_os_calloc_t(TerrainColorUpdateContext),
        .ctx_free = biomeTerrainColorUpdateContextFree
    });
}
