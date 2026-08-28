#define ORE_ELSE_UI_IMPL
#include "ore_else.h"

#include <stdio.h>

void oreToast(ecs_world_t *world, const char *text, bool warn) {
    OreUiState *ui = ecs_singleton_ensure(world, OreUiState);

    oreTextSet(&ui->toast_text, text);
    ui->toast_warn = warn;
    ui->toast = ecs_const_var_get_t(world,
        warn ? "cfg.warnToast" : "cfg.shipToastTime", ecs_f32_t);

    ecs_singleton_modified(world, OreUiState);
}

static void OreToastFade(ecs_iter_t *it) {
    OreUiState *ui = ecs_field(it, OreUiState, 0);

    if (ui->toast <= 0) {
        return;
    }

    ui->toast -= it->delta_time;

    if (ui->toast < 0) {
        ui->toast = 0;
    }

    ecs_singleton_modified(it->world, OreUiState);
}

void oreAffordSet(ecs_world_t *world, ecs_entity_t widget, bool value) {
    OreAfford *afford = ecs_get_mut(world, widget, OreAfford);
    if (!afford || afford->value == value) {
        return;
    }

    afford->value = value;
    ecs_modified(world, widget, OreAfford);
}

static int32_t oreSlotBatch(ecs_world_t *world) {
    return oreRunHeld(world, ecs_singleton_get(world, OreGame))
        ? ORE_CRAFT_BATCH : 1;
}

void oreSlotClick(ecs_world_t *world, ecs_entity_t widget) {
    if (!orePlaying(world)) {
        return;
    }

    const OreSlot *slot = ecs_get(world, widget, OreSlot);
    if (!slot) {
        return;
    }

    if (slot->cancel) {
        if (slot->order) {
            oreCraftCancelOrder(world, slot->order);
        }
        return;
    }

    if (slot->craft) {
        int32_t batch = oreSlotBatch(world);
        for (int i = 0; slot->item && i < batch; i ++) {
            if (!oreCraftEnqueue(world, slot->item, i > 0)) {
                break;
            }
        }
        return;
    }

    if (!slot->place || !slot->item ||
        oreInventoryGet(world, slot->item) <= 0)
    {
        return;
    }

    OreGame *game = ecs_singleton_ensure(world, OreGame);

    game->selected = (game->selected == slot->item) ? 0 : slot->item;

    ecs_singleton_modified(world, OreGame);

    flecsEngine_inputEmit(world, ecs_lookup(world, "ore_else.actions.Pick"),
        FlecsInputPressed);
}

void oreTabClick(ecs_world_t *world, ecs_entity_t widget) {
    if (!orePlaying(world)) {
        return;
    }

    const OreTab *tab = ecs_get(world, widget, OreTab);
    if (!tab) {
        return;
    }

    OreUiState *ui = ecs_singleton_ensure(world, OreUiState);

    int32_t next = tab->index;
    if (next < 0) {
        if (!ui->open) {
            return;
        }
        next = (ui->tab + 1) % ORE_UI_TABS;
    }

    if (next < 0 || next >= ORE_UI_TABS || next == ui->tab) {
        return;
    }

    ui->tab = next;

    ecs_singleton_modified(world, OreUiState);
}

static void oreUiHoverInfo(
    ecs_world_t *world,
    OreUiState *ui,
    const OreCraftState *craft,
    const OreSlot *slot)
{
    char name[64];
    char note[96];
    char detail[160];

    ui->hovering = true;

    oreItemName(world, slot->item, name, sizeof(name));

    char power[32];
    power[0] = 0;

    const OrePowerConsumer *draw = ecs_get(world, slot->item, OrePowerConsumer);
    const OrePowerProducer *gen = ecs_get(world, slot->item, OrePowerProducer);

    if (draw && draw->watts > 0) {
        snprintf(power, sizeof(power), " - %d W", (int)draw->watts);
    } else if (gen && gen->watts > 0) {
        snprintf(power, sizeof(power), " - +%d W", (int)gen->watts);
    }

    if (slot->place) {
        snprintf(note, sizeof(note), "Ready to place%s", power);

        ui->hover_item = slot->item;
        ui->hover_recipe = 0;
        ui->hover_raw_count = 0;
    } else {
        const OreRecipe *recipe = ecs_get(world, slot->item, OreRecipe);

        if (recipe && recipe->output_amount > 1) {
            snprintf(note, sizeof(note), "Craft %.1fs - yields %d%s",
                (double)recipe->craft_time, recipe->output_amount, power);
        } else if (recipe) {
            snprintf(note, sizeof(note), "Craft %.1fs%s",
                (double)recipe->craft_time, power);
        } else {
            ecs_os_strcpy(note, "Mined from deposits");
        }

        ui->hover_item = slot->item;
        ui->hover_recipe = (recipe && ecs_map_count(&recipe->inputs))
            ? slot->item
            : 0;

        ui->hover_raw_count = ui->hover_recipe
            ? oreCraftRawCost(world, craft, slot->item, &ui->hover_raw,
                &ui->hover_have)
            : 0;
    }

    oreDocText(ecs_doc_get_detail(world, slot->item), detail, sizeof(detail));

    oreTextSet(&ui->hover_name, name);
    oreTextSet(&ui->hover_note, note);
    oreTextSet(&ui->hover_detail, detail);
}

