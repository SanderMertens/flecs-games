#define ORE_ELSE_RULES_IMPL
#include "ore_else.h"

static ecs_query_t *ore_rule_query;
static ecs_map_t ore_connector_map;

static bool oreRuleSlot(
    ecs_world_t *world,
    const ecs_vec_t *slot,
    ecs_entity_t building)
{
    int32_t count = ecs_vec_count(slot);
    if (!count) {
        return true;
    }

    if (!building || !ecs_is_alive(world, building)) {
        return false;
    }

    const ecs_entity_t *prefabs = ecs_vec_first_t(slot, ecs_entity_t);
    for (int i = 0; i < count; i ++) {
        if (ecs_has_pair(world, building, EcsIsA, prefabs[i])) {
            return true;
        }
    }

    return false;
}

static bool oreRuleMatches(
    ecs_world_t *world,
    const OreBuildingRule2x1 *rule,
    ecs_entity_t first,
    ecs_entity_t second,
    bool vertical)
{
    bool horizontal_pattern = ecs_vec_count(&rule->left) ||
        ecs_vec_count(&rule->right);
    bool vertical_pattern = ecs_vec_count(&rule->top) ||
        ecs_vec_count(&rule->bottom);

    const ecs_vec_t *a, *b;

    if (!vertical) {
        if (!horizontal_pattern) {
            return false;
        }

        a = &rule->left;
        b = &rule->right;
    } else if (vertical_pattern) {
        a = &rule->top;
        b = &rule->bottom;
    } else if (rule->rotate && horizontal_pattern) {
        a = &rule->left;
        b = &rule->right;
    } else {
        return false;
    }

    if (oreRuleSlot(world, a, first) && oreRuleSlot(world, b, second)) {
        return true;
    }

    if (!rule->rotate) {
        return false;
    }

    return oreRuleSlot(world, b, first) && oreRuleSlot(world, a, second);
}

static ecs_map_key_t oreRuleEdgeKey(
    int32_t row,
    int32_t col,
    bool vertical)
{
    return (ecs_map_key_t)((row * ORE_COLS + col) * 2 + vertical) + 1;
}

static ecs_entity_t oreRuleOut(
    ecs_world_t *world,
    OreGame *game,
    int32_t row,
    int32_t col,
    bool vertical)
{
    ecs_entity_t first = *oreBuildingCell(game, row, col);
    ecs_entity_t second = *oreBuildingCell(
        game, row + vertical, col + !vertical);
    ecs_entity_t out = 0;

    ecs_iter_t it = ecs_query_iter(world, ore_rule_query);
    while (ecs_query_next(&it)) {
        const OreBuildingRule2x1 *rule = ecs_field(
            &it, OreBuildingRule2x1, 0);

        for (int i = 0; i < it.count; i ++) {
            if (!rule[i].out || !ecs_is_alive(world, rule[i].out)) {
                continue;
            }

            if (!oreRuleMatches(world, &rule[i], first, second, vertical)) {
                continue;
            }

            out = rule[i].out;
            break;
        }

        if (out) {
            ecs_iter_fini(&it);
            break;
        }
    }

    return out;
}

static void oreRuleSyncEdge(
    ecs_world_t *world,
    OreGame *game,
    const OreMap *map,
    int32_t row,
    int32_t col,
    bool vertical)
{
    if (!oreOnGrid(row, col) ||
        !oreOnGrid(row + vertical, col + !vertical))
    {
        return;
    }

    ecs_map_key_t key = oreRuleEdgeKey(row, col, vertical);
    ecs_entity_t out = oreRuleOut(world, game, row, col, vertical);

    ecs_map_val_t *value = ecs_map_get(&ore_connector_map, key);
    ecs_entity_t instance = value ? (ecs_entity_t)*value : 0;

    if (instance && ecs_is_alive(world, instance) && out &&
        ecs_has_pair(world, instance, EcsIsA, out))
    {
        return;
    }

    if (value) {
        if (ecs_is_alive(world, instance)) {
            ecs_delete(world, instance);
        }

        ecs_map_remove(&ore_connector_map, key);
    }

    if (!out) {
        return;
    }

    float half = map->tile * 0.5f;
    float x = oreTileX(map, col) + (vertical ? 0 : half);
    float z = oreTileZ(map, row) + (vertical ? half : 0);

    instance = ecs_new_w_pair(world, EcsIsA, out);
    ecs_add_pair(world, instance, EcsChildOf, game->connectors);
    ecs_set(world, instance, FlecsPosition3, {x, 0, z});
    ecs_set(world, instance, FlecsRotation3, {0, vertical ? 1.5708f : 0, 0});
    ecs_set(world, instance, FlecsScale3, {1, 1, 1});

    ecs_map_insert(&ore_connector_map, key, (ecs_map_val_t)instance);
}

void oreRulesEvalCell(
    ecs_world_t *world,
    OreGame *game,
    int32_t row,
    int32_t col)
{
    if (!game->connectors || !ore_rule_query) {
        return;
    }

    OreMap map = ecs_const_var_get_t(world, "cfg.map", OreMap);

    oreRuleSyncEdge(world, game, &map, row, col - 1, false);
    oreRuleSyncEdge(world, game, &map, row, col, false);
    oreRuleSyncEdge(world, game, &map, row - 1, col, true);
    oreRuleSyncEdge(world, game, &map, row, col, true);
}

void oreRulesClear(ecs_world_t *world, OreGame *game) {
    if (game->connectors) {
        ecs_delete_with(world, ecs_pair(EcsChildOf, game->connectors));
    }

    ecs_map_clear(&ore_connector_map);
}

static void OreBuildingOnSet(ecs_iter_t *it) {
    const OreBuilding *building = ecs_field(it, OreBuilding, 0);
    OreGame *game = ecs_field(it, OreGame, 1);

    for (int i = 0; i < it->count; i ++) {
        int32_t w, h;
        oreFootprint(it->world,
            ecs_get_target(it->world, it->entities[i], EcsIsA, 0), &w, &h);

        for (int r = building[i].row; r < building[i].row + h; r ++) {
            for (int c = building[i].col; c < building[i].col + w; c ++) {
                if (oreOnGrid(r, c)) {
                    oreRulesEvalCell(it->world, game, r, c);
                }
            }
        }
    }
}

void oreRulesImport(ecs_world_t *world) {
    ECS_META_COMPONENT(world, OreBuildingRule2x1);

    ecs_map_init(&ore_connector_map, NULL);

    ore_rule_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreRuleQuery" }),
        .terms = {
            { .id = ecs_id(OreBuildingRule2x1) }
        }
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreBuildingOnSet" }),
        .query.terms = {
            { .id = ecs_id(OreBuilding) },
            { .id = ecs_id(OreGame) }
        },
        .events = { EcsOnSet },
        .callback = OreBuildingOnSet
    });
}
