#define BIOME_BUILDING_RULE_IMPL

#include "biome.h"

#include <math.h>

#define BIOME_RULE_PI (3.14159265358979323846f)

ECS_COMPONENT_DECLARE(BiomeBuildingRule3x1Impl);

enum {
    BiomeBuildingRuleLeft,
    BiomeBuildingRuleRight,
    BiomeBuildingRuleTop,
    BiomeBuildingRuleBottom,
    BiomeBuildingRuleSlotCount
};

static void biome_building_rule_ctx_free(void *ptr) {
    ecs_os_free(ptr);
}

static void BiomeBuildingRule2x1Impl_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    BiomeBuildingRule2x1Impl *impl = ptr;
    for (int32_t i = 0; i < count; i ++) {
        for (int32_t slot = 0; slot < BiomeBuildingRuleSlotCount; slot ++) {
            impl[i].building_masks[slot] = 0;
        }
        ecs_vec_init_t(NULL, &impl[i].observers, ecs_entity_t, 0);
        ecs_map_init(&impl[i].instances, NULL);
    }
}

static void biome_building_rule_instances_fini(
    BiomeBuildingRule2x1Impl *impl)
{
    ecs_map_iter_t it = ecs_map_iter(&impl->instances);
    while (ecs_map_next(&it)) {
        ecs_vec_t *instances = ecs_map_ptr(&it);
        ecs_vec_fini_t(NULL, instances, ecs_entity_t);
        ecs_os_free(instances);
    }
    ecs_map_fini(&impl->instances);
}

static void BiomeBuildingRule2x1Impl_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    BiomeBuildingRule2x1Impl *impl = ptr;
    for (int32_t i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &impl[i].observers, ecs_entity_t);
        biome_building_rule_instances_fini(&impl[i]);
    }
}

static uint64_t biome_building_rule_mask(
    const ecs_world_t *world,
    ecs_entity_t rule_entity,
    const ecs_vec_t *prefabs,
    bool *valid)
{
    uint64_t result = 0;
    int32_t count = ecs_vec_count(prefabs);
    const ecs_entity_t *entities = ecs_vec_first_t(prefabs, ecs_entity_t);

    *valid = true;
    for (int32_t i = 0; i < count; i ++) {
        ecs_entity_t prefab = entities[i];
        const BiomeBuildingBit *bit = NULL;
        if (ecs_is_alive(world, prefab)) {
            bit = ecs_get(world, prefab, BiomeBuildingBit);
        }
        if (!bit || *bit < 0 || *bit >= 64) {
            char *rule_path = ecs_get_path(world, rule_entity);
            char *prefab_path = ecs_is_alive(world, prefab)
                ? ecs_get_path(world, prefab)
                : NULL;
            ecs_err("building rule '%s' references entity '%s' without a "
                "valid BuildingBit",
                rule_path ? rule_path : "<unnamed>",
                prefab_path ? prefab_path : "<unknown>");
            ecs_os_free(rule_path);
            ecs_os_free(prefab_path);
            *valid = false;
            continue;
        }
        result |= 1llu << *bit;
    }

    return result;
}

static bool biome_building_rule_slot_matches(
    const TerrainOccupancy *occupancy,
    int32_t index,
    uint64_t mask)
{
    return !mask || (occupancy[index].buildings & mask) != 0;
}

static bool biome_building_rule_placement_slot_matches(
    const FlecsTerrain *terrain,
    const TerrainOccupancy *occupancy,
    int32_t x,
    int32_t y,
    uint64_t mask)
{
    if (!mask) {
        return true;
    }
    if (!occupancy || x < 0 || y < 0 ||
        x >= terrain->width || y >= terrain->depth)
    {
        return false;
    }

    return (occupancy[y * terrain->width + x].buildings & mask) != 0;
}

static bool biome_building_rule_3x1_matches_pattern(
    const FlecsTerrain *terrain,
    const TerrainOccupancy *occupancy,
    int32_t first_x,
    int32_t first_y,
    int32_t second_x,
    int32_t second_y,
    uint64_t first_mask,
    uint64_t second_mask)
{
    return biome_building_rule_placement_slot_matches(
        terrain, occupancy, first_x, first_y, first_mask) &&
        biome_building_rule_placement_slot_matches(
            terrain, occupancy, second_x, second_y, second_mask);
}

