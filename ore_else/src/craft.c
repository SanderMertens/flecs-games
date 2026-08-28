#define ORE_ELSE_CRAFT_IMPL
#include "ore_else.h"

#include <stdio.h>

typedef struct ore_plan_t {
    ecs_entity_t items[ORE_CRAFT_ITEMS];
    int32_t crafts[ORE_CRAFT_ITEMS];
    int32_t count;
    OreCraftBag avail;
    OreCraftBag draw;
    OreCraftBag incoming;
    OreCraftBag lack;
    bool borrow;
    bool tolerant;
} ore_plan_t;

static int32_t oreRecipeYield(const OreRecipe *recipe) {
    return recipe->output_amount > 0 ? recipe->output_amount : 1;
}

static ecs_entity_t oreRecipeItem(const OreRecipe *recipe, ecs_entity_t item) {
    return recipe->output ? recipe->output : item;
}

static int32_t oreBagGet(const OreCraftBag *bag, ecs_entity_t item) {
    for (int i = 0; i < bag->count; i ++) {
        if (bag->items[i] == item) {
            return bag->amounts[i];
        }
    }

    return 0;
}

static bool oreBagAdd(OreCraftBag *bag, ecs_entity_t item, int32_t amount) {
    if (!amount) {
        return true;
    }

    for (int i = 0; i < bag->count; i ++) {
        if (bag->items[i] != item) {
            continue;
        }

        bag->amounts[i] += amount;

        if (bag->amounts[i] <= 0) {
            bag->count --;
            bag->items[i] = bag->items[bag->count];
            bag->amounts[i] = bag->amounts[bag->count];
        }

        return true;
    }

    if (amount < 0) {
        return true;
    }

    if (bag->count >= ORE_CRAFT_BAG) {
        return false;
    }

    bag->items[bag->count] = item;
    bag->amounts[bag->count] = amount;
    bag->count ++;
    return true;
}

static int32_t oreBagDraw(OreCraftBag *bag, ecs_entity_t item, int32_t need) {
    int32_t have = oreBagGet(bag, item);
    int32_t take = have < need ? have : need;

    oreBagAdd(bag, item, -take);

    return need - take;
}

static bool oreBagAffordable(
    const OreCraftBag *bag,
    const OreRecipe *recipe)
{
    ecs_map_iter_t it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        int32_t amount = (int32_t)ecs_map_value(&it);
        if (amount > 0 && oreBagGet(bag, ecs_map_key(&it)) < amount) {
            return false;
        }
    }

    return true;
}

static void oreBagRecipe(
    OreCraftBag *bag,
    const OreRecipe *recipe,
    int32_t times)
{
    ecs_map_iter_t it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        int32_t amount = (int32_t)ecs_map_value(&it);
        if (amount > 0) {
            oreBagAdd(bag, ecs_map_key(&it), amount * times);
        }
    }
}

static void oreCraftRowsClear(OreCraftState *craft);

static OreCraftOrder* oreOrderById(OreCraftState *craft, int32_t id) {
    if (!id) {
        return NULL;
    }

    for (int i = 0; i < craft->count; i ++) {
        if (craft->orders[i].id == id) {
            return &craft->orders[i];
        }
    }

    return NULL;
}

static int32_t oreOrderSlot(const OreCraftState *craft, int32_t id) {
    for (int i = 0; i < craft->count; i ++) {
        if (craft->orders[i].id == id) {
            return i;
        }
    }

    return -1;
}

static const OreCraftEntry* oreOrderFind(
    const OreCraftOrder *order,
    ecs_entity_t item)
{
    for (int i = 0; i < order->entry_count; i ++) {
        if (order->entries[i].item == item) {
            return &order->entries[i];
        }
    }

    return NULL;
}

static OreCraftEntry* oreOrderEntry(OreCraftOrder *order, ecs_entity_t item) {
    return (OreCraftEntry*)oreOrderFind(order, item);
}

static void oreOrderRunning(const OreCraftOrder *order, OreCraftBag *bag) {
    ecs_os_memset_t(bag, 0, OreCraftBag);

    for (int i = 0; i < order->entry_count; i ++) {
        if (order->entries[i].running > 0) {
            oreBagAdd(bag, order->entries[i].item, order->entries[i].running);
        }
    }
}

static int32_t oreOrderRows(const OreCraftOrder *order) {
    return order->entry_count > 0 ? order->entry_count : 1;
}

static int32_t oreCraftRows(const OreCraftState *craft) {
    int32_t rows = 0;

    for (int i = 0; i < craft->count; i ++) {
        rows += oreOrderRows(&craft->orders[i]);
    }

    return rows;
}

