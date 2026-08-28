#define SHIP_HAPPENS_GAME_IMPL
#include "ship_happens.h"

#include <stdio.h>

ECS_TAG_DECLARE(ShipDollarPart);

float shipRandf(void) {
    return (float)rand() / (float)RAND_MAX;
}

const char *ship_title_first[] = {
    "Rogue", "Super", "Mega", "Tiny", "Cosmic", "Grim", "Turbo", "Pixel",
    "Neon", "Hyper"
};

const char *ship_title_second[] = {
    "Clover", "Falcon", "Dungeon", "Galaxy", "Bakery", "Kingdom", "Drifter",
    "Raccoon", "Harvest", "Nebula"
};

const char *ship_title_third[] = {
    "Deluxe", "II", "Remastered", "Origins", "Ultimate", "64", "Tactics",
    "Saga", "Forever", "Redux"
};

const char *ship_feature_first[] = {
    "Handcrafted", "Procedural", "Dynamic", "Cozy", "Epic", "Immersive",
    "Retro", "Quantum", "Photorealistic", "Emergent", "Cinematic", "Seamless"
};

const char *ship_feature_second[] = {
    "Save System", "Skill Tree", "Photo Mode", "Boss Fights", "Crafting",
    "Pet Companion", "Weather", "Dialogue Wheel", "New Game Plus",
    "Fishing Minigame", "Inventory Tetris", "Soundtrack"
};

#define ship_pick(array) (array[rand() % (sizeof(array) / sizeof(array[0]))])

void shipRollTitle(ShipGame *game) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s %s %s", ship_pick(ship_title_first),
        ship_pick(ship_title_second), ship_pick(ship_title_third));
    ecs_os_free(game->game_name);
    game->game_name = ecs_os_strdup(buf);
}

void shipRollFeature(ShipGame *game) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s %s", ship_pick(ship_feature_first),
        ship_pick(ship_feature_second));
    ecs_os_free(game->feature_name);
    game->feature_name = ecs_os_strdup(buf);
}

float shipTileX(const ShipOffice *office, int32_t col) {
    return office->x0 + (float)col * office->tile;
}

float shipTileZ(const ShipOffice *office, int32_t row) {
    return office->z0 + (float)row * office->tile;
}

int32_t shipColAt(const ShipOffice *office, float x) {
    return (int32_t)floorf((x - office->x0) / office->tile + 0.5f);
}

int32_t shipRowAt(const ShipOffice *office, float z) {
    return (int32_t)floorf((z - office->z0) / office->tile + 0.5f);
}

bool shipOnGrid(int32_t row, int32_t col) {
    return row >= 0 && row < SHIP_ROWS && col >= 0 && col < SHIP_COLS;
}

ecs_entity_t* shipCell(ShipGame *game, int32_t row, int32_t col) {
    return &game->grid[row * SHIP_COLS + col];
}

void shipBurst(ecs_world_t *world, ecs_entity_t pool, ecs_entity_t burst,
    float x, float y, float z)
{
    const FlecsParticleBurst *b = ecs_get(world, burst, FlecsParticleBurst);
    float at[3] = {x, y, z};
    flecsEngine_particlesBurst(world, pool, at, b);
}

void shipSetState(ecs_world_t *world, ShipGameState state) {
    ecs_singleton_add_pair(world, ShipGameState,
        ecs_constant_to_entity(world, ShipGameState, state));
}

float shipDevMood(const ShipDevNeeds *needs) {
    return (needs->caffeine + needs->food + needs->fun) / 3.0f;
}

void shipWalkTo(ShipDev *dev, float x, float z) {
    dev->tx = x;
    dev->tz = z;
}

void shipQueueSpot(ecs_world_t *world, int32_t slot, float *x, float *z) {
    *x = ecs_const_var_get_t(world, "cfg.queueX", ecs_f32_t) +
        (float)slot * ecs_const_var_get_t(world, "cfg.queueGap", ecs_f32_t);
    *z = ecs_const_var_get_t(world, "cfg.doorZ", ecs_f32_t);
}

void shipDeskSpot(ecs_world_t *world, ecs_entity_t desk, float *x, float *z) {
    const FlecsPosition3 *pos = ecs_get(world, desk, FlecsPosition3);
    *x = pos->x;
    *z = pos->z - ecs_const_var_get_t(world, "cfg.standOff", ecs_f32_t);
}

void shipSpawnDollar(ecs_world_t *world, const ShipGame *game,
    float x, float y, float z)
{
    ecs_entity_t dollar = ecs_new_w_pair(world, EcsIsA, game->dollar_prefab);
    ecs_add_pair(world, dollar, EcsChildOf, game->dollars);
    ecs_set(world, dollar, FlecsPosition3, {x, y, z});
    ecs_set(world, dollar, FlecsRotation3, {0, 0, 0});
    ecs_set(world, dollar, FlecsScale3, {1, 1, 1});
    ecs_set(world, dollar, ShipFloater, {
        .life = 1.2f,
        .rise = 1.4f
    });
}

void shipSendHome(ecs_world_t *world, ecs_entity_t e, ShipDev *dev) {
    if (dev->station && ecs_is_alive(world, dev->station)) {
        ShipAmenity *amenity = ecs_get_mut(world, dev->station, ShipAmenity);
        if (amenity && amenity->user == e) {
            amenity->user = 0;
        }
    }
    dev->station = 0;

    if (dev->desk && ecs_is_alive(world, dev->desk)) {
        ShipDesk *desk = ecs_get_mut(world, dev->desk, ShipDesk);
        if (desk && desk->owner == e) {
            desk->owner = 0;
        }
    }
    dev->desk = 0;

    dev->state = SHIP_DEV_QUIT;
    shipWalkTo(dev,
        ecs_const_var_get_t(world, "cfg.doorInX", ecs_f32_t),
        ecs_const_var_get_t(world, "cfg.doorZ", ecs_f32_t));
}