bool biomeBuildingRuleMatches(
    const ecs_world_t *world,
    ecs_entity_t rule_entity,
    const FlecsTerrain *terrain,
    const TerrainOccupancy *occupancy,
    int32_t x,
    int32_t y)
{
    if (!rule_entity) {
        return true;
    }
    if (!terrain || !ecs_is_alive(world, rule_entity)) {
        return false;
    }

    const BiomeBuildingRule3x1 *rule = ecs_get(
        world, rule_entity, BiomeBuildingRule3x1);
    const BiomeBuildingRule3x1Impl *impl = ecs_get(
        world, rule_entity, BiomeBuildingRule3x1Impl);
    if (!rule || !impl || !impl->valid) {
        return false;
    }

    bool horizontal = impl->populated_slots &
        ((1u << BiomeBuildingRuleLeft) |
         (1u << BiomeBuildingRuleRight));
    bool vertical = impl->populated_slots &
        ((1u << BiomeBuildingRuleTop) |
         (1u << BiomeBuildingRuleBottom));
    uint64_t left = impl->building_masks[BiomeBuildingRuleLeft];
    uint64_t right = impl->building_masks[BiomeBuildingRuleRight];
    uint64_t top = impl->building_masks[BiomeBuildingRuleTop];
    uint64_t bottom = impl->building_masks[BiomeBuildingRuleBottom];

    if (horizontal && biome_building_rule_3x1_matches_pattern(
        terrain, occupancy, x - 1, y, x + 1, y, left, right))
    {
        return true;
    }
    if (horizontal && rule->rotate &&
        biome_building_rule_3x1_matches_pattern(
            terrain, occupancy, x - 1, y, x + 1, y, right, left))
    {
        return true;
    }
    if (vertical && biome_building_rule_3x1_matches_pattern(
        terrain, occupancy, x, y - 1, x, y + 1, top, bottom))
    {
        return true;
    }
    if (vertical && rule->rotate &&
        biome_building_rule_3x1_matches_pattern(
            terrain, occupancy, x, y - 1, x, y + 1, bottom, top))
    {
        return true;
    }

    /* Preserve the 2x1 rule convention where rotate also applies a configured
     * left/right pattern vertically when no explicit vertical pattern exists. */
    if (!vertical && horizontal && rule->rotate) {
        if (biome_building_rule_3x1_matches_pattern(
            terrain, occupancy, x, y - 1, x, y + 1, left, right))
        {
            return true;
        }
        if (biome_building_rule_3x1_matches_pattern(
            terrain, occupancy, x, y - 1, x, y + 1, right, left))
        {
            return true;
        }
    }

    return false;
}

static void biome_building_rule_populate_placements(
    const FlecsTerrain *terrain,
    const TerrainOccupancy *occupancy,
    uint64_t building_mask,
    const BiomeBuildingRulePlacement *placements,
    int32_t placement_count,
    TerrainOccupancy *result)
{
    int32_t cell_count = terrain->width * terrain->depth;
    if (occupancy) {
        ecs_os_memcpy(
            result, occupancy, (ecs_size_t)cell_count * sizeof(*result));
    } else {
        ecs_os_memset(result, 0, (ecs_size_t)cell_count * sizeof(*result));
    }

    for (int32_t i = 0; i < placement_count; i ++) {
        const BiomeBuildingRulePlacement *placement = &placements[i];
        if (!placement->active) {
            continue;
        }
        for (int32_t y = placement->y;
            y < placement->y + placement->height;
            y ++)
        {
            for (int32_t x = placement->x;
                x < placement->x + placement->width;
                x ++)
            {
                result[y * terrain->width + x].buildings |= building_mask;
            }
        }
    }
}