static bool orePlanPush(ore_plan_t *plan, ecs_entity_t item, int32_t count) {
    for (int i = 0; i < plan->count; i ++) {
        if (plan->items[i] == item) {
            plan->crafts[i] += count;
            return true;
        }
    }

    if (plan->count >= ORE_CRAFT_ITEMS) {
        return false;
    }

    plan->items[plan->count] = item;
    plan->crafts[plan->count] = count;
    plan->count ++;
    return true;
}

static bool orePlanProvide(
    ecs_world_t *world,
    ore_plan_t *plan,
    ecs_entity_t item,
    int32_t need,
    int32_t depth);

static bool orePlanInputs(
    ecs_world_t *world,
    ore_plan_t *plan,
    const OreRecipe *recipe,
    int32_t times,
    int32_t depth)
{
    ecs_map_iter_t it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        int32_t amount = (int32_t)ecs_map_value(&it);
        if (amount <= 0) {
            continue;
        }

        if (!orePlanProvide(world, plan, ecs_map_key(&it),
            amount * times, depth))
        {
            return false;
        }
    }

    return true;
}

static bool orePlanCraft(
    ecs_world_t *world,
    ore_plan_t *plan,
    ecs_entity_t item,
    const OreRecipe *recipe,
    int32_t need,
    int32_t depth)
{
    int32_t yield = oreRecipeYield(recipe);
    int32_t crafts = (need + yield - 1) / yield;

    if (!orePlanInputs(world, plan, recipe, crafts, depth)) {
        return false;
    }

    if (!oreBagAdd(&plan->avail, item, crafts * yield - need)) {
        return false;
    }

    return orePlanPush(plan, item, crafts);
}

static bool orePlanProvide(
    ecs_world_t *world,
    ore_plan_t *plan,
    ecs_entity_t item,
    int32_t need,
    int32_t depth)
{
    need = oreBagDraw(&plan->avail, item, need);

    if (need > 0 && plan->borrow) {
        int32_t stock = oreInventoryGet(world, item) -
            oreBagGet(&plan->draw, item);

        if (stock > 0) {
            int32_t take = stock < need ? stock : need;
            if (!oreBagAdd(&plan->draw, item, take)) {
                return false;
            }

            need -= take;
        }
    }

    if (need <= 0) {
        return true;
    }

    const OreRecipe *recipe = ecs_get(world, item, OreRecipe);
    bool craftable = depth > 0 && recipe && ecs_map_count(&recipe->inputs) &&
        oreRecipeItem(recipe, item) == item;

    if (!craftable) {
        if (plan->tolerant) {
            return oreBagAdd(&plan->lack, item, need);
        }

        return false;
    }

    return orePlanCraft(world, plan, item, recipe, need, depth - 1);
}

static bool oreOrderPlan(
    ecs_world_t *world,
    ecs_entity_t head,
    const OreCraftBag *pool,
    const OreCraftBag *running,
    int32_t demand,
    bool borrow,
    bool tolerant,
    ore_plan_t *plan)
{
    ecs_os_memset_t(plan, 0, ore_plan_t);
    plan->avail = *pool;
    plan->borrow = borrow;
    plan->tolerant = tolerant;

    for (int i = 0; i < running->count; i ++) {
        int32_t units = running->amounts[i];
        if (units <= 0) {
            continue;
        }

        const OreRecipe *recipe = ecs_get(world, running->items[i], OreRecipe);
        if (!recipe) {
            return false;
        }

        ecs_entity_t out = oreRecipeItem(recipe, running->items[i]);
        int32_t made = units * oreRecipeYield(recipe);

        if (!oreBagAdd(&plan->incoming, out, made)) {
            return false;
        }

        if (!oreBagAdd(&plan->avail, out, made)) {
            return false;
        }
    }

    if (demand <= 0) {
        return true;
    }

    const OreRecipe *recipe = ecs_get(world, head, OreRecipe);
    if (!recipe || !ecs_map_count(&recipe->inputs)) {
        return false;
    }

    if (oreRecipeItem(recipe, head) != head) {
        return false;
    }

    int32_t need = oreBagDraw(&plan->avail, head, demand);
    if (need <= 0) {
        return true;
    }

    return orePlanCraft(world, plan, head, recipe, need, ORE_CRAFT_DEPTH);
}

static int32_t orePlanEntries(
    const ore_plan_t *plan,
    const OreCraftBag *running)
{
    int32_t n = plan->count;

    for (int i = 0; i < running->count; i ++) {
        if (running->amounts[i] <= 0) {
            continue;
        }

        bool found = false;
        for (int j = 0; j < plan->count; j ++) {
            found = found || plan->items[j] == running->items[i];
        }

        if (!found) {
            n ++;
        }
    }

    return n;
}

