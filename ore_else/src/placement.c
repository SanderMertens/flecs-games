#define ORE_ELSE_PLACEMENT_IMPL
#include "ore_else.h"

#include <stdio.h>

bool oreRunHeld(ecs_world_t *world, const OreGame *game) {
    const OrePlayerIntent *intent = game
        ? ecs_get(world, game->player, OrePlayerIntent) : NULL;
    return intent && intent->run;
}

static bool oreInReach(
    ecs_world_t *world,
    const OreGame *game,
    const OreMap *map,
    int32_t row,
    int32_t col,
    int32_t w,
    int32_t h)
{
    const FlecsPosition3 *pos = game->player
        ? ecs_get(world, game->player, FlecsPosition3)
        : NULL;

    if (!pos) {
        return true;
    }

    float reach = ecs_const_var_get_t(world, "cfg.reach", ecs_f32_t);

    float x0 = oreTileX(map, col);
    float x1 = oreTileX(map, col + w - 1);
    float z0 = oreTileZ(map, row);
    float z1 = oreTileZ(map, row + h - 1);

    float x = pos->x < x0 ? x0 : (pos->x > x1 ? x1 : pos->x);
    float z = pos->z < z0 ? z0 : (pos->z > z1 ? z1 : pos->z);

    float dx = x - pos->x;
    float dz = z - pos->z;

    return (dx * dx + dz * dz) <= reach * reach;
}

bool oreCellFree(
    ecs_world_t *world,
    OreGame *game,
    ecs_entity_t prefab,
    int32_t row,
    int32_t col)
{
    if (!prefab || !oreOnGrid(row, col)) {
        return false;
    }

    int32_t w, h;
    oreFootprint(world, prefab, &w, &h);

    if (!oreOnGrid(row + h - 1, col + w - 1)) {
        return false;
    }

    bool wants_deposit = ecs_has(world, prefab, OreDrillState);

    for (int r = row; r < row + h; r ++) {
        for (int c = col; c < col + w; c ++) {
            if (*oreBuildingCell(game, r, c)) {
                return false;
            }

            if ((*oreDepositCell(game, r, c) != 0) != wants_deposit) {
                return false;
            }
        }
    }

    return true;
}

static bool oreCanPlace(
    ecs_world_t *world,
    OreGame *game,
    const OreMap *map,
    ecs_entity_t prefab,
    int32_t row,
    int32_t col)
{
    if (!oreCellFree(world, game, prefab, row, col)) {
        return false;
    }

    int32_t w, h;
    oreFootprint(world, prefab, &w, &h);

    if (!oreInReach(world, game, map, row, col, w, h)) {
        return false;
    }

    const OreBuildLimit *limit = ecs_get(world, prefab, OreBuildLimit);
    if (limit && limit->max > 0) {
        int32_t placed = 0;

        ecs_iter_t it = ecs_each_id(world, ecs_pair(EcsIsA, prefab));
        while (ecs_each_next(&it)) {
            if (it.table && ecs_table_has_id(
                ecs_get_world(world), it.table, EcsPrefab))
            {
                continue;
            }

            placed += it.count;
        }

        if (placed >= limit->max) {
            return false;
        }
    }

    return oreInventoryGet(world, prefab) > 0;
}
#define ORE_DRAG_MAX (64)

static void oreGhostPaint(ecs_world_t *world, ecs_entity_t e, bool bad) {
    FlecsRgba color = bad
        ? (FlecsRgba){248, 108, 96, 118}
        : (FlecsRgba){158, 226, 255, 118};

    ecs_set_id(world, e, ecs_id(FlecsRgba), sizeof(FlecsRgba), &color);
    ecs_set(world, e, FlecsEmissive, {.strength = 0, .color = color});
    ecs_add_id(world, e, FlecsAlphaBlend);
}