int32_t biomeBuildingRuleFilterPlacements(
    const ecs_world_t *world,
    ecs_entity_t rule,
    const FlecsTerrain *terrain,
    const TerrainOccupancy *occupancy,
    uint64_t building_mask,
    BiomeBuildingRulePlacement *placements,
    int32_t placement_count)
{
    if (!terrain || terrain->width <= 0 || terrain->depth <= 0 ||
        placement_count <= 0)
    {
        return 0;
    }

    int32_t active_count = 0;
    for (int32_t i = 0; i < placement_count; i ++) {
        active_count += placements[i].active;
    }
    if (!rule || !active_count) {
        return active_count;
    }

    int32_t cell_count = terrain->width * terrain->depth;
    TerrainOccupancy *planned = ecs_os_malloc_n(
        TerrainOccupancy, cell_count);
    bool changed;
    do {
        biome_building_rule_populate_placements(
            terrain, occupancy, building_mask,
            placements, placement_count, planned);
        changed = false;
        for (int32_t i = 0; i < placement_count; i ++) {
            BiomeBuildingRulePlacement *placement = &placements[i];
            if (placement->active && !biomeBuildingRuleMatches(
                world, rule, terrain, planned,
                placement->x, placement->y))
            {
                placement->active = false;
                active_count --;
                changed = true;
            }
        }
    } while (changed && active_count);

    ecs_os_free(planned);
    return active_count;
}

static int32_t biome_building_rule_matching_rotations(
    const FlecsTerrain *terrain,
    const TerrainOccupancy *occupancy,
    const BiomeBuildingRule2x1 *rule,
    const BiomeBuildingRule2x1Impl *impl,
    int32_t x,
    int32_t y,
    bool vertical,
    int8_t rotations[2])
{
    int32_t x1 = x + !vertical;
    int32_t y1 = y + vertical;
    if (x < 0 || y < 0 || x1 >= terrain->width || y1 >= terrain->depth) {
        return 0;
    }

    int32_t first = y * terrain->width + x;
    int32_t second = y1 * terrain->width + x1;
    bool horizontal = ecs_vec_count(&rule->left) ||
        ecs_vec_count(&rule->right);
    bool explicit_vertical = ecs_vec_count(&rule->top) ||
        ecs_vec_count(&rule->bottom);
    uint64_t first_mask;
    uint64_t second_mask;
    int32_t count = 0;

    if (!vertical) {
        if (!horizontal) {
            return 0;
        }
        first_mask = impl->building_masks[BiomeBuildingRuleLeft];
        second_mask = impl->building_masks[BiomeBuildingRuleRight];
    } else if (explicit_vertical) {
        first_mask = impl->building_masks[BiomeBuildingRuleTop];
        second_mask = impl->building_masks[BiomeBuildingRuleBottom];
    } else if (rule->rotate && horizontal) {
        /* Preserve the original behavior where rotate applies a left/right
         * pattern to vertical edges as well. */
        first_mask = impl->building_masks[BiomeBuildingRuleLeft];
        second_mask = impl->building_masks[BiomeBuildingRuleRight];
    } else {
        return 0;
    }

    /* Positive Y rotation maps local +X towards world -Z. Use -90 degrees
     * for a vertical forward pattern so local -X still points at the first
     * (top) tile, just like it points at the first (left) tile horizontally. */
    int8_t forward_rotation = vertical ? -1 : 0;
    if (biome_building_rule_slot_matches(
        occupancy, first, first_mask) &&
        biome_building_rule_slot_matches(occupancy, second, second_mask))
    {
        rotations[count ++] = forward_rotation;
    }

    /* When the two masks are equal, the reverse rotation is the same pattern
     * and would place a duplicate instance on the same edge. */
    if (rule->rotate && first_mask != second_mask &&
        biome_building_rule_slot_matches(
            occupancy, first, second_mask) &&
        biome_building_rule_slot_matches(
            occupancy, second, first_mask))
    {
        rotations[count ++] = forward_rotation + 2;
    }

    return count;
}

static uint64_t biome_building_rule_edge_key(
    int32_t x,
    int32_t y,
    bool vertical)
{
    /* Tile centers have even coordinates in this doubled coordinate system;
     * edge centers have one odd coordinate. */
    return biome_terrainItemIndex_pos(
        x * 2 + !vertical,
        y * 2 + vertical);
}

static bool biome_building_rule_instances_match(
    const ecs_world_t *world,
    const ecs_vec_t *instances,
    const int8_t *rotations,
    int32_t rotation_count,
    float base_yaw)
{
    if (!instances || ecs_vec_count(instances) != rotation_count) {
        return false;
    }

    const ecs_entity_t *entities = ecs_vec_first_t(instances, ecs_entity_t);
    for (int32_t i = 0; i < rotation_count; i ++) {
        ecs_entity_t entity = entities[i];
        if (!ecs_is_alive(world, entity)) {
            return false;
        }
        const FlecsRotation3 *rotation = ecs_get(
            world, entity, FlecsRotation3);
        float expected = base_yaw + (float)rotations[i] *
            (BIOME_RULE_PI * 0.5f);
        if (!rotation ||
            fabsf(rotation->y - expected) > 0.00001f)
        {
            return false;
        }
    }

    return true;
}

