"""Book a new trade from the Journal without becoming a second catalog writer.

The Journal serialises exactly the bundle shape that
`scripts/import_trade_bundle.py` already validates, drops it in `trades/incoming/`
and shells out to that script — it stays the only code that INSERTs into
`products` / `trades` / `trade_legs`. New trades land as `PENDING`; the C++
`dev_main --price-booking` fills `execution_price` and promotes them to `LIVE`.

Only the instrument types wired here can be booked (PVE, EQF, EQS, IXS, FUT); everything
else in `catalog_instrument_type` stays read-only inventory.

Deletion follows the same rule: `scripts/delete_trade.py` does the DELETE, the
Journal only asks for it. The bundle stays in `trades/incoming/`, so a deleted trade
falls back to "defined but not booked" and the importer can restore it.
"""

from __future__ import annotations

import json
import math
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from datetime import date, datetime
from pathlib import Path

from django.conf import settings

from django.db.models import Q

from journal.models import (
    FuturesContract,
    FuturesProduct,
    Product,
    Trade,
    TradeLeg,
    TradeLegExposureEod,
    TradeLegMtmEod,
    UniverseInstrument,
)

IMPORT_TIMEOUT_SEC = 60
BOOKING_TIMEOUT_SEC = 300
DELETE_TIMEOUT_SEC = 60

# Import only accepts ids in this shape; auto-numbering continues the TRD_1000x series.
_NUMBERED_TRADE_ID = re.compile(r'^TRD_(\d+)$')
_FIRST_TRADE_NUMBER = 10001


@dataclass(frozen=True)
class BookableInstrument:
    """One catalog code the booking form knows how to serialise."""

    code: str
    label: str
    instrument_type: str  # products_equity / products_commodity.instrument_type
    strategy_type: str  # default trades.strategy_type
    product_prefix: str
    asset_kind: str
    has_option_type: bool
    has_strike: bool
    has_expiry: bool
    underlier_asset_class: str  # filter universe_instrument.asset_class
    strike_label: str
    strike_help: str
    default_contract_size: float
    default_settlement: str
    contract_size_help: str
    # 'equity' → products_equity; 'commodity' → products_commodity
    extension: str = 'equity'
    # Commodity futures: pick listed contract ticker (tenor) from futures_contract.
    has_contract_ticker: bool = False


PLAIN_VANILLA_EUROPEAN = BookableInstrument(
    code='PVE',
    label='PVE — Plain Vanilla European Option',
    instrument_type='plain_vanilla_european_option',
    strategy_type='VANILLA_OPTION',
    product_prefix='OPT_PVE',
    asset_kind='EQUITY',
    has_option_type=True,
    has_strike=True,
    has_expiry=True,
    underlier_asset_class='EQUITY',
    strike_label='Strike',
    strike_help='Option strike per share.',
    default_contract_size=100.0,
    default_settlement='CASH',
    contract_size_help='Shares per contract — 100 for listed equity options.',
)

EQUITY_FORWARD = BookableInstrument(
    code='EQF',
    label='EQF — Equity Forward',
    instrument_type='equity_forward',
    strategy_type='EQUITY_FORWARD',
    product_prefix='FWD_EQF',
    asset_kind='EQUITY',
    has_option_type=False,
    has_strike=True,
    has_expiry=True,
    underlier_asset_class='EQUITY',
    strike_label='Forward price',
    strike_help='Agreed delivery price K per share (no call / put on a forward).',
    default_contract_size=1.0,
    default_settlement='CASH',
    contract_size_help='OTC forward: 1 = quantity counts shares.',
)

EQUITY_SPOT = BookableInstrument(
    code='EQS',
    label='EQS — Equity Spot (shares)',
    instrument_type='equity_spot',
    strategy_type='EQUITY_SPOT',
    product_prefix='SPOT_EQS',
    asset_kind='EQUITY',
    has_option_type=False,
    has_strike=False,
    has_expiry=False,
    underlier_asset_class='EQUITY',
    strike_label='Strike',
    strike_help='',
    default_contract_size=1.0,
    default_settlement='PHYSICAL',
    contract_size_help='1 = quantity counts shares. Hedge ≈ option qty × contract_size × Δ.',
)

