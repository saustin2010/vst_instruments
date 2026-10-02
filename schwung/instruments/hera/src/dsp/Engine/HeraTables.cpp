// SPDX-License-Identifier: GPL-3.0-or-later

#include "HeraTables.h"
#include <cmath>

#define DEFTABLE(id, min, max, ...)                  \
    static const float data__##id[] = __VA_ARGS__;   \
    const LerpTable id(data__##id, min, max, false)

DEFTABLE(curveFromLfoRateSliderToFreq, 0.0, 1.0, {0.3, 0.85, 3.39, 11.49, 22.22});
DEFTABLE(curveFromLfoDelaySliderToDelay, 0.0, 1.0, {0.0, 0.0639, 0.85, 1.2, 2.685});
DEFTABLE(curveFromLfoDelaySliderToAttack, 0.0, 1.0, {0.001, 0.053, 0.188, 0.348, 1.15});

DEFTABLE(curveFromHpfSliderToFreq, 0.0, 1.0, {140, 250, 520, 1220});

DEFTABLE(curveFromAttackSliderToDuration, 0.0, 1.0, {0.001, 0.03, 0.24, 0.65, 3.25});
DEFTABLE(curveFromDecaySliderToDuration, 0.0, 1.0, {0.002, 0.096, 0.984, 4.449, 19.783});
DEFTABLE(curveFromReleaseSliderToDuration, 0.0, 1.0, {0.002, 0.096, 0.984, 4.449, 19.783});

const LerpTable curveSoftClipTanh3(
    [](double x) -> double { return std::tanh(3.0 * x); },
    -1.0, 1.0, 128);
const LerpTable curveSoftClipCubic(
    [](double x) -> double { return x - x * x * x / 3.0; },
    -1.0, 1.0, 128);

const LerpTable curveSineLFO(
    [](double x) -> double { return std::sin(2.0 * M_PI * x); },
    0.0, 1.0, 128);
