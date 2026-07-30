"""One-step GBM Monte Carlo for European vanilla (sandbox what-if).

Risk-neutral: ``S_T = S exp((r-q-½σ²)τ + σ√τ Z)``, payoff discounted by ``e^{-rτ}``.
Antithetic normals for variance reduction. Pure Python — not the C++ CCR path engine.
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass


@dataclass(frozen=True)
class McResult:
    pv_unit: float
    stderr: float  # std error of the discounted mean (per share)
    n_paths: int
    seed: int


def monte_carlo_vanilla(
    spot: float,
    strike: float,
    tau: float,
    vol: float,
    rate: float,
    div: float,
    *,
    is_call: bool,
    n_paths: int = 20_000,
    seed: int = 42,
) -> McResult:
    s, k = float(spot), float(strike)
    t, sig = float(tau), float(vol)
    r, q = float(rate), float(div)
    n = max(2, int(n_paths))
    # Pair antithetic draws → even count of terminal samples.
    n_pairs = max(1, n // 2)

    if s <= 0 or k <= 0:
        raise ValueError('spot and strike must be positive')

    if t <= 0:
        intrinsic = max(s - k, 0.0) if is_call else max(k - s, 0.0)
        return McResult(pv_unit=intrinsic, stderr=0.0, n_paths=0, seed=seed)

    rng = random.Random(int(seed))
    drift = (r - q - 0.5 * sig * sig) * t
    vol_sqrt = sig * math.sqrt(t)
    df = math.exp(-r * t)

    # Welford online mean / M2 over antithetic pair averages (unbiased stderr of mean).
    mean = 0.0
    m2 = 0.0
    count = 0

    for _ in range(n_pairs):
        z = rng.gauss(0.0, 1.0)
        st_pos = s * math.exp(drift + vol_sqrt * z)
        st_neg = s * math.exp(drift - vol_sqrt * z)
        if is_call:
            pay = 0.5 * (max(st_pos - k, 0.0) + max(st_neg - k, 0.0))
        else:
            pay = 0.5 * (max(k - st_pos, 0.0) + max(k - st_neg, 0.0))
        x = df * pay
        count += 1
        delta = x - mean
        mean += delta / count
        m2 += delta * (x - mean)

    stderr = math.sqrt(m2 / (count * (count - 1))) if count > 1 else 0.0
    return McResult(pv_unit=mean, stderr=stderr, n_paths=n_pairs * 2, seed=int(seed))
