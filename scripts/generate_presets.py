#!/usr/bin/env python3
"""
scripts/generate_presets.py
Generates canonical, granular machining preset JSON files for NanoELS.
Standards:
  - ISO 261 / ISO 965-1 (Metric Coarse & Fine Threads)
  - ASME B1.1 (Unified National UNC & UNF Threads)
  - ASME B1.20.1 (National Pipe Taper - NPT)
  - DIN 228-1 / ISO 296 (Morse Machine Tapers)
  - Machine Spindle & Collet Standards (R8, 5C, ER, 7:24)
  - Taper Pins & Centers (ISO 2339, ASME B18.8.2, 60°/45°/30°)

Outputs to:
  data/presets/threads/*.json
  data/presets/tapers/*.json
"""

import os
import json
import math

# -----------------------------------------------------------------------------
# Math & Conversion Helpers
# -----------------------------------------------------------------------------

def mm_to_du(val_mm):
    """Converts millimeters to deci-microns (1 du = 0.0001 mm = 0.1 um)."""
    return int(round(val_mm * 10000.0))

def in_to_du(val_in):
    """Converts inches to deci-microns."""
    return int(round(val_in * 25.4 * 10000.0))

# -----------------------------------------------------------------------------
# 1. ISO Metric Threads (ISO 261 / ISO 965-1)
# -----------------------------------------------------------------------------

METRIC_SIZES = [
    # (name, nominal_dia_mm, pitch_mm, series, choice, tap_drill_mm)
    ("M1", 1.0, 0.25, "coarse", 1, 0.75),
    ("M1.2", 1.2, 0.25, "coarse", 1, 0.95),
    ("M1.4", 1.4, 0.30, "coarse", 2, 1.10),
    ("M1.6", 1.6, 0.35, "coarse", 1, 1.25),
    ("M2", 2.0, 0.40, "coarse", 1, 1.60),
    ("M2.5", 2.5, 0.45, "coarse", 1, 2.05),
    ("M3", 3.0, 0.50, "coarse", 1, 2.50),
    ("M3.5", 3.5, 0.60, "coarse", 2, 2.90),
    ("M4", 4.0, 0.70, "coarse", 1, 3.30),
    ("M5", 5.0, 0.80, "coarse", 1, 4.20),
    ("M6", 6.0, 1.00, "coarse", 1, 5.00),
    ("M7", 7.0, 1.00, "coarse", 2, 6.00),
    ("M8", 8.0, 1.25, "coarse", 1, 6.80),
    ("M8x1", 8.0, 1.00, "fine", 1, 7.00),
    ("M10", 10.0, 1.50, "coarse", 1, 8.50),
    ("M10x1.25", 10.0, 1.25, "fine", 2, 8.80),
    ("M10x1", 10.0, 1.00, "fine", 2, 9.00),
    ("M12", 12.0, 1.75, "coarse", 1, 10.20),
    ("M12x1.5", 12.0, 1.50, "fine", 2, 10.50),
    ("M12x1.25", 12.0, 1.25, "fine", 2, 10.80),
    ("M14", 14.0, 2.00, "coarse", 2, 12.00),
    ("M14x1.5", 14.0, 1.50, "fine", 2, 12.50),
    ("M16", 16.0, 2.00, "coarse", 1, 14.00),
    ("M16x1.5", 16.0, 1.50, "fine", 2, 14.50),
    ("M18", 18.0, 2.50, "coarse", 2, 15.50),
    ("M18x1.5", 18.0, 1.50, "fine", 2, 16.50),
    ("M20", 20.0, 2.50, "coarse", 1, 17.50),
    ("M20x1.5", 20.0, 1.50, "fine", 2, 18.50),
    ("M22", 22.0, 2.50, "coarse", 2, 19.50),
    ("M22x1.5", 22.0, 1.50, "fine", 2, 20.50),
    ("M24", 24.0, 3.00, "coarse", 1, 21.00),
    ("M24x2", 24.0, 2.00, "fine", 2, 22.00),
    ("M27", 27.0, 3.00, "coarse", 2, 24.00),
    ("M27x2", 27.0, 2.00, "fine", 2, 25.00),
    ("M30", 30.0, 3.50, "coarse", 1, 26.50),
    ("M30x2", 30.0, 2.00, "fine", 2, 28.00),
    ("M33", 33.0, 3.50, "coarse", 2, 29.50),
    ("M36", 36.0, 4.00, "coarse", 1, 32.00)
]

