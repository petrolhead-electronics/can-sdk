/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#include "cansdk/units.h"

#include <cstdio>

namespace cansdk::units
{

namespace
{

std::string with(double value, const char* unit, int places)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f %s", places, value, unit);
    return buf;
}

} // namespace

std::string format_pressure(double kpa, System s, int places)
{
    return s == System::Imperial ? with(kpa_to_psi(kpa), "psi", places < 1 ? 1 : places) : with(kpa, "kPa", places);
}

std::string format_mass_flow(double gps, System s, int places)
{
    return s == System::Imperial ? with(gps_to_lbmin(gps), "lb/min", places + 1) : with(gps, "g/s", places);
}

std::string format_temperature(double celsius, System s, int places)
{
    return s == System::Imperial ? with(c_to_f(celsius), "°F", places) : with(celsius, "°C", places);
}

std::string format_power(double kw, System s, int places)
{
    return s == System::Imperial ? with(kw_to_hp(kw), "hp", places) : with(kw, "kW", places);
}

std::string format_torque(double nm, System s, int places)
{
    return s == System::Imperial ? with(nm_to_lbft(nm), "lb-ft", places) : with(nm, "Nm", places);
}

std::string format_speed(double kmh, System s, int places)
{
    return s == System::Imperial ? with(kmh_to_mph(kmh), "mph", places) : with(kmh, "km/h", places);
}

} // namespace cansdk::units
