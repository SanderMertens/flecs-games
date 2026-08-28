#define BIOME_WEATHER_IMPL

#include "biome.h"
#include "weather_model.h"

static void WeatherPrintEquilibrium(ecs_iter_t *it) {
    const Weather *weather = ecs_field(it, Weather, 0);
    if (weather->radiative_cooling <= 0) {
        return;
    }

    const float ghg[] = {0, 1, 2, 4, 6, 8, 10, 15, 20, 50};

    ecs_trace(
        "weather: equilibrium temperatures "
        "(stellar_heating=%f, radiative_cooling=%f)",
        (double)weather->stellar_heating,
        (double)weather->radiative_cooling);
    ecs_log_push();
    for (int32_t i = 0; i < (int32_t)(sizeof(ghg) / sizeof(ghg[0])); i ++) {
        ecs_trace("ghg=%6.2f -> %8.2f C", (double)ghg[i],
            (double)biomeWeatherEquilibriumTemperature(weather, ghg[i]));
    }
    ecs_trace("ghg=%6.2f -> %8.2f C (configured)",
        (double)weather->greenhouse_gas,
        (double)biomeWeatherEquilibriumTemperature(
            weather, weather->greenhouse_gas));
    ecs_log_pop();
}

static void WeatherUpdate(ecs_iter_t *it) {
    Weather *weather = ecs_field(it, Weather, 0);

    weather->temperature += biomeWeatherTemperatureChange(
        weather, it->delta_time);
}

void biomeWeatherImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeWeather);

    ecs_set_name_prefix(world, "Weather");
    ECS_META_COMPONENT(world, Weather);

    ecs_add_id(world, ecs_id(Weather), EcsSingleton);

    ECS_SYSTEM(world, WeatherPrintEquilibrium, EcsOnStart,
        [in] Weather);

    ECS_SYSTEM(world, WeatherUpdate, EcsPostUpdate,
        [inout] Weather);
}