def generate_metric_threads():
    items = []
    for designation, d_nom, p, series, choice, tap_drill in METRIC_SIZES:
        # Fundamental triangle H = (sqrt(3)/2)*P
        h = 0.8660254038 * p
        # Thread depth h3 = (17/24)*H
        depth = (17.0 / 24.0) * h
        # Basic pitch diameter d2 = d - (3/4)*H
        d2 = d_nom - 0.649519 * p
        # Basic minor diameter d1 = d - (5/4)*H
        d1 = d_nom - 1.082532 * p

        # ISO 965-1 Grade 6g Fundamental deviation es (mm)
        es_um = 15.0 + 11.0 * p
        es_mm = es_um / 1000.0

        td2_um = 90.0 * (p ** 0.4) * (d_nom ** 0.1)
        td2_mm = td2_um / 1000.0

        td_um = 180.0 * (p ** (2.0 / 3.0)) - 3.15 / math.sqrt(p)
        td_mm = td_um / 1000.0

        major_max = d_nom - es_mm
        major_min = major_max - td_mm
        pitch_max = d2 - es_mm
        pitch_min = pitch_max - td2_mm
        minor_max = d1 - es_mm
        minor_min = minor_max - (td2_mm * 1.25)

        w_best = 0.577350269 * p
        mow = pitch_max + 3.0 * w_best - 0.8660254038 * p

        item_id = designation.lower().replace(".", "_").replace("x", "_") + "_6g"

        items.append({
            "id": item_id,
            "designation": designation,
            "series": series,
            "choice": choice,
            "gender": "external",
            "tolerance_class": "6g",
            "nominal_dia_du": mm_to_du(d_nom),
            "pitch_du": mm_to_du(p),
            "tpi": 0,
            "major_dia_max_du": mm_to_du(major_max),
            "major_dia_min_du": mm_to_du(major_min),
            "pitch_dia_max_du": mm_to_du(pitch_max),
            "pitch_dia_min_du": mm_to_du(pitch_min),
            "minor_dia_max_du": mm_to_du(minor_max),
            "minor_dia_min_du": mm_to_du(minor_min),
            "thread_depth_du": mm_to_du(depth),
            "tap_drill_dia_du": mm_to_du(tap_drill),
            "wire_best_dia_du": mm_to_du(w_best),
            "wire_mow_target_du": mm_to_du(mow),
            "taper_ratio": 0.0
        })

    return {
        "meta": {
            "domain": "threads",
            "standard_org": "ISO",
            "standard_id": "261 / 965-1",
            "standard_title": "ISO General Purpose Metric Screw Threads",
            "unit_system": "metric",
            "flank_angle_deg": 60.0,
            "version": "1.0.0"
        },
        "items": items
    }

# -----------------------------------------------------------------------------
# 2. Unified National Threads (ASME B1.1)
# -----------------------------------------------------------------------------

