# Configuration files

JSON configurations consumed by `numeraire::utils::Config` (Sprint 2+).

## Files

- **`default.json`** — baseline configuration committed to the repo. Acts as
  the source of defaults and as documentation of every supported key. Loaded
  by `Config::Load("configs/default.json")`.
  Notable pricing keys: `pricing.monte_carlo.*`, `pricing.binomial.default_steps`
  (CRR / American tree density; read by `BinomialBlackScholesEquityPricer`,
  overridable via `NUMERAIRE_BINOMIAL_STEPS`; early exercise at every step).
- **`option_universe_grid.json`** — parametric expiry pillars and OTM % levels for
  `dev_main --build-option-universe` (see [`docs/volatility_surface.md`](../docs/volatility_surface.md)).

## Conventions

- Keys are `snake_case`.
- Sections marked `"_comment_"` are placeholders documenting future modules.
- Values that need to change per developer / environment go into `.env`
  instead (see `.env.example`).

## Loading order (Stage 2+)

1. `configs/default.json` — committed defaults
2. `configs/local.json`   — optional, gitignored, overrides defaults
3. Environment variables — final override (e.g. `NUMERAIRE_LOG_LEVEL`)