static bool orePlanHolds(
    const ore_plan_t *plan,
    const OreCraftBag *pool)
{
    OreCraftBag kinds;
    ecs_os_memset_t(&kinds, 0, OreCraftBag);

    for (int i = 0; i < pool->count; i ++) {
        if (pool->amounts[i] > 0 && !oreBagAdd(&kinds, pool->items[i], 1)) {
            return false;
        }
    }

    for (int i = 0; i < plan->draw.count; i ++) {
        if (plan->draw.amounts[i] > 0 &&
            !oreBagAdd(&kinds, plan->draw.items[i], 1))
        {
            return false;
        }
    }

    for (int i = 0; i < plan->count; i ++) {
        if (!oreBagAdd(&kinds, plan->items[i], 1)) {
            return false;
        }
    }

    return kinds.count <= ORE_CRAFT_HOLD;
}

static void oreCraftText(ecs_world_t *world, OreCraftEntry *entry) {
    char name[64];
    char buf[96];

    snprintf(buf, sizeof(buf), "%s (%dx)",
        oreItemName(world, entry->item, name, sizeof(name)), entry->count);

    ecs_os_free(entry->text);
    entry->text = ecs_os_strdup(buf);
}

static void oreOrderApply(
    ecs_world_t *world,
    OreCraftOrder *order,
    const ore_plan_t *plan,
    const OreCraftBag *running)
{
    OreCraftEntry next[ORE_CRAFT_ITEMS];
    ecs_os_memset(next, 0, sizeof(next));

    int32_t n = 0;

    for (int i = 0; i < plan->count && n < ORE_CRAFT_ITEMS; i ++) {
        next[n].item = plan->items[i];
        next[n].running = oreBagGet(running, plan->items[i]);
        next[n].count = plan->crafts[i] + next[n].running;
        n ++;
    }

    for (int i = 0; i < running->count && n < ORE_CRAFT_ITEMS; i ++) {
        if (running->amounts[i] <= 0) {
            continue;
        }

        bool found = false;
        for (int j = 0; j < n; j ++) {
            found = found || next[j].item == running->items[i];
        }

        if (found) {
            continue;
        }

        next[n].item = running->items[i];
        next[n].running = running->amounts[i];
        next[n].count = running->amounts[i];
        n ++;
    }

    for (int i = 0; i < order->entry_count; i ++) {
        OreCraftEntry *old = &order->entries[i];

        for (int j = 0; j < n; j ++) {
            if (next[j].item != old->item) {
                continue;
            }

            next[j].text = old->text;
            next[j].stalled = old->stalled;
            next[j].full = old->full;
            old->text = NULL;
            break;
        }

        ecs_os_free(old->text);
        old->text = NULL;
    }

    ecs_os_memset(order->entries, 0, sizeof(order->entries));

    for (int i = 0; i < n; i ++) {
        order->entries[i] = next[i];
        oreCraftText(world, &order->entries[i]);
    }

    order->entry_count = n;
}

static OreCraftOrder* oreOrderPush(OreCraftState *craft, ecs_entity_t item) {
    if (craft->count >= ORE_CRAFT_ORDERS) {
        return NULL;
    }

    OreCraftOrder *order = &craft->orders[craft->count];
    ecs_os_memset_t(order, 0, OreCraftOrder);

    craft->next_id ++;
    order->id = craft->next_id;
    order->item = item;
    craft->count ++;

    return order;
}

static void oreOrderClear(OreCraftOrder *order) {
    for (int i = 0; i < ORE_CRAFT_ITEMS; i ++) {
        ecs_os_free(order->entries[i].text);
    }

    ecs_os_memset_t(order, 0, OreCraftOrder);
}

static void oreOrderDrop(OreCraftState *craft, int32_t index) {
    oreOrderClear(&craft->orders[index]);

    for (int i = index + 1; i < craft->count; i ++) {
        craft->orders[i - 1] = craft->orders[i];
    }

    craft->count --;
    ecs_os_memset_t(&craft->orders[craft->count], 0, OreCraftOrder);
}

static void oreOrderDropEntry(OreCraftOrder *order, int32_t index) {
    ecs_os_free(order->entries[index].text);

    for (int i = index + 1; i < order->entry_count; i ++) {
        order->entries[i - 1] = order->entries[i];
    }

    order->entry_count --;
    ecs_os_memset_t(&order->entries[order->entry_count], 0, OreCraftEntry);
}

static void oreCrafterStop(OreCrafter *crafter) {
    crafter->item = 0;
    crafter->order = 0;
    crafter->left = 0;
    crafter->total = 0;
}