static ecs_entity_t oreGhostFind(
    ecs_world_t *world,
    ecs_entity_t prefab,
    bool bad)
{
    ecs_entity_t scope = ecs_lookup(world, "ghosts");
    const char *name = prefab ? ecs_get_name(world, prefab) : NULL;
    if (!scope || !name) {
        return 0;
    }

    char id[80];
    snprintf(id, sizeof(id), "%s_%s", name, bad ? "bad" : "ok");

    return ecs_lookup_child(world, scope, id);
}

static ecs_entity_t oreGhost(
    ecs_world_t *world,
    ecs_entity_t prefab,
    bool bad)
{
    ecs_entity_t ghost = oreGhostFind(world, prefab, bad);
    if (ghost) {
        return ghost;
    }

    ecs_entity_t scope = ecs_lookup(world, "ghosts");
    const char *name = prefab ? ecs_get_name(world, prefab) : NULL;
    if (!scope || !name) {
        return 0;
    }

    char id[80];
    snprintf(id, sizeof(id), "%s_%s", name, bad ? "bad" : "ok");

    ghost = ecs_entity(world, { .name = id, .parent = scope });
    ecs_add_id(world, ghost, EcsPrefab);
    ecs_add_pair(world, ghost, EcsIsA, prefab);

    oreGhostPaint(world, ghost, bad);

    return ghost;
}

/* The drag in flight is the input module's armed dragged(Place) wait. Its
 * press-time cursor is resolved to a world cell once, on the first frame the
 * drag is seen, and that cell anchors both the preview and the placement until
 * the drag ends: re-projecting the press pixel every frame would slide the
 * anchor across the grid while the camera follows the walking player. */
static ecs_entity_t orePlaceAction(ecs_world_t *world) {
    return ecs_lookup(world, "ore_else.actions.Place");
}

static struct {
    OreCell cell;
    int32_t grace;
    bool armed;
} ore_drag;

static void oreDragDisarm(void) {
    ore_drag.armed = false;
    ore_drag.grace = 0;
}

static void oreDragTrack(ecs_world_t *world, const OreGame *game) {
    flecs_engine_mouse_state_t start;

    if (flecsEngine_inputDrag(world, orePlaceAction(world), &start)) {
        if (!ore_drag.armed) {
            ore_drag.armed = oreCellAt(world, game, start.view_norm.x,
                start.view_norm.y, &ore_drag.cell);
        }

        ore_drag.grace = 1;
        return;
    }

    if (ore_drag.grace > 0) {
        ore_drag.grace --;
        return;
    }

    ore_drag.armed = false;
}

static bool oreDragAnchor(OreCell *out) {
    if (!ore_drag.armed) {
        return false;
    }

    *out = ore_drag.cell;
    return true;
}

typedef struct ore_region_t {
    int32_t r0;
    int32_t r1;
    int32_t c0;
    int32_t c1;
    int32_t anchor_r;
    int32_t anchor_c;
    int32_t dest_r;
    int32_t dest_c;
    bool rect;
    bool horizontal;
} ore_region_t;

static bool oreDragRect(ecs_world_t *world) {
    return oreRunHeld(world, ecs_singleton_get(world, OreGame));
}

static ore_region_t oreRegion(
    ecs_world_t *world,
    const OreCell *anchor,
    const OreCell *dest,
    int32_t step_r,
    int32_t step_c)
{
    int32_t row = dest->row;
    int32_t col = dest->col;

    ore_region_t rg = {
        .r0 = row, .r1 = row, .c0 = col, .c1 = col,
        .anchor_r = row, .anchor_c = col,
        .dest_r = row, .dest_c = col,
        .rect = false, .horizontal = false
    };

    if (!anchor) {
        return rg;
    }

    rg.rect = oreDragRect(world);

    int32_t ar = anchor->row;
    int32_t ac = anchor->col;

    rg.r0 = ar < row ? ar : row;
    rg.r1 = ar > row ? ar : row;
    rg.c0 = ac < col ? ac : col;
    rg.c1 = ac > col ? ac : col;
    rg.anchor_r = ar;
    rg.anchor_c = ac;

    if (col == ac) {
        rg.horizontal = false;
    } else if (row == ar) {
        rg.horizontal = true;
    } else {
        float across = col > ac ? dest->fx : 1.0f - dest->fx;
        float along = row > ar ? dest->fz : 1.0f - dest->fz;
        rg.horizontal = across <= along;
    }

    if (rg.c0 < ac) {
        rg.c0 = ac - ((ac - rg.c0 + step_c - 1) / step_c) * step_c;
    }
    if (rg.r0 < ar) {
        rg.r0 = ar - ((ar - rg.r0 + step_r - 1) / step_r) * step_r;
    }

    rg.dest_c = col < ac ? rg.c0 : ac + ((col - ac) / step_c) * step_c;
    rg.dest_r = row < ar ? rg.r0 : ar + ((row - ar) / step_r) * step_r;

    return rg;
}

