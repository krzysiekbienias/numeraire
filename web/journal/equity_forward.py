"""Equity forward closed-form (per-share), aligned with mathematical_background.md."""

from __future__ import annotations

import math
from dataclasses import dataclass


@dataclass(frozen=True)
class ForwardResult:
    pv_unit: float
    delta: float  # ∂V/∂S = e^{-qτ}
    theta: float  # per calendar year (∂V/∂t)


def equity_forward(
    spot: float,
    forward_price: float,
    tau: float,
    rate: float,
    div: float,
) -> ForwardResult:
    """Long physical forward: receive S_T, pay K. No volatility."""
    s, k = float(spot), float(forward_price)
    t, r, q = float(tau), float(rate), float(div)
    if s <= 0:
        raise ValueError('spot must be positive')

    if t <= 0:
        return ForwardResult(pv_unit=s - k, delta=1.0, theta=0.0)

    eq = math.exp(-q * t)
    er = math.exp(-r * t)
    pv = s * eq - k * er
    delta = eq
    # τ = T − t → Θ = −∂V/∂τ = q S e^{-qτ} − r K e^{-rτ}
    theta = q * s * eq - r * k * er
    return ForwardResult(pv_unit=pv, delta=delta, theta=theta)