bool oreCrafterBusy(const OreCrafter *crafter) {
    return crafter->item != 0 && crafter->total > 0;
}

void oreCraftClear(ecs_world_t *world, OreCraftState *craft) {
    for (int i = 0; i < ORE_CRAFT_ORDERS; i ++) {
        oreOrderClear(&craft->orders[i]);
    }

    craft->count = 0;
    craft->stalled = false;
    oreCraftRowsClear(craft);

    if (!craft->crafter_query) {
        return;
    }

    ecs_iter_t it = ecs_query_iter(world, craft->crafter_query);
    while (ecs_query_next(&it)) {
        OreCrafter *crafter = ecs_field(&it, OreCrafter, 0);
        for (int i = 0; i < it.count; i ++) {
            oreCrafterStop(&crafter[i]);
        }
    }
}

static bool oreCraftContext(
    ecs_world_t *world,
    const OreCraftState *craft,
    ecs_entity_t item,
    OreCraftBag *pool,
    OreCraftBag *running,
    int32_t *demand,
    ore_plan_t *plan,
    bool merge,
    bool tolerant)
{
    ecs_os_memset_t(pool, 0, OreCraftBag);
    ecs_os_memset_t(running, 0, OreCraftBag);
    *demand = 1;

    const OreCraftOrder *order = NULL;
    if (merge && craft->count > 0 &&
        craft->orders[craft->count - 1].item == item)
    {
        order = &craft->orders[craft->count - 1];

        *pool = order->pool;
        oreOrderRunning(order, running);
        *demand = order->count + 1;
    }

    if (!oreOrderPlan(world, item, pool, running, *demand, true, tolerant,
        plan))
    {
        return false;
    }

    if (tolerant) {
        return true;
    }

    if (orePlanEntries(plan, running) > ORE_CRAFT_ITEMS) {
        return false;
    }

    if (!orePlanHolds(plan, pool)) {
        return false;
    }

    int32_t rows = oreCraftRows(craft) + orePlanEntries(plan, running);
    if (order) {
        rows -= oreOrderRows(order);
    } else if (craft->count >= ORE_CRAFT_ORDERS) {
        return false;
    }

    return rows <= ORE_CRAFT_ROWS;
}

bool oreCraftReachable(
    ecs_world_t *world,
    const OreCraftState *craft,
    ecs_entity_t item,
    bool queue)
{
    ore_plan_t plan;

    if (!queue) {
        const OreRecipe *recipe = ecs_get(world, item, OreRecipe);
        if (!recipe || !ecs_map_count(&recipe->inputs)) {
            return false;
        }

        ecs_os_memset_t(&plan, 0, ore_plan_t);
        plan.borrow = true;

        return orePlanInputs(world, &plan, recipe, 1, ORE_CRAFT_DEPTH);
    }

    OreCraftBag pool, running;
    int32_t demand;

    return oreCraftContext(world, craft, item, &pool, &running, &demand, &plan,
        false, false);
}

int32_t oreCraftRawCost(
    ecs_world_t *world,
    const OreCraftState *craft,
    ecs_entity_t item,
    ecs_map_t *dst,
    ecs_map_t *have)
{
    ecs_map_init_if(dst, NULL);
    ecs_map_clear(dst);
    ecs_map_init_if(have, NULL);
    ecs_map_clear(have);

    OreCraftBag pool, running;
    ore_plan_t total, marginal;
    int32_t demand;

    ecs_os_memset_t(&pool, 0, OreCraftBag);
    ecs_os_memset_t(&running, 0, OreCraftBag);

    if (!oreOrderPlan(world, item, &pool, &running, 1, false, true, &total)) {
        return 0;
    }

    if (!oreCraftContext(world, craft, item, &pool, &running, &demand,
        &marginal, false, true))
    {
        return 0;
    }

    for (int i = 0; i < total.lack.count; i ++) {
        if (total.lack.amounts[i] <= 0) {
            continue;
        }

        int32_t *value = (int32_t*)ecs_map_ensure(dst, total.lack.items[i]);
        *value += total.lack.amounts[i];
    }

    for (int i = 0; i < marginal.lack.count; i ++) {
        if (marginal.lack.amounts[i] <= 0) {
            continue;
        }

        int32_t *value = (int32_t*)ecs_map_ensure(dst, marginal.lack.items[i]);
        if (*value < marginal.lack.amounts[i]) {
            *value = marginal.lack.amounts[i];
        }
    }

    ecs_map_iter_t it = ecs_map_iter(dst);
    while (ecs_map_next(&it)) {
        int32_t need = (int32_t)ecs_map_value(&it);
        int32_t missing = oreBagGet(&marginal.lack, ecs_map_key(&it));

        if (missing > need) {
            missing = need;
        }

        int32_t *value = (int32_t*)ecs_map_ensure(have, ecs_map_key(&it));
        *value = need - missing;
    }

    return ecs_map_count(dst);
}