void ShipFurnitureOnRemove(ecs_iter_t *it) {
    const ShipFurniture *item = ecs_field(it, ShipFurniture, 0);
    ShipGame *game = ecs_field(it, ShipGame, 1);

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t *cell = shipCell(game, item[i].row, item[i].col);
        if (*cell == it->entities[i]) {
            *cell = 0;
        }
    }
}

void ShipResetGrid(ecs_iter_t *it) {
    ShipGame *game = ecs_field(it, ShipGame, 0);
    game->work_rate = 0;
    game->mood_sum = 0;
    game->mood_count = 0;
}

static void ShipUiClick(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    ecs_entity_t playing = ecs_constant_to_entity(
        world, ShipGameState, ShipGameStatePlaying);
    if (!ecs_has_pair(world, ecs_id(ShipGameState),
        ecs_id(ShipGameState), playing))
    {
        return;
    }

    ShipPacket *packet = ecs_field(it, ShipPacket, 0);
    ShipGame *game = ecs_singleton_ensure(world, ShipGame);

    for (int i = 0; i < it->count; i ++) {
        if (!packet[i].item) {
            game->bulldoze = !game->bulldoze;
            game->selected = 0;
        } else {
            game->selected = (game->selected == packet[i].item) ?
                0 : packet[i].item;
            game->bulldoze = false;
        }
    }

    ecs_singleton_modified(world, ShipGame);
}

void shipAddFeatureClick(ecs_world_t *world, ecs_entity_t widget, void *ctx) {
    (void)widget;
    (void)ctx;

    ecs_entity_t playing = ecs_constant_to_entity(
        world, ShipGameState, ShipGameStatePlaying);
    if (!ecs_has_pair(world, ecs_id(ShipGameState),
        ecs_id(ShipGameState), playing))
    {
        return;
    }

    ShipGame *game = ecs_singleton_ensure(world, ShipGame);
    game->features_total ++;
    ecs_singleton_modified(world, ShipGame);
}

void shipCrunchClick(ecs_world_t *world, ecs_entity_t widget, void *ctx) {
    (void)widget;
    (void)ctx;

    ecs_entity_t playing = ecs_constant_to_entity(
        world, ShipGameState, ShipGameStatePlaying);
    if (!ecs_has_pair(world, ecs_id(ShipGameState),
        ecs_id(ShipGameState), playing))
    {
        return;
    }

    ShipGame *game = ecs_singleton_ensure(world, ShipGame);
    game->crunch = !game->crunch;
    ecs_singleton_modified(world, ShipGame);
}

void ShipUiBind(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    ecs_entity_t add_feature = ecs_lookup(
        world, "ship_happens.assets.AddFeatureButton");
    if (add_feature) {
        ecs_iter_t ait = ecs_each_id(world, add_feature);
        while (ecs_each_next(&ait)) {
            for (int i = 0; i < ait.count; i ++) {
                ecs_entity_t e = ait.entities[i];
                if (!ecs_has(world, e, FlecsUiEventListener)) {
                    ecs_set(world, e, FlecsUiEventListener, {
                        .on_lmb_up = shipAddFeatureClick
                    });
                }
            }
        }
    }

    ecs_entity_t packet = ecs_lookup(world, "ship_happens.assets.Packet");
    if (!packet) {
        return;
    }

    ecs_entity_t packet_mut = ecs_lookup_child(world, packet, "mut");
    ecs_member_t *active_member = packet_mut
        ? ecs_struct_get_member(world, packet_mut, "active")
        : NULL;

    const ShipGame *game = ecs_singleton_get(world, ShipGame);

    ecs_iter_t pit = ecs_each_id(world, packet);
    while (ecs_each_next(&pit)) {
        for (int i = 0; i < pit.count; i ++) {
            ecs_entity_t e = pit.entities[i];

            const ShipPacket *p = ecs_get(world, e, ShipPacket);
            const void *mut_data = active_member
                ? ecs_get_id(world, e, packet_mut)
                : NULL;
            if (!p || !mut_data) {
                continue;
            }

            bool value = game && (p->item
                ? game->selected == p->item
                : game->bulldoze);
            ecs_size_t mut_size = ecs_get_type_info(world, packet_mut)->size;
            void *dst = ecs_os_malloc(mut_size);
            ecs_os_memcpy(dst, mut_data, (size_t)mut_size);
            *(bool*)ECS_OFFSET(dst, active_member->offset) = value;
            ecs_set_id(world, e, packet_mut, (size_t)mut_size, dst);
            ecs_os_free(dst);
        }
    }

    ecs_entity_t crunch_btn = ecs_lookup(
        world, "ship_happens.assets.CrunchButton");
    if (!crunch_btn) {
        return;
    }

    ecs_entity_t crunch_mut = ecs_lookup_child(world, crunch_btn, "mut");
    ecs_member_t *crunch_active = crunch_mut
        ? ecs_struct_get_member(world, crunch_mut, "active")
        : NULL;

    ecs_iter_t cit = ecs_each_id(world, crunch_btn);
    while (ecs_each_next(&cit)) {
        for (int i = 0; i < cit.count; i ++) {
            ecs_entity_t e = cit.entities[i];

            if (!ecs_has(world, e, FlecsUiEventListener)) {
                ecs_set(world, e, FlecsUiEventListener, {
                    .on_lmb_up = shipCrunchClick
                });
            }

            const void *mut_data = crunch_active
                ? ecs_get_id(world, e, crunch_mut)
                : NULL;
            if (!mut_data) {
                continue;
            }

            bool value = game && game->crunch;
            ecs_size_t mut_size = ecs_get_type_info(world, crunch_mut)->size;
            void *dst = ecs_os_malloc(mut_size);
            ecs_os_memcpy(dst, mut_data, (size_t)mut_size);
            *(bool*)ECS_OFFSET(dst, crunch_active->offset) = value;
            ecs_set_id(world, e, crunch_mut, (size_t)mut_size, dst);
            ecs_os_free(dst);
        }
    }
}