static bool oreRegionHas(
    const ore_region_t *rg,
    int32_t row,
    int32_t col)
{
    if (rg->rect) {
        return true;
    }

    if (rg->horizontal) {
        return col == rg->anchor_c || row == rg->dest_r;
    }

    return row == rg->anchor_r || col == rg->dest_c;
}

static int32_t oreDragCells(
    const ore_region_t *rg,
    int32_t step_r,
    int32_t step_c,
    int32_t *rows,
    int32_t *cols)
{
    int32_t n = 0;

    for (int32_t r = rg->r0; r <= rg->r1 && n < ORE_DRAG_MAX; r += step_r) {
        for (int32_t c = rg->c0; c <= rg->c1 && n < ORE_DRAG_MAX;
            c += step_c)
        {
            if (!oreRegionHas(rg, r, c)) {
                continue;
            }

            rows[n] = r;
            cols[n] = c;
            n ++;
        }
    }

    return n;
}

static ecs_entity_t* oreFloraCell(
    OreGame *game,
    int32_t row,
    int32_t col)
{
    return &game->flora_grid[row * ORE_COLS + col];
}

static void oreFloraShow(
    ecs_world_t *world,
    ecs_entity_t plant,
    bool show)
{
    if (!plant || !ecs_is_alive(world, plant)) {
        return;
    }

    const FlecsPosition3 *pos = ecs_get(world, plant, FlecsPosition3);
    if (!pos) {
        return;
    }

    float x = pos->x, z = pos->z;
    float y = show
        ? 0
        : -ecs_const_var_get_t(world, "cfg.floraSink", ecs_f32_t);

    if (pos->y == y) {
        return;
    }

    ecs_set(world, plant, FlecsPosition3, {x, y, z});
}

static void oreFloraScan(
    ecs_world_t *world,
    OreGame *game)
{
    if (game->flora_root && ecs_is_alive(world, game->flora_root)) {
        return;
    }

    ecs_entity_t root = ecs_lookup(world, "flora");
    if (!root) {
        return;
    }

    game->flora_root = root;
    ecs_os_memset(game->flora_grid, 0, sizeof(game->flora_grid));

    OreMap map = ecs_const_var_get_t(world, "cfg.map", OreMap);

    ecs_defer_begin(world);

    ecs_iter_t it = ecs_children(world, root);
    while (ecs_children_next(&it)) {
        for (int i = 0; i < it.count; i ++) {
            ecs_entity_t plant = it.entities[i];

            const FlecsPosition3 *pos = ecs_get(world, plant, FlecsPosition3);
            if (!pos) {
                continue;
            }

            int32_t col = oreColAt(&map, pos->x);
            int32_t row = oreRowAt(&map, pos->z);
            if (!oreOnGrid(row, col)) {
                continue;
            }

            ecs_entity_t *cell = oreFloraCell(game, row, col);
            if (*cell || *oreDepositCell(game, row, col)) {
                ecs_delete(world, plant);
                continue;
            }

            *cell = plant;
        }
    }

    ecs_defer_end(world);
}

