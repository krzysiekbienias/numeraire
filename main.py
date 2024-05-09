from tool_kit.fundamentals import InterestRateFundamentals
import os
import django

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'base.settings')
django.setup()

from wiener.src.wiener.underlier_modeling import GeometricBrownianMotion


if __name__ == '__main__':
    os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'numeraire.base.settings')


    valuation_date: str = "2023-05-12"
    maturity_date: str = "2024-05-12"
    spot_rate = 0.02
    drift =0.4
    init_point=44
    volatility=0.23

    geometric_brownian_motion = GeometricBrownianMotion(drift=drift,
                                                        volatility=volatility,
                                                        start_simulation_date=valuation_date,
                                                        end_simulation_date=maturity_date,
                                                        grid_time_step='daily',
                                                        initialisation_point=init_point)


    discount_factor = InterestRateFundamentals.discount_factor(spot_rate=spot_rate, freq_period="annual",
                                                               valuation_date=valuation_date,
                                                               maturity_date=maturity_date)
    print("THE END")