bool shipBulldoze(ecs_world_t *world, ShipGame *game,
    const ShipOffice *office, int32_t row, int32_t col)
{
    ecs_entity_t *cell = shipCell(game, row, col);
    if (!*cell) {
        return false;
    }

    ecs_entity_t item = *cell;

    const ShipDesk *desk = ecs_get(world, item, ShipDesk);
    if (desk && desk->owner && ecs_is_alive(world, desk->owner)) {
        ShipDev *dev = ecs_get_mut(world, desk->owner, ShipDev);
        if (dev) {
            shipSendHome(world, desk->owner, dev);
        }
    }

    const ShipAmenity *amenity = ecs_get(world, item, ShipAmenity);
    if (amenity && amenity->user && ecs_is_alive(world, amenity->user)) {
        ShipDev *dev = ecs_get_mut(world, amenity->user, ShipDev);
        if (dev && ecs_is_alive(world, dev->desk)) {
            dev->station = 0;
            dev->state = SHIP_DEV_ARRIVE;

            float sx, sz;
            shipDeskSpot(world, dev->desk, &sx, &sz);
            shipWalkTo(dev, sx, sz);
        }
    }

    const ShipCost *cost = ecs_get(world, item, ShipCost);
    if (cost) {
        game->money += cost->money / 2;
    }

    float x = shipTileX(office, col);
    float z = shipTileZ(office, row);
    shipBurst(world, game->fx_pool, game->poof_burst, x, 0.6f, z);

    ecs_delete(world, item);
    return true;
}

static void shipResolve(ecs_script_future_t *future, bool value) {
    ecs_script_future_resolve(future,
        &(ecs_value_t){ecs_id(ecs_bool_t), &value});
    ecs_script_future_release(future);
}

static bool shipPlaying(ecs_world_t *world) {
    return ecs_has_pair(world, ecs_id(ShipGameState), ecs_id(ShipGameState),
        ecs_constant_to_entity(world, ShipGameState, ShipGameStatePlaying));
}

static ShipGame* shipInputGame(ecs_world_t *world) {
    if (!shipPlaying(world) || flecsEngine_uiMouseCaptured(world)) {
        return NULL;
    }
    return ecs_singleton_ensure(world, ShipGame);
}

static void shipCancel(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    ShipGame *game = shipPlaying(world)
        ? ecs_singleton_ensure(world, ShipGame) : NULL;
    if (!game || !(game->selected || game->bulldoze)) {
        shipResolve(future, false);
        return;
    }

    game->selected = 0;
    game->bulldoze = false;
    ecs_singleton_modified(world, ShipGame);
    shipResolve(future, true);
}

static bool shipPlaceFurniture(ecs_world_t *world, ShipGame *game,
    const ShipOffice *office)
{
    ecs_entity_t *cell = shipCell(game, game->hover_row, game->hover_col);
    const ShipCost *cost = ecs_get(world, game->selected, ShipCost);

    if (*cell || !cost) {
        return false;
    }

    float x = shipTileX(office, game->hover_col);
    float z = shipTileZ(office, game->hover_row);

    ecs_entity_t item = ecs_new_w_pair(world, EcsIsA, game->selected);
    ecs_add_pair(world, item, EcsChildOf, game->furniture);
    ecs_set(world, item, FlecsPosition3, {x, 0, z});
    ecs_set(world, item, FlecsRotation3, {0, 0, 0});
    ecs_set(world, item, FlecsScale3, {1, 1, 1});
    ecs_set(world, item, ShipFurniture, {
        .row = game->hover_row,
        .col = game->hover_col
    });

    *cell = item;

    game->money -= cost->money;

    shipBurst(world, game->fx_pool, game->poof_burst, x, 0.4f, z);
    return true;
}

static void shipPlace(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    ShipGame *game = shipInputGame(world);
    if (!game || game->hover_row < 0) {
        shipResolve(future, false);
        return;
    }

    ShipOffice office = ecs_const_var_get_t(world, "cfg.office", ShipOffice);
    bool changed = false;

    if (game->bulldoze) {
        changed = shipBulldoze(world, game, &office,
            game->hover_row, game->hover_col);
    } else if (game->selected) {
        changed = shipPlaceFurniture(world, game, &office);
    }

    if (changed) {
        ecs_singleton_modified(world, ShipGame);
    }
    shipResolve(future, changed);
}

static void shipBulldozeAction(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    ShipGame *game = shipInputGame(world);
    if (!game) {
        shipResolve(future, false);
        return;
    }

    if (game->selected || game->bulldoze) {
        game->selected = 0;
        game->bulldoze = false;
        ecs_singleton_modified(world, ShipGame);
        shipResolve(future, true);
        return;
    }

    if (game->hover_row < 0) {
        shipResolve(future, false);
        return;
    }

    ShipOffice office = ecs_const_var_get_t(world, "cfg.office", ShipOffice);
    bool changed = shipBulldoze(world, game, &office,
        game->hover_row, game->hover_col);
    if (changed) {
        ecs_singleton_modified(world, ShipGame);
    }
    shipResolve(future, changed);
}