static void OreFloraScan(ecs_iter_t *it) {
    OreGame *game = ecs_field(it, OreGame, 0);
    oreFloraScan(it->world, game);
}

static void OreFloraUncover(ecs_iter_t *it) {
    const OreBuilding *building = ecs_field(it, OreBuilding, 0);
    OreGame *game = ecs_field(it, OreGame, 1);

    for (int i = 0; i < it->count; i ++) {
        int32_t w, h;
        oreFootprint(it->world,
            ecs_get_target(it->world, it->entities[i], EcsIsA, 0), &w, &h);

        for (int r = building[i].row; r < building[i].row + h; r ++) {
            for (int c = building[i].col; c < building[i].col + w; c ++) {
                if (!oreOnGrid(r, c)) {
                    continue;
                }

                oreFloraShow(it->world, *oreFloraCell(game, r, c), true);
            }
        }
    }
}

static void orePlaceCenter(
    const OreMap *map,
    int32_t row,
    int32_t col,
    int32_t w,
    int32_t h,
    float *x,
    float *z)
{
    *x = oreTileX(map, col) + (float)(w - 1) * map->tile * 0.5f;
    *z = oreTileZ(map, row) + (float)(h - 1) * map->tile * 0.5f;
}

ecs_entity_t orePlaceBuilding(
    ecs_world_t *world,
    OreGame *game,
    const OreMap *map,
    ecs_entity_t prefab,
    int32_t row,
    int32_t col)
{
    int32_t w, h;
    oreFootprint(world, prefab, &w, &h);

    float x, z;
    orePlaceCenter(map, row, col, w, h, &x, &z);

    ecs_entity_t building = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_add_pair(world, building, EcsChildOf, game->buildings);
    ecs_set(world, building, FlecsPosition3, {x, 0, z});
    ecs_set(world, building, FlecsRotation3, {0, 0, 0});
    ecs_set(world, building, FlecsScale3, {1, 1, 1});
    ecs_set(world, building, OreBuilding, {.row = row, .col = col});

    for (int r = row; r < row + h; r ++) {
        for (int c = col; c < col + w; c ++) {
            *oreBuildingCell(game, r, c) = building;
            oreFloraShow(world, *oreFloraCell(game, r, c), false);
        }
    }

    return building;
}

static bool orePlaceAt(
    ecs_world_t *world,
    OreGame *game,
    const OreMap *map,
    int32_t row,
    int32_t col)
{
    if (!oreCanPlace(world, game, map, game->selected, row, col)) {
        return false;
    }

    ecs_entity_t prefab = game->selected;
    orePlaceBuilding(world, game, map, prefab, row, col);

    oreInventoryAdd(world, prefab, -1);

    if (oreInventoryGet(world, prefab) <= 0) {
        game->selected = 0;
    }

    int32_t w, h;
    oreFootprint(world, prefab, &w, &h);

    float x, z;
    orePlaceCenter(map, row, col, w, h, &x, &z);
    oreBurst(world, game->fx_pool, game->poof_burst, x, 0.4f, z);
    return true;
}

static bool oreRegionPlace(
    ecs_world_t *world,
    OreGame *game,
    const OreMap *map,
    const OreCell *anchor,
    const OreCell *dest)
{
    int32_t w, h;
    oreFootprint(world, game->selected, &w, &h);

    ore_region_t rg = oreRegion(world, anchor, dest, h, w);

    int32_t rows[ORE_DRAG_MAX], cols[ORE_DRAG_MAX];
    int32_t n = oreDragCells(&rg, h, w, rows, cols);

    bool placed = false;
    bool far = false;

    for (int32_t i = 0; i < n && game->selected; i ++) {
        if (orePlaceAt(world, game, map, rows[i], cols[i])) {
            placed = true;
            continue;
        }

        if (!far && oreOnGrid(rows[i] + h - 1, cols[i] + w - 1) &&
            !oreInReach(world, game, map, rows[i], cols[i], w, h))
        {
            far = true;
        }
    }

    if (far && !placed) {
        oreToast(world, "Too far away", true);
    }

    return placed;
}

