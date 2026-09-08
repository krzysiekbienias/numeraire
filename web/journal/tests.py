from datetime import date, timedelta

from django.test import SimpleTestCase

from journal.commodity_curves import load_tenor_history
from journal.commodity_curve_backtest import (
    MAX_HORIZON_DAYS,
    MIN_HORIZON_DAYS,
    _compare,
    _parse_date,
    _parse_draw,
    list_target_sessions,
)


class CurveBacktestHelpersTest(SimpleTestCase):
    def test_parse_draw_clamps_to_available_range(self):
        self.assertEqual(_parse_draw('3', 8), 3)
        self.assertEqual(_parse_draw('0', 8), 1)
        self.assertEqual(_parse_draw('99', 8), 1)
        self.assertEqual(_parse_draw('nope', 8), 1)

    def test_parse_date_rejects_sessions_that_were_not_quoted(self):
        available = [date(2026, 6, 4), date(2026, 9, 2)]
        self.assertEqual(_parse_date('2026-06-04', available), date(2026, 6, 4))
        self.assertIsNone(_parse_date('2026-07-01', available))
        self.assertIsNone(_parse_date('not-a-date', available))

    def test_target_sessions_stay_inside_the_horizon_the_kernel_trusts(self):
        origin = date(2026, 6, 4)
        available = [
            date(2026, 6, 5),
            date(2026, 6, 11),
            date(2026, 9, 2),
            origin + timedelta(days=MAX_HORIZON_DAYS + 1),
        ]
        targets = list_target_sessions(available, origin)
        self.assertEqual(targets, [date(2026, 6, 11), date(2026, 9, 2)])
        self.assertTrue(
            all(MIN_HORIZON_DAYS <= (d - origin).days <= MAX_HORIZON_DAYS for d in targets)
        )

    def test_compare_joins_the_draw_to_the_printed_settle(self):
        stats = [
            {
                'ticker': 'NGV26',
                'expiry': '2026-10-28',
                'settlement_years': 0.4,
                'anchor': 3.0,
                'p5': 3.2,
                'p95': 4.5,
            },
            {
                'ticker': 'NGX26',
                'expiry': '2026-11-25',
                'settlement_years': 0.5,
                'anchor': 3.2,
                'p5': 2.2,
                'p95': 4.8,
            },
            {
                'ticker': 'EXPIRED',
                'expiry': '2026-07-01',
                'settlement_years': 0.1,
                'anchor': 3.5,
                'p5': 3.0,
                'p95': 4.0,
            },
        ]
        rows = _compare(stats, [5.0, 3.0, 3.4], {'NGV26': 2.5, 'NGX26': 3.0})
        self.assertEqual([r['ticker'] for r in rows], ['NGV26', 'NGX26'])
        self.assertFalse(rows[0]['inside_band'])
        self.assertTrue(rows[1]['inside_band'])
        self.assertAlmostEqual(rows[0]['gap_pct'], 100.0)
        self.assertAlmostEqual(rows[1]['gap_pct'], 0.0)


class CommodityCurveHelpersTest(SimpleTestCase):
    def test_tenor_history_is_empty_without_a_contract(self):
        self.assertEqual(load_tenor_history('', ''), [])
        self.assertEqual(load_tenor_history('NG', ''), [])
        self.assertEqual(load_tenor_history('', 'NGV26'), [])