void ShipHover(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    ShipGame *game = ecs_field(it, ShipGame, 0);
    const FlecsInput *input = ecs_field(it, FlecsInput, 1);

    ShipOffice office = ecs_const_var_get_t(world, "cfg.office", ShipOffice);

    game->hover_row = -1;
    game->hover_col = -1;

    vec3 origin, dir, ground;
    flecsEngine_cameraScreenRay(world, game->camera,
        input->mouse.view_norm.x, input->mouse.view_norm.y, origin, dir);

    if (flecsEngine_rayPlaneY(origin, dir, 0.0f, ground)) {
        int32_t col = shipColAt(&office, ground[0]);
        int32_t row = shipRowAt(&office, ground[2]);
        if (shipOnGrid(row, col)) {
            game->hover_row = row;
            game->hover_col = col;
        }
    }
}

void ShipHoverGhost(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    const ShipGame *game = ecs_field(it, ShipGame, 0);

    if (game->hover_row < 0 || (!game->selected && !game->bulldoze)) {
        return;
    }

    ShipOffice office = ecs_const_var_get_t(world, "cfg.office", ShipOffice);
    float x = shipTileX(&office, game->hover_col);
    float z = shipTileZ(&office, game->hover_row);

    bool free_cell = !game->grid[game->hover_row * SHIP_COLS +
        game->hover_col];

    if (game->bulldoze && free_cell) {
        return;
    }

    flecsEngine_draw(world, game->marker_prefab,
        &(flecs_draw_instance_t){
            .position = {x, 0.12f, z},
            .scale = {1, 1, 1}
        }, 1);

    if (game->bulldoze) {
        return;
    }

    if (free_cell) {
        flecsEngine_draw(world, game->selected,
            &(flecs_draw_instance_t){
                .position = {x, 0.1f, z},
                .scale = {0.8f, 0.8f, 0.8f}
            }, 1);
    }
}

void ShipAnimIncrement(ecs_iter_t *it) {
    ShipAnim *anim = ecs_field(it, ShipAnim, 0);
    const ShipAnimSpeed *speed = ecs_field(it, ShipAnimSpeed, 1);

    for (int i = 0; i < it->count; i ++) {
        anim[i].value += speed[i].value * it->delta_time;
    }
}

void ShipHiring(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    ShipGame *game = ecs_field(it, ShipGame, 0);
    bool changed = false;

    float door_z = ecs_const_var_get_t(world, "cfg.doorZ", ecs_f32_t);
    float spawn_x = ecs_const_var_get_t(world, "cfg.spawnX", ecs_f32_t);
    int32_t max_applicants = ecs_const_var_get_t(
        world, "cfg.maxApplicants", ecs_i32_t);

    game->apply_timer -= it->delta_time;
    if (game->apply_timer <= 0) {
        game->apply_timer = ecs_const_var_get_t(
            world, "cfg.applyInterval", ecs_f32_t);

        if (game->applicants < max_applicants) {
            int32_t slot = game->applicants;
            float qx, qz;
            shipQueueSpot(world, slot, &qx, &qz);

            ecs_entity_t dev = ecs_new(world);
            ecs_add_pair(world, dev, EcsChildOf, game->staff);
            ecs_set(world, dev, Dev, {
                .outfit = rand(),
                .skin = rand(),
                .hair = rand(),
                .phones = rand(),
                .mood = 0.85f,
                .energy = 1
            });
            ecs_set(world, dev, FlecsPosition3, {
                spawn_x + shipRandf(), 0,
                door_z + (shipRandf() - 0.5f)});
            ecs_set(world, dev, FlecsRotation3, {0, 0, 0});

            float s = 0.72f + shipRandf() * 0.12f;
            ecs_set(world, dev, FlecsScale3, {s, s, s});

            ecs_set(world, dev, ShipApplicant, {
                .slot = slot,
                .tx = qx,
                .tz = qz
            });
            ecs_set(world, dev, ShipAnim, {shipRandf() * 6.2832f});

            game->applicants ++;
            changed = true;
        }
    }

    ecs_entity_t free_desk = 0;

    ecs_iter_t dit = ecs_query_iter(world, game->desk_query);
    while (ecs_query_next(&dit)) {
        const ShipDesk *desk = ecs_field(&dit, ShipDesk, 0);

        for (int i = 0; i < dit.count; i ++) {
            if (!desk[i].owner) {
                free_desk = dit.entities[i];
                break;
            }
        }

        if (free_desk) {
            ecs_iter_fini(&dit);
            break;
        }
    }

    if (free_desk && game->applicants > 0) {
        ecs_entity_t front = 0;

        ecs_iter_t vit = ecs_query_iter(world, game->applicant_query);
        while (ecs_query_next(&vit)) {
            ShipApplicant *app = ecs_field(&vit, ShipApplicant, 0);

            for (int i = 0; i < vit.count; i ++) {
                if (app[i].slot == 0) {
                    front = vit.entities[i];
                    ecs_remove(world, front, ShipApplicant);
                    ecs_set(world, front, ShipDev, {
                        .state = SHIP_DEV_ARRIVE,
                        .desk = free_desk,
                        .pay_timer = shipRandf() *
                            ecs_const_var_get_t(
                                world, "cfg.payInterval", ecs_f32_t),
                        .tx = ecs_const_var_get_t(
                            world, "cfg.doorInX", ecs_f32_t),
                        .tz = door_z
                    });
                    ecs_set(world, front, ShipDevNeeds, {
                        .caffeine = 1,
                        .food = 0.8f + shipRandf() * 0.2f,
                        .fun = 0.8f + shipRandf() * 0.2f
                    });
                } else {
                    app[i].slot --;
                    float qx, qz;
                    shipQueueSpot(world, app[i].slot, &qx, &qz);
                    app[i].tx = qx;
                    app[i].tz = qz;
                }
            }
        }

        if (front) {
            ShipDesk *desk = ecs_get_mut(world, free_desk, ShipDesk);
            desk->owner = front;

            game->applicants --;
            game->devs ++;
            changed = true;
        }
    }

    if (changed) {
        ecs_singleton_modified(world, ShipGame);
    }
}