static void orePipette(
    ecs_world_t *world,
    OreGame *game,
    const OreCell *cell)
{
    ecs_entity_t building = cell
        ? *oreBuildingCell(game, cell->row, cell->col)
        : 0;

    ecs_entity_t prefab = building
        ? ecs_get_target(world, building, EcsIsA, 0)
        : 0;

    if (!prefab && cell && *oreDepositCell(game, cell->row, cell->col)) {
        prefab = game->drill;
    }

    if (!prefab) {
        game->selected = 0;
        return;
    }

    if (oreInventoryGet(world, prefab) <= 0) {
        char name[64];
        char buf[96];

        snprintf(buf, sizeof(buf), "No %s in inventory",
            oreItemName(world, prefab, name, sizeof(name)));
        oreToast(world, buf, true);
        return;
    }

    game->selected = prefab;
}

#define ORE_SALVAGE_MAX (12)
#define ORE_SALVAGE_NONE (0)
#define ORE_SALVAGE_DONE (1)
#define ORE_SALVAGE_BLOCKED (2)

static int32_t oreSalvage(
    ecs_world_t *world,
    OreGame *game,
    ecs_entity_t building,
    ecs_entity_t prefab)
{
    const OreHealth *health = ecs_get(world, building, OreHealth);
    if (!health || health->max <= 0 || health->value >= health->max) {
        return ORE_SALVAGE_NONE;
    }

    const OreRecipe *recipe = prefab ? ecs_get(world, prefab, OreRecipe) : NULL;
    if (!recipe) {
        return ORE_SALVAGE_NONE;
    }

    float frac = health->value / health->max;
    if (frac < 0) {
        frac = 0;
    }

    ecs_entity_t items[ORE_SALVAGE_MAX];
    int32_t amounts[ORE_SALVAGE_MAX];
    int32_t count = 0;

    ecs_map_iter_t it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it) && count < ORE_SALVAGE_MAX) {
        int32_t amount = (int32_t)ecs_map_value(&it);
        if (amount <= 0) {
            continue;
        }

        items[count] = ecs_map_key(&it);
        amounts[count] = (int32_t)((float)amount * frac);
        count ++;
    }

    if (!oreInventoryFits(world, items, amounts, count)) {
        oreToast(world, "Inventory full - can't salvage", true);
        return ORE_SALVAGE_BLOCKED;
    }

    char buf[256];
    int32_t n = snprintf(buf, sizeof(buf), "Salvaged ");
    int32_t given = 0;

    for (int32_t i = 0; i < count; i ++) {
        if (amounts[i] <= 0) {
            continue;
        }

        oreInventoryAdd(world, items[i], amounts[i]);

        char name[64];
        oreItemName(world, items[i], name, sizeof(name));

        if (n < (int32_t)sizeof(buf)) {
            n += snprintf(buf + n, sizeof(buf) - n, "%s%d %s",
                given ? ", " : "", amounts[i], name);
        }

        given ++;
    }

    if (!given) {
        n = snprintf(buf, sizeof(buf), "Salvaged nothing");
    }

    if (n < (int32_t)sizeof(buf)) {
        snprintf(buf + n, sizeof(buf) - n, " (%d%%)",
            (int32_t)(frac * 100.0f + 0.5f));
    }

    oreToast(world, buf, false);
    return ORE_SALVAGE_DONE;
}