static void biome_building_rule_delete_instances(
    ecs_world_t *world,
    ecs_vec_t *instances)
{
    int32_t count = ecs_vec_count(instances);
    ecs_entity_t *entities = ecs_vec_first_t(instances, ecs_entity_t);
    for (int32_t i = 0; i < count; i ++) {
        if (ecs_is_alive(world, entities[i])) {
            ecs_delete(world, entities[i]);
        }
    }
}

static ecs_entity_t biome_building_rule_instantiate(
    ecs_world_t *world,
    ecs_entity_t terrain_entity,
    const FlecsTerrain *terrain,
    ecs_entity_t prefab,
    int32_t x,
    int32_t y,
    bool vertical,
    int8_t rotation_index,
    bool follow_slope)
{
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t parent = ecs_lookup(world, "scene.buildings");
    if (!parent) {
        parent = terrain_entity;
    }
    ecs_add_pair(world, instance, EcsChildOf, parent);

    float pattern_yaw = (float)rotation_index * (BIOME_RULE_PI * 0.5f);
    float yaw = pattern_yaw;
    float slope_rotation = 0;
    const FlecsPosition3 *prefab_position = ecs_get(
        world, prefab, FlecsPosition3);
    const FlecsRotation3 *prefab_rotation = ecs_get(
        world, prefab, FlecsRotation3);

    float px = ((float)x + (vertical ? 0.5f : 1.0f)) *
        terrain->cell_size;
    float pz = ((float)y + (vertical ? 1.0f : 0.5f)) *
        terrain->cell_size;
    float first_x = ((float)x + 0.5f) * terrain->cell_size;
    float first_z = ((float)y + 0.5f) * terrain->cell_size;
    float second_x = first_x + (vertical ? 0 : terrain->cell_size);
    float second_z = first_z + (vertical ? terrain->cell_size : 0);
    float first_height = flecsEngine_terrainSampleHeight(
        terrain, first_x, first_z);
    float second_height = flecsEngine_terrainSampleHeight(
        terrain, second_x, second_z);
    float py = (first_height + second_height) * 0.5f;

    if (follow_slope) {
        /* Rotation3 uses XYZ Euler rotation. The output prefab's long axis is
         * local X, so terrain slope is a local Z rotation. Pattern rotations
         * 1 and 2 point local X opposite to the edge's positive direction. */
        float local_direction = vertical
            ? -sinf(pattern_yaw)
            : cosf(pattern_yaw);
        slope_rotation = atan2f(
            (second_height - first_height) * local_direction,
            terrain->cell_size);
    }

    if (parent != terrain_entity) {
        const FlecsPosition3 *terrain_position = ecs_get(
            world, terrain_entity, FlecsPosition3);
        if (terrain_position) {
            px += terrain_position->x;
            py += terrain_position->y;
            pz += terrain_position->z;
        }
    }

    if (prefab_position) {
        float c = cosf(yaw), s = sinf(yaw);
        px += prefab_position->x * c + prefab_position->z * s;
        py += prefab_position->y;
        pz += -prefab_position->x * s + prefab_position->z * c;
    }
    if (prefab_rotation) {
        yaw += prefab_rotation->y;
    }

    ecs_set(world, instance, FlecsPosition3, { px, py, pz });
    ecs_set(world, instance, FlecsRotation3, {
        prefab_rotation ? prefab_rotation->x : 0,
        yaw,
        (prefab_rotation ? prefab_rotation->z : 0) + slope_rotation
    });

    return instance;
}

