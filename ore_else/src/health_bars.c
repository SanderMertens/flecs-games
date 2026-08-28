#define ORE_ELSE_HEALTH_BARS_IMPL
#include "ore_else.h"

#include <math.h>

typedef struct {
    float pos[3];
    float f[3];
    float s[3];
    float u[3];
    float tan_half;
    float aspect;
    float near_;
    float width;
    float height;
} OreProjection;

static bool oreProjectionInit(
    ecs_world_t *world,
    ecs_entity_t camera,
    OreProjection *out)
{
    if (!camera || !ecs_is_alive(world, camera)) {
        return false;
    }

    const FlecsPosition3 *pos = ecs_get(world, camera, FlecsPosition3);
    const FlecsCamera *cam = ecs_get(world, camera, FlecsCamera);
    if (!pos || !cam) {
        return false;
    }

    if (!flecsEngine_ui2dScreenSize(world, &out->width, &out->height)) {
        return false;
    }

    vec3 f, s, u;
    if (!flecsEngine_cameraViewBasis(world, camera, f, s, u)) {
        return false;
    }

    for (int32_t i = 0; i < 3; i ++) {
        out->f[i] = f[i];
        out->s[i] = s[i];
        out->u[i] = u[i];
    }

    out->pos[0] = pos->x;
    out->pos[1] = pos->y;
    out->pos[2] = pos->z;

    out->tan_half = tanf(cam->fov * 0.5f);
    out->near_ = cam->near_ > 0 ? cam->near_ : 0.01f;
    out->aspect = cam->aspect_ratio > 0 ? cam->aspect_ratio :
        (out->height > 0 ? out->width / out->height : 1.6f);

    return out->tan_half > 0 && out->aspect > 0;
}

static bool oreProject(
    const OreProjection *p,
    float x,
    float y,
    float z,
    float *out_x,
    float *out_y)
{
    float rel[3] = {
        x - p->pos[0], y - p->pos[1], z - p->pos[2]
    };

    float depth = rel[0] * p->f[0] + rel[1] * p->f[1] + rel[2] * p->f[2];
    if (depth <= p->near_) {
        return false;
    }

    float right = rel[0] * p->s[0] + rel[1] * p->s[1] + rel[2] * p->s[2];
    float up = rel[0] * p->u[0] + rel[1] * p->u[1] + rel[2] * p->u[2];

    float ndc_x = right / (depth * p->tan_half * p->aspect);
    float ndc_y = up / (depth * p->tan_half);

    *out_x = (ndc_x + 1.0f) * 0.5f * p->width;
    *out_y = (1.0f - ndc_y) * 0.5f * p->height;
    return true;
}

static void oreAabbUnion(
    FlecsAABB *out,
    bool *found,
    const FlecsAABB *bounds)
{
    if (!*found) {
        *out = *bounds;
        *found = true;
        return;
    }

    for (int32_t i = 0; i < 3; i ++) {
        if (bounds->min[i] < out->min[i]) {
            out->min[i] = bounds->min[i];
        }
        if (bounds->max[i] > out->max[i]) {
            out->max[i] = bounds->max[i];
        }
    }
}

static void oreBoxAABB(
    const ecs_world_t *world,
    ecs_entity_t entity,
    FlecsAABB *out,
    bool *found)
{
    const FlecsBox *box = ecs_get(world, entity, FlecsBox);
    const FlecsWorldTransform3 *wt = ecs_get(
        world, entity, FlecsWorldTransform3);

    if (box && wt) {
        float local_min[3] = {
            box->x * -0.5f, box->y * -0.5f, box->z * -0.5f
        };
        float local_max[3] = {
            box->x * 0.5f, box->y * 0.5f, box->z * 0.5f
        };

        FlecsAABB bounds;
        for (int32_t i = 0; i < 3; i ++) {
            bounds.min[i] = bounds.max[i] = wt->m[3][i];
            for (int32_t j = 0; j < 3; j ++) {
                float a = wt->m[j][i] * local_min[j];
                float b = wt->m[j][i] * local_max[j];
                bounds.min[i] += a < b ? a : b;
                bounds.max[i] += a > b ? a : b;
            }
        }

        oreAabbUnion(out, found, &bounds);
    }

    ecs_iter_t it = ecs_children(world, entity);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            oreBoxAABB(world, it.entities[i], out, found);
        }
    }
}

static bool oreBuildingAABB(
    ecs_world_t *world,
    ecs_entity_t entity,
    FlecsAABB *out)
{
    bool found = false;

    FlecsAABB mesh_aabb;
    if (flecsEngine_objectWorldAABB(world, entity, &mesh_aabb)) {
        oreAabbUnion(out, &found, &mesh_aabb);
    }

    oreBoxAABB(world, entity, out, &found);
    return found;
}

static FlecsRgba oreBarLerp(FlecsRgba a, FlecsRgba b, float t) {
    if (t < 0) {
        t = 0;
    } else if (t > 1) {
        t = 1;
    }

    FlecsRgba result;
    result.r = (uint8_t)(a.r + (b.r - a.r) * t);
    result.g = (uint8_t)(a.g + (b.g - a.g) * t);
    result.b = (uint8_t)(a.b + (b.b - a.b) * t);
    result.a = (uint8_t)(a.a + (b.a - a.a) * t);
    return result;
}