bool shipWalkStep(float tx, float tz, bool *moving, const ShipAnim *anim,
    FlecsPosition3 *pos, FlecsRotation3 *rot, float speed, float dt)
{
    float dx = tx - pos->x;
    float dz = tz - pos->z;
    float dist = sqrtf(dx * dx + dz * dz);

    if (dist < 0.15f) {
        pos->x = tx;
        pos->z = tz;
        pos->y = 0;
        *moving = false;
        return true;
    }

    float step = speed * dt;
    if (step > dist) {
        step = dist;
    }

    pos->x += dx / dist * step;
    pos->z += dz / dist * step;
    pos->y = fabsf(sinf(anim->value)) * 0.05f;
    rot->y = atan2f(dx, dz);
    *moving = true;
    return false;
}

bool shipWalk(ShipDev *dev, const ShipAnim *anim, FlecsPosition3 *pos,
    FlecsRotation3 *rot, float speed, float dt)
{
    return shipWalkStep(dev->tx, dev->tz, &dev->moving, anim, pos, rot,
        speed, dt);
}

void shipSeekAmenity(ecs_world_t *world, ecs_entity_t e, ShipDev *dev,
    const FlecsPosition3 *pos, ShipGame *game, int32_t need,
    float stand_off)
{
    ecs_entity_t best = 0;
    float best_d2 = 1e30f;
    float best_x = 0, best_z = 0;

    ecs_iter_t ait = ecs_query_iter(world, game->amenity_query);
    while (ecs_query_next(&ait)) {
        const ShipAmenity *amenity = ecs_field(&ait, ShipAmenity, 0);
        const FlecsPosition3 *apos = ecs_field(&ait, FlecsPosition3, 1);

        for (int i = 0; i < ait.count; i ++) {
            if (amenity[i].need != need || amenity[i].user) {
                continue;
            }

            float dx = apos[i].x - pos->x;
            float dz = apos[i].z - pos->z;
            float d2 = dx * dx + dz * dz;
            if (d2 < best_d2) {
                best = ait.entities[i];
                best_d2 = d2;
                best_x = apos[i].x;
                best_z = apos[i].z;
            }
        }
    }

    if (!best) {
        return;
    }

    ShipAmenity *amenity = ecs_get_mut(world, best, ShipAmenity);
    amenity->user = e;

    dev->station = best;
    dev->state = SHIP_DEV_SEEK;
    shipWalkTo(dev, best_x, best_z + stand_off);
}

void ShipApplicants(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    ShipApplicant *app = ecs_field(it, ShipApplicant, 0);
    const ShipAnim *anim = ecs_field(it, ShipAnim, 1);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 2);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 3);

    float dt = it->delta_time;
    float walk_speed = ecs_const_var_get_t(world, "cfg.walkSpeed", ecs_f32_t);

    for (int i = 0; i < it->count; i ++) {
        if (shipWalkStep(app[i].tx, app[i].tz, &app[i].moving, &anim[i],
            &pos[i], &rot[i], walk_speed, dt))
        {
            rot[i].y = -(float)M_PI * 0.5f;
        }
    }
}

void ShipMood(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    ShipDev *dev = ecs_field(it, ShipDev, 0);
    ShipDevNeeds *needs = ecs_field(it, ShipDevNeeds, 1);
    ShipGame *game = ecs_field(it, ShipGame, 2);

    float dt = it->delta_time;
    float drain_caffeine = ecs_const_var_get_t(
        world, "cfg.drainCaffeine", ecs_f32_t);
    float drain_food = ecs_const_var_get_t(world, "cfg.drainFood", ecs_f32_t);
    float drain_fun = ecs_const_var_get_t(world, "cfg.drainFun", ecs_f32_t);

    for (int i = 0; i < it->count; i ++) {
        ShipDev *d = &dev[i];
        ShipDevNeeds *n = &needs[i];
        ecs_entity_t e = it->entities[i];

        float crunch_mult = game->crunch ? 2.0f : 1.0f;

        n->caffeine -= drain_caffeine * dt;
        n->food -= drain_food * crunch_mult * dt;
        n->fun -= drain_fun * crunch_mult * dt;

        if (n->caffeine < 0) n->caffeine = 0;
        if (n->food < 0) n->food = 0;
        if (n->fun < 0) n->fun = 0;

        float mood = shipDevMood(n);

        game->mood_sum += mood;
        game->mood_count ++;

        d->mood_timer -= dt;
        if (d->mood_timer <= 0) {
            d->mood_timer += 1.0f;
            Dev *look = ecs_get_mut(world, e, Dev);
            if (look && (look->mood != mood ||
                look->energy != n->caffeine))
            {
                look->mood = mood;
                look->energy = n->caffeine;
                ecs_modified(world, e, Dev);
            }
        }
    }
}

