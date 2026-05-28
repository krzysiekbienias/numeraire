# Numeraire++ — mathematical background

Living reference for pricing formulas, conventions, and notation used in the codebase.
For module wiring and persistence see [`architecture.md`](architecture.md); for sprint status see [`development.md`](development.md).

> **Preview (Cursor / VS Code)**
>
> - Open **`Ctrl+K V`** (*Markdown: Open Preview to the Side*). Requires built-in **Markdown Math** (`@builtin markdown-math`) and `"markdown.math.enabled": true` in [`.vscode/settings.json`](../.vscode/settings.json).
> - **Do not** use the tab **Preview** (magnifying glass) — it often skips KaTeX and can corrupt `\(` `\)` / `$$` in the source file.
> - Optional: **Markdown Preview Enhanced** → *Open Preview to the Side*.

**Convention in this file:** display math uses single-line `$$…$$` blocks; inline math uses `\(...\)`. Avoid math inside Markdown tables.

---

## Notation (aligned with code)

Symbols used below:

$$S,\ K,\ r,\ q,\ \sigma,\ \tau,\ \Phi(\cdot),\ \phi(\cdot),\ d_1,\ d_2,\ M,\ E_q,\ E_r$$

| LaTeX | Meaning | Code / config |
| ----- | ------- | ------------- |
| see above | Spot | `market.Spot()` |
| see above | Strike | `vanilla.Strike()` |
| see above | Risk-free rate (continuous) | `market.RiskFreeRate()`, `NUMERAIRE_DEV_RATE` |
| see above | Dividend yield (continuous) | `market.DividendYield()`, `NUMERAIRE_DEV_DIV_YIELD` |
| see above | Implied vol (absolute, not %) | `market.ImpliedVolatility()`, `NUMERAIRE_DEV_VOL` |
| see above | Time to expiry (Act/365 year fraction) | `Act365FixedYearFraction(ValuationDate, ExpiryDate)` |
| see above | Standard normal CDF / PDF | `NormCdf`, `NormPdf` in [`analytic_black_scholes_equity_pricer.cpp`](../src/pricers/analytic_black_scholes_equity_pricer.cpp) |

Position multiplier (same as [`LegPvTotal`](../include/numeraire/database/leg_pv.hpp)):

$$M = \mathrm{sign}(\mathrm{direction}) \times \mathrm{quantity} \times \mathrm{contract\_size}$$

Subscripts \(d_1\), \(d_2\) are the usual Black–Scholes auxiliary variables, not database columns.

---

## Black–Scholes–Merton (European vanilla on equity)

**Scope in repo:** [`AnalyticBlackScholesEquityPricer`](../include/numeraire/pricers/analytic_black_scholes_equity_pricer.hpp) — European calls and puts; flat \(r\), \(q\), \(\sigma\) over \(\tau\); no American exercise. Invoked via [`AnalyticCompositePricer`](../include/numeraire/pricers/analytic_composite_pricer.hpp) for vanilla legs (same factory entry as binaries below).

### Auxiliary terms

$$\sigma\sqrt{\tau}$$

$$d_1 = \frac{\ln(S/K) + \bigl(r - q + \tfrac{1}{2}\sigma^2\bigr)\,\tau}{\sigma\sqrt{\tau}}$$

$$d_2 = d_1 - \sigma\sqrt{\tau}$$

### Present value (per one share)

**Call:**

$$V_{\mathrm{call}} = S\,e^{-q\tau}\,\Phi(d_1) - K\,e^{-r\tau}\,\Phi(d_2)$$

**Put:**

$$V_{\mathrm{put}} = K\,e^{-r\tau}\,\Phi(-d_2) - S\,e^{-q\tau}\,\Phi(-d_1)$$

These match `BlackScholesNpv` in the pricer implementation.

### Expiry and degenerate cases

- **\(\tau \le 0\):** \(V = \max(S-K,\,0)\) (call) or \(\max(K-S,\,0)\) (put) — intrinsic only (`IntrinsicNpv`).
- **\(\sigma \le 0\):** forward intrinsic with discounting (`DiscountedForwardIntrinsic`), using \(F = S\,e^{(r-q)\tau}\).

