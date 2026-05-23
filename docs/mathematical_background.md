# Numeraire++ — mathematical background

Living reference for pricing formulas, conventions, and notation used in the codebase.
For module wiring and persistence see [`architecture.md`](architecture.md); for sprint status see [`development.md`](development.md).

**Preview tip:** display equations use `$$ … $$` blocks (GitHub, Markdown Preview Enhanced, Markdown+Math). Inline math uses `$ … $`.

---

## Notation (aligned with code)

| Symbol | Meaning | Code / config |
| ------ | ------- | ------------- |
| $S$ | Spot price of the underlying | `market.Spot()` |
| $K$ | Strike | `vanilla.Strike()` |
| $r$ | Risk-free rate (continuously compounded) | `market.RiskFreeRate()`, `NUMERAIRE_DEV_RATE` |
| $q$ | Dividend yield on the underlying (continuous) | `market.DividendYield()`, `NUMERAIRE_DEV_DIV_YIELD` |
| $\sigma$ | Implied volatility (absolute, not %) | `market.ImpliedVolatility()`, `NUMERAIRE_DEV_VOL` |
| $\tau$ | Time to expiry (year fraction) | `Act365FixedYearFraction(ValuationDate, ExpiryDate)` |
| $\Phi(\cdot)$ | Standard normal CDF | `NormCdf` in [`analytic_black_scholes_equity_pricer.cpp`](../src/pricers/analytic_black_scholes_equity_pricer.cpp) |
| $\phi(\cdot)$ | Standard normal PDF | `NormPdf` (same file) |

Subscripts $d_1$, $d_2$ below are the usual Black–Scholes auxiliary variables, not database columns.

---

## Black–Scholes–Merton (European vanilla on equity)

**Scope in repo:** [`AnalyticBlackScholesEquityPricer`](../include/numeraire/pricers/analytic_black_scholes_equity_pricer.hpp) — European calls and puts only; flat $r$, $q$, $\sigma$ over $\tau$; no American exercise.

### Auxiliary terms

$$
\sigma\sqrt{\tau}
$$

$$
d_1 = \frac{\ln(S/K) + (r - q + \tfrac{1}{2}\sigma^2)\,\tau}{\sigma\sqrt{\tau}}
$$

$$
d_2 = d_1 - \sigma\sqrt{\tau}
$$

### Present value (per one share)

**Call:**

$$
V_{\mathrm{call}} = S\,e^{-q\tau}\,\Phi(d_1) - K\,e^{-r\tau}\,\Phi(d_2)
$$

**Put:**

$$
V_{\mathrm{put}} = K\,e^{-r\tau}\,\Phi(-d_2) - S\,e^{-q\tau}\,\Phi(-d_1)
$$

These match `BlackScholesNpv` in the pricer implementation.

### Expiry and degenerate cases

- **$\tau \le 0$:** $V = \max(S-K, 0)$ (call) or $\max(K-S, 0)$ (put) — intrinsic only (`IntrinsicNpv`).
- **$\sigma \le 0$:** forward intrinsic with discounting (`DiscountedForwardIntrinsic`), using $F = S\,e^{(r-q)\tau}$.

---

## Greeks (same conventions as persisted MTM)

Sensitivities are w.r.t. **absolute** $\sigma$ and **calendar** time; `theta` is **per year** (not per day). Unit greeks are **per share**; position totals multiply by $M = \mathrm{sign}(\mathrm{direction}) \times \mathrm{quantity} \times \mathrm{contract\_size}$ — see [`architecture.md`](architecture.md) § *EOD MTM — unit vs position columns*.

Let $E_q = e^{-q\tau}$, $E_r = e^{-r\tau}$.

**Delta**

$$
\Delta_{\mathrm{call}} = E_q\,\Phi(d_1), \qquad
\Delta_{\mathrm{put}} = -E_q\,\Phi(-d_1)
$$

**Gamma** (same for call and put)

$$
\Gamma = \frac{E_q\,\phi(d_1)}{S\,\sigma\sqrt{\tau}}
$$

**Vega** (derivative w.r.t. $\sigma$, not “per 1% vol”)

$$
\mathcal{V} = S\,E_q\,\phi(d_1)\,\sqrt{\tau}
$$

**Theta** (time decay, **per calendar year**)

Call:

$$
\Theta_{\mathrm{call}} =
-\frac{S\,\phi(d_1)\,\sigma\,E_q}{2\sqrt{\tau}}
- r\,K\,E_r\,\Phi(d_2)
+ q\,S\,E_q\,\Phi(d_1)
$$

Put:

$$
\Theta_{\mathrm{put}} =
-\frac{S\,\phi(d_1)\,\sigma\,E_q}{2\sqrt{\tau}}
+ r\,K\,E_r\,\Phi(-d_2)
- q\,S\,E_q\,\Phi(-d_1)
$$

**Rho** (derivative w.r.t. $r$)

$$
\rho_{\mathrm{call}} = K\,\tau\,E_r\,\Phi(d_2), \qquad
\rho_{\mathrm{put}} = -K\,\tau\,E_r\,\Phi(-d_2)
$$

Unit tests benchmark NPV and greeks against `QuantLib::BlackCalculator` with the same $\tau$ convention.

---

## From model price to book columns (reminder)

Per-share model output is `pv_unit` $= V$ above. Position mark:

$$
\texttt{pv\_total} = M \times \texttt{pv\_unit}
$$

PnL conventions (position-level): [`architecture.md`](architecture.md) § *EOD MTM — PnL columns*.

---

## Planned extensions (placeholder)

Future sections may cover: asset-or-nothing payoffs, local/stochastic vol, American bounds, Monte Carlo GBM, etc., as corresponding pricers land in `src/pricers/`.
