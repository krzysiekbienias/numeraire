"""Instrument inventory: catalog codes vs what ProductFactory can price today."""

from __future__ import annotations

# `products_equity.instrument_type` / catalog `maps_to_instrument_type` keys that
# `ProductFactory` accepts (see src/products/product_factory.cpp).
PRICED_MAPS_TO = frozenset(
    {
        'plain_vanilla_european_option',
        'asset_or_nothing',
        'binary_cash_or_nothing',
        'digital',
        'equity_forward',
    }
)

# NPV-only in v1 MTM (greeks zero-filled) — still priceable.
NPV_ONLY_MAPS_TO = frozenset(
    {
        'asset_or_nothing',
        'binary_cash_or_nothing',
        'digital',
        'equity_forward',
    }
)


def is_priceable(maps_to: str) -> bool:
    return (maps_to or '').strip().lower() in PRICED_MAPS_TO


def pricing_notes(maps_to: str) -> str:
    key = (maps_to or '').strip().lower()
    if key not in PRICED_MAPS_TO:
        return 'Not wired in ProductFactory yet'
    if key in NPV_ONLY_MAPS_TO:
        return 'Priced · NPV only (greeks zero in MTM)'
    if key == 'plain_vanilla_european_option':
        return 'Priced · analytic BS + greeks'
    return 'Priced'
