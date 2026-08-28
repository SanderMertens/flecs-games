#ifndef BIOME_BUILDING_RULE_H
#define BIOME_BUILDING_RULE_H

#undef ECS_META_IMPL
#ifndef BIOME_BUILDING_RULE_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* Instantiates out in the center of a matching 2x1 building pattern. The
 * left/right fields define a horizontal pattern and top/bottom define a
 * vertical pattern. If top/bottom are unset and rotate is true, the vertical
 * pattern is derived from left/right for backwards compatibility. Empty slots
 * in a configured pattern are wildcards. A populated slot matches when at
 * least one of its prefabs occupies the corresponding terrain tile.
 * 
 * Buildings placed by the pattern are not part of the occupancy grid. They are
 * only intended to be visual (for example, a wire between electricity poles).
 *
 * The implementation assumes a single terrain, that all rules are created 
 * before buildings are created, and that rules are persistent (so it is not
 * necessary to cleanup instances when a rule is deleted).
 */
ECS_STRUCT(BiomeBuildingRule2x1, {
    ecs_vec(ecs_entity_t) left;
    ecs_vec(ecs_entity_t) right;
    ecs_vec(ecs_entity_t) top;
    ecs_vec(ecs_entity_t) bottom;
    ecs_entity_t out;
    bool rotate; /* If true, configured patterns are also tested in the reverse direction */
    bool follow_slope; /* Apply rotation that follows the terrain slope */
});

/* Matches a 3x1 pattern centered on the tile where a building is about to be
 * placed. Left/right describe its horizontal neighbors, while top/bottom
 * describe its vertical neighbors. Empty slots are wildcards. When rotate is
 * true, a left/right pattern is also matched right/left, top/bottom and
 * bottom/top. This rule is intended for placement of 1x1 buildings. */
ECS_STRUCT(BiomeBuildingRule3x1, {
    ecs_vec(ecs_entity_t) left;
    ecs_vec(ecs_entity_t) right;
    ecs_vec(ecs_entity_t) top;
    ecs_vec(ecs_entity_t) bottom;
    bool rotate;
});

/* Instantiated by implementation after setting BiomeBuildingRule2x1 */
ECS_STRUCT(BiomeBuildingRule2x1Impl, {
    /* Mask that is built from the rule. If an element is 0, it is treated as a
     * wildcard. If it is non-zero, at least one of the bits must match the occupancy
     * grid of the terrain. */
    uint64_t building_masks[4]; /* left, right, top, bottom */

    /* Vector with observers that track prefabs that are part of the mask. An
     * observer is created for each slot, with context that stores which slot the
     * observer is created for. This way the triggered observer can trivially
     * match the pattern against surrounding tiles. 
     *
     * Observers are created for OnAdd/OnRemove events, so instances are 
     * automatically created and removed after a pattern starts/stops matching.
     */
    ecs_vec(ecs_entity_t) observers;

    /* Map with instances instantiated by rule. The map is keyed by a packed 
     * position, similar to the terrain item index. However, because the instance
     * does not occupy a tile, the values are duplicated, because we can't
     * neatly represent halves in a packed key:
     *
     *   x = left_tile_x * 2 + 1
     *   y = left_tile_y * 2 + 1
     * 
     *   instances[pack(x, y)] = ...
     *
     * This means that when an OnRemove observer triggers, it has to check each
     * of its four edges for a possible instance.
     */
    ecs_map_t instances; /* map<uint64_t, vec<ecs_entity_t>> */
});

/* Instantiated by implementation after setting BiomeBuildingRule3x1. */
typedef struct BiomeBuildingRule3x1Impl {
    uint64_t building_masks[4]; /* left, right, top, bottom */
    uint8_t populated_slots;
    bool valid;
} BiomeBuildingRule3x1Impl;

extern ECS_COMPONENT_DECLARE(BiomeBuildingRule3x1Impl);

/* Observer context for a rule */
typedef struct BiomeBuildingRule2x1ObserverCtx {
    ecs_entity_t rule;
    int32_t slot;
} BiomeBuildingRule2x1ObserverCtx;

bool biomeBuildingRuleMatches(
    const ecs_world_t *world,
    ecs_entity_t rule,
    const FlecsTerrain *terrain,
    const TerrainOccupancy *occupancy,
    int32_t x,
    int32_t y);

typedef struct BiomeBuildingRulePlacement {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    bool active;
} BiomeBuildingRulePlacement;

int32_t biomeBuildingRuleFilterPlacements(
    const ecs_world_t *world,
    ecs_entity_t rule,
    const FlecsTerrain *terrain,
    const TerrainOccupancy *occupancy,
    uint64_t building_mask,
    BiomeBuildingRulePlacement *placements,
    int32_t placement_count);

void biomeBuilding_ruleImport(ecs_world_t *world);

#endif