static void biome_building_rule_sync_edge(
    ecs_world_t *world,
    ecs_entity_t rule_entity,
    ecs_entity_t terrain_entity,
    const FlecsTerrain *terrain,
    const TerrainOccupancy *occupancy,
    int32_t x,
    int32_t y,
    bool vertical)
{
    const BiomeBuildingRule2x1 *rule = ecs_get(
        world, rule_entity, BiomeBuildingRule2x1);
    BiomeBuildingRule2x1Impl *impl = ecs_get_mut(
        world, rule_entity, BiomeBuildingRule2x1Impl);
    if (!rule || !impl || !rule->out || !ecs_is_alive(world, rule->out)) {
        return;
    }

    uint64_t key = biome_building_rule_edge_key(x, y, vertical);
    ecs_vec_t *instances = ecs_map_get_deref(
        &impl->instances, ecs_vec_t, (ecs_map_key_t)key);
    int8_t rotations[2];
    int32_t rotation_count = biome_building_rule_matching_rotations(
        terrain, occupancy, rule, impl, x, y, vertical, rotations);
    const FlecsRotation3 *prefab_rotation = ecs_get(
        world, rule->out, FlecsRotation3);
    float base_yaw = prefab_rotation ? prefab_rotation->y : 0;

    if (biome_building_rule_instances_match(
        world, instances, rotations, rotation_count, base_yaw))
    {
        return;
    }

    if (instances) {
        biome_building_rule_delete_instances(world, instances);
        ecs_vec_fini_t(NULL, instances, ecs_entity_t);
        ecs_map_remove_free(&impl->instances, (ecs_map_key_t)key);
        instances = NULL;
    }

    if (!rotation_count) {
        return;
    }

    instances = ecs_map_insert_alloc_t(
        &impl->instances, ecs_vec_t, (ecs_map_key_t)key);
    ecs_vec_init_t(NULL, instances, ecs_entity_t, rotation_count);
    for (int32_t i = 0; i < rotation_count; i ++) {
        *ecs_vec_append_t(NULL, instances, ecs_entity_t) =
            biome_building_rule_instantiate(
                world, terrain_entity, terrain, rule->out,
                x, y, vertical, rotations[i], rule->follow_slope);
    }
}

static bool biome_building_rule_edge_inside_event(
    const BiomeBuildingOccupancyChanged *event,
    int32_t x,
    int32_t y,
    bool vertical)
{
    int32_t x1 = x + !vertical;
    int32_t y1 = y + vertical;
    int32_t event_x1 = event->x + event->width;
    int32_t event_y1 = event->y + event->height;

    return event->occupied &&
        x >= event->x && x < event_x1 &&
        y >= event->y && y < event_y1 &&
        x1 >= event->x && x1 < event_x1 &&
        y1 >= event->y && y1 < event_y1;
}

static void biome_building_rule_sync_changed_edge(
    ecs_world_t *world,
    ecs_entity_t rule_entity,
    ecs_entity_t terrain_entity,
    const FlecsTerrain *terrain,
    const TerrainOccupancy *occupancy,
    const BiomeBuildingOccupancyChanged *event,
    int32_t x,
    int32_t y,
    bool vertical)
{
    /* A multi-cell building marks every tile in its footprint with the same
     * building bit. Do not interpret an edge inside that footprint as an edge
     * between two buildings. On removal the edge is still synchronized so a
     * stale instance, if any, is cleaned up. */
    if (biome_building_rule_edge_inside_event(event, x, y, vertical)) {
        return;
    }

    biome_building_rule_sync_edge(world, rule_entity, terrain_entity,
        terrain, occupancy, x, y, vertical);
}