### Implied-vol inversion (European vanilla)

**Scope in repo:** [`ImpliedVolEuropeanVanilla`](../src/quant/implied_vol_european.cpp) in `numeraire_quant`, used by EOD surface build flow.

Given observed premium \(V_{\mathrm{mkt}}\) (per one underlying unit), solve:

$$V_{\mathrm{BS}}(\sigma; S, K, r, q, \tau) = V_{\mathrm{mkt}}$$

Implementation is a **hybrid solver**:

- **Primary:** Newton-Raphson update

$$\sigma_{n+1} = \sigma_n - \frac{V_{\mathrm{BS}}(\sigma_n) - V_{\mathrm{mkt}}}{\mathcal{V}(\sigma_n)}$$

- **Fallback:** bisection on \([\sigma_{\min}, \sigma_{\max}]\) when Newton stalls or becomes numerically unsafe (e.g., very small vega / poor local conditioning).

Why hybrid: Newton is typically much faster near the root; bisection is slower but robust once a valid bracket exists. This balances throughput and stability on large market batches.

Pre-checks reject impossible or unstable points before solve (invalid inputs, below intrinsic, outside bracket at \(\sigma_{\max}\)); status is returned as `kInvalidInputs`, `kBelowIntrinsic`, `kNoConvergence`, or `kOk`.

---

## Greeks (vanilla only; same conventions as persisted MTM)

**Scope:** [`AnalyticBlackScholesEquityPricer`](../include/numeraire/pricers/analytic_black_scholes_equity_pricer.hpp) for European vanilla only. Asset-or-nothing, cash-or-nothing, and equity forward pricers return **NPV only** in v1 (MTM greek columns are zero-filled).

Sensitivities are w.r.t. **absolute** \(\sigma\) and **calendar** time; `theta` is **per year** (not per day). Unit greeks are **per share**; position totals multiply by \(M\) — see [`architecture.md`](architecture.md) § *EOD MTM — unit vs position columns*.

$$E_q = e^{-q\tau}, \qquad E_r = e^{-r\tau}$$

**Delta**

$$\Delta_{\mathrm{call}} = E_q\,\Phi(d_1), \qquad \Delta_{\mathrm{put}} = -E_q\,\Phi(-d_1)$$

**Gamma** (same for call and put)

$$\Gamma = \frac{E_q\,\phi(d_1)}{S\,\sigma\sqrt{\tau}}$$

**Vega** (derivative w.r.t. \(\sigma\), not “per 1% vol”)

$$\mathcal{V} = S\,E_q\,\phi(d_1)\,\sqrt{\tau}$$

**Theta** (time decay, **per calendar year**)

$$\Theta_{\mathrm{call}} = -\frac{S\,\phi(d_1)\,\sigma\,E_q}{2\sqrt{\tau}} - r\,K\,E_r\,\Phi(d_2) + q\,S\,E_q\,\Phi(d_1)$$

$$\Theta_{\mathrm{put}} = -\frac{S\,\phi(d_1)\,\sigma\,E_q}{2\sqrt{\tau}} + r\,K\,E_r\,\Phi(-d_2) - q\,S\,E_q\,\Phi(-d_1)$$

**Rho** (derivative w.r.t. \(r\))

$$\rho_{\mathrm{call}} = K\,\tau\,E_r\,\Phi(d_2), \qquad \rho_{\mathrm{put}} = -K\,\tau\,E_r\,\Phi(-d_2)$$

Unit tests benchmark NPV and greeks against `QuantLib::BlackCalculator` with the same \(\tau\) convention.

---

## From model price to book columns (reminder)

Per-share model output is `pv_unit` \(= V\) above. Position mark:

$$\texttt{pv\_total} = M \times \texttt{pv\_unit}$$

PnL conventions (position-level): [`architecture.md`](architecture.md) § *EOD MTM — PnL columns*.

---

## Asset-or-nothing (European, equity)

