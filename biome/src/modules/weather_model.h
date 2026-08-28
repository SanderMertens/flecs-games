#ifndef BIOME_WEATHER_MODEL_H
#define BIOME_WEATHER_MODEL_H

#define BiomeWeatherStandardPressure (101325.0f)
#define BiomeWeatherStandardGravity (9.80665f)
#define BiomeWeatherWaterTriplePointPressure (611.657f)
#define BiomeWeatherWaterCriticalTemperature (373.946f)

static float biomeWeatherTemperatureChange(
    const Weather *weather,
    float delta_time)
{
    if (delta_time <= 0) {
        return 0;
    }

    float gravity = weather->gravity > 0
        ? weather->gravity
        : BiomeWeatherStandardGravity;
    float pressure = weather->pressure > 0 ? weather->pressure : 0;
    float greenhouse_gas = weather->greenhouse_gas > 0
        ? weather->greenhouse_gas
        : 0;
    float column = pressure / gravity;
    float standard_column =
        BiomeWeatherStandardPressure / BiomeWeatherStandardGravity;
    float optical_depth = greenhouse_gas * column / standard_column;
    float kelvin = weather->temperature + 273.15f;
    if (kelvin < 0) {
        kelvin = 0;
    }
    float relative_temperature = kelvin / 288.15f;
    float cooling = weather->radiative_cooling *
        relative_temperature * relative_temperature *
        relative_temperature * relative_temperature /
        (1.0f + optical_depth);
    float heating = weather->stellar_heating > 0
        ? weather->stellar_heating
        : 0;
    return (heating - cooling) * delta_time;
}

static float biomeWeatherEquilibriumTemperature(
    const Weather *weather,
    float greenhouse_gas)
{
    if (weather->radiative_cooling <= 0) {
        return weather->temperature;
    }

    float gravity = weather->gravity > 0
        ? weather->gravity
        : BiomeWeatherStandardGravity;
    float pressure = weather->pressure > 0 ? weather->pressure : 0;
    float column = pressure / gravity;
    float standard_column =
        BiomeWeatherStandardPressure / BiomeWeatherStandardGravity;
    float optical_depth = greenhouse_gas > 0
        ? greenhouse_gas * column / standard_column
        : 0;
    float heating = weather->stellar_heating > 0
        ? weather->stellar_heating
        : 0;
    float relative_temperature = powf(
        heating * (1.0f + optical_depth) / weather->radiative_cooling,
        0.25f);
    return 288.15f * relative_temperature - 273.15f;
}

static float biomeWeatherWaterVaporPressure(float temperature) {
    if (temperature <= -273.15f) {
        return 0;
    }
    if (temperature < 0) {
        return 610.94f * expf(
            22.587f * temperature / (temperature + 273.86f));
    }
    return 610.94f * expf(
        17.625f * temperature / (temperature + 243.04f));
}

static bool biomeWeatherWaterIsCondensed(
    float temperature,
    float pressure)
{
    if (pressure <= 0 ||
        temperature >= BiomeWeatherWaterCriticalTemperature)
    {
        return false;
    }
    return pressure >= biomeWeatherWaterVaporPressure(temperature);
}

static bool biomeWeatherWaterIsLiquid(
    float temperature,
    float pressure)
{
    if (temperature < 0 ||
        temperature >= BiomeWeatherWaterCriticalTemperature ||
        pressure < BiomeWeatherWaterTriplePointPressure)
    {
        return false;
    }
    return biomeWeatherWaterIsCondensed(temperature, pressure);
}

#endif