static void BiomeBuildingRule2x1OccupancyChanged(ecs_iter_t *it) {
    const BiomeBuildingOccupancyChanged *event = it->param;
    const BiomeBuildingRule2x1ObserverCtx *ctx = it->ctx;
    if (!event || !ctx || ctx->slot < 0 ||
        ctx->slot >= BiomeBuildingRuleSlotCount ||
        event->building_bit < 0 || event->building_bit >= 64)
    {
        return;
    }

    const BiomeBuildingRule2x1Impl *impl = ecs_get(
        it->world, ctx->rule, BiomeBuildingRule2x1Impl);
    if (!impl || !(impl->building_masks[ctx->slot] &
        (1llu << event->building_bit)))
    {
        return;
    }

    const FlecsTerrain *terrain = ecs_get(
        it->world, event->terrain, FlecsTerrain);
    if (!terrain || ecs_vec_count(&terrain->layerTypes) <=
        TerrainOccupancyIndex)
    {
        return;
    }
    const TerrainOccupancy *occupancy = flecsEngine_terrain_getLayer(
        it->world, event->terrain, TerrainOccupancyIndex, TerrainOccupancy);
    if (!occupancy) {
        return;
    }

    const BiomeBuildingRule2x1 *rule = ecs_get(
        it->world, ctx->rule, BiomeBuildingRule2x1);
    if (!rule) {
        return;
    }

    for (int32_t y = event->y; y < event->y + event->height; y ++) {
        for (int32_t x = event->x; x < event->x + event->width; x ++) {
            if (rule->rotate) {
                biome_building_rule_sync_changed_edge(it->world, ctx->rule,
                    event->terrain, terrain, occupancy, event,
                    x - 1, y, false);
                biome_building_rule_sync_changed_edge(it->world, ctx->rule,
                    event->terrain, terrain, occupancy, event,
                    x, y, false);
                biome_building_rule_sync_changed_edge(it->world, ctx->rule,
                    event->terrain, terrain, occupancy, event,
                    x, y - 1, true);
                biome_building_rule_sync_changed_edge(it->world, ctx->rule,
                    event->terrain, terrain, occupancy, event,
                    x, y, true);
            } else if (ctx->slot == BiomeBuildingRuleLeft) {
                biome_building_rule_sync_changed_edge(it->world, ctx->rule,
                    event->terrain, terrain, occupancy, event,
                    x, y, false);
            } else if (ctx->slot == BiomeBuildingRuleRight) {
                biome_building_rule_sync_changed_edge(it->world, ctx->rule,
                    event->terrain, terrain, occupancy, event,
                    x - 1, y, false);
            } else if (ctx->slot == BiomeBuildingRuleTop) {
                biome_building_rule_sync_changed_edge(it->world, ctx->rule,
                    event->terrain, terrain, occupancy, event,
                    x, y, true);
            } else {
                biome_building_rule_sync_changed_edge(it->world, ctx->rule,
                    event->terrain, terrain, occupancy, event,
                    x, y - 1, true);
            }
        }
    }
}

static void biome_building_rule_reset_impl(
    ecs_world_t *world,
    BiomeBuildingRule2x1Impl *impl)
{
    int32_t observer_count = ecs_vec_count(&impl->observers);
    ecs_entity_t *observers = ecs_vec_first_t(
        &impl->observers, ecs_entity_t);
    for (int32_t i = 0; i < observer_count; i ++) {
        if (ecs_is_alive(world, observers[i])) {
            ecs_delete(world, observers[i]);
        }
    }
    ecs_vec_clear(&impl->observers);

    ecs_map_iter_t map_it = ecs_map_iter(&impl->instances);
    while (ecs_map_next(&map_it)) {
        ecs_vec_t *instances = ecs_map_ptr(&map_it);
        biome_building_rule_delete_instances(world, instances);
    }
    biome_building_rule_instances_fini(impl);
    ecs_map_init(&impl->instances, NULL);
}