bool oreDemolish(
    ecs_world_t *world,
    OreGame *game,
    const OreMap *map,
    int32_t row,
    int32_t col)
{
    ecs_entity_t *cell = oreBuildingCell(game, row, col);
    if (!*cell) {
        return false;
    }

    ecs_entity_t building = *cell;

    ecs_entity_t prefab = ecs_get_target(world, building, EcsIsA, 0);

    int32_t salvage = oreSalvage(world, game, building, prefab);
    if (salvage == ORE_SALVAGE_BLOCKED) {
        return false;
    }

    if (salvage == ORE_SALVAGE_NONE && prefab &&
        !oreInventoryAdd(world, prefab, 1))
    {
        return false;
    }

    float x = oreTileX(map, col);
    float z = oreTileZ(map, row);
    oreBurst(world, game->fx_pool, game->poof_burst, x, 0.6f, z);

    ecs_delete(world, building);
    return true;
}
static void OreGhostBuild(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    const OreGame *game = ecs_field(it, OreGame, 0);
    if (!game->selected) {
        return;
    }

    oreGhost(world, game->selected, false);
    oreGhost(world, game->selected, true);
}

static void OreHover(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreGame *game = ecs_field(it, OreGame, 0);
    const FlecsInput *input = ecs_field(it, FlecsInput, 1);

    game->hover_row = -1;
    game->hover_col = -1;

    OreCell cell;
    if (oreCellAt(world, game, input->mouse.view_norm.x,
        input->mouse.view_norm.y, &cell))
    {
        game->hover_row = cell.row;
        game->hover_col = cell.col;
        game->hover_fx = cell.fx;
        game->hover_fz = cell.fz;
    }

    oreDragTrack(world, game);
}

static OreGame* oreInputGame(ecs_world_t *world) {
    if (!orePlaying(world)) {
        return NULL;
    }
    return ecs_singleton_ensure(world, OreGame);
}

static bool oreInputBlocked(ecs_world_t *world) {
    const OreUiState *ui = ecs_singleton_get(world, OreUiState);
    return (ui && ui->open) || flecsEngine_uiMouseCaptured(world);
}

/* Places what the drag covered: the press cell anchors the region, the release
 * cell picks its far end. A click that never moved places one building at the
 * cell it started on. */
static void orePlaceRegion(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    OreGame *game = oreInputGame(world);
    if (argc != 1 || !argv[0].ptr || !game || oreInputBlocked(world) ||
        !game->selected)
    {
        oreResolve(future, false);
        return;
    }

    const FlecsInputEvent *ev = argv[0].ptr;

    OreCell start, end;
    bool anchored = oreDragAnchor(&start);
    oreDragDisarm();

    if (!anchored && !oreCellAt(world, game, ev->start.view_norm.x,
        ev->start.view_norm.y, &start))
    {
        oreResolve(future, false);
        return;
    }

    if (!ev->moved) {
        end = start;
    } else if (!oreCellAt(world, game, ev->mouse.view_norm.x,
        ev->mouse.view_norm.y, &end))
    {
        oreResolve(future, false);
        return;
    }

    OreMap map = ecs_const_var_get_t(world, "cfg.map", OreMap);
    bool placed = oreRegionPlace(world, game, &map,
        ev->moved ? &start : NULL, &end);

    ecs_singleton_modified(world, OreGame);
    oreResolve(future, placed);
}

static void oreAbortPlacement(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    OreGame *game = oreInputGame(world);
    if (!game) {
        oreResolve(future, false);
        return;
    }

    oreToast(world, "Placement cancelled", false);
    ecs_singleton_modified(world, OreGame);
    oreResolve(future, true);
}

/* Aborting a drag means rejecting the dragged(Place) wait the placement script
 * is suspended on, which makes it take its catch branch. Only reject while a
 * drag is actually in flight: the same script waits on the same action for the
 * next press, and that wait must survive. */
bool oreRejectDrag(ecs_world_t *world) {
    ecs_entity_t place = orePlaceAction(world);
    if (!flecsEngine_inputDrag(world, place, NULL)) {
        return false;
    }

    oreDragDisarm();

    return flecsEngine_inputReject(world, place, 0) != 0;
}

static void oreCancelDrag(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    oreResolve(future, oreRejectDrag(ctx->world));
}

bool orePaused(ecs_world_t *world) {
    ecs_entity_t paused = ecs_constant_to_entity(
        world, OreGameState, OreGameStatePaused);
    return ecs_has_pair(world, ecs_id(OreGameState),
        ecs_id(OreGameState), paused);
}

