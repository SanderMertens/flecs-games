#define BIOME_TOOL_IMPL

#include "biome.h"

#define BiomeToolMaxGhosts (65536)
#define BiomeToolCursorValid (1)
#define BiomeToolCursorInvalid (2)
#define BiomeToolCursorDoze (3)

typedef struct ToolCursorState {
    ecs_entity_t scope;
    ecs_entity_t valid;
    ecs_entity_t invalid;
    ecs_entity_t doze;
} ToolCursorState;

ECS_COMPONENT_DECLARE(ToolCursorState);

void biomeToolPlaceBuilding(
    ecs_world_t *world,
    ecs_entity_t building)
{
    BiomeTool *tool = ecs_singleton_ensure(world, BiomeTool);
    tool->building = building;
    tool->doze = false;
    ecs_singleton_modified(world, BiomeTool);
}

void biomeToolDoze(
    ecs_world_t *world)
{
    BiomeTool *tool = ecs_singleton_ensure(world, BiomeTool);
    tool->building = 0;
    tool->doze = true;
    tool->dragging = false;
    ecs_singleton_modified(world, BiomeTool);
}

static ecs_entity_t biome_tool_camera(ecs_world_t *world) {
    ecs_entity_t camera = 0;
    ecs_iter_t it = ecs_each(world, FlecsRenderView);
    while (ecs_each_next(&it)) {
        FlecsRenderView *v = ecs_field(&it, FlecsRenderView, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            if (!camera) {
                camera = v[i].camera;
            }
        }
    }
    return camera;
}

static ecs_entity_t biome_tool_terrain(ecs_world_t *world) {
    ecs_entity_t terrain = 0;
    ecs_iter_t it = ecs_each(world, FlecsTerrain);
    while (ecs_each_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            if (!terrain) {
                terrain = it.entities[i];
            }
        }
    }
    return terrain;
}

static bool biome_tool_pickTile(
    ecs_world_t *world,
    ecs_entity_t camera,
    ecs_entity_t terrain,
    float view_norm_x,
    float view_norm_y,
    int32_t *tile_x,
    int32_t *tile_y,
    float *tile_offset_x,
    float *tile_offset_y)
{
    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    if (!t || t->width <= 0 || t->depth <= 0 || t->cell_size <= 0) {
        return false;
    }

    vec3 origin, dir;
    if (!flecsEngine_cameraScreenRay(
        world, camera, view_norm_x, view_norm_y, origin, dir))
    {
        return false;
    }

    float ox = 0, oy = 0, oz = 0;
    const FlecsPosition3 *tp = ecs_get(world, terrain, FlecsPosition3);
    if (tp) {
        ox = tp->x;
        oy = tp->y;
        oz = tp->z;
    }

    float step = t->cell_size * 0.25f;
    float max_dist = 2.0f * (float)(t->width + t->depth) * t->cell_size;

    for (float d = 0; d < max_dist; d += step) {
        float px = origin[0] + dir[0] * d;
        float py = origin[1] + dir[1] * d;
        float pz = origin[2] + dir[2] * d;

        int32_t cx = (int32_t)floorf((px - ox) / t->cell_size);
        int32_t cz = (int32_t)floorf((pz - oz) / t->cell_size);
        if (cx < 0 || cz < 0 || cx >= t->width || cz >= t->depth) {
            continue;
        }

        if (py - oy <= flecsEngine_terrainCellHeight(t, cx, cz)) {
            *tile_x = cx;
            *tile_y = cz;
            if (tile_offset_x) {
                *tile_offset_x =
                    (px - ox) / t->cell_size - (float)cx;
            }
            if (tile_offset_y) {
                *tile_offset_y =
                    (pz - oz) / t->cell_size - (float)cz;
            }
            return true;
        }
    }

    return false;
}

static TerrainOccupancy* biome_tool_occupancy(
    ecs_world_t *world,
    ecs_entity_t terrain)
{
    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    if (!t || ecs_vec_count(&t->layerTypes) <= TerrainOccupancyIndex) {
        return NULL;
    }

    return flecsEngine_terrain_getLayer(
        world, terrain, TerrainOccupancyIndex, TerrainOccupancy);
}

static uint64_t biome_tool_buildingMask(
    ecs_world_t *world,
    ecs_entity_t building)
{
    const BiomeBuildingBit *bit = ecs_get(
        world, building, BiomeBuildingBit);
    if (!bit || *bit < 0 || *bit >= 64) {
        return 0;
    }

    return 1llu << *bit;
}

static uint64_t biome_tool_buildingRequirementMask(
    ecs_world_t *world,
    ecs_entity_t building)
{
    const BiomeBuildingRequirementMask *mask = ecs_get(
        world, building, BiomeBuildingRequirementMask);
    return mask ? *mask : 0;
}

