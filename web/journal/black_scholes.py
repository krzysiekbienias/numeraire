"""European Black–Scholes–Merton (per-share), aligned with mathematical_background.md.

Sandbox / what-if only — not the official C++ MTM path.
"""

from __future__ import annotations

import math
from dataclasses import dataclass


def _norm_cdf(x: float) -> float:
    return 0.5 * (1.0 + math.erf(x / math.sqrt(2.0)))


def _norm_pdf(x: float) -> float:
    return math.exp(-0.5 * x * x) / math.sqrt(2.0 * math.pi)


@dataclass(frozen=True)
class BsResult:
    pv_unit: float
    delta: float
    gamma: float
    vega: float
    theta: float  # per calendar year
    rho: float


def black_scholes(
    spot: float,
    strike: float,
    tau: float,
    vol: float,
    rate: float,
    div: float,
    *,
    is_call: bool,
) -> BsResult:
    """Price one European vanilla (per share of underlying)."""
    s, k = float(spot), float(strike)
    t, sig = float(tau), float(vol)
    r, q = float(rate), float(div)

    if s <= 0 or k <= 0:
        raise ValueError('spot and strike must be positive')

    if t <= 0:
        intrinsic = max(s - k, 0.0) if is_call else max(k - s, 0.0)
        # Digital delta at expiry (ATM convention = 0.5 / −0.5).
        if is_call:
            if s > k:
                dig = 1.0
            elif s < k:
                dig = 0.0
            else:
                dig = 0.5
        else:
            if s < k:
                dig = -1.0
            elif s > k:
                dig = 0.0
            else:
                dig = -0.5
        return BsResult(
            pv_unit=intrinsic, delta=dig, gamma=0.0, vega=0.0, theta=0.0, rho=0.0
        )

    if sig <= 0:
        # Discounted forward intrinsic (σ→0).
        forward = s * math.exp((r - q) * t)
        df_r = math.exp(-r * t)
        df_q = math.exp(-q * t)
        if is_call:
            pv = df_r * max(forward - k, 0.0)
        else:
            pv = df_r * max(k - forward, 0.0)
        # Rough greeks at zero vol: skip; UI cares about PV shocks mainly.
        return BsResult(pv_unit=pv, delta=0.0, gamma=0.0, vega=0.0, theta=0.0, rho=0.0)

    sqrt_t = math.sqrt(t)
    d1 = (math.log(s / k) + (r - q + 0.5 * sig * sig) * t) / (sig * sqrt_t)
    d2 = d1 - sig * sqrt_t
    eq = math.exp(-q * t)
    er = math.exp(-r * t)
    nd1 = _norm_cdf(d1)
    nd2 = _norm_cdf(d2)
    nmd1 = _norm_cdf(-d1)
    nmd2 = _norm_cdf(-d2)
    pdf_d1 = _norm_pdf(d1)

    if is_call:
        pv = s * eq * nd1 - k * er * nd2
        delta = eq * nd1
        theta = (
            -s * pdf_d1 * sig * eq / (2.0 * sqrt_t)
            - r * k * er * nd2
            + q * s * eq * nd1
        )
        rho = k * t * er * nd2
    else:
        pv = k * er * nmd2 - s * eq * nmd1
        delta = -eq * nmd1
        theta = (
            -s * pdf_d1 * sig * eq / (2.0 * sqrt_t)
            + r * k * er * nmd2
            - q * s * eq * nmd1
        )
        rho = -k * t * er * nmd2

    gamma = eq * pdf_d1 / (s * sig * sqrt_t)
    vega = s * eq * pdf_d1 * sqrt_t
    return BsResult(pv_unit=pv, delta=delta, gamma=gamma, vega=vega, theta=theta, rho=rho)


def position_sign(direction: str) -> float:
    d = (direction or '').strip().upper()
    if d == 'SHORT':
        return -1.0
    return 1.0


def scale_unit(direction: str, quantity: float, contract_size: float, unit: float) -> float:
    return position_sign(direction) * float(quantity) * float(contract_size) * float(unit)