void ShipDevs(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    ShipDev *dev = ecs_field(it, ShipDev, 0);
    ShipDevNeeds *needs = ecs_field(it, ShipDevNeeds, 1);
    const ShipAnim *anim = ecs_field(it, ShipAnim, 2);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 3);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 4);
    ShipGame *game = ecs_field(it, ShipGame, 5);

    float dt = it->delta_time;
    float walk_speed = ecs_const_var_get_t(world, "cfg.walkSpeed", ecs_f32_t);
    float stand_off = ecs_const_var_get_t(world, "cfg.standOff", ecs_f32_t);
    float need_seek = ecs_const_var_get_t(world, "cfg.needSeek", ecs_f32_t);
    float caffeine_seek = ecs_const_var_get_t(
        world, "cfg.caffeineSeek", ecs_f32_t);
    float progress_rate = ecs_const_var_get_t(
        world, "cfg.progressRate", ecs_f32_t);
    float spawn_x = ecs_const_var_get_t(world, "cfg.spawnX", ecs_f32_t);
    float door_z = ecs_const_var_get_t(world, "cfg.doorZ", ecs_f32_t);
    float salary = ecs_const_var_get_t(world, "cfg.salary", ecs_f32_t);
    float pay_interval = ecs_const_var_get_t(
        world, "cfg.payInterval", ecs_f32_t);

    bool changed = false;

    for (int i = 0; i < it->count; i ++) {
        ShipDev *d = &dev[i];
        ecs_entity_t e = it->entities[i];

        if (d->desk) {
            d->pay_timer -= dt;
            if (d->pay_timer <= 0) {
                d->pay_timer += pay_interval;
                game->money -= (int32_t)(salary * pay_interval);
                changed = true;

                shipSpawnDollar(world, game,
                    pos[i].x, pos[i].y + 2.4f, pos[i].z);
            }
        }

        switch (d->state) {
        case SHIP_DEV_ARRIVE:
            if (shipWalk(d, &anim[i], &pos[i], &rot[i], walk_speed, dt)) {
                if (!ecs_is_alive(world, d->desk)) {
                    shipSendHome(world, e, d);
                    break;
                }

                float sx, sz;
                shipDeskSpot(world, d->desk, &sx, &sz);

                if (fabsf(d->tx - sx) > 0.01f || fabsf(d->tz - sz) > 0.01f) {
                    shipWalkTo(d, sx, sz);
                } else {
                    d->state = SHIP_DEV_WORK;
                    rot[i].y = 0;
                }
            }
            break;

        case SHIP_DEV_WORK: {
            ShipDevNeeds *n = &needs[i];

            float rate = progress_rate;
            if (shipDevMood(n) > 0.8f) {
                rate *= 1.5f;
            }
            if (game->crunch) {
                rate *= 2.0f;
            }
            game->feature_progress += rate * dt;
            game->work_rate += rate;

            int32_t need = SHIP_NEED_CAFFEINE;
            float lowest = n->caffeine;
            if (n->food < lowest) { lowest = n->food; need = SHIP_NEED_FOOD; }
            if (n->fun < lowest) { lowest = n->fun; need = SHIP_NEED_FUN; }

            if (lowest < need_seek) {
                shipSeekAmenity(world, e, d, &pos[i], game, need, stand_off);
            } else if (n->caffeine < caffeine_seek) {
                shipSeekAmenity(world, e, d, &pos[i], game,
                    SHIP_NEED_CAFFEINE, stand_off);
            }
            break;
        }

        case SHIP_DEV_SEEK:
            if (!ecs_is_alive(world, d->station)) {
                d->station = 0;
                d->state = SHIP_DEV_ARRIVE;
                break;
            }
            if (shipWalk(d, &anim[i], &pos[i], &rot[i], walk_speed, dt)) {
                const ShipAmenity *amenity = ecs_get(
                    world, d->station, ShipAmenity);
                d->timer = amenity->use_time;
                d->state = SHIP_DEV_USE;
                rot[i].y = (float)M_PI;
            }
            break;

        case SHIP_DEV_USE: {
            if (!ecs_is_alive(world, d->station)) {
                d->station = 0;
                d->state = SHIP_DEV_ARRIVE;
                break;
            }

            pos[i].y = fabsf(sinf(anim[i].value * 0.6f)) * 0.03f;

            d->timer -= dt;
            if (d->timer > 0) {
                break;
            }

            ShipAmenity *amenity = ecs_get_mut(
                world, d->station, ShipAmenity);

            if (amenity->need == SHIP_NEED_CAFFEINE) {
                needs[i].caffeine = 1;
            } else if (amenity->need == SHIP_NEED_FOOD) {
                needs[i].food = 1;
            } else {
                needs[i].fun = 1;
            }

            amenity->user = 0;
            d->station = 0;
            d->state = SHIP_DEV_ARRIVE;

            if (ecs_is_alive(world, d->desk)) {
                float sx, sz;
                shipDeskSpot(world, d->desk, &sx, &sz);
                shipWalkTo(d, sx, sz);
            } else {
                shipSendHome(world, e, d);
            }
            break;
        }

        case SHIP_DEV_QUIT:
            if (shipWalk(d, &anim[i], &pos[i], &rot[i], walk_speed, dt)) {
                if (fabsf(d->tx - spawn_x) > 0.01f) {
                    shipWalkTo(d, spawn_x, door_z);
                } else {
                    ecs_delete(world, e);
                    game->devs --;
                    changed = true;
                }
            }
            break;
        }
    }

    if (changed) {
        ecs_singleton_modified(world, ShipGame);
    }
}