UNIFIED_SIZES = [
    # (name, nominal_dia_in, tpi, series, choice, tap_drill_in)
    ("#0-80 UNF", 0.0600, 80, "UNF", 2, 0.0469),
    ("#1-64 UNC", 0.0730, 64, "UNC", 2, 0.0595),
    ("#1-72 UNF", 0.0730, 72, "UNF", 2, 0.0595),
    ("#2-56 UNC", 0.0860, 56, "UNC", 2, 0.0700),
    ("#2-64 UNF", 0.0860, 64, "UNF", 2, 0.0700),
    ("#3-48 UNC", 0.0990, 48, "UNC", 2, 0.0785),
    ("#4-40 UNC", 0.1120, 40, "UNC", 1, 0.0890),
    ("#4-48 UNF", 0.1120, 48, "UNF", 2, 0.0935),
    ("#6-32 UNC", 0.1380, 32, "UNC", 1, 0.1065),
    ("#6-40 UNF", 0.1380, 40, "UNF", 2, 0.1130),
    ("#8-32 UNC", 0.1640, 32, "UNC", 1, 0.1360),
    ("#8-36 UNF", 0.1640, 36, "UNF", 2, 0.1360),
    ("#10-24 UNC", 0.1900, 24, "UNC", 1, 0.1495),
    ("#10-32 UNF", 0.1900, 32, "UNF", 1, 0.1590),
    ("1/4-20 UNC", 0.2500, 20, "UNC", 1, 0.2010),
    ("1/4-28 UNF", 0.2500, 28, "UNF", 1, 0.2130),
    ("5/16-18 UNC", 0.3125, 18, "UNC", 1, 0.2570),
    ("5/16-24 UNF", 0.3125, 24, "UNF", 1, 0.2720),
    ("3/8-16 UNC", 0.3750, 16, "UNC", 1, 0.3125),
    ("3/8-24 UNF", 0.3750, 24, "UNF", 1, 0.3320),
    ("7/16-14 UNC", 0.4375, 14, "UNC", 2, 0.3680),
    ("7/16-20 UNF", 0.4375, 20, "UNF", 2, 0.3906),
    ("1/2-13 UNC", 0.5000, 13, "UNC", 1, 0.4219),
    ("1/2-20 UNF", 0.5000, 20, "UNF", 1, 0.4531),
    ("9/16-12 UNC", 0.5625, 12, "UNC", 2, 0.4844),
    ("9/16-18 UNF", 0.5625, 18, "UNF", 2, 0.5078),
    ("5/8-11 UNC", 0.6250, 11, "UNC", 1, 0.5312),
    ("5/8-18 UNF", 0.6250, 18, "UNF", 1, 0.5703),
    ("3/4-10 UNC", 0.7500, 10, "UNC", 1, 0.6562),
    ("3/4-16 UNF", 0.7500, 16, "UNF", 1, 0.6875),
    ("7/8-9 UNC", 0.8750, 9, "UNC", 1, 0.7656),
    ("7/8-14 UNF", 0.8750, 14, "UNF", 2, 0.8031),
    ("1-8 UNC", 1.0000, 8, "UNC", 1, 0.8750),
    ("1-12 UNF", 1.0000, 12, "UNF", 2, 0.9167),
    ("1-14 UNS", 1.0000, 14, "UNS", 2, 0.9286),
    ("1-1/8-7 UNC", 1.1250, 7, "UNC", 2, 0.9844),
    ("1-1/4-7 UNC", 1.2500, 7, "UNC", 2, 1.1094),
    ("1-1/2-6 UNC", 1.5000, 6, "UNC", 2, 1.3438)
]

def generate_unified_threads():
    items = []
    for designation, d_nom_in, tpi, series, choice, tap_drill_in in UNIFIED_SIZES:
        p_in = 1.0 / tpi
        p_mm = p_in * 25.4
        d_nom_mm = d_nom_in * 25.4

        # ASME B1.1 Class 2A external limits
        allowance_in = 0.0015 * (d_nom_in ** (1.0 / 3.0)) + 0.0015 * math.sqrt(d_nom_in) + 0.015 * (p_in ** (2.0 / 3.0))
        allowance_in = 0.30 * allowance_in

        depth_in = 0.61343411 * p_in
        d2_in = d_nom_in - 0.64951905 * p_in
        d1_in = d_nom_in - 1.08253175 * p_in

        td_in = 0.060 * (p_in ** (2.0 / 3.0))
        td2_in = 0.0015 * (d_nom_in ** (1.0 / 3.0)) + 0.0015 * math.sqrt(d_nom_in) + 0.015 * (p_in ** (2.0 / 3.0))

        major_max_in = d_nom_in - allowance_in
        major_min_in = major_max_in - td_in
        pitch_max_in = d2_in - allowance_in
        pitch_min_in = pitch_max_in - td2_in
        minor_max_in = d1_in - allowance_in
        minor_min_in = minor_max_in - (td2_in * 1.25)

        w_best_in = 0.577350269 * p_in
        mow_in = pitch_max_in + 3.0 * w_best_in - 0.8660254038 * p_in

        item_id = designation.lower().replace("#", "no_").replace("/", "_").replace("-", "_").replace(" ", "_") + "_2a"

        items.append({
            "id": item_id,
            "designation": designation,
            "series": series,
            "choice": choice,
            "gender": "external",
            "tolerance_class": "2A",
            "nominal_dia_du": in_to_du(d_nom_in),
            "pitch_du": mm_to_du(p_mm),
            "tpi": tpi,
            "major_dia_max_du": in_to_du(major_max_in),
            "major_dia_min_du": in_to_du(major_min_in),
            "pitch_dia_max_du": in_to_du(pitch_max_in),
            "pitch_dia_min_du": in_to_du(pitch_min_in),
            "minor_dia_max_du": in_to_du(minor_max_in),
            "minor_dia_min_du": in_to_du(minor_min_in),
            "thread_depth_du": in_to_du(depth_in),
            "tap_drill_dia_du": in_to_du(tap_drill_in),
            "wire_best_dia_du": in_to_du(w_best_in),
            "wire_mow_target_du": in_to_du(mow_in),
            "taper_ratio": 0.0
        })

    return {
        "meta": {
            "domain": "threads",
            "standard_org": "ASME",
            "standard_id": "B1.1",
            "standard_title": "Unified Inch Screw Threads (UN and UNR Thread Form)",
            "unit_system": "imperial",
            "flank_angle_deg": 60.0,
            "version": "1.0.0"
        },
        "items": items
    }

