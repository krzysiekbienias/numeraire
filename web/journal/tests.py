from datetime import date, timedelta

from django.contrib.auth import get_user_model
from django.test import Client, SimpleTestCase, TestCase
from django.urls import reverse

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


class CalibrationHelpersTest(SimpleTestCase):
    def test_param_groups_and_factor_product_codes(self):
        from journal.calibration import model_label, param_group, param_label, source_label
        from journal.calibration import _product_from_factor

        self.assertEqual(param_group('mean_reversion'), 'dynamics')
        self.assertEqual(param_group('curve_short_level'), 'curve')
        self.assertEqual(param_group('vol_fit_rmse'), 'fit')
        self.assertEqual(param_label('mean_reversion'), 'Mean reversion κ')
        self.assertEqual(model_label('gabillon_2f'), 'Gabillon 2F')
        self.assertEqual(source_label('historical'), 'historical EOD')
        self.assertEqual(_product_from_factor('CL_SHORT'), 'CL')
        self.assertEqual(_product_from_factor('NG_M3'), 'NG')
        self.assertEqual(_product_from_factor('AAPL'), 'AAPL')

    def test_param_view_picks_one_underlier_and_one_scalar(self):
        from journal.calibration import select_param_view

        detail = {
            'param_blocks': [
                {
                    'curve': 'CL',
                    'sections': [
                        {
                            'label': 'Dynamics',
                            'rows': [
                                {
                                    'name': 'mean_reversion',
                                    'label': 'Mean reversion κ',
                                    'value': 1.6,
                                    'is_int': False,
                                }
                            ],
                        }
                    ],
                },
                {
                    'curve': 'NG',
                    'sections': [
                        {
                            'label': 'Fit quality',
                            'rows': [
                                {
                                    'name': 'vol_fit_rmse',
                                    'label': 'Vol fit RMSE',
                                    'value': 0.02,
                                    'is_int': False,
                                }
                            ],
                        }
                    ],
                },
            ]
        }
        ng = select_param_view(detail, {'param_curve': 'NG'})
        self.assertEqual(ng['curve'], 'NG')
        self.assertIsNone(ng['selected'])
        one = select_param_view(detail, {'param_curve': 'NG', 'param_name': 'vol_fit_rmse'})
        self.assertEqual(one['selected']['value'], 0.02)
        fallback = select_param_view(detail, {'param_curve': 'ZZ'})
        self.assertEqual(fallback['curve'], 'CL')


class LandingPageTests(TestCase):
    def test_guest_landing_has_scattered_photos(self):
        response = Client().get(reverse('journal:landing'))
        self.assertEqual(response.status_code, 200)
        html = response.content.decode()
        self.assertIn('nj-landing-collage', html)
        for name in ('tanks.jpg', 'bars.jpg', 'code.jpg', 'desck.jpg'):
            self.assertIn(name, html)
        self.assertIn('Open Quant Lab', html)
        self.assertIn('nj-landing-orbit', html)


class JournalHubNavTests(TestCase):
    def setUp(self):
        self.user = get_user_model().objects.create_user('nav', password='nav-pass')
        self.client.login(username='nav', password='nav-pass')

    def test_guest_is_sent_to_login_from_market_and_risk(self):
        anon = Client()
        for name in ('market_data', 'market_equities', 'risk', 'calibration'):
            response = anon.get(reverse(f'journal:{name}'))
            self.assertEqual(response.status_code, 302, name)
            self.assertIn('/accounts/login/', response['Location'])

        lab = anon.get(reverse('journal:quant_lab'))
        self.assertEqual(lab.status_code, 200)
        nav = lab.content.decode().split('aria-label="Primary"', 1)[1].split('</nav>', 1)[0]
        self.assertIn('bi-flask', nav)
        self.assertNotIn('bi-lightning-charge', nav)
        self.assertNotIn('Market Data', nav)
        self.assertNotIn('bi-umbrella', nav)

    def test_signed_in_sidebar_uses_hubs_and_lab_flask(self):
        response = self.client.get(reverse('journal:market_data'))
        self.assertEqual(response.status_code, 200)
        html = response.content.decode()
        self.assertIn('Market Data', html)
        self.assertIn('Risk', html)
        self.assertIn('bi-flask', html)
        self.assertIn('bi-umbrella', html)
        self.assertNotIn('bi-lightning-charge', html)
        self.assertIn('Futures curves', html)
        self.assertIn('Discount curve', html)
        self.assertNotIn('>Commodity curves<', html)
        self.assertNotIn('>Underliers<', html)

    def test_equities_and_risk_hubs_render_tiles(self):
        equities = self.client.get(reverse('journal:market_equities'))
        self.assertEqual(equities.status_code, 200)
        self.assertContains(equities, 'Vol surfaces')
        self.assertContains(equities, 'Spots')
        risk = self.client.get(reverse('journal:risk'))
        self.assertEqual(risk.status_code, 200)
        self.assertContains(risk, 'Exposure')
        self.assertContains(risk, 'Calibration')
        self.assertContains(risk, 'beacon.jpg')
        self.assertContains(risk, 'celownik.jpg')
        self.assertContains(risk, 'bi-umbrella')