bool oreCraftEnqueue(ecs_world_t *world, ecs_entity_t item, bool merge) {
    OreCraftState *craft = ecs_singleton_ensure(world, OreCraftState);

    OreCraftBag pool, running;
    ore_plan_t plan;
    int32_t demand;

    if (!oreCraftContext(world, craft, item, &pool, &running, &demand, &plan,
        merge, false))
    {
        return false;
    }

    OreCraftOrder *order = NULL;
    if (merge && craft->count > 0 &&
        craft->orders[craft->count - 1].item == item)
    {
        order = &craft->orders[craft->count - 1];
    } else {
        order = oreOrderPush(craft, item);
    }

    if (!order) {
        return false;
    }

    for (int i = 0; i < plan.draw.count; i ++) {
        int32_t amount = plan.draw.amounts[i];
        if (amount <= 0) {
            continue;
        }

        oreInventoryAdd(world, plan.draw.items[i], -amount);
        oreBagAdd(&order->pool, plan.draw.items[i], amount);
    }

    oreOrderApply(world, order, &plan, &running);
    order->count = demand;

    ecs_singleton_modified(world, OreCraftState);
    return true;
}

#define ORE_WATCH_MAX (32)

typedef struct ore_watch_t {
    ecs_entity_t item;
    int32_t order;
    float progress;
    bool live;
} ore_watch_t;

static bool oreCraftFits(ecs_world_t *world, ecs_entity_t out) {
    return oreInventoryFits(world, &out, NULL, 1);
}

static bool oreCraftStartable(
    ecs_world_t *world,
    const OreCraftOrder *order,
    const OreCraftEntry *entry,
    bool *blocked)
{
    if (blocked) {
        *blocked = false;
    }

    if (entry->count - entry->running <= 0) {
        return false;
    }

    const OreRecipe *recipe = ecs_get(world, entry->item, OreRecipe);
    if (!recipe) {
        return false;
    }

    if (!oreBagAffordable(&order->pool, recipe)) {
        return false;
    }

    if (entry->item != order->item) {
        return true;
    }

    if (!oreCraftFits(world, oreRecipeItem(recipe, entry->item))) {
        if (blocked) {
            *blocked = true;
        }

        return false;
    }

    return true;
}

static int32_t oreCraftWatch(
    ecs_world_t *world,
    const OreCraftState *craft,
    ore_watch_t *watch)
{
    int32_t n = 0;

    if (!craft->crafter_query) {
        return 0;
    }

    ecs_iter_t it = ecs_query_iter(world, craft->crafter_query);
    while (ecs_query_next(&it)) {
        const OreCrafter *crafter = ecs_field(&it, OreCrafter, 0);

        for (int i = 0; i < it.count && n < ORE_WATCH_MAX; i ++) {
            if (!crafter[i].item || crafter[i].total <= 0) {
                continue;
            }

            watch[n].item = crafter[i].item;
            watch[n].order = crafter[i].order;
            watch[n].progress = 1.0f - crafter[i].left / crafter[i].total;
            watch[n].live = crafter[i].left > 0;
            n ++;
        }
    }

    return n;
}

static bool oreCraftBegin(
    ecs_world_t *world,
    OreCraftState *craft,
    OreCrafter *crafter)
{
    for (int oi = 0; oi < craft->count; oi ++) {
        OreCraftOrder *order = &craft->orders[oi];

        for (int ei = 0; ei < order->entry_count; ei ++) {
            OreCraftEntry *entry = &order->entries[ei];

            if (!oreCraftStartable(world, order, entry, NULL)) {
                continue;
            }

            const OreRecipe *recipe = ecs_get(world, entry->item, OreRecipe);
            oreBagRecipe(&order->pool, recipe, -1);

            float time = recipe->craft_time > 0 ? recipe->craft_time : 0.5f;

            entry->running ++;
            crafter->item = entry->item;
            crafter->order = order->id;
            crafter->total = time;
            crafter->left = time;

            return true;
        }
    }

    return false;
}

