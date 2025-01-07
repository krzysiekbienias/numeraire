import os

import django

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'base.settings')
django.setup()
from wiener.src.wiener.analytical_methods import EuropeanPlainVanillaOption

if __name__ == '__main__':
    trade_id = 1
    valuation_date = '2023-05-10'

    european_option = EuropeanPlainVanillaOption(valuation_date=valuation_date, trade_id=trade_id)
    european_option.set_up_market_environment(risk_free_rate=0.03)
    european_option.set_trade_attributes(trade_id=trade_id)
    price=european_option.run_analytical_pricer()
    print("THE END")