static void BiomeBuildingRule2x1OnSet(ecs_iter_t *it) {
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t rule_entity = it->entities[i];
        const BiomeBuildingRule2x1 *rule = ecs_get(
            it->world, rule_entity, BiomeBuildingRule2x1);
        if (!rule) {
            continue;
        }

        /* Adding the implementation component can move the rule to another
         * table, so finish reading the public component first. */
        const ecs_vec_t *slots[BiomeBuildingRuleSlotCount] = {
            &rule->left,
            &rule->right,
            &rule->top,
            &rule->bottom
        };
        uint64_t masks[BiomeBuildingRuleSlotCount];
        int32_t prefab_counts[BiomeBuildingRuleSlotCount];
        bool has_configured_pattern = false;
        bool has_invalid_slot = false;
        for (int32_t slot = 0;
            slot < BiomeBuildingRuleSlotCount;
            slot ++)
        {
            bool valid;
            masks[slot] = biome_building_rule_mask(
                it->world, rule_entity, slots[slot], &valid);
            prefab_counts[slot] = ecs_vec_count(slots[slot]);
            has_configured_pattern |= prefab_counts[slot] != 0;
            has_invalid_slot |= !valid;
        }
        ecs_entity_t out = rule->out;

        BiomeBuildingRule2x1Impl *impl = ecs_ensure(
            it->world, rule_entity, BiomeBuildingRule2x1Impl);
        biome_building_rule_reset_impl(it->world, impl);

        for (int32_t slot = 0;
            slot < BiomeBuildingRuleSlotCount;
            slot ++)
        {
            impl->building_masks[slot] = masks[slot];
        }

        if (!has_configured_pattern) {
            const char *name = ecs_get_name(it->world, rule_entity);
            ecs_warn("building rule '%s' has no populated slots",
                name ? name : "<unnamed>");
            ecs_modified(it->world, rule_entity, BiomeBuildingRule2x1Impl);
            continue;
        }
        if (has_invalid_slot) {
            ecs_modified(it->world, rule_entity, BiomeBuildingRule2x1Impl);
            continue;
        }
        if (!out || !ecs_is_alive(it->world, out)) {
            const char *name = ecs_get_name(it->world, rule_entity);
            ecs_warn("building rule '%s' has no valid output prefab",
                name ? name : "<unnamed>");
            ecs_modified(it->world, rule_entity, BiomeBuildingRule2x1Impl);
            continue;
        }

        for (int32_t slot = 0;
            slot < BiomeBuildingRuleSlotCount;
            slot ++)
        {
            if (!impl->building_masks[slot]) {
                continue;
            }

            BiomeBuildingRule2x1ObserverCtx *ctx = ecs_os_malloc_t(
                BiomeBuildingRule2x1ObserverCtx);
            *ctx = (BiomeBuildingRule2x1ObserverCtx) {
                .rule = rule_entity,
                .slot = slot
            };
            ecs_entity_t observer = ecs_observer(it->world, {
                .query.terms = {{ .id = EcsAny }},
                .events = { ecs_id(BiomeBuildingOccupancyChanged) },
                .callback = BiomeBuildingRule2x1OccupancyChanged,
                .ctx = ctx,
                .ctx_free = biome_building_rule_ctx_free
            });
            *ecs_vec_append_t(NULL, &impl->observers, ecs_entity_t) = observer;
        }

        ecs_modified(it->world, rule_entity, BiomeBuildingRule2x1Impl);
    }
}

static void BiomeBuildingRule3x1OnSet(ecs_iter_t *it) {
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t rule_entity = it->entities[i];
        const BiomeBuildingRule3x1 *rule = ecs_get(
            it->world, rule_entity, BiomeBuildingRule3x1);
        if (!rule) {
            continue;
        }

        const ecs_vec_t *slots[BiomeBuildingRuleSlotCount] = {
            &rule->left,
            &rule->right,
            &rule->top,
            &rule->bottom
        };
        BiomeBuildingRule3x1Impl value = {0};
        bool has_invalid_slot = false;
        for (int32_t slot = 0;
            slot < BiomeBuildingRuleSlotCount;
            slot ++)
        {
            int32_t count = ecs_vec_count(slots[slot]);
            if (!count) {
                continue;
            }

            value.populated_slots |= (uint8_t)(1u << slot);
            bool valid;
            value.building_masks[slot] = biome_building_rule_mask(
                it->world, rule_entity, slots[slot], &valid);
            has_invalid_slot |= !valid;
        }

        value.valid = value.populated_slots && !has_invalid_slot;
        if (!value.populated_slots) {
            const char *name = ecs_get_name(it->world, rule_entity);
            ecs_warn("building placement rule '%s' has no populated slots",
                name ? name : "<unnamed>");
        }

        ecs_set_ptr(it->world, rule_entity,
            BiomeBuildingRule3x1Impl, &value);
    }
}

void biomeBuilding_ruleImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeBuilding_rule);

    ecs_set_name_prefix(world, "Biome");
    ECS_META_COMPONENT(world, BiomeBuildingRule2x1);
    ECS_META_COMPONENT(world, BiomeBuildingRule3x1);
    /* The implementation contains an opaque map and is intentionally not
     * reflected/serialized. */
    ECS_COMPONENT_DEFINE(world, BiomeBuildingRule2x1Impl);
    ECS_COMPONENT_DEFINE(world, BiomeBuildingRule3x1Impl);

    ecs_set_hooks(world, BiomeBuildingRule2x1Impl, {
        .ctor = BiomeBuildingRule2x1Impl_ctor,
        .dtor = BiomeBuildingRule2x1Impl_dtor
    });
    ecs_set_hooks(world, BiomeBuildingRule2x1, {
        .on_set = BiomeBuildingRule2x1OnSet
    });
    ecs_set_hooks(world, BiomeBuildingRule3x1, {
        .on_set = BiomeBuildingRule3x1OnSet
    });
}