static bool oreCraftWork(
    ecs_world_t *world,
    OreCraftState *craft,
    OreCrafter *crafter,
    float advance)
{
    OreCraftOrder *order = oreOrderById(craft, crafter->order);
    OreCraftEntry *entry = order
        ? oreOrderEntry(order, crafter->item)
        : NULL;

    if (!entry || entry->running <= 0) {
        oreCrafterStop(crafter);
        return true;
    }

    const OreRecipe *recipe = ecs_get(world, entry->item, OreRecipe);
    if (!recipe) {
        oreCrafterStop(crafter);
        return true;
    }

    if (crafter->left > 0) {
        crafter->left -= advance;

        if (crafter->left > 0) {
            return false;
        }

        crafter->left = 0;
    }

    ecs_entity_t out = oreRecipeItem(recipe, entry->item);
    int32_t yield = oreRecipeYield(recipe);
    bool head = entry->item == order->item;

    if (head) {
        if (!oreCraftFits(world, out)) {
            return false;
        }

        if (!oreInventoryAdd(world, out, yield)) {
            return false;
        }
    } else if (!oreBagAdd(&order->pool, out, yield)) {
        return false;
    }

    entry->count --;
    entry->running --;

    if (head && order->count > 0) {
        order->count --;
    }

    oreCrafterStop(crafter);

    int32_t index = (int32_t)(entry - order->entries);

    if (entry->count <= 0) {
        oreOrderDropEntry(order, index);
    } else {
        oreCraftText(world, entry);
    }

    if (order->count <= 0) {
        for (int i = order->entry_count - 1; i >= 0; i --) {
            if (!order->entries[i].running) {
                oreOrderDropEntry(order, i);
            }
        }
    }

    return true;
}

static bool oreOrderFlush(ecs_world_t *world, OreCraftOrder *order) {
    bool changed = false;
    bool blocked = false;

    for (int i = order->pool.count - 1; i >= 0; i --) {
        ecs_entity_t item = order->pool.items[i];
        int32_t amount = order->pool.amounts[i];

        if (!oreCraftFits(world, item) ||
            !oreInventoryAdd(world, item, amount))
        {
            blocked = true;
            continue;
        }

        oreBagAdd(&order->pool, item, -amount);
        changed = true;
    }

    if (order->full != blocked) {
        order->full = blocked;
        changed = true;
    }

    return changed;
}

static bool oreCraftSettle(ecs_world_t *world, OreCraftState *craft) {
    bool changed = false;

    for (int i = craft->count - 1; i >= 0; i --) {
        OreCraftOrder *order = &craft->orders[i];

        if (order->entry_count) {
            continue;
        }

        if (oreOrderFlush(world, order)) {
            changed = true;
        }

        if (!order->pool.count) {
            oreOrderDrop(craft, i);
            changed = true;
        }
    }

    return changed;
}

static bool oreCraftMarks(ecs_world_t *world, OreCraftState *craft) {
    ore_watch_t watch[ORE_WATCH_MAX];
    int32_t watched = oreCraftWatch(world, craft, watch);

    bool changed = false;
    bool any = false;

    for (int i = 0; i < craft->count; i ++) {
        OreCraftOrder *order = &craft->orders[i];

        bool live = false;
        for (int w = 0; w < watched; w ++) {
            live = live || (watch[w].order == order->id && watch[w].live);
        }

        bool work = false;
        bool start = false;
        bool full = order->full;

        for (int j = 0; j < order->entry_count; j ++) {
            OreCraftEntry *entry = &order->entries[j];
            bool blocked = false;

            if (entry->count - entry->running > 0) {
                work = true;
            }

            if (oreCraftStartable(world, order, entry, &blocked)) {
                start = true;
            }

            for (int w = 0; w < watched; w ++) {
                if (watch[w].order == order->id &&
                    watch[w].item == entry->item && !watch[w].live)
                {
                    blocked = true;
                }
            }

            if (entry->full != blocked) {
                entry->full = blocked;
                changed = true;
            }

            full = full || blocked;
        }

        bool stalled = !live && !start && (work || full);

        if (order->stalled != stalled) {
            order->stalled = stalled;
            changed = true;
        }

        for (int j = 0; j < order->entry_count; j ++) {
            OreCraftEntry *entry = &order->entries[j];

            bool mark = stalled &&
                (entry->count - entry->running > 0 || entry->full);

            if (entry->stalled != mark) {
                entry->stalled = mark;
                changed = true;
            }
        }

        any = any || stalled;
    }

    if (craft->stalled != any) {
        craft->stalled = any;
        changed = true;
    }

    return changed;
}