INDEX_SPOT = BookableInstrument(
    code='IXS',
    label='IXS — Index Spot',
    instrument_type='index_spot',
    strategy_type='INDEX_SPOT',
    product_prefix='SPOT_IXS',
    asset_kind='INDEX',
    has_option_type=False,
    has_strike=False,
    has_expiry=False,
    underlier_asset_class='INDEX',
    strike_label='Strike',
    strike_help='',
    default_contract_size=1.0,
    default_settlement='CASH',
    contract_size_help='1 = quantity counts index units (hedge vs index options).',
)

COMMODITY_FUTURES_OUTRIGHT = BookableInstrument(
    code='FUT',
    label='FUT — Commodity futures outright',
    instrument_type='commodity_futures_outright',
    strategy_type='COMMODITY_FUTURES',
    product_prefix='FUT_OUTRIGHT',
    asset_kind='COMMODITY',
    has_option_type=False,
    has_strike=False,
    has_expiry=True,  # taken from futures_contract.settlement_date
    underlier_asset_class='COMMODITY',
    strike_label='Strike',
    strike_help='',
    default_contract_size=1000.0,
    default_settlement='PHYSICAL',
    contract_size_help='Futures multiplier (e.g. 1000 bbl for CL). Override from product catalog if known.',
    extension='commodity',
    has_contract_ticker=True,
)

BOOKABLE = (
    PLAIN_VANILLA_EUROPEAN,
    EQUITY_FORWARD,
    EQUITY_SPOT,
    INDEX_SPOT,
    COMMODITY_FUTURES_OUTRIGHT,
)


def bookable_instruments() -> list[BookableInstrument]:
    return list(BOOKABLE)


def get_instrument(code: str | None) -> BookableInstrument | None:
    wanted = (code or '').strip().upper()
    return next((spec for spec in BOOKABLE if spec.code == wanted), None)


def repo_root() -> Path:
    return Path(settings.REPO_ROOT)


def incoming_dir() -> Path:
    return repo_root() / 'trades' / 'incoming'


def _db_path() -> str:
    return str(settings.DATABASES['numeraire']['NAME'])


def underlier_choices(asset_class: str | None = None) -> list[tuple[str, str]]:
    """Underliers whose market data the ingest covers (equity/index EOD or futures)."""
    rows = UniverseInstrument.objects.filter(is_active=1)
    if asset_class:
        ac = asset_class.strip().upper()
        rows = rows.filter(asset_class=ac)
        if ac == 'COMMODITY':
            rows = rows.filter(Q(ingest_futures_eod=1) | Q(ingest_futures_product=1))
        elif ac == 'EQUITY':
            rows = rows.filter(ingest_equity_eod=1)
        elif ac == 'INDEX':
            rows = rows.filter(ingest_index_eod=1)
    else:
        rows = rows.filter(
            Q(ingest_equity_eod=1) | Q(ingest_index_eod=1) | Q(ingest_futures_eod=1)
        )
    choices = []
    for row in rows.order_by('asset_class', 'instrument_id'):
        name = (row.display_name or '').strip()
        suffix = f' — {name}' if name else ''
        # Commodity universe uses instrument_id == provider_symbol (CL); show both if they differ.
        sym = (row.provider_symbol or '').strip().upper()
        label_id = row.instrument_id
        if sym and sym != str(label_id).upper():
            label_id = f'{label_id}/{sym}'
        choices.append((row.instrument_id, f'{label_id} ({row.asset_class}){suffix}'))
    return choices


def _latest_futures_listing_as_of(product_code: str) -> date | None:
    raw = (
        FuturesContract.objects.filter(product_code=product_code.upper())
        .order_by('-listing_as_of')
        .values_list('listing_as_of', flat=True)
        .first()
    )
    return raw