# -----------------------------------------------------------------------------
# 3. National Pipe Taper Threads (ASME B1.20.1)
# -----------------------------------------------------------------------------

NPT_SIZES = [
    # (name, nominal_od_in, tpi, choice, tap_drill_in, l1_gauge_length_in)
    ("1/16-27 NPT", 0.3125, 27, 2, 0.2460, 0.160),
    ("1/8-27 NPT", 0.4050, 27, 1, 0.3390, 0.161),
    ("1/4-18 NPT", 0.5400, 18, 1, 0.4375, 0.227),
    ("3/8-18 NPT", 0.6750, 18, 1, 0.5625, 0.240),
    ("1/2-14 NPT", 0.8400, 14, 1, 0.7031, 0.320),
    ("3/4-14 NPT", 1.0500, 14, 1, 0.9062, 0.339),
    ("1-11.5 NPT", 1.3150, 11.5, 1, 1.1406, 0.400),
    ("1-1/4-11.5 NPT", 1.6600, 11.5, 1, 1.4844, 0.420),
    ("1-1/2-11.5 NPT", 1.9000, 11.5, 1, 1.7188, 0.420),
    ("2-11.5 NPT", 2.3750, 11.5, 1, 2.1875, 0.436)
]

def generate_npt_threads():
    items = []
    # NPT taper is 1:16 = 0.75 in/ft = cone ratio 0.0625
    cone_ratio = 0.0625
    for designation, od_in, tpi, choice, tap_drill_in, l1_in in NPT_SIZES:
        p_in = 1.0 / tpi
        p_mm = p_in * 25.4
        # Thread depth h = 0.800 * P
        depth_in = 0.800 * p_in

        # Hand-tight pitch diameter E1 = D - (0.05 D + 1.1) * P
        e1_in = od_in - (0.05 * od_in + 1.1) * p_in
        # Pitch dia at pipe end E0 = E1 - 0.0625 * L1
        e0_in = e1_in - cone_ratio * l1_in

        item_id = designation.lower().replace("/", "_").replace("-", "_").replace(".", "_").replace(" ", "_")

        items.append({
            "id": item_id,
            "designation": designation,
            "series": "NPT",
            "choice": choice,
            "gender": "external",
            "tolerance_class": "standard",
            "nominal_dia_du": in_to_du(od_in),
            "pitch_du": mm_to_du(p_mm),
            "tpi": float(tpi),
            "major_dia_max_du": in_to_du(od_in),
            "major_dia_min_du": in_to_du(od_in - 0.005),
            "pitch_dia_max_du": in_to_du(e1_in),
            "pitch_dia_min_du": in_to_du(e0_in),
            "minor_dia_max_du": in_to_du(od_in - 2.0 * depth_in),
            "minor_dia_min_du": in_to_du(od_in - 2.0 * depth_in - 0.010),
            "thread_depth_du": in_to_du(depth_in),
            "tap_drill_dia_du": in_to_du(tap_drill_in),
            "gauge_length_du": in_to_du(l1_in),
            "cone_ratio": cone_ratio,
            "taper_ratio": cone_ratio
        })

    return {
        "meta": {
            "domain": "threads",
            "standard_org": "ASME",
            "standard_id": "B1.20.1",
            "standard_title": "Pipe Threads, General Purpose (Inch) - NPT",
            "unit_system": "imperial",
            "flank_angle_deg": 60.0,
            "taper_ratio": 0.0625,
            "version": "1.0.0"
        },
        "items": items
    }