static ecs_entity_t biome_tool_buildingRule(
    const ecs_world_t *world,
    ecs_entity_t building)
{
    const BiomeBuilding *b = ecs_get(world, building, BiomeBuilding);
    return b ? b->rule : 0;
}

static int32_t biome_tool_buildingMaxCount(
    ecs_world_t *world,
    ecs_entity_t building)
{
    const BiomeBuilding *b = ecs_get(world, building, BiomeBuilding);
    if (!b) {
        return 0;
    }

    return (int32_t)b->max_count;
}

static int32_t biome_tool_buildingCount(
    ecs_world_t *world,
    ecs_entity_t building)
{
    int32_t result = 0;
    ecs_iter_t it = ecs_each_id(world, ecs_pair(EcsIsA, building));
    while (ecs_each_next(&it)) {
        if (it.table &&
            ecs_table_has_id(ecs_get_world(world), it.table, EcsPrefab))
        {
            continue;
        }

        result += it.count;
    }

    return result;
}

static void biome_tool_footprint(
    ecs_world_t *world,
    ecs_entity_t building,
    int32_t *w,
    int32_t *h,
    int32_t *stride)
{
    *w = 1;
    *h = 1;
    *stride = 0;

    const BiomeBuilding *b = ecs_get(world, building, BiomeBuilding);
    if (b) {
        if (b->footprint.x > 1) {
            *w = (int32_t)b->footprint.x;
        }
        if (b->footprint.y > 1) {
            *h = (int32_t)b->footprint.y;
        }
        if (b->drag_stride > 0) {
            *stride = (int32_t)b->drag_stride;
        }
    }
}