def futures_contract_choices(product_code: str | None) -> list[tuple[str, str]]:
    """Listed outright tickers for one commodity product_code (latest listing day)."""
    code = (product_code or '').strip().upper()
    if not code:
        return []
    listing = _latest_futures_listing_as_of(code)
    if listing is None:
        return []
    rows = (
        FuturesContract.objects.filter(product_code=code, listing_as_of=listing)
        .filter(Q(active=1) | Q(active__isnull=True))
        .order_by('settlement_date', 'ticker')
    )
    out: list[tuple[str, str]] = []
    for row in rows:
        settle = (row.settlement_date or '').strip() or '?'
        out.append((row.ticker, f'{row.ticker} · settle {settle} · listing {listing}'))
    return out


def lookup_futures_contract(product_code: str, ticker: str) -> FuturesContract | None:
    code = product_code.strip().upper()
    tick = ticker.strip().upper()
    listing = _latest_futures_listing_as_of(code)
    if listing is None:
        return None
    return (
        FuturesContract.objects.filter(
            product_code=code, ticker=tick, listing_as_of=listing
        )
        .order_by()
        .first()
    )


def default_futures_multiplier(product_code: str) -> float | None:
    row = FuturesProduct.objects.filter(product_code=product_code.strip().upper()).first()
    if row is None or row.unit_of_measure_qty is None:
        return None
    try:
        qty = float(row.unit_of_measure_qty)
    except (TypeError, ValueError):
        return None
    return qty if qty > 0 else None


def commodity_product_code(underlying_id: str) -> str:
    """Map universe instrument_id → Massive product_code (usually the same)."""
    uid = underlying_id.strip()
    row = UniverseInstrument.objects.filter(instrument_id=uid).first()
    if row is not None and (row.provider_symbol or '').strip():
        return row.provider_symbol.strip().upper()
    return uid.upper()



def portfolio_suggestions() -> list[str]:
    return sorted(
        {
            (pid or '').strip()
            for pid in Trade.objects.order_by().values_list('portfolio_id', flat=True).distinct()
            if (pid or '').strip()
        }
    )


def strategy_suggestions() -> list[str]:
    return sorted(
        {
            (stype or '').strip()
            for stype in Trade.objects.order_by()
            .values_list('strategy_type', flat=True)
            .distinct()
            if (stype or '').strip()
        }
    )


def next_trade_id() -> str:
    """Next free TRD_<n>, skipping ids already in the book or left over in `incoming/`."""
    taken = set()
    for trade_id in Trade.objects.values_list('trade_id', flat=True):
        match = _NUMBERED_TRADE_ID.match((trade_id or '').strip())
        if match:
            taken.add(int(match.group(1)))

    candidate = max(taken) + 1 if taken else _FIRST_TRADE_NUMBER
    folder = incoming_dir()
    while candidate in taken or (folder / f'TRD_{candidate}.json').exists():
        candidate += 1
    return f'TRD_{candidate}'


def _fmt_id_number(value: float) -> str:
    number = float(value)
    if number.is_integer():
        return str(int(number))
    return f'{number:f}'.rstrip('0').rstrip('.').replace('.', '_')


def build_product_id(
    spec: BookableInstrument,
    *,
    underlying_id: str,
    expiry_date: date | None,
    strike: float | None,
    option_type: str | None,
    contract_ticker: str | None = None,
) -> str:
    """Deterministic id following the conventions in `trades/incoming/*.sample.json`."""
    und = underlying_id.strip().upper()
    if spec.has_contract_ticker:
        ticker = (contract_ticker or '').strip().upper()
        return '_'.join([spec.product_prefix, und, ticker])
    parts = [spec.product_prefix, und]
    if spec.has_option_type:
        parts.append('C' if (option_type or '').lower() == 'call' else 'P')
        parts.append(_fmt_id_number(float(strike or 0.0)))
    if spec.has_expiry and expiry_date is not None:
        parts.append(expiry_date.strftime('%Y%m%d'))
    return '_'.join(parts)


