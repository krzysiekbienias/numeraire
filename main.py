import os

import django

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'base.settings')
django.setup()
from wiener.src.wiener.analytical_methods import EuropeanPlainVanillaOption
from tool_kit.quantlib_tool_kit import QuantLibToolKit

if __name__ == '__main__':
    trade_id = 3
    valuation_date = '2025-01-07' # YYYY-MM-DD
    date_ql=QuantLibToolKit.string_2ql_date(valuation_date)

    european_option = EuropeanPlainVanillaOption(valuation_date=valuation_date, trade_id=trade_id)
    european_option.set_up_market_environment(risk_free_rate=0.03)
    european_option.set_trade_attributes(trade_id=trade_id)
    price=european_option.run_analytical_pricer()
    print(f'Price of option of a trade with id: {trade_id} is equal {round(price,4)}')
    print("THE END")

