/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Lavrentiy Ivanov <laffkin@gmail.com>
 * See LICENSE.
 */

#pragma once

/// @file units.h
/// One place for every unit conversion in the suite.
///
/// Tuning is a field where half the world's data sheets are metric and the
/// other half are not: injectors rated in cc/min and lb/hr, fuel pressure in
/// kPa, bar and psi, airflow in g/s and lb/min, power in kW and hp. Scattering
/// those factors through the code is how a tool ends up off by 3% somewhere
/// nobody looks.
///
/// So: the SI value is the truth everywhere inside the code, conversions live
/// only here, and the factors are exact where an exact definition exists.
///
/// **The one judgement call is fuel density**, which is needed to convert a
/// volumetric injector rating (cc/min) to a mass one (lb/hr). It is a property
/// of the fuel, not a constant of nature, so it is a parameter with a stated
/// default rather than a hidden number — see `fuel` below.
///
/// vdyno keeps its own copies deliberately: it ships as a standalone,
/// zero-dependency library. If a factor changes here, check `vdyno.h` too.

#include <cmath>
#include <string>

namespace cansdk::units
{

// ---------------------------------------------------------------------------
// Exact definitions. These are not measurements; do not "improve" them.
// ---------------------------------------------------------------------------
inline constexpr double kPoundKg = 0.45359237;      ///< international pound, exact
inline constexpr double kInchM = 0.0254;            ///< exact
inline constexpr double kGravity = 9.80665;         ///< standard gravity, exact
inline constexpr double kPsiPa = 6894.757293168361; ///< lbf/in², from the two above
inline constexpr double kBarPa = 100000.0;          ///< exact
inline constexpr double kAtmPa = 101325.0;          ///< standard atmosphere, exact

// ---------------------------------------------------------------------------
// Pressure — SI is kPa, because that is what the ECU and every Mazda document
// use. Gauge vs absolute is the caller's business; these only scale.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr double kpa_to_psi(double kpa)
{
    return kpa * 1000.0 / kPsiPa;
}
[[nodiscard]] constexpr double psi_to_kpa(double psi)
{
    return psi * kPsiPa / 1000.0;
}
[[nodiscard]] constexpr double kpa_to_bar(double kpa)
{
    return kpa * 1000.0 / kBarPa;
}
[[nodiscard]] constexpr double bar_to_kpa(double bar)
{
    return bar * kBarPa / 1000.0;
}
[[nodiscard]] constexpr double kpa_to_atm(double kpa)
{
    return kpa * 1000.0 / kAtmPa;
}
/// Manifold vacuum, as gauges and the NC's own tables express it: how far below
/// atmosphere, in kPa. Positive is vacuum, negative is boost.
[[nodiscard]] constexpr double map_to_vacuum(double map_kpa_absolute, double baro_kpa = kAtmPa / 1000.0)
{
    return baro_kpa - map_kpa_absolute;
}
[[nodiscard]] constexpr double vacuum_to_map(double vacuum_kpa, double baro_kpa = kAtmPa / 1000.0)
{
    return baro_kpa - vacuum_kpa;
}
[[nodiscard]] inline double kpa_to_inhg(double kpa)
{
    return kpa * 0.2952998751;
}

// ---------------------------------------------------------------------------
// Mass flow — SI is g/s, which is what the MAF table and every NC log use.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr double gps_to_lbmin(double gps)
{
    return gps * 60.0 / (kPoundKg * 1000.0);
}
[[nodiscard]] constexpr double lbmin_to_gps(double lbmin)
{
    return lbmin * kPoundKg * 1000.0 / 60.0;
}
[[nodiscard]] constexpr double gps_to_kgh(double gps)
{
    return gps * 3.6;
}
[[nodiscard]] constexpr double kgh_to_gps(double kgh)
{
    return kgh / 3.6;
}

// ---------------------------------------------------------------------------
// Fuel. Density is a property of what is in the tank, so it is named and
// defaulted rather than baked in.
// ---------------------------------------------------------------------------
namespace fuel
{
/// The density injector manufacturers rate against. 0.75 g/cc is the industry
/// convention for petrol; real pump fuel is 0.72–0.78 and E85 is heavier still,
/// which is exactly why this is a parameter.
inline constexpr double kPetrolGPerCc = 0.75;
inline constexpr double kE85GPerCc = 0.7815;
inline constexpr double kEthanolGPerCc = 0.789;
inline constexpr double kMethanolGPerCc = 0.7918;

/// Stoichiometric air-fuel ratios, for turning lambda into AFR.
inline constexpr double kAfrPetrol = 14.7;
inline constexpr double kAfrE85 = 9.765;
inline constexpr double kAfrEthanol = 9.0;
inline constexpr double kAfrMethanol = 6.4;
} // namespace fuel

/// Injector rating: volumetric (cc/min) to mass (lb/hr) at a stated density.
[[nodiscard]] inline double ccmin_to_lbhr(double cc_min, double density_g_cc = fuel::kPetrolGPerCc)
{
    return cc_min * density_g_cc * 60.0 / (kPoundKg * 1000.0);
}
[[nodiscard]] inline double lbhr_to_ccmin(double lb_hr, double density_g_cc = fuel::kPetrolGPerCc)
{
    return lb_hr * kPoundKg * 1000.0 / (60.0 * density_g_cc);
}

/// Lambda ↔ AFR for a given fuel.
[[nodiscard]] constexpr double lambda_to_afr(double lambda, double stoich = fuel::kAfrPetrol)
{
    return lambda * stoich;
}
[[nodiscard]] constexpr double afr_to_lambda(double afr, double stoich = fuel::kAfrPetrol)
{
    return stoich > 0 ? afr / stoich : 0.0;
}

// ---------------------------------------------------------------------------
// Temperature
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr double c_to_f(double c)
{
    return c * 9.0 / 5.0 + 32.0;
}
[[nodiscard]] constexpr double f_to_c(double f)
{
    return (f - 32.0) * 5.0 / 9.0;
}
[[nodiscard]] constexpr double c_to_k(double c)
{
    return c + 273.15;
}
[[nodiscard]] constexpr double k_to_c(double k)
{
    return k - 273.15;
}

// ---------------------------------------------------------------------------
// Speed, torque, power
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr double kmh_to_ms(double kmh)
{
    return kmh / 3.6;
}
[[nodiscard]] constexpr double ms_to_kmh(double ms)
{
    return ms * 3.6;
}
[[nodiscard]] constexpr double kmh_to_mph(double kmh)
{
    return kmh * 1000.0 / (kInchM * 12.0 * 5280.0);
}
[[nodiscard]] constexpr double mph_to_kmh(double mph)
{
    return mph * kInchM * 12.0 * 5280.0 / 1000.0;
}

[[nodiscard]] constexpr double nm_to_lbft(double nm)
{
    return nm / (kPoundKg * kGravity * kInchM * 12.0);
}
[[nodiscard]] constexpr double lbft_to_nm(double lbft)
{
    return lbft * kPoundKg * kGravity * kInchM * 12.0;
}

/// Mechanical horsepower (550 ft·lbf/s), the one dyno sheets mean by "hp".
inline constexpr double kHorsepowerW = 745.6998715822702;
/// Metric horsepower (PS / CV / pk), which European brochures mean instead.
inline constexpr double kMetricHorsepowerW = 735.49875;

[[nodiscard]] constexpr double kw_to_hp(double kw)
{
    return kw * 1000.0 / kHorsepowerW;
}
[[nodiscard]] constexpr double hp_to_kw(double hp)
{
    return hp * kHorsepowerW / 1000.0;
}
[[nodiscard]] constexpr double kw_to_ps(double kw)
{
    return kw * 1000.0 / kMetricHorsepowerW;
}
[[nodiscard]] constexpr double ps_to_kw(double ps)
{
    return ps * kMetricHorsepowerW / 1000.0;
}

/// Power from torque and engine speed: `P = τ·ω`, ω = rpm·2π/60.
[[nodiscard]] inline double power_kw(double torque_nm, double rpm)
{
    return torque_nm * rpm * 2.0 * 3.14159265358979323846 / 60.0 / 1000.0;
}
[[nodiscard]] inline double torque_nm(double power_kw_value, double rpm)
{
    return rpm > 0 ? power_kw_value * 1000.0 * 60.0 / (rpm * 2.0 * 3.14159265358979323846) : 0.0;
}

// ---------------------------------------------------------------------------
// Which set of units to show. The stored value never changes; only the display.
// ---------------------------------------------------------------------------
enum class System
{
    Metric,   ///< kPa, g/s, °C, kW, Nm, km/h
    Imperial, ///< psi, lb/min, °F, hp, lb-ft, mph
};

/// Format a value with the unit the chosen system prefers. Returned as a string
/// because every caller wants "391 kPa" or "56.8 psi", not a bare number.
[[nodiscard]] std::string format_pressure(double kpa, System s, int places = 0);
[[nodiscard]] std::string format_mass_flow(double gps, System s, int places = 1);
[[nodiscard]] std::string format_temperature(double celsius, System s, int places = 0);
[[nodiscard]] std::string format_power(double kw, System s, int places = 1);
[[nodiscard]] std::string format_torque(double nm, System s, int places = 1);
[[nodiscard]] std::string format_speed(double kmh, System s, int places = 0);

} // namespace cansdk::units