# -----------------------------------------------------------------------------
# 4. Morse Machine Tapers (DIN 228-1 / ISO 296 / ASME B5.10)
# -----------------------------------------------------------------------------

MORSE_TAPERS = [
    # (name, choice, cone_ratio, half_angle_deg, tpf_in, d_large_mm, d_small_mm, length_mm)
    ("MT0", 2, 0.052052, 1.4908, 0.6246, 9.045, 6.442, 50.0),
    ("MT1", 1, 0.049882, 1.4286, 0.5986, 12.065, 9.400, 53.5),
    ("MT2", 1, 0.049951, 1.4306, 0.5994, 17.780, 14.600, 64.0),
    ("MT3", 1, 0.050196, 1.4376, 0.6024, 23.825, 19.800, 81.0),
    ("MT4", 1, 0.051938, 1.4875, 0.6233, 31.267, 26.000, 102.5),
    ("MT5", 2, 0.052626, 1.5072, 0.6315, 44.399, 37.600, 129.5),
    ("MT6", 2, 0.052140, 1.4933, 0.6257, 63.348, 53.800, 182.0)
]

def generate_morse_tapers():
    items = []
    for designation, choice, ratio, half_ang, tpf, d_lg, d_sm, length in MORSE_TAPERS:
        items.append({
            "id": designation.lower(),
            "designation": designation,
            "series": "MT",
            "choice": choice,
            "form": "external",
            "cone_ratio": round(ratio, 6),
            "half_angle_deg": round(half_ang, 4),
            "included_angle_deg": round(half_ang * 2.0, 4),
            "taper_in_per_ft": round(tpf, 4),
            "gauge_dia_large_du": mm_to_du(d_lg),
            "gauge_dia_small_du": mm_to_du(d_sm),
            "gauge_length_du": mm_to_du(length)
        })

    return {
        "meta": {
            "domain": "tapers",
            "standard_org": "DIN",
            "standard_id": "228-1",
            "standard_title": "Morse Tapers and Metric Tapers",
            "unit_system": "metric",
            "version": "1.0.0"
        },
        "items": items
    }

# -----------------------------------------------------------------------------
# 5. Collets & Machine Spindles (R8, 5C, ER, 7:24)
# -----------------------------------------------------------------------------

COLLETS_SPINDLES = [
    # (name, choice, angle_deg, ratio, tpf_in, spec)
    ("R8 Collet", 1, 8.4250, 0.296270, 3.555, "16° 51' included (Bridgeport standard)"),
    ("5C Collet", 1, 10.0000, 0.352654, 4.232, "20° 00' included (Hardinge standard)"),
    ("ER Collet (ER11-ER40)", 1, 8.0000, 0.281096, 3.373, "16° 00' included (DIN 6499)"),
    ("7:24 Steep Taper (ISO/CAT/BT)", 1, 8.2971, 0.291667, 3.500, "7:24 ratio (ISO 7388 / ASME B5.50)")
]

def generate_collets_spindles():
    items = []
    for designation, choice, half_ang, ratio, tpf, spec in COLLETS_SPINDLES:
        slug = designation.split()[0].lower().replace(":", "_")
        items.append({
            "id": slug,
            "designation": designation,
            "series": "SPINDLE",
            "choice": choice,
            "form": "internal",
            "cone_ratio": round(ratio, 6),
            "half_angle_deg": round(half_ang, 4),
            "included_angle_deg": round(half_ang * 2.0, 4),
            "taper_in_per_ft": round(tpf, 4),
            "spec_notes": spec
        })

    return {
        "meta": {
            "domain": "tapers",
            "standard_org": "ISO/ASME/DIN",
            "standard_id": "SPINDLE_COLLETS",
            "standard_title": "Machine Tool Spindles and Collets",
            "unit_system": "hybrid",
            "version": "1.0.0"
        },
        "items": items
    }