def product_conflicts(product_id: str, *, spec: BookableInstrument, terms: dict) -> list[str]:
    """Differences against an existing product with the same id.

    Ids are deterministic and the importer uses `INSERT OR IGNORE`, so a product
    that already exists silently wins. Booking against terms the user did not type
    would be worse than refusing, hence this check.
    """
    try:
        product = Product.objects.select_related('equity', 'commodity').get(pk=product_id)
    except Product.DoesNotExist:
        return []

    diffs: list[str] = []

    def compare(label: str, existing, wanted) -> None:
        if existing != wanted:
            diffs.append(f'{label}: book has {existing!r}, form says {wanted!r}')

    compare('underlying_id', product.underlying_id, terms['underlying_id'])
    compare('asset_kind', product.asset_kind, spec.asset_kind)
    if spec.has_expiry:
        compare('expiry_date', product.expiry_date, terms['expiry_date'])
    compare('settlement', product.settlement, terms['settlement'])
    compare('currency', product.currency, terms['currency'])
    if not math.isclose(
        float(product.contract_size), float(terms['contract_size']), rel_tol=1e-9, abs_tol=1e-9
    ):
        diffs.append(
            f'contract_size: book has {product.contract_size!r}, form says {terms["contract_size"]!r}'
        )

    if spec.extension == 'commodity':
        commodity = getattr(product, 'commodity', None)
        if commodity is not None:
            compare('instrument_type', commodity.instrument_type, spec.instrument_type)
            compare('product_code', commodity.product_code, terms.get('product_code'))
            compare(
                'contract_ticker',
                (commodity.contract_ticker or '').upper(),
                (terms.get('contract_ticker') or '').upper(),
            )
        return diffs

    equity = getattr(product, 'equity', None)
    if equity is not None:
        compare('instrument_type', equity.instrument_type, spec.instrument_type)
        if spec.has_strike and equity.strike is not None and terms.get('strike') is not None:
            if not math.isclose(
                float(equity.strike), float(terms['strike']), rel_tol=1e-9, abs_tol=1e-9
            ):
                diffs.append(f'strike: book has {equity.strike!r}, form says {terms["strike"]!r}')
        if spec.has_option_type:
            compare('option_type', equity.option_type, terms['option_type'])
    return diffs


def build_bundle(
    spec: BookableInstrument,
    *,
    trade_id: str,
    product_id: str,
    cleaned: dict,
    booked_by: str = '',
) -> dict:
    """Bundle in exactly the shape `import_trade_bundle.py` expects."""
    now = datetime.now()
    who = f' by {booked_by}' if booked_by else ''
    leg: dict = {
        'direction': cleaned['direction'],
        'quantity': cleaned['quantity'],
        # Booking pricer fills this on trade_date; import writes 0.
        'execution_price': None,
        'commission_per_contract': cleaned['commission_per_contract'],
    }
    bundle: dict = {
        '_comment': (
            f'Booked from the Numeraire Journal{who} at {now:%Y-%m-%d %H:%M:%S}. '
            f'Import → PENDING; price with: dev_main --price-booking {trade_id}'
        ),
        'product': {
            'product_id': product_id,
            'asset_kind': spec.asset_kind,
            'underlying_id': cleaned['underlying_id'],
            'expiry_date': (
                cleaned['expiry_date'].isoformat()
                if spec.has_expiry and cleaned.get('expiry_date') is not None
                else None
            ),
            'settlement': cleaned['settlement'],
            'currency': cleaned['currency'],
            'contract_size': cleaned['contract_size'],
            'day_count': 'Actual365Fixed',
            'calendar': 'America/Chicago' if spec.extension == 'commodity' else 'UnitedStates',
        },
        'trade': {
            'trade_id': trade_id,
            'portfolio_id': cleaned['portfolio_id'],
            'strategy_type': cleaned['strategy_type'],
            'booking_timestamp': f'{now:%Y-%m-%d %H:%M:%S}',
            'trade_date': cleaned['trade_date'].isoformat(),
            'legs': [leg],
        },
    }
    if spec.extension == 'commodity':
        bundle['commodity'] = {
            'instrument_type': spec.instrument_type,
            'product_code': cleaned['product_code'],
            'contract_ticker': cleaned['contract_ticker'],
            'contract_month': cleaned.get('contract_month'),
            'settlement_date': (
                cleaned['expiry_date'].isoformat()
                if cleaned.get('expiry_date') is not None
                else None
            ),
            'multiplier': cleaned.get('contract_size'),
            'tick_size': cleaned.get('tick_size'),
            'tick_value': cleaned.get('tick_value'),
            'option_type': None,
            'strike': None,
            'exercise_style': None,
            'option_ticker': None,
            'underlying_contract_ticker': None,
            'structured_params': {},
        }
    else:
        bundle['equity'] = {
            'instrument_type': spec.instrument_type,
            'option_type': cleaned.get('option_type') if spec.has_option_type else None,
            'strike': cleaned.get('strike') if spec.has_strike else None,
            'exercise_style': 'european',
            'structured_params': {},
        }
    return bundle