static int32_t oreStockList(
    ecs_world_t *world,
    ecs_entity_t *items,
    int32_t *counts)
{
    ecs_value_t value = orePlayerAttrGet(world, "Inventory");
    if (!value.ptr) {
        return 0;
    }

    int32_t n = 0;

    ecs_map_iter_t it = ecs_map_iter(value.ptr);
    while (ecs_map_next(&it)) {
        int32_t amount = (int32_t)ecs_map_value(&it);
        if (amount <= 0) {
            continue;
        }

        ecs_entity_t item = ecs_map_key(&it);

        int32_t at = n;
        while (at > 0 && items[at - 1] > item) {
            items[at] = items[at - 1];
            counts[at] = counts[at - 1];
            at --;
        }

        items[at] = item;
        counts[at] = amount;

        n ++;
        if (n == ORE_STOCK_SLOTS) {
            break;
        }
    }

    return n;
}

static bool oreStockAssign(
    ecs_world_t *world,
    const OreGame *game,
    OreUiState *ui)
{
    OreStockSlot prev[ORE_STOCK_SLOTS];
    ecs_os_memcpy(prev, ui->slots, sizeof(prev));

    OreStockSlot *slots = ui->slots;

    for (int i = 0; i < ORE_STOCK_SLOTS; i ++) {
        if (slots[i].item && oreInventoryGet(world, slots[i].item) <= 0) {
            slots[i].item = 0;
        }
    }

    ecs_entity_t items[ORE_STOCK_SLOTS];
    int32_t counts[ORE_STOCK_SLOTS];
    int32_t n = oreStockList(world, items, counts);

    for (int i = 0; i < n; i ++) {
        bool held = false;

        for (int j = 0; j < ORE_STOCK_SLOTS && !held; j ++) {
            held = slots[j].item == items[i];
        }

        if (held) {
            continue;
        }

        for (int j = 0; j < ORE_STOCK_SLOTS; j ++) {
            if (!slots[j].item) {
                slots[j].item = items[i];
                break;
            }
        }
    }

    for (int i = 0; i < ORE_STOCK_SLOTS; i ++) {
        ecs_entity_t item = slots[i].item;

        if (!item) {
            slots[i] = (OreStockSlot){0};
            continue;
        }

        const OreIcon *icon = ecs_get(world, item, OreIcon);

        slots[i].icon = icon ? icon->texture : 0;
        slots[i].count = oreInventoryGet(world, item);
        slots[i].place = ecs_has_id(world, item, EcsPrefab);
        slots[i].active = game->selected == item;
    }

    for (int i = 0; i < ORE_STOCK_SLOTS; i ++) {
        if (prev[i].item != slots[i].item ||
            prev[i].icon != slots[i].icon ||
            prev[i].count != slots[i].count ||
            prev[i].place != slots[i].place ||
            prev[i].active != slots[i].active)
        {
            return true;
        }
    }

    return false;
}

static void OreUiBind(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    const OreGame *game = ecs_field(it, OreGame, 0);
    const OreCraftState *craft = ecs_field(it, OreCraftState, 1);
    OreUiState *ui = ecs_field(it, OreUiState, 2);

    bool open = ui->open && orePlaying(world);

    bool changed = oreStockAssign(world, game, ui);

    bool was_hovering = ui->hovering;

    ui->hovering = false;

    if (open) {
        ecs_iter_t lit = ecs_each_id(world, ecs_id(OreSlot));
        while (ecs_each_next(&lit)) {
            for (int i = 0; i < lit.count; i ++) {
                ecs_entity_t e = lit.entities[i];

                const OreSlot *slot = ecs_get(world, e, OreSlot);
                if (!slot || !slot->item || slot->cancel) {
                    continue;
                }

                const FlecsUiWidgetState *state = ecs_get(
                    world, e, FlecsUiWidgetState);
                if (state && state->hover) {
                    oreUiHoverInfo(world, ui, craft, slot);
                }

                oreAffordSet(world, e,
                    oreCraftReachable(world, craft, slot->item, slot->craft));
            }
        }
    }

    if (ui->hovering || was_hovering) {
        changed = true;
    }

    if (changed) {
        ecs_singleton_modified(world, OreUiState);
    }
}

static void oreInventorySet(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    OreUiState *ui = ecs_singleton_ensure(world, OreUiState);

    bool open = argc == 1 && argv[0].ptr && *(bool*)argv[0].ptr;

    if (ui->open == open || (open && !orePlaying(world))) {
        oreResolve(future, false);
        return;
    }

    if (open) {
        oreRejectDrag(world);
    }

    ui->open = open;

    ecs_singleton_modified(world, OreUiState);
    oreResolve(future, true);
}

void oreUiImport(ecs_world_t *world) {
    ECS_META_COMPONENT(world, OreStockSlot);
    ECS_META_COMPONENT(world, OreSlot);
    ECS_META_COMPONENT(world, OreTab);
    ECS_META_COMPONENT(world, OreAfford);
    ECS_META_COMPONENT(world, OreUiState);

    ecs_add_id(world, ecs_id(OreUiState), EcsSingleton);

    ecs_add_pair(world, ecs_id(OreSlot), EcsWith, ecs_id(FlecsUiWidgetState));
    ecs_add_pair(world, ecs_id(OreTab), EcsWith, ecs_id(FlecsUiWidgetState));

    ecs_entity_t playing = ecs_constant_to_entity(
        world, OreGameState, OreGameStatePlaying);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreUiBind" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreGame), .inout = EcsIn },
            { .id = ecs_id(OreCraftState), .inout = EcsIn },
            { .id = ecs_id(OreUiState) }
        },
        .callback = OreUiBind
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreToastFade" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreUiState) },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreToastFade
    });

    ecs_async_function(world, {
        .name = "inventorySet",
        .parent = ecs_get_scope(world),
        .return_type = ecs_id(ecs_bool_t),
        .params = {{ "open", ecs_id(ecs_bool_t) }},
        .callback = oreInventorySet
    });
}