void ShipLimbs(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    const ShipLimb *limb = ecs_field(it, ShipLimb, 0);
    const ShipAnim *anim = ecs_field(it, ShipAnim, 1);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 2);

    float alpha = it->delta_time * 12.0f;
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    float alpha_fast = it->delta_time * 40.0f;
    if (alpha_fast > 1.0f) {
        alpha_fast = 1.0f;
    }

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t parent = ecs_get_parent(world, it->entities[i]);
        const ShipAnim *panim = ecs_get(world, parent, ShipAnim);
        if (!panim) {
            continue;
        }

        bool walking;
        int32_t state;
        const ShipDev *dev = ecs_get(world, parent, ShipDev);
        if (dev) {
            state = dev->state;
            walking = dev->moving &&
                (state == SHIP_DEV_ARRIVE ||
                state == SHIP_DEV_SEEK ||
                state == SHIP_DEV_QUIT);
        } else {
            const ShipApplicant *app = ecs_get(world, parent, ShipApplicant);
            if (!app) {
                continue;
            }
            state = -1;
            walking = app->moving;
        }

        float target;
        float a = alpha;
        if (walking) {
            target = sinf(panim->value + anim[i].value) * limb[i].swing;
        } else if (state == SHIP_DEV_WORK && limb[i].arm > 0) {
            target = -1.05f + sinf(panim->value * 3.5f + anim[i].value) * 0.08f;
            a = alpha_fast;
        } else if (state == SHIP_DEV_USE && limb[i].arm > 0) {
            target = -0.65f + sinf(panim->value * 1.5f + anim[i].value) * 0.25f;
            a = alpha_fast;
        } else {
            target = 0;
        }

        rot[i].x += (target - rot[i].x) * a;
    }
}

void ShipFloaters(ecs_iter_t *it) {
    ShipFloater *floater = ecs_field(it, ShipFloater, 0);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 1);

    for (int i = 0; i < it->count; i ++) {
        floater[i].age += it->delta_time;
        pos[i].y += floater[i].rise * it->delta_time;

        if (floater[i].age >= floater[i].life) {
            ecs_delete(it->world, it->entities[i]);
        }
    }
}

void ShipDollarParts(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t parent = ecs_get_parent(world, it->entities[i]);
        const ShipFloater *floater = ecs_get(world, parent, ShipFloater);
        if (!floater) {
            continue;
        }

        float t = floater->age / floater->life;
        if (t < 0) t = 0;
        if (t > 1) t = 1;

        uint8_t a = (uint8_t)(255.0f * (1.0f - t));
        ecs_set(world, it->entities[i], FlecsRgba, {235, 60, 50, a});
        ecs_set(world, it->entities[i], FlecsEmissive, {
            .strength = 2.0f * (1.0f - t),
            .color = {235, 60, 50, 255}
        });
    }
}

void ShipStudio(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    ShipGame *game = ecs_field(it, ShipGame, 0);

    if (!game->game_name || !game->game_name[0]) {
        shipRollTitle(game);
    }
    if (!game->feature_name || !game->feature_name[0]) {
        shipRollFeature(game);
    }

    game->avg_mood = game->mood_count
        ? game->mood_sum / (float)game->mood_count
        : 1.0f;
    if (game->avg_mood < 0.75f) {
        game->morale_bonus = false;
    }

    float feature_work = ecs_const_var_get_t(
        world, "cfg.featureWork", ecs_f32_t);
    int32_t feature_revenue = ecs_const_var_get_t(
        world, "cfg.featureRevenue", ecs_i32_t);

    while (game->feature_progress >= feature_work &&
        game->features_done < game->features_total)
    {
        game->feature_progress -= feature_work;
        game->features_done ++;

        shipBurst(world, game->glow_pool, game->ship_burst, 0, 2.5f, 0);

        if (game->features_done >= game->features_total) {
            game->money += game->features_total * feature_revenue *
                (game->morale_bonus ? 3 : 1);
            shipSetState(world, ShipGameStateShipped);
        } else {
            shipRollFeature(game);
        }
    }

    game->deadline_left -= it->delta_time;
    if (game->deadline_left <= 0) {
        game->deadline_left = 0;
        if (game->features_done < game->features_total) {
            shipSetState(world, ShipGameStateMissed);
        }
    }

    float projected = (float)game->features_done +
        (game->feature_progress + game->work_rate * game->deadline_left) /
            feature_work;
    if (projected >= (float)game->features_total) {
        game->behind_frac = -1;
    } else {
        game->behind_frac = projected / (float)game->features_total;
    }

    int32_t secs = (int32_t)ceilf(game->deadline_left);
    if (secs != game->deadline_secs) {
        game->deadline_secs = secs;

        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", secs / 60, secs % 60);
        ecs_os_free(game->clock);
        game->clock = ecs_os_strdup(buf);
    }

    ecs_singleton_modified(world, ShipGame);
}

static void shipRestart(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    if (shipPlaying(world)) {
        shipResolve(future, false);
        return;
    }

    ShipGame *game = ecs_singleton_ensure(world, ShipGame);

    ecs_delete_with(world, ecs_pair(EcsChildOf, game->furniture));
    ecs_delete_with(world, ecs_pair(EcsChildOf, game->staff));
    ecs_delete_with(world, ecs_pair(EcsChildOf, game->dollars));

    memset(game->grid, 0, sizeof(game->grid));

    if (!ecs_script(world, { .filename = "etc/scene.flecs" })) {
        ecs_err("failed to reload etc/scene.flecs");
    }
    shipResolve(future, true);
}