def write_bundle(trade_id: str, bundle: dict) -> Path:
    folder = incoming_dir()
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f'{trade_id}.json'
    path.write_text(json.dumps(bundle, indent=2) + '\n', encoding='utf-8')
    return path


@dataclass(frozen=True)
class CommandResult:
    ok: bool
    message: str
    stdout: str = ''
    stderr: str = ''


def _subprocess_env() -> dict[str, str]:
    env = dict(os.environ)
    # Both tools resolve a relative NUMERAIRE_DB_PATH against their own cwd; the web
    # process must not let them guess.
    env['NUMERAIRE_DB_PATH'] = _db_path()
    return env


def _first_line(text: str) -> str:
    for line in (text or '').splitlines():
        stripped = line.strip()
        if stripped:
            return stripped
    return ''


# dev_main logs `[timestamp] [level] [pid] message` on stdout, banner first.
_LOG_LINE = re.compile(r'^\[[^\]]*\]\s*\[(?P<level>[^\]]*)\]\s*\[[^\]]*\]\s*(?P<body>.*)$')


def _pricer_message(stdout: str, stderr: str) -> str:
    """Last error the pricer logged, without the log prefix."""
    lines = [line.strip() for line in f'{stderr}\n{stdout}'.splitlines() if line.strip()]
    if not lines:
        return ''
    parsed = [(_LOG_LINE.match(line), line) for line in lines]
    errors = [
        match.group('body')
        for match, _raw in parsed
        if match and match.group('level') in ('error', 'critical')
    ]
    if errors:
        return errors[-1]
    match, raw = parsed[-1]
    return match.group('body') if match else raw


def run_import(bundle_path: Path) -> CommandResult:
    """Run the importer for one bundle.

    A duplicate `trade_id` prints `SKIP:` and still exits 0, so the exit code alone
    would report success for a trade that was never written — the stdout markers decide.
    """
    script = repo_root() / 'scripts' / 'import_trade_bundle.py'
    try:
        proc = subprocess.run(
            [sys.executable, str(script), str(bundle_path)],
            cwd=str(repo_root()),
            env=_subprocess_env(),
            capture_output=True,
            text=True,
            timeout=IMPORT_TIMEOUT_SEC,
        )
    except subprocess.TimeoutExpired:
        return CommandResult(False, f'Import timed out after {IMPORT_TIMEOUT_SEC}s.')
    except OSError as exc:
        return CommandResult(False, f'Could not run the importer: {exc}')

    stdout, stderr = proc.stdout or '', proc.stderr or ''
    lines = stdout.splitlines()
    if any(line.startswith('OK:') for line in lines):
        return CommandResult(True, 'Trade imported as PENDING.', stdout, stderr)

    skipped = next((line for line in lines if line.startswith('SKIP:')), '')
    if skipped:
        return CommandResult(False, skipped, stdout, stderr)

    detail = _first_line(stderr) or _first_line(stdout) or f'exit code {proc.returncode}'
    return CommandResult(False, f'Import rejected the trade — {detail}', stdout, stderr)