**Scope in repo:** [`EquityAssetOrNothingProduct`](../include/numeraire/products/equity_asset_or_nothing_product.hpp) priced by [`AnalyticBlackScholesEquityPricer`](../include/numeraire/pricers/analytic_black_scholes_equity_pricer.hpp). Pays **spot at expiry** \(S_T\) if the option finishes in the money (catalog family **AON**).

Use the same \(d_1\) as vanilla Black–Scholes.

**Call** (pays \(S_T\) if \(S_T > K\)):

$$V_{\mathrm{AON,call}} = S\,e^{-q\tau}\,\Phi(d_1)$$

**Put** (pays \(S_T\) if \(S_T < K\)):

$$V_{\mathrm{AON,put}} = S\,e^{-q\tau}\,\Phi(-d_1)$$

**Expiry / degenerate cases** (same pattern as vanilla):

- **\(\tau \le 0\):** intrinsic — call pays \(S\) if \(S > K\), else 0; put pays \(S\) if \(S < K\), else 0.
- **\(\sigma \le 0\):** forward test on \(F = S\,e^{(r-q)\tau}\) with spot scaled by \(e^{-q\tau}\).

---

## Cash-or-nothing (European, equity)

**Scope in repo:** [`EquityCashOrNothingProduct`](../include/numeraire/products/equity_cash_or_nothing_product.hpp) priced by [`AnalyticBlackScholesEquityPricer`](../include/numeraire/pricers/analytic_black_scholes_equity_pricer.cpp). Pays fixed cash **\(Q\)** per share if ITM (`structured_params.cash_payout_per_share` in JSON import; catalog family **BIN** / `binary_cash_or_nothing`).

Use the same \(d_1\), \(d_2\) as vanilla.

**Call** (pays \(Q\) if \(S_T > K\)):

$$V_{\mathrm{CON,call}} = Q\,e^{-r\tau}\,\Phi(d_2)$$

**Put** (pays \(Q\) if \(S_T < K\)):

$$V_{\mathrm{CON,put}} = Q\,e^{-r\tau}\,\Phi(-d_2)$$

**Expiry / degenerate cases:**

- **\(\tau \le 0\):** pays \(Q\) on intrinsic ITM, else 0.
- **\(\sigma \le 0\):** forward ITM test with cash discounted by \(e^{-r\tau}\).

---

## Equity forward (European delivery)

**Scope in repo:** [`EquityForwardProduct`](../include/numeraire/products/equity_forward_product.hpp) priced by [`AnalyticForwardPricer`](../include/numeraire/pricers/analytic_forward_pricer.hpp) (routed by [`AnalyticCompositePricer`](../include/numeraire/pricers/analytic_composite_pricer.hpp)). Long physical forward: receive \(S_T\), pay fixed **forward price** \(K\) (stored as `products_equity.strike`; catalog **EQF**). **No volatility** enters the formula.

**Present value (per one share):**

$$V_{\mathrm{fwd}} = S\,e^{-q\tau} - K\,e^{-r\tau}$$

**Expiry:**

- **\(\tau \le 0\):** \(V_{\mathrm{fwd}} = S - K\) (undiscounted intrinsic).

---

## Planned extensions (placeholder)

Future sections may cover: local/stochastic vol, American bounds, Monte Carlo GBM, FX forwards (**FXF**), FRAs (**IRF**), etc., as corresponding pricers land in `src/pricers/`.

**Scope:** Sparse EOD points built at surface ingest; loaded from SQLite; interpolated in `InterpolateImpliedVol`; consumed by `SqliteVolSurfaceMarketData` via `IMarketData::ImpliedVolatility`.

**Out of scope:** SVI/SSVI calibration, arbitrage-free projection, American vol. We use **piecewise linear** interpolation on inverted EOD points only.

---

### Coordinates

Each grid point has:

- **Tenor** $\tau$ — `years_to_maturity` (Act/365 year fraction, same basis as pricers: valuation date → expiry).
- **Log-moneyness**

$$
m = \ln\frac{K}{S_{\mathrm{ref}}}
$$

$S_{\mathrm{ref}}$ = `spot_used` on the surface header (fixed at build/as-of). It need not equal live spot at MTM.

For a leg being priced:

$$
m_{\mathrm{leg}} = \ln\frac{K_{\mathrm{leg}}}{S_{\mathrm{ref}}}
$$

$K_{\mathrm{leg}}$ = product strike; $\tau_{\mathrm{leg}}$ = pricer time to expiry.

---

### Call vs put legs

- Call quotes → **call** grid; put quotes → **put** grid.
- `ImpliedVolatility(..., option_kind)` selects the grid for the product side.
- No put–call parity blend of $\sigma$ at runtime.
- Only rows with `quality = ok` are used.

---

### Within one expiry slice (fixed $\tau$)

Points with the same $\tau$ (bucket tolerance $10^{-8}$) form one **smile slice**.

1. Sort points by $m$.
2. **Inside** $[m_{\min}, m_{\max}]$: $\sigma$ is **linear in** $m$ between adjacent nodes.
3. **Outside** the smile: **flat extrapolation** — use $\sigma$ at the nearest wing ($m \le m_{\min}$ or $m \ge m_{\max}$).
4. **Single point** in the slice: return that $\sigma$.

No cubic spline, no SVI.

---

### Across tenors

Distinct pillars $\tau_1 < \tau_2 < \cdots < \tau_N$. Let $\sigma_{\tau}(m)$ denote the slice interpolator above.

**One pillar only**

$$
\sigma(m_{\mathrm{leg}}, \tau_{\mathrm{leg}}) = \sigma_{\tau_1}(m_{\mathrm{leg}})
$$

**Below shortest pillar** ($\tau_{\mathrm{leg}} < \tau_1$)

$$
\sigma(m_{\mathrm{leg}}, \tau_{\mathrm{leg}}) = \sigma_{\tau_1}(m_{\mathrm{leg}})
$$

**Above longest pillar** ($\tau_{\mathrm{leg}} > \tau_N$)

$$
\sigma(m_{\mathrm{leg}}, \tau_{\mathrm{leg}}) = \sigma_{\tau_N}(m_{\mathrm{leg}})
$$

**Between** $\tau_j < \tau_{\mathrm{leg}} < \tau_{j+1}$

$$
\sigma(m_{\mathrm{leg}}, \tau_{\mathrm{leg}}) = w\,\sigma_{\tau_j}(m_{\mathrm{leg}}) + (1 - w)\,\sigma_{\tau_{j+1}}(m_{\mathrm{leg}})
$$

$$
w = \frac{\tau_{j+1} - \tau_{\mathrm{leg}}}{\tau_{j+1} - \tau_j}
$$

**Summary:** interpolate each smile, then **linear blend in** $\tau$; **flat** extension beyond end pillars. This is a **separable** scheme (smile, then tenor), **not** full bilinear interpolation on a filled $(m,\tau)$ rectangle.

---

### Edge cases

- Empty grid → error.
- Non-positive $\tau$ inside the interpolator → error.
- Non-positive $\tau$ in the SQLite market-data provider → returns $0$ (pricer may use intrinsic / zero-vol branch).
- Missing surface or empty call/put leg → error on load or query.
- Static dev provider (flat env vol): ignores strike, $\tau$, and call/put; always returns `NUMERAIRE_DEV_VOL`.

---

### Link to Black–Scholes pricing

Returned $\sigma$ is treated as **constant over the leg’s** $\tau_{\mathrm{leg}}$: one `ImpliedVolatility` call per price; no smile inside the BS integral.

**Dev MTM today:** still flat vol from env unless `VOL_SOURCE=db` is wired in `dev_main`.

---

### Short notation cheat sheet (for your repo table later)

| Symbol | Meaning |
|--------|---------|
| $S_{\mathrm{ref}}$ | `spot_used` on surface header |
| $m_{\mathrm{leg}}$ | $\ln(K_{\mathrm{leg}}/S_{\mathrm{ref}})$ |
| $\tau_{\mathrm{leg}}$ | Act/365 year fraction to expiry |
| $\sigma_{\tau}(m)$ | Linear-in-$m$ smile at fixed $\tau$ |
| $w$ | Tenor blend weight between $\tau_j$ and $\tau_{j+1}$ |
