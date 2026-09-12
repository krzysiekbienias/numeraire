"""Commodity futures forward closed-form (per unit of the listed contract)."""

from __future__ import annotations

import math
from dataclasses import dataclass


@dataclass(frozen=True)
class CommodityFuturesForwardResult:
    pv_unit: float
    delta: float  # ∂V/∂F = e^{-rτ}


def commodity_futures_forward(
    futures_price: float,
    strike: float,
    tau: float,
    rate: float,
) -> CommodityFuturesForwardResult:
    """Uncollateralized lock: PV = e^{-rτ}(F − K). No dividend yield."""
    f, k = float(futures_price), float(strike)
    t, r = float(tau), float(rate)
    if f <= 0:
        raise ValueError('futures price must be positive')
    if k <= 0:
        raise ValueError('strike K must be positive')

    if t <= 0:
        return CommodityFuturesForwardResult(pv_unit=f - k, delta=1.0)

    df = math.exp(-r * t)
    return CommodityFuturesForwardResult(pv_unit=df * (f - k), delta=df)