static bool biome_tool_slotAvailable(
    const ecs_world_t *world,
    const FlecsTerrain *t,
    const TerrainOccupancy *occ,
    int32_t x,
    int32_t y,
    int32_t fw,
    int32_t fh,
    uint64_t requirement_mask,
    bool is_plant)
{
    if (x < 0 || y < 0 || (x + fw) > t->width || (y + fh) > t->depth) {
        return false;
    }

    if (occ) {
        for (int32_t cy = y; cy < y + fh; cy ++) {
            for (int32_t cx = x; cx < x + fw; cx ++) {
                uint64_t buildings = occ[cy * t->width + cx].buildings;
                if (requirement_mask && !(buildings & requirement_mask)) {
                    return false;
                }
                if (buildings & ~requirement_mask) {
                    return false;
                }
            }
        }
    } else if (requirement_mask) {
        return false;
    }

    if (is_plant) {
        for (int32_t cy = y; cy < y + fh; cy ++) {
            for (int32_t cx = x; cx < x + fw; cx ++) {
                const TerrainItemRecord *record =
                    biome_terrainItemIndex_get(world, cx, cy);
                if (!record) {
                    continue;
                }
                int32_t count = ecs_vec_count(&record->entities);
                const ecs_entity_t *entities = ecs_vec_first_t(
                    &record->entities, ecs_entity_t);
                for (int32_t e = 0; e < count; e ++) {
                    if (ecs_is_alive(world, entities[e]) &&
                        !ecs_has(world, entities[e], BiomePlant))
                    {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

typedef struct biome_tool_region_t {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    int32_t anchor_x;
    int32_t anchor_y;
    int32_t dest_x;
    int32_t dest_y;
    int32_t count;
    bool rect;
    bool horizontal_entry;
} biome_tool_region_t;

static bool biome_tool_regionContains(
    const biome_tool_region_t *region,
    int32_t x,
    int32_t y);

static BiomeBuildingRulePlacement* biome_tool_plan(
    const ecs_world_t *world,
    const FlecsTerrain *t,
    const TerrainOccupancy *occ,
    const biome_tool_region_t *region,
    int32_t fw,
    int32_t fh,
    int32_t step_x,
    int32_t step_y,
    uint64_t requirement_mask,
    ecs_entity_t building_rule,
    uint64_t building_mask,
    bool is_plant,
    int32_t *active_count)
{
    int32_t count = region->count;
    BiomeBuildingRulePlacement *result = ecs_os_malloc_n(
        BiomeBuildingRulePlacement, count);
    int32_t p = 0;
    for (int32_t sy = region->y0; sy <= region->y1; sy += step_y) {
        for (int32_t sx = region->x0; sx <= region->x1; sx += step_x) {
            if (!biome_tool_regionContains(region, sx, sy)) {
                continue;
            }
            result[p ++] = (BiomeBuildingRulePlacement) {
                .x = sx,
                .y = sy,
                .width = fw,
                .height = fh,
                .active = biome_tool_slotAvailable(
                    world, t, occ, sx, sy, fw, fh, requirement_mask,
                    is_plant)
            };
        }
    }

    ecs_assert(p == count, ECS_INTERNAL_ERROR, NULL);
    *active_count = biomeBuildingRuleFilterPlacements(
        world, building_rule, t, occ, building_mask, result, count);
    return result;
}

static bool biome_tool_shiftDown(const FlecsInput *input) {
    return input->keys[FLECS_KEY_LEFT_SHIFT].state ||
        input->keys[FLECS_KEY_RIGHT_SHIFT].state;
}

static biome_tool_region_t biome_tool_region(
    const BiomeTool *tool,
    bool rect,
    int32_t cur_x,
    int32_t cur_y,
    float tile_offset_x,
    float tile_offset_y,
    int32_t step_x,
    int32_t step_y)
{
    biome_tool_region_t result = {
        .x0 = cur_x, .y0 = cur_y, .x1 = cur_x, .y1 = cur_y,
        .anchor_x = cur_x, .anchor_y = cur_y,
        .dest_x = cur_x, .dest_y = cur_y,
        .count = 1, .rect = rect
    };

    if (!tool->dragging) {
        return result;
    }

    int32_t ax = tool->anchor_x, ay = tool->anchor_y;
    result.x0 = ax < cur_x ? ax : cur_x;
    result.x1 = ax > cur_x ? ax : cur_x;
    result.y0 = ay < cur_y ? ay : cur_y;
    result.y1 = ay > cur_y ? ay : cur_y;
    result.anchor_x = ax;
    result.anchor_y = ay;

    if (cur_x == ax) {
        result.horizontal_entry = false;
    } else if (cur_y == ay) {
        result.horizontal_entry = true;
    } else {
        /* Each L can enter the destination through only one of the two sides
         * that face the anchor. Compare the cursor against those sides, not
         * against the nearest side on each axis. */
        float horizontal_dist = cur_x > ax
            ? tile_offset_x : 1.0f - tile_offset_x;
        float vertical_dist = cur_y > ay
            ? tile_offset_y : 1.0f - tile_offset_y;
        result.horizontal_entry = horizontal_dist <= vertical_dist;
    }

    if (result.x0 < ax) {
        result.x0 = ax - ((ax - result.x0 + step_x - 1) / step_x) * step_x;
    }
    if (result.y0 < ay) {
        result.y0 = ay - ((ay - result.y0 + step_y - 1) / step_y) * step_y;
    }

    int32_t nx = (result.x1 - result.x0) / step_x + 1;
    int32_t ny = (result.y1 - result.y0) / step_y + 1;
    result.dest_x = cur_x < ax
        ? result.x0
        : ax + ((cur_x - ax) / step_x) * step_x;
    result.dest_y = cur_y < ay
        ? result.y0
        : ay + ((cur_y - ay) / step_y) * step_y;
    result.count = rect ? nx * ny : nx + ny - 1;

    return result;
}

static bool biome_tool_regionContains(
    const biome_tool_region_t *region,
    int32_t x,
    int32_t y)
{
    if (region->rect) {
        return true;
    }

    if (region->horizontal_entry) {
        return x == region->anchor_x || y == region->dest_y;
    } else {
        return y == region->anchor_y || x == region->dest_x;
    }
}

static bool biome_tool_regionOverlaps(
    const biome_tool_region_t *region,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h)
{
    int32_t bx1 = x + w - 1;
    int32_t by1 = y + h - 1;
    if (x > region->x1 || bx1 < region->x0 ||
        y > region->y1 || by1 < region->y0)
    {
        return false;
    }

    if (region->rect) {
        return true;
    }

    int32_t line_x = region->horizontal_entry
        ? region->anchor_x : region->dest_x;
    int32_t line_y = region->horizontal_entry
        ? region->dest_y : region->anchor_y;
    bool overlaps_vertical = x <= line_x && bx1 >= line_x;
    bool overlaps_horizontal = y <= line_y && by1 >= line_y;
    return overlaps_vertical || overlaps_horizontal;
}

static float biome_tool_maxHeight(
    const FlecsTerrain *t,
    int32_t x,
    int32_t y,
    int32_t fw,
    int32_t fh)
{
    float height = flecsEngine_terrainCellHeight(t, x, y);
    for (int32_t cy = y; cy < y + fh; cy ++) {
        for (int32_t cx = x; cx < x + fw; cx ++) {
            float cell_height = flecsEngine_terrainCellHeight(t, cx, cy);
            if (cell_height > height) {
                height = cell_height;
            }
        }
    }
    return height;
}

static ecs_entity_t biome_tool_cursorAsset(
    ecs_world_t *world,
    ecs_entity_t *asset,
    ecs_entity_t parent,
    const char *name,
    flecs_rgba_t color)
{
    if (*asset) {
        return *asset;
    }

    *asset = ecs_entity(world, {
        .name = name,
        .parent = parent,
        .add = ecs_ids(EcsPrefab)
    });
    ecs_ensure(world, *asset, FlecsMesh3);
    ecs_set_ptr(world, *asset, FlecsRgba, &color);
    ecs_set(world, *asset, FlecsPbrMaterial, {
        .metallic = 0,
        .roughness = 1.0f
    });
    ecs_set(world, *asset, FlecsEmissive, {
        .strength = 0.35f,
        .color = { color.r, color.g, color.b, 255 }
    });
    ecs_add_id(world, *asset, FlecsAlphaBlend);
    return *asset;
}

static void biome_tool_cursorVertex(
    flecs_vec3_t *vertices,
    flecs_vec3_t *normals,
    uint32_t *indices,
    int32_t index,
    float x,
    float y,
    float z)
{
    vertices[index] = (flecs_vec3_t){x, y, z};
    normals[index] = (flecs_vec3_t){0, 1, 0};
    indices[index] = (uint32_t)index;
}

static int32_t biome_tool_cursorMesh(
    ecs_world_t *world,
    ecs_entity_t asset,
    const FlecsTerrain *t,
    const uint8_t *cells,
    uint8_t value)
{
    int32_t cell_count = 0;
    for (int32_t i = 0; i < t->width * t->depth; i ++) {
        if (cells[i] == value) {
            cell_count ++;
        }
    }
    if (!cell_count) {
        return 0;
    }

    int32_t vertex_count = cell_count * 6;
    bool mesh_ready = ecs_has(world, asset, FlecsMesh3);
    FlecsMesh3 *mesh = ecs_ensure(world, asset, FlecsMesh3);
    ecs_vec_set_count_t(
        NULL, &mesh->vertices, flecs_vec3_t, vertex_count);
    ecs_vec_set_count_t(
        NULL, &mesh->normals, flecs_vec3_t, vertex_count);
    ecs_vec_set_count_t(
        NULL, &mesh->indices, uint32_t, vertex_count);
    ecs_vec_set_count_t(NULL, &mesh->uvs, flecs_vec2_t, 0);
    ecs_vec_set_count_t(NULL, &mesh->tangents, flecs_vec4_t, 0);
    ecs_vec_set_count_t(NULL, &mesh->colors, flecs_rgba_t, 0);

    flecs_vec3_t *vertices = ecs_vec_first_t(
        &mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *normals = ecs_vec_first_t(
        &mesh->normals, flecs_vec3_t);
    uint32_t *indices = ecs_vec_first_t(
        &mesh->indices, uint32_t);
    const float *heights = ecs_vec_first_t(&t->heights, float);
    int32_t stride = t->width + 1;
    float lift = t->cell_size * 0.01f;
    int32_t v = 0;

    for (int32_t z = 0; z < t->depth; z ++) {
        for (int32_t x = 0; x < t->width; x ++) {
            if (cells[z * t->width + x] != value) {
                continue;
            }

            float x0 = (float)x * t->cell_size;
            float x1 = (float)(x + 1) * t->cell_size;
            float z0 = (float)z * t->cell_size;
            float z1 = (float)(z + 1) * t->cell_size;
            float h00 = heights[z * stride + x] + lift;
            float h10 = heights[z * stride + x + 1] + lift;
            float h01 = heights[(z + 1) * stride + x] + lift;
            float h11 = heights[(z + 1) * stride + x + 1] + lift;

            if (fabsf(h00 - h11) <= fabsf(h10 - h01)) {
                biome_tool_cursorVertex(
                    vertices, normals, indices, v ++, x0, h00, z0);
                biome_tool_cursorVertex(
                    vertices, normals, indices, v ++, x1, h11, z1);
                biome_tool_cursorVertex(
                    vertices, normals, indices, v ++, x1, h10, z0);
                biome_tool_cursorVertex(
                    vertices, normals, indices, v ++, x0, h00, z0);
                biome_tool_cursorVertex(
                    vertices, normals, indices, v ++, x0, h01, z1);
                biome_tool_cursorVertex(
                    vertices, normals, indices, v ++, x1, h11, z1);
            } else {
                biome_tool_cursorVertex(
                    vertices, normals, indices, v ++, x0, h00, z0);
                biome_tool_cursorVertex(
                    vertices, normals, indices, v ++, x0, h01, z1);
                biome_tool_cursorVertex(
                    vertices, normals, indices, v ++, x1, h10, z0);
                biome_tool_cursorVertex(
                    vertices, normals, indices, v ++, x1, h10, z0);
                biome_tool_cursorVertex(
                    vertices, normals, indices, v ++, x0, h01, z1);
                biome_tool_cursorVertex(
                    vertices, normals, indices, v ++, x1, h11, z1);
            }
        }
    }

    ecs_assert(v == vertex_count, ECS_INTERNAL_ERROR, NULL);
    bool was_deferred = ecs_is_deferred(world);
    if (was_deferred && mesh_ready) {
        ecs_defer_suspend(world);
    }
    flecsEngine_mesh3_updateVertices(world, asset);
    if (was_deferred && mesh_ready) {
        ecs_defer_resume(world);
    }
    return cell_count;
}

static void biome_tool_cursorDraw(
    ecs_world_t *world,
    ecs_entity_t asset,
    const FlecsTerrain *t,
    const FlecsPosition3 *terrain_pos,
    const uint8_t *cells,
    uint8_t value)
{
    if (!biome_tool_cursorMesh(world, asset, t, cells, value)) {
        return;
    }

    flecs_draw_instance_t instance = {0};
    if (terrain_pos) {
        instance.position = *terrain_pos;
    }
    flecsEngine_draw(world, asset, &instance, 1);
}

static void biome_tool_cursorRender(
    ecs_world_t *world,
    const FlecsTerrain *t,
    const FlecsPosition3 *terrain_pos,
    const uint8_t *cells)
{
    ToolCursorState *state = ecs_singleton_get_mut(
        world, ToolCursorState);
    ecs_entity_t valid = biome_tool_cursorAsset(
        world, &state->valid, state->scope, "cursor_valid",
        (flecs_rgba_t){90, 210, 90, 230});
    ecs_entity_t invalid = biome_tool_cursorAsset(
        world, &state->invalid, state->scope, "cursor_invalid",
        (flecs_rgba_t){220, 60, 50, 230});
    ecs_entity_t doze = biome_tool_cursorAsset(
        world, &state->doze, state->scope, "cursor_doze",
        (flecs_rgba_t){230, 120, 40, 230});
    biome_tool_cursorDraw(
        world, valid, t, terrain_pos, cells, BiomeToolCursorValid);
    biome_tool_cursorDraw(
        world, invalid, t, terrain_pos, cells, BiomeToolCursorInvalid);
    biome_tool_cursorDraw(
        world, doze, t, terrain_pos, cells, BiomeToolCursorDoze);
}

static void biome_tool_cursorMark(
    const FlecsTerrain *t,
    uint8_t *cells,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint8_t value)
{
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + width > t->width ? t->width : x + width;
    int32_t y1 = y + height > t->depth ? t->depth : y + height;
    for (int32_t cy = y0; cy < y1; cy ++) {
        for (int32_t cx = x0; cx < x1; cx ++) {
            cells[cy * t->width + cx] = value;
        }
    }
}

static void biome_tool_ghostInstance(
    const FlecsTerrain *t,
    const FlecsPosition3 *terrain_pos,
    int32_t x,
    int32_t y,
    int32_t fw,
    int32_t fh,
    flecs_draw_instance_t *out)
{
    float px = ((float)x + (float)fw * 0.5f) * t->cell_size;
    float pz = ((float)y + (float)fh * 0.5f) * t->cell_size;
    float py = biome_tool_maxHeight(t, x, y, fw, fh);

    if (terrain_pos) {
        px += terrain_pos->x;
        py += terrain_pos->y;
        pz += terrain_pos->z;
    }

    *out = (flecs_draw_instance_t){
        .position = { .x = px, .y = py, .z = pz }
    };
}

static void biome_tool_burst(
    ecs_world_t *world,
    ecs_entity_t effect,
    const FlecsTerrain *t,
    const FlecsPosition3 *terrain_pos,
    int32_t x,
    int32_t y,
    int32_t fw,
    int32_t fh)
{
    const FlecsParticleBurst *burst = ecs_get(
        world, effect, FlecsParticleBurst);
    if (!burst || !burst->pool) {
        return;
    }

    float pos[3];
    pos[0] = ((float)x + (float)fw * 0.5f) * t->cell_size;
    pos[2] = ((float)y + (float)fh * 0.5f) * t->cell_size;
    pos[1] = biome_tool_maxHeight(t, x, y, fw, fh);

    if (terrain_pos) {
        pos[0] += terrain_pos->x;
        pos[1] += terrain_pos->y;
        pos[2] += terrain_pos->z;
    }

    FlecsParticleBurst b = *burst;
    b.count = burst->count * fw * fh;
    b.spread.x = burst->spread.x + (float)(fw - 1) * t->cell_size;
    b.spread.z = burst->spread.z + (float)(fh - 1) * t->cell_size;

    flecsEngine_particlesBurst(world, burst->pool, pos, &b);
}

static void biome_tool_doze(
    ecs_world_t *world,
    ecs_entity_t terrain,
    ecs_entity_t doze_effect,
    const biome_tool_region_t *region)
{
    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    TerrainOccupancy *occ = biome_tool_occupancy(world, terrain);
    if (!t) {
        return;
    }

    const FlecsPosition3 *terrain_pos = ecs_get(
        world, terrain, FlecsPosition3);

    bool power_dirty = false;

    ecs_iter_t it = ecs_each(world, FlecsTerrainPosition);
    while (ecs_each_next(&it)) {
        FlecsTerrainPosition *tp = ecs_field(&it, FlecsTerrainPosition, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            if (tp[i].terrain != terrain) {
                continue;
            }

            int32_t fw = tp[i].span_x ? tp[i].span_x : 1;
            int32_t fh = tp[i].span_y ? tp[i].span_y : 1;
            if (!biome_tool_regionOverlaps(
                region, tp[i].x, tp[i].y, fw, fh))
            {
                continue;
            }

            ecs_entity_t e = it.entities[i];
            const BiomeBuilding *b = ecs_get(world, e, BiomeBuilding);
            if (!b || !b->can_doze) {
                continue;
            }

            uint64_t mask = biome_tool_buildingMask(world, e);
            if (occ && mask) {
                for (int32_t cy = tp[i].y; cy < tp[i].y + fh; cy ++) {
                    if (cy < 0 || cy >= t->depth) {
                        continue;
                    }
                    for (int32_t cx = tp[i].x; cx < tp[i].x + fw; cx ++) {
                        if (cx < 0 || cx >= t->width) {
                            continue;
                        }
                        occ[cy * t->width + cx].buildings &= ~mask;
                    }
                }
            }

            if (ecs_has(world, e, BiomePower)) {
                power_dirty = true;
            }

            biome_factory_refund(world, e, 1);

            if (doze_effect) {
                biome_tool_burst(world, doze_effect, t, terrain_pos,
                    tp[i].x, tp[i].y, fw, fh);
            }

            biome_terrainItemIndex_remove(world, e);
            ecs_delete(world, e);
        }
    }

    if (power_dirty) {
        biome_power_markDirty(world);
    }
}

void BiomeToolPreview(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    BiomeTool *tool = ecs_field(it, BiomeTool, 0);
    FlecsTerrain *t = ecs_field(it, FlecsTerrain, 1);

    if (!tool->building && !tool->doze) {
        return;
    }

    const FlecsInput *input = ecs_singleton_get(world, FlecsInput);
    ecs_entity_t camera = biome_tool_camera(world);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t terrain = it->entities[i];

        int32_t x = 0, y = 0;
        float tile_offset_x = 0, tile_offset_y = 0;
        bool hover = input && camera &&
            !flecsEngine_uiMouseCaptured(world) &&
            biome_tool_pickTile(world, camera, terrain,
                input->mouse.view_norm.x, input->mouse.view_norm.y,
                &x, &y, &tile_offset_x, &tile_offset_y);

        if (!hover) {
            continue;
        }

        int32_t fw, fh, stride;
        if (tool->doze) {
            fw = 1;
            fh = 1;
            stride = 0;
        } else {
            biome_tool_footprint(world, tool->building, &fw, &fh, &stride);
        }
        int32_t step_x = fw + stride, step_y = fh + stride;

        biome_tool_region_t region = biome_tool_region(
            tool, biome_tool_shiftDown(input), x, y,
            tile_offset_x, tile_offset_y, step_x, step_y);
        int32_t x0 = region.x0, y0 = region.y0;
        int32_t x1 = region.x1, y1 = region.y1;
        int32_t count = region.count;

        if (tool->doze) {
            uint8_t *cursor = ecs_os_calloc_n(
                uint8_t, t[i].width * t[i].depth);
            for (int32_t cy = y0; cy <= y1; cy ++) {
                for (int32_t cx = x0; cx <= x1; cx ++) {
                    if (biome_tool_regionContains(&region, cx, cy)) {
                        biome_tool_cursorMark(
                            &t[i], cursor, cx, cy, 1, 1,
                            BiomeToolCursorDoze);
                    }
                }
            }
            biome_tool_cursorRender(
                world, &t[i],
                ecs_get(world, terrain, FlecsPosition3), cursor);
            ecs_os_free(cursor);
            continue;
        }

        const TerrainOccupancy *occ = biome_tool_occupancy(world, terrain);
        uint64_t requirement_mask = biome_tool_buildingRequirementMask(
            world, tool->building);
        ecs_entity_t building_rule = biome_tool_buildingRule(
            world, tool->building);
        uint64_t building_mask = biome_tool_buildingMask(
            world, tool->building);
        int32_t maxCount = biome_tool_buildingMaxCount(world, tool->building);
        int32_t curCount = biome_tool_buildingCount(world, tool->building);
        bool is_plant = ecs_has(world, tool->building, BiomePlant);
        int32_t freeSlots;
        BiomeBuildingRulePlacement *placements = biome_tool_plan(
            world, &t[i], occ, &region, fw, fh, step_x, step_y,
            requirement_mask, building_rule, building_mask, is_plant,
            &freeSlots);
        int32_t affordable = biome_factory_canAfford(world, tool->building);
        bool tooMany = (maxCount > 0 &&
            (freeSlots + curCount) > maxCount) ||
            freeSlots > affordable;

        uint8_t *cursor = ecs_os_calloc_n(
            uint8_t, t[i].width * t[i].depth);
        for (int32_t p = 0; p < region.count; p ++) {
            BiomeBuildingRulePlacement *placement = &placements[p];
            biome_tool_cursorMark(
                &t[i], cursor, placement->x, placement->y, fw, fh,
                tooMany || !placement->active
                    ? BiomeToolCursorInvalid
                    : BiomeToolCursorValid);
        }
        biome_tool_cursorRender(
            world, &t[i],
            ecs_get(world, terrain, FlecsPosition3), cursor);
        ecs_os_free(cursor);

        if (tooMany) {
            count = 0;
        } else {
            count = freeSlots;
        }

        if (count > BiomeToolMaxGhosts) {
            count = BiomeToolMaxGhosts;
        }

        ecs_entity_t ghost = 0;
        if (count) {
            ghost = biomeGhostGet(world, tool->building);
        }

        if (ghost) {
            const FlecsPosition3 *tp = ecs_get(world, terrain, FlecsPosition3);
            flecs_draw_instance_t *instances = ecs_os_malloc_n(
                flecs_draw_instance_t, count);
            int32_t g = 0;
            for (int32_t p = 0; p < region.count && g < count; p ++) {
                BiomeBuildingRulePlacement *placement = &placements[p];
                if (placement->active) {
                    biome_tool_ghostInstance(
                        &t[i], tp, placement->x, placement->y, fw, fh,
                        &instances[g ++]);
                }
            }
            flecsEngine_draw(world, ghost, instances, g);
            ecs_os_free(instances);
        }
        ecs_os_free(placements);
    }
}

static void biome_tool_removePlants(
    ecs_world_t *world,
    int32_t x,
    int32_t y,
    int32_t fw,
    int32_t fh)
{
    for (int32_t cy = y; cy < y + fh; cy ++) {
        for (int32_t cx = x; cx < x + fw; cx ++) {
            const TerrainItemRecord *record = biome_terrainItemIndex_get(
                world, cx, cy);
            if (!record) {
                continue;
            }
            int32_t count = ecs_vec_count(&record->entities);
            const ecs_entity_t *entities = ecs_vec_first_t(
                &record->entities, ecs_entity_t);
            for (int32_t i = 0; i < count; i ++) {
                if (ecs_is_alive(world, entities[i]) &&
                    ecs_has(world, entities[i], BiomePlant))
                {
                    ecs_delete(world, entities[i]);
                }
            }
        }
    }
}

void BiomeToolBind(ecs_iter_t *it) {
    BiomeTool *tool = ecs_field(it, BiomeTool, 0);
    tool->place_effect = ecs_lookup(it->world, "cfg.effects.build_burst");
    if (!tool->place_effect) {
        ecs_warn("tool: effect 'cfg.effects.build_burst' not found");
    }
    tool->doze_effect = ecs_lookup(it->world, "cfg.effects.doze_burst");
    if (!tool->doze_effect) {
        ecs_warn("tool: effect 'cfg.effects.doze_burst' not found");
    }
}

void BiomeToolUpdate(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    BiomeTool *tool = ecs_field(it, BiomeTool, 0);

    const FlecsInput *input = ecs_singleton_get(world, FlecsInput);
    if (!input) {
        return;
    }

    if (!tool->doze &&
        (!tool->building || !ecs_is_alive(world, tool->building)))
    {
        tool->dragging = false;
        return;
    }

    if (input->keys[FLECS_KEY_ESCAPE].pressed) {
        tool->building = 0;
        tool->doze = false;
        tool->dragging = false;
        return;
    }

    ecs_entity_t camera = biome_tool_camera(world);
    ecs_entity_t terrain = biome_tool_terrain(world);
    if (!camera || !terrain) {
        return;
    }

    int32_t x = 0, y = 0;
    float tile_offset_x = 0, tile_offset_y = 0;
    bool hover = !flecsEngine_uiMouseCaptured(world) &&
        biome_tool_pickTile(world, camera, terrain,
            input->mouse.view_norm.x, input->mouse.view_norm.y,
            &x, &y, &tile_offset_x, &tile_offset_y);

    if (!tool->dragging) {
        if (hover && input->mouse.left.pressed) {
            tool->dragging = true;
            tool->anchor_x = x;
            tool->anchor_y = y;
        }
        return;
    }

    if (input->mouse.left.state) {
        return;
    }

    if (hover) {
        int32_t fw, fh, stride;
        if (tool->doze) {
            fw = 1;
            fh = 1;
            stride = 0;
        } else {
            biome_tool_footprint(world, tool->building, &fw, &fh, &stride);
        }
        int32_t step_x = fw + stride, step_y = fh + stride;

        biome_tool_region_t region = biome_tool_region(
            tool, biome_tool_shiftDown(input), x, y,
            tile_offset_x, tile_offset_y, step_x, step_y);

        if (tool->doze) {
            biome_tool_doze(
                world, terrain, tool->doze_effect, &region);
            tool->dragging = false;
            return;
        }

        const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
        TerrainOccupancy *occ = biome_tool_occupancy(world, terrain);
        uint64_t requirement_mask = biome_tool_buildingRequirementMask(
            world, tool->building);
        ecs_entity_t building_rule = biome_tool_buildingRule(
            world, tool->building);
        uint64_t building_mask = biome_tool_buildingMask(
            world, tool->building);
        int32_t maxCount = biome_tool_buildingMaxCount(world, tool->building);
        int32_t curCount = biome_tool_buildingCount(world, tool->building);
        bool is_plant = ecs_has(world, tool->building, BiomePlant);
        int32_t freeSlots;
        BiomeBuildingRulePlacement *placements = biome_tool_plan(
            world, t, occ, &region, fw, fh, step_x, step_y,
            requirement_mask, building_rule, building_mask, is_plant,
            &freeSlots);

        if (maxCount > 0 && (freeSlots + curCount) > maxCount) {
            ecs_os_free(placements);
            tool->dragging = false;
            return;
        }

        if (!biome_factory_purchase(world, tool->building, freeSlots)) {
            ecs_os_free(placements);
            tool->dragging = false;
            return;
        }

        int32_t placed = 0;
        for (int32_t p = 0; p < region.count; p ++) {
            BiomeBuildingRulePlacement *placement = &placements[p];
            if (!placement->active) {
                continue;
            }
            if (is_plant) {
                biome_tool_removePlants(
                    world, placement->x, placement->y, fw, fh);
            }
            if (biomePlaceBuilding(
                world, tool->building, terrain,
                placement->x, placement->y, fw, fh,
                tool->place_effect))
            {
                placed ++;
            }
        }
        ecs_os_free(placements);

        if (maxCount > 0 && (curCount + placed) >= maxCount) {
            tool->building = 0;
        }
    }

    tool->dragging = false;
}

void BiomeToolUpdateButtons(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    ecs_entity_t tool_button = ecs_lookup(
        world, "cfg.widgets.ToolButton");
    if (!tool_button) {
        return;
    }

    ecs_member_t *building_member = ecs_struct_get_member(
        world, tool_button, "building");
    if (!building_member) {
        return;
    }

    ecs_entity_t button_type = ecs_lookup(
        world, "cfg.widgets.Button");
    ecs_entity_t button_mut = button_type
        ? ecs_lookup_child(world, button_type, "mut")
        : 0;
    ecs_member_t *disabled_member = button_mut
        ? ecs_struct_get_member(world, button_mut, "disabled")
        : NULL;
    if (!disabled_member) {
        return;
    }

    ecs_iter_t buttons = ecs_each_id(world, tool_button);
    while (ecs_each_next(&buttons)) {
        for (int32_t i = 0; i < buttons.count; i ++) {
            ecs_entity_t button = buttons.entities[i];
            const void *data = ecs_get_id(world, button, tool_button);
            const void *mut_data = ecs_get_id(world, button, button_mut);
            if (!data || !mut_data) {
                continue;
            }

            ecs_entity_t building = *(const ecs_entity_t*)ECS_OFFSET(
                data, building_member->offset);
            if (!building || !ecs_is_alive(world, building)) {
                continue;
            }

            const bool *disabled = ECS_OFFSET(
                mut_data, disabled_member->offset);
            bool value = biome_factory_canAfford(world, building) < 1;
            if (*disabled != value) {
                mut_data = ecs_get_mut_id(world, button, button_mut);
                bool *disabled_mut = ECS_OFFSET(
                    mut_data, disabled_member->offset);
                *disabled_mut = value;
                ecs_modified_id(world, button, button_mut);
            }
        }
    }
}

void biomeToolImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeTool);

    ECS_IMPORT(world, biomeTerrainItemIndex);

    ecs_set_name_prefix(world, "Biome");

    ECS_META_COMPONENT(world, BiomeTool);
    ECS_COMPONENT_DEFINE(world, ToolCursorState);

    ecs_add_id(world, ecs_id(BiomeTool), EcsSingleton);
    ecs_add_id(world, ecs_id(ToolCursorState), EcsSingleton);
    ecs_singleton_set(world, BiomeTool, {0});

    ecs_entity_t old_scope = ecs_set_scope(world, 0);
    ecs_entity(world, { .name = "scene.buildings" });
    ecs_entity_t cursor_scope = ecs_entity(world, { .name = "tool_cursor" });
    ecs_set_scope(world, old_scope);
    ecs_singleton_set(world, ToolCursorState, {
        .scope = cursor_scope
    });

    ECS_SYSTEM(world, BiomeToolBind, EcsOnStart, [inout] BiomeTool);

    ECS_SYSTEM(world, BiomeToolUpdate, EcsOnUpdate, [inout] BiomeTool);

    ECS_SYSTEM(world, BiomeToolUpdateButtons, EcsOnUpdate, 0);

    ECS_SYSTEM(world, BiomeToolPreview, EcsPreStore,
        [inout] BiomeTool,
        [inout] FlecsTerrain);
}