static bool ore_sim_frozen;

static void OreSimFreeze(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    bool paused = orePaused(world);
    if (paused == ore_sim_frozen) {
        return;
    }

    ore_sim_frozen = paused;
    ecs_set_time_scale(world, (ecs_ftime_t)(paused ? 0.0 : 1.0));
}

/* Escape backs out one step: it aborts a placement drag that is in flight
 * (which rejects the script waiting on it), and otherwise deselects. */
static void oreCancel(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;

    if (orePaused(world)) {
        oreSetState(world, OreGameStatePlaying);
        oreResolve(future, true);
        return;
    }

    OreGame *game = oreInputGame(world);
    if (!game) {
        oreResolve(future, false);
        return;
    }

    if (oreRejectDrag(world)) {
        oreResolve(future, true);
        return;
    }

    if (game->selected) {
        game->selected = 0;
        ecs_singleton_modified(world, OreGame);
        oreResolve(future, true);
        return;
    }

    const OreUiState *ui = ecs_singleton_get(world, OreUiState);

    if (ui && ui->open) {
        oreResolve(future, false);
        return;
    }

    oreSetState(world, OreGameStatePaused);
    oreResolve(future, true);
}
static void orePickUnderCursor(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    OreGame *game = oreInputGame(world);
    const OreUiState *ui = ecs_singleton_get(world, OreUiState);
    if (argc != 1 || !argv[0].ptr || !game || (ui && ui->open)) {
        oreResolve(future, false);
        return;
    }

    const flecs_engine_mouse_state_t *mouse = argv[0].ptr;

    OreCell cell;
    bool on_grid = oreCellAt(
        world, game, mouse->view_norm.x, mouse->view_norm.y, &cell);

    orePipette(world, game, on_grid ? &cell : NULL);
    ecs_singleton_modified(world, OreGame);
    oreResolve(future, game->selected != 0);
}

static void OreHoverGhost(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreGame *game = ecs_field(it, OreGame, 0);
    const OreUiState *ui = ecs_field(it, OreUiState, 1);

    if (ui->open || game->hover_row < 0 || !game->selected) {
        return;
    }

    OreMap map = ecs_const_var_get_t(world, "cfg.map", OreMap);

    int32_t w, h;
    oreFootprint(world, game->selected, &w, &h);

    OreCell dest = {
        .row = game->hover_row, .col = game->hover_col,
        .fx = game->hover_fx, .fz = game->hover_fz
    };

    OreCell anchor;
    bool dragging = oreDragAnchor(&anchor);

    ore_region_t rg = oreRegion(
        world, dragging ? &anchor : NULL, &dest, h, w);

    int32_t rows[ORE_DRAG_MAX], cols[ORE_DRAG_MAX];
    int32_t n = oreDragCells(&rg, h, w, rows, cols);

    int32_t stock = oreInventoryGet(world, game->selected);
    int32_t used = 0;

    flecs_draw_instance_t good[ORE_DRAG_MAX * 4];
    flecs_draw_instance_t bad[ORE_DRAG_MAX * 4];
    flecs_draw_instance_t ok_ghost[ORE_DRAG_MAX];
    flecs_draw_instance_t bad_ghost[ORE_DRAG_MAX];
    int32_t good_n = 0, bad_n = 0, ok_n = 0, bad_g = 0;

    for (int32_t i = 0; i < n; i ++) {
        bool ok = used < stock &&
            oreCanPlace(world, game, &map, game->selected, rows[i], cols[i]);

        if (ok) {
            used ++;
        }

        for (int r = rows[i]; r < rows[i] + h; r ++) {
            for (int c = cols[i]; c < cols[i] + w; c ++) {
                if (!oreOnGrid(r, c)) {
                    continue;
                }

                flecs_draw_instance_t *dst = ok
                    ? &good[good_n ++]
                    : &bad[bad_n ++];

                *dst = (flecs_draw_instance_t){
                    .position = {
                        oreTileX(&map, c), 0.12f, oreTileZ(&map, r)},
                    .scale = {1, 1, 1}
                };
            }
        }

        if (!oreOnGrid(rows[i] + h - 1, cols[i] + w - 1)) {
            continue;
        }

        flecs_draw_instance_t *dst = ok
            ? &ok_ghost[ok_n ++]
            : &bad_ghost[bad_g ++];

        *dst = (flecs_draw_instance_t){
            .position = {
                oreTileX(&map, cols[i]) + (float)(w - 1) * map.tile * 0.5f,
                0.1f,
                oreTileZ(&map, rows[i]) + (float)(h - 1) * map.tile * 0.5f
            },
            .scale = {0.86f, 0.86f, 0.86f}
        };
    }

    if (good_n) {
        flecsEngine_draw(world, game->marker_prefab, good, good_n);
    }

    if (bad_n) {
        flecsEngine_draw(world, game->marker_bad, bad, bad_n);
    }

    ecs_entity_t ok_prefab = oreGhostFind(world, game->selected, false);
    ecs_entity_t bad_prefab = oreGhostFind(world, game->selected, true);

    if (ok_n && ok_prefab) {
        flecsEngine_draw(world, ok_prefab, ok_ghost, ok_n);
    }

    if (bad_g && bad_prefab) {
        flecsEngine_draw(world, bad_prefab, bad_ghost, bad_g);
    }
}