static void oreBarRect(
    ecs_world_t *world,
    float x0,
    float y0,
    float x1,
    float y1,
    FlecsRgba color)
{
    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    flecsEngine_ui2dRect(world, x0, y0, x1 - x0, y1 - y0, color);
}

static void OreHealthBars(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    const OreHealth *health = ecs_field(it, OreHealth, 1);
    OreGame *game = ecs_field(it, OreGame, 3);

    OreProjection proj;
    if (!oreProjectionInit(world, game->camera, &proj)) {
        return;
    }

    float bar_h = ecs_const_var_get_t(world, "cfg.hpBarHeight", ecs_f32_t);
    float margin = ecs_const_var_get_t(world, "cfg.hpBarMargin", ecs_f32_t);
    float min_w = ecs_const_var_get_t(world, "cfg.hpBarMinWidth", ecs_f32_t);
    FlecsRgba back = ecs_const_var_get_t(world, "cfg.hpBarBack", FlecsRgba);
    FlecsRgba edge = ecs_const_var_get_t(world, "cfg.hpBarEdge", FlecsRgba);
    FlecsRgba full = ecs_const_var_get_t(world, "cfg.hpBarFull", FlecsRgba);
    FlecsRgba mid = ecs_const_var_get_t(world, "cfg.hpBarMid", FlecsRgba);
    FlecsRgba low = ecs_const_var_get_t(world, "cfg.hpBarLow", FlecsRgba);

    if (bar_h <= 0) {
        return;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        float max = health[i].max;
        float value = health[i].value;
        if (max <= 0 || value <= 0 || value >= max) {
            continue;
        }

        FlecsAABB aabb;
        const OreBounds *cached = ecs_get(world, it->entities[i], OreBounds);

        if (cached) {
            for (int32_t a = 0; a < 3; a ++) {
                aabb.min[a] = cached->min[a];
                aabb.max[a] = cached->max[a];
            }
        } else {
            if (!oreBuildingAABB(world, it->entities[i], &aabb)) {
                continue;
            }

            ecs_set(world, it->entities[i], OreBounds, {
                {aabb.min[0], aabb.min[1], aabb.min[2]},
                {aabb.max[0], aabb.max[1], aabb.max[2]}
            });
        }

        float min_x = 0, max_x = 0, top_y = 0;
        int32_t seen = 0;

        for (int32_t c = 0; c < 8; c ++) {
            float px = (c & 1) ? aabb.max[0] : aabb.min[0];
            float py = (c & 2) ? aabb.max[1] : aabb.min[1];
            float pz = (c & 4) ? aabb.max[2] : aabb.min[2];

            float sx, sy;
            if (!oreProject(&proj, px, py, pz, &sx, &sy)) {
                continue;
            }

            if (!seen) {
                min_x = max_x = sx;
                top_y = sy;
            } else {
                if (sx < min_x) {
                    min_x = sx;
                }
                if (sx > max_x) {
                    max_x = sx;
                }
                if (sy < top_y) {
                    top_y = sy;
                }
            }
            seen ++;
        }

        if (!seen) {
            continue;
        }

        float bx = min_x;
        float bw = max_x - min_x;
        float by = top_y - margin - bar_h;

        if (bw < min_w) {
            continue;
        }

        if (bx + bw <= 0 || bx >= proj.width ||
            by + bar_h <= 0 || by >= proj.height)
        {
            continue;
        }

        float cx0 = bx < 0 ? 0 : bx;
        float cx1 = bx + bw > proj.width ? proj.width : bx + bw;
        float cy0 = by < 0 ? 0 : by;
        float cy1 = by + bar_h > proj.height ? proj.height : by + bar_h;

        if (cx1 <= cx0 || cy1 <= cy0) {
            continue;
        }

        oreBarRect(world, cx0, cy0, cx1, cy1, back);

        float frac = value / max;
        FlecsRgba color = frac >= 0.5f
            ? oreBarLerp(mid, full, (frac - 0.5f) * 2.0f)
            : oreBarLerp(low, mid, frac * 2.0f);

        float fx0 = bx + 1;
        float fx1 = fx0 + (bw - 2) * frac;
        float fy0 = by + 1;
        float fy1 = by + bar_h - 1;

        if (fx0 < cx0) {
            fx0 = cx0;
        }
        if (fx1 > cx1) {
            fx1 = cx1;
        }
        if (fy0 < cy0) {
            fy0 = cy0;
        }
        if (fy1 > cy1) {
            fy1 = cy1;
        }

        oreBarRect(world, fx0, fy0, fx1, fy1, color);

        flecsEngine_ui2dBorder(world, cx0, cy0, cx1 - cx0, cy1 - cy0,
            0, 0, 0, 0, 1, 1, 1, 1, edge);
    }
}

void oreHealthBarsImport(ecs_world_t *world) {
    ECS_META_COMPONENT(world, OreBounds);

    ecs_entity_t playing = ecs_constant_to_entity(
        world, OreGameState, OreGameStatePlaying);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreHealthBars" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreBuilding) },
            { .id = ecs_id(OreHealth) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(OreGame) },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreHealthBars
    });
}