def run_price_booking(trade_id: str) -> CommandResult:
    """Fill `execution_price` on `trade_date` and promote PENDING → LIVE."""
    binary = repo_root() / 'build' / 'dev_main'
    if not binary.exists():
        return CommandResult(
            False,
            f'Pricer binary not found at {binary} — build it with ./scripts/build.sh.',
        )
    try:
        proc = subprocess.run(
            [str(binary), '--price-booking', trade_id],
            cwd=str(repo_root()),
            env=_subprocess_env(),
            capture_output=True,
            text=True,
            timeout=BOOKING_TIMEOUT_SEC,
        )
    except subprocess.TimeoutExpired:
        return CommandResult(False, f'Booking timed out after {BOOKING_TIMEOUT_SEC}s.')
    except OSError as exc:
        return CommandResult(False, f'Could not run the pricer: {exc}')

    stdout, stderr = proc.stdout or '', proc.stderr or ''
    if proc.returncode == 0:
        return CommandResult(True, f'{trade_id} priced at trade date.', stdout, stderr)

    # Missing market data on trade_date surfaces here; show what the pricer said.
    detail = _pricer_message(stdout, stderr) or f'exit code {proc.returncode}'
    return CommandResult(False, detail, stdout, stderr)


@dataclass(frozen=True)
class DeletePreview:
    """What the cascade takes along with the trade header."""

    legs: int
    marks: int
    exposures: int
    bundle_name: str | None

    @property
    def has_history(self) -> bool:
        return bool(self.marks or self.exposures)

    @property
    def restorable(self) -> bool:
        """Whether re-importing can bring this exact trade back."""
        return self.bundle_name is not None


def delete_preview(trade_id: str) -> DeletePreview:
    """Counts for the confirmation dialog, so nobody deletes history unknowingly.

    Archived marks cascade too but are not mirrored as models; the script reports
    them in its own summary line.

    Trades booked before the Journal could book (or imported from a differently
    named bundle) have no `<trade_id>.json` to restore from — the dialog has to say
    so rather than promise an undo that does not exist.
    """
    bundle = incoming_dir() / f'{trade_id}.json'
    return DeletePreview(
        legs=TradeLeg.objects.filter(trade_id=trade_id).count(),
        marks=TradeLegMtmEod.objects.filter(trade_id=trade_id).count(),
        exposures=TradeLegExposureEod.objects.filter(trade_id=trade_id).count(),
        bundle_name=bundle.name if bundle.is_file() else None,
    )


def run_delete(trade_id: str) -> CommandResult:
    """Remove one trade with its legs, marks and exposure rows. Products survive."""
    script = repo_root() / 'scripts' / 'delete_trade.py'
    try:
        proc = subprocess.run(
            [sys.executable, str(script), trade_id],
            cwd=str(repo_root()),
            env=_subprocess_env(),
            capture_output=True,
            text=True,
            timeout=DELETE_TIMEOUT_SEC,
        )
    except subprocess.TimeoutExpired:
        return CommandResult(False, f'Delete timed out after {DELETE_TIMEOUT_SEC}s.')
    except OSError as exc:
        return CommandResult(False, f'Could not run the delete script: {exc}')

    stdout, stderr = proc.stdout or '', proc.stderr or ''
    lines = stdout.splitlines()

    # Same contract as the importer: a no-op exits 0 too, so the markers decide.
    done = next((line for line in lines if line.startswith('OK:')), '')
    if done:
        return CommandResult(True, done[len('OK:'):].strip(), stdout, stderr)

    skipped = next((line for line in lines if line.startswith('SKIP:')), '')
    if skipped:
        return CommandResult(False, skipped[len('SKIP:'):].strip(), stdout, stderr)

    detail = _first_line(stderr) or _first_line(stdout) or f'exit code {proc.returncode}'
    return CommandResult(False, detail, stdout, stderr)