# -----------------------------------------------------------------------------
# 6. Taper Pins & Centers (ISO 2339 / ASME B18.8.2 / Chamfers)
# -----------------------------------------------------------------------------

def generate_pins_and_centers():
    items = [
        {
            "id": "pin_1_50_metric",
            "designation": "1:50 Metric Taper Pin",
            "series": "PIN",
            "choice": 1,
            "form": "external",
            "standard_org": "ISO",
            "standard_id": "2339",
            "cone_ratio": 0.020000,
            "half_angle_deg": 0.5729,
            "included_angle_deg": 1.1459,
            "taper_in_per_ft": 0.2400,
            "spec_notes": "ISO 2339 / DIN 1 (1:50)"
        },
        {
            "id": "pin_1_48_us",
            "designation": "1:48 US Taper Pin",
            "series": "PIN",
            "choice": 1,
            "form": "external",
            "standard_org": "ASME",
            "standard_id": "B18.8.2",
            "cone_ratio": 0.020833,
            "half_angle_deg": 0.5968,
            "included_angle_deg": 1.1937,
            "taper_in_per_ft": 0.2500,
            "spec_notes": "1/4 inch per foot (ASME B18.8.2)"
        },
        {
            "id": "center_60_deg",
            "designation": "60° Lathe Center",
            "series": "CENTER",
            "choice": 1,
            "form": "external",
            "standard_org": "ISO",
            "standard_id": "STANDARD",
            "cone_ratio": 1.154701,
            "half_angle_deg": 30.0000,
            "included_angle_deg": 60.0000,
            "spec_notes": "Standard 60° included live/dead center"
        },
        {
            "id": "chamfer_45_deg",
            "designation": "45° Lead Chamfer",
            "series": "CHAMFER",
            "choice": 1,
            "form": "external",
            "standard_org": "ISO",
            "standard_id": "STANDARD",
            "cone_ratio": 2.000000,
            "half_angle_deg": 45.0000,
            "included_angle_deg": 90.0000,
            "spec_notes": "1:1 slope 45° corner break"
        },
        {
            "id": "bevel_30_deg",
            "designation": "30° Lead Bevel",
            "series": "CHAMFER",
            "choice": 2,
            "form": "external",
            "standard_org": "ISO",
            "standard_id": "STANDARD",
            "cone_ratio": 1.154701,
            "half_angle_deg": 30.0000,
            "included_angle_deg": 60.0000,
            "spec_notes": "30° lead angle bevel"
        }
    ]

    return {
        "meta": {
            "domain": "tapers",
            "standard_org": "ISO/ASME",
            "standard_id": "PINS_CENTERS",
            "standard_title": "Standard Taper Pins, Chamfers & Lathe Centers",
            "unit_system": "hybrid",
            "version": "1.0.0"
        },
        "items": items
    }

# -----------------------------------------------------------------------------
# Main Generation Runner
# -----------------------------------------------------------------------------

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.abspath(os.path.join(script_dir, ".."))
    presets_dir = os.path.join(project_dir, "data", "presets")

    threads_dir = os.path.join(presets_dir, "threads")
    tapers_dir = os.path.join(presets_dir, "tapers")

    os.makedirs(threads_dir, exist_ok=True)
    os.makedirs(tapers_dir, exist_ok=True)

    catalog = [
        (os.path.join(threads_dir, "iso_metric.json"), generate_metric_threads()),
        (os.path.join(threads_dir, "unified_national.json"), generate_unified_threads()),
        (os.path.join(threads_dir, "pipe_npt.json"), generate_npt_threads()),
        (os.path.join(tapers_dir, "morse_din228.json"), generate_morse_tapers()),
        (os.path.join(tapers_dir, "collets_spindles.json"), generate_collets_spindles()),
        (os.path.join(tapers_dir, "pins_and_centers.json"), generate_pins_and_centers())
    ]

    for filepath, data in catalog:
        with open(filepath, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)
        item_count = len(data.get("items", []))
        rel_path = os.path.relpath(filepath, project_dir)
        print(f"[generate_presets] Wrote {rel_path} ({item_count} items)")

if __name__ == "__main__":
    main()