void Ship_happensImport(ecs_world_t *world) {
    ECS_MODULE(world, Ship_happens);

    ECS_META_COMPONENT(world, ShipGameState);
    ECS_META_COMPONENT(world, ShipOffice);
    ECS_META_COMPONENT(world, ShipGame);
    ECS_META_COMPONENT(world, ShipCost);
    ECS_META_COMPONENT(world, ShipPacket);

    ecs_add_pair(world, ecs_id(ShipPacket), EcsWith,
        ecs_id(FlecsUiWidgetState));
    ECS_META_COMPONENT(world, ShipFurniture);
    ECS_META_COMPONENT(world, ShipDesk);
    ECS_META_COMPONENT(world, ShipAmenity);
    ECS_META_COMPONENT(world, ShipAura);
    ECS_META_COMPONENT(world, ShipApplicant);
    ECS_META_COMPONENT(world, ShipDev);
    ECS_META_COMPONENT(world, ShipDevNeeds);
    ECS_META_COMPONENT(world, Dev);
    ECS_META_COMPONENT(world, ShipLimb);
    ECS_META_COMPONENT(world, ShipAnim);
    ECS_META_COMPONENT(world, ShipAnimSpeed);
    ECS_META_COMPONENT(world, ShipFloater);

    ECS_TAG_DEFINE(world, ShipDollarPart);

    ecs_add_pair(world, ecs_id(ShipCost), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(ShipAnimSpeed), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(ShipLimb), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(ShipLimb), EcsWith, ecs_id(FlecsRotation3));

    ecs_add_id(world, ecs_id(ShipGameState), EcsExclusive);
    ecs_add_id(world, ecs_id(ShipGameState), EcsSingleton);
    ecs_add_id(world, ecs_id(ShipGame), EcsSingleton);

    ecs_async_function(world, {
        .name = "placeAtCursor",
        .parent = ecs_id(Ship_happens),
        .return_type = ecs_id(ecs_bool_t),
        .callback = shipPlace
    });

    ecs_async_function(world, {
        .name = "bulldozeAtCursor",
        .parent = ecs_id(Ship_happens),
        .return_type = ecs_id(ecs_bool_t),
        .callback = shipBulldozeAction
    });

    ecs_async_function(world, {
        .name = "clearSelection",
        .parent = ecs_id(Ship_happens),
        .return_type = ecs_id(ecs_bool_t),
        .callback = shipCancel
    });

    ecs_async_function(world, {
        .name = "restart",
        .parent = ecs_id(Ship_happens),
        .return_type = ecs_id(ecs_bool_t),
        .callback = shipRestart
    });

    ecs_add_pair(world, ecs_id(ShipDev), EcsWith, FlecsDynamicTransform);
    ecs_add_pair(world, ecs_id(ShipAnim), EcsWith, FlecsDynamicTransform);
    ecs_add_pair(world, ecs_id(ShipFloater), EcsWith, FlecsDynamicTransform);

    ShipGame *game = ecs_singleton_ensure(world, ShipGame);

    game->desk_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "ShipDeskQuery" }),
        .terms = {
            { .id = ecs_id(ShipDesk) },
            { .id = ecs_id(FlecsPosition3) }
        }
    });

    game->amenity_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "ShipAmenityQuery" }),
        .terms = {
            { .id = ecs_id(ShipAmenity) },
            { .id = ecs_id(FlecsPosition3) }
        }
    });

    game->applicant_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "ShipApplicantQuery" }),
        .terms = {
            { .id = ecs_id(ShipApplicant) }
        }
    });

    ecs_singleton_modified(world, ShipGame);

    ecs_entity_t playing = ecs_constant_to_entity(
        world, ShipGameState, ShipGameStatePlaying);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "ShipFurnitureOnRemove" }),
        .query.terms = {
            { .id = ecs_id(ShipFurniture) },
            { .id = ecs_id(ShipGame) }
        },
        .events = { EcsOnRemove },
        .callback = ShipFurnitureOnRemove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShipUiBind" }),
        .phase = EcsOnUpdate,
        .callback = ShipUiBind
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "ShipUiClick" }),
        .query.terms = {
            { .id = ecs_id(ShipPacket) }
        },
        .events = { FlecsUiClicked },
        .callback = ShipUiClick
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShipResetGrid" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(ShipGame) }
        },
        .callback = ShipResetGrid
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShipHover" }),
        .phase = EcsOnLoad,
        .query.terms = {
            { .id = ecs_id(ShipGame) },
            { .id = ecs_id(FlecsInput) },
            { .id = ecs_pair_t(ShipGameState, playing) }
        },
        .callback = ShipHover
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShipHoverGhost" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(ShipGame) },
            { .id = ecs_pair_t(ShipGameState, playing) }
        },
        .callback = ShipHoverGhost
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShipAnimIncrement" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(ShipAnim) },
            { .id = ecs_id(ShipAnimSpeed) }
        },
        .callback = ShipAnimIncrement
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShipHiring" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(ShipGame) },
            { .id = ecs_pair_t(ShipGameState, playing) }
        },
        .callback = ShipHiring
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShipApplicants" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(ShipApplicant) },
            { .id = ecs_id(ShipAnim) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(FlecsRotation3) },
            { .id = ecs_pair_t(ShipGameState, playing) }
        },
        .callback = ShipApplicants
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShipMood" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(ShipDev) },
            { .id = ecs_id(ShipDevNeeds) },
            { .id = ecs_id(ShipGame) },
            { .id = ecs_pair_t(ShipGameState, playing) }
        },
        .callback = ShipMood
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShipDevs" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(ShipDev) },
            { .id = ecs_id(ShipDevNeeds) },
            { .id = ecs_id(ShipAnim) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(FlecsRotation3) },
            { .id = ecs_id(ShipGame) },
            { .id = ecs_pair_t(ShipGameState, playing) }
        },
        .callback = ShipDevs
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShipLimbs" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(ShipLimb) },
            { .id = ecs_id(ShipAnim) },
            { .id = ecs_id(FlecsRotation3) }
        },
        .callback = ShipLimbs
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShipFloaters" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(ShipFloater) },
            { .id = ecs_id(FlecsPosition3) }
        },
        .callback = ShipFloaters
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShipDollarParts" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ShipDollarPart }
        },
        .callback = ShipDollarParts
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShipStudio" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(ShipGame) },
            { .id = ecs_pair_t(ShipGameState, playing) }
        },
        .callback = ShipStudio
    });

}