bool oreCraftCancelOrder(ecs_world_t *world, int32_t id) {
    OreCraftState *craft = ecs_singleton_ensure(world, OreCraftState);

    int32_t index = oreOrderSlot(craft, id);
    if (index < 0) {
        return false;
    }

    OreCraftOrder *order = &craft->orders[index];
    if (order->count <= 0) {
        return false;
    }

    int32_t demand = order->count - 1;

    OreCraftBag pool, running;
    pool = order->pool;
    oreOrderRunning(order, &running);

    if (!demand) {
        for (int i = 0; i < running.count; i ++) {
            if (running.amounts[i] <= 0) {
                continue;
            }

            const OreRecipe *recipe = ecs_get(
                world, running.items[i], OreRecipe);
            if (recipe) {
                oreBagRecipe(&pool, recipe, running.amounts[i]);
            }
        }

        ecs_os_memset_t(&running, 0, OreCraftBag);
    }

    ore_plan_t plan;
    if (!oreOrderPlan(world, order->item, &pool, &running, demand, false,
        false, &plan))
    {
        return false;
    }

    OreCraftBag refund;
    ecs_os_memset_t(&refund, 0, OreCraftBag);

    for (int i = 0; i < plan.avail.count; i ++) {
        int32_t left = plan.avail.amounts[i] -
            oreBagGet(&plan.incoming, plan.avail.items[i]);

        if (left > 0) {
            oreBagAdd(&refund, plan.avail.items[i], left);
        }
    }

    if (!oreInventoryFits(world, refund.items, refund.amounts, refund.count)) {
        oreToast(world, "No room for the refund", true);
        return false;
    }

    if (!demand && craft->crafter_query) {
        ecs_iter_t it = ecs_query_iter(world, craft->crafter_query);
        while (ecs_query_next(&it)) {
            OreCrafter *crafter = ecs_field(&it, OreCrafter, 0);

            for (int i = 0; i < it.count; i ++) {
                if (crafter[i].order == id) {
                    oreCrafterStop(&crafter[i]);
                }
            }
        }
    }

    for (int i = 0; i < refund.count; i ++) {
        if (refund.amounts[i] <= 0) {
            continue;
        }

        oreInventoryAdd(world, refund.items[i], refund.amounts[i]);
        oreBagAdd(&pool, refund.items[i], -refund.amounts[i]);
    }

    order->pool = pool;
    oreOrderApply(world, order, &plan, &running);
    order->count = demand;

    if (!order->entry_count && !order->pool.count) {
        oreOrderDrop(craft, index);
    }

    ecs_singleton_modified(world, OreCraftState);
    return true;
}

static void OreCrafterOnRemove(ecs_iter_t *it) {
    const OreCrafter *crafter = ecs_field(it, OreCrafter, 0);
    OreCraftState *craft = ecs_field(it, OreCraftState, 1);

    for (int i = 0; i < it->count; i ++) {
        if (!crafter[i].item || crafter[i].total <= 0) {
            continue;
        }

        OreCraftOrder *order = oreOrderById(craft, crafter[i].order);
        if (!order) {
            continue;
        }

        const OreRecipe *recipe = ecs_get(it->world, crafter[i].item,
            OreRecipe);
        if (recipe) {
            oreBagRecipe(&order->pool, recipe, 1);
        }

        OreCraftEntry *entry = oreOrderEntry(order, crafter[i].item);
        if (entry && entry->running > 0) {
            entry->running --;
        }
    }
}

static int32_t oreCraftRowList(
    ecs_world_t *world,
    const OreCraftState *craft,
    OreCraftRow *rows,
    char labels[ORE_CRAFT_ROWS][128])
{
    int32_t n = 0;

    for (int oi = 0; oi < craft->count && n < ORE_CRAFT_ROWS; oi ++) {
        const OreCraftOrder *order = &craft->orders[oi];
        const OreCraftEntry *head = oreOrderFind(order, order->item);
        const OreIcon *icon = ecs_get(world, order->item, OreIcon);

        char name[64];
        oreItemName(world, order->item, name, sizeof(name));

        OreCraftRow *row = &rows[n];
        ecs_os_memset_t(row, 0, OreCraftRow);

        row->item = order->item;
        row->icon = icon ? icon->texture : 0;
        row->order = order->id;
        row->slot = 0;
        row->cancel = order->count > 0;
        row->stalled = order->stalled;

        if (head) {
            snprintf(labels[n], 128, "%s%s",
                head->text ? head->text : name,
                head->full ? " - inventory full" : "");
        } else {
            snprintf(labels[n], 128, "%s - %s", name,
                order->full ? "inventory full" : "wrapping up");
        }

        row->text = labels[n];
        n ++;

        for (int ei = 0; ei < order->entry_count && n < ORE_CRAFT_ROWS; ei ++)
        {
            const OreCraftEntry *entry = &order->entries[ei];
            if (entry->item == order->item) {
                continue;
            }

            const OreIcon *sub = ecs_get(world, entry->item, OreIcon);

            row = &rows[n];
            ecs_os_memset_t(row, 0, OreCraftRow);

            row->item = entry->item;
            row->icon = sub ? sub->texture : 0;
            row->order = order->id;
            row->slot = ei + 1;
            row->sub = true;
            row->stalled = entry->stalled;

            snprintf(labels[n], 128, "%s%s",
                entry->text ? entry->text : "",
                entry->full ? " - inventory full" : "");

            row->text = labels[n];
            n ++;
        }
    }

    ore_watch_t watch[ORE_WATCH_MAX];
    int32_t watched = oreCraftWatch(world, craft, watch);

    for (int w = 0; w < watched; w ++) {
        for (int i = 0; i < n; i ++) {
            if (rows[i].order != watch[w].order ||
                rows[i].item != watch[w].item)
            {
                continue;
            }

            rows[i].active = true;

            if (watch[w].progress > rows[i].progress) {
                rows[i].progress = watch[w].progress;
            }
        }
    }

    return n;
}