void orePlacementImport(ecs_world_t *world) {
    ecs_entity_t scope = ecs_set_scope(world, 0);
    ecs_entity(world, { .name = "ghosts" });
    ecs_set_scope(world, scope);

    ecs_entity_t playing = ecs_constant_to_entity(
        world, OreGameState, OreGameStatePlaying);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreGhostBuild" }),
        .phase = EcsPreUpdate,
        .immediate = true,
        .query.terms = {
            { .id = ecs_id(OreGame) }
        },
        .callback = OreGhostBuild
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreSimFreeze" }),
        .phase = EcsPreUpdate,
        .immediate = true,
        .query.terms = {
            { .id = ecs_id(OreGame) }
        },
        .callback = OreSimFreeze
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreFloraScan" }),
        .phase = EcsPreUpdate,
        .query.terms = {
            { .id = ecs_id(OreGame) }
        },
        .callback = OreFloraScan
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreFloraUncover" }),
        .query.terms = {
            { .id = ecs_id(OreBuilding) },
            { .id = ecs_id(OreGame) }
        },
        .events = { EcsOnRemove },
        .callback = OreFloraUncover
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreHover" }),
        .phase = EcsOnLoad,
        .query.terms = {
            { .id = ecs_id(OreGame) },
            { .id = ecs_id(FlecsInput) }
        },
        .callback = OreHover
    });

    struct {
        const char *name;
        ecs_entity_t param_type;
        ecs_async_function_callback_t callback;
    } functions[] = {
        { "placeRegion", ecs_id(FlecsInputEvent), orePlaceRegion },
        { "abortPlacement", 0, oreAbortPlacement },
        { "cancelDrag", 0, oreCancelDrag },
        { "cancel", 0, oreCancel },
        { "pickUnderCursor", ecs_id(flecs_engine_mouse_state_t),
            orePickUnderCursor }
    };

    for (int32_t i = 0; i < (int32_t)(sizeof(functions) / sizeof(functions[0])); i ++) {
        ecs_async_function(world, {
            .name = functions[i].name,
            .parent = ecs_get_scope(world),
            .return_type = ecs_id(ecs_bool_t),
            .params = {{ functions[i].param_type ? "event" : NULL,
                functions[i].param_type }},
            .callback = functions[i].callback
        });
    }

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreHoverGhost" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreGame) },
            { .id = ecs_id(OreUiState), .inout = EcsIn },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreHoverGhost
    });
}