static void oreCraftRowsClear(OreCraftState *craft) {
    for (int i = 0; i < ORE_CRAFT_ROWS; i ++) {
        ecs_os_free(craft->rows[i].text);
        craft->rows[i] = (OreCraftRow){0};
    }

    craft->row_count = 0;
}

static void oreCraftRowsSync(ecs_world_t *world, OreCraftState *craft) {
    OreCraftRow next[ORE_CRAFT_ROWS];
    char labels[ORE_CRAFT_ROWS][128];
    int32_t n = oreCraftRowList(world, craft, next, labels);

    oreCraftRowsClear(craft);

    for (int i = 0; i < n; i ++) {
        craft->rows[i] = next[i];
        craft->rows[i].text = ecs_os_strdup(next[i].text);
    }

    craft->row_count = n;
}

static void OreCraftTick(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreCrafter *crafter = ecs_field(it, OreCrafter, 0);
    const OrePowerConsumer *power = ecs_field(it, OrePowerConsumer, 1);
    const OrePowerState *power_state = ecs_field(it, OrePowerState, 2);
    OreCraftState *craft = ecs_field(it, OreCraftState, 3);

    bool changed = false;
    bool busy = false;
    bool dark = power && power_state->blackout;

    for (int i = 0; i < it->count && !dark; i ++) {
        float mul = orePowerMul(world, it->entities[i]);
        if (mul <= 0) {
            continue;
        }

        if (oreCrafterBusy(&crafter[i])) {
            float speed = crafter[i].speed > 0 ? crafter[i].speed : 1.0f;
            busy = true;

            if (oreCraftWork(world, craft, &crafter[i],
                it->delta_time * speed * mul))
            {
                changed = true;
            }

            continue;
        }

        if (crafter[i].item) {
            oreCrafterStop(&crafter[i]);
        }

        if (!craft->count) {
            continue;
        }

        if (oreCraftBegin(world, craft, &crafter[i])) {
            changed = true;
        }
    }

    if (oreCraftSettle(world, craft)) {
        changed = true;
    }

    if (oreCraftMarks(world, craft)) {
        changed = true;
    }

    if (changed || busy) {
        oreCraftRowsSync(world, craft);
        ecs_singleton_modified(world, OreCraftState);
    }
}

void oreCraftImport(ecs_world_t *world) {
    ECS_META_COMPONENT(world, OreCrafter);
    ECS_META_COMPONENT(world, OreCraftEntry);
    ECS_META_COMPONENT(world, OreCraftBag);
    ECS_META_COMPONENT(world, OreCraftOrder);
    ECS_META_COMPONENT(world, OreCraftRow);
    ECS_META_COMPONENT(world, OreCraftState);

    ecs_add_id(world, ecs_id(OreCraftState), EcsSingleton);

    OreCraftState *craft = ecs_singleton_ensure(world, OreCraftState);

    craft->crafter_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreCrafterQuery" }),
        .terms = {
            { .id = ecs_id(OreCrafter), .src.id = EcsSelf }
        }
    });

    ecs_singleton_modified(world, OreCraftState);

    ecs_entity_t playing = ecs_constant_to_entity(
        world, OreGameState, OreGameStatePlaying);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreCrafterOnRemove" }),
        .query.terms = {
            { .id = ecs_id(OreCrafter) },
            { .id = ecs_id(OreCraftState) }
        },
        .events = { EcsOnRemove },
        .callback = OreCrafterOnRemove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCraftTick" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCrafter) },
            { .id = ecs_id(OrePowerConsumer), .oper = EcsOptional },
            { .id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_id(OreCraftState) },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreCraftTick
    });
}
