import os

import django
import numpy as np
import pandas as pd

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'base.settings')
django.setup()
from wiener.src.wiener.analytical_pricer import EuropeanPlainVanillaOption
from tool_kit.quantlib_tool_kit import QuantLibToolKit
from tool_kit.yahoo_data_extractor import YahooDataExtractor

from wiener.src.wiener.underlier_modeling import GeometricBrownianMotion
from app_settings import AppSettings

if __name__ == '__main__':
    # ===========================================
    # REGION: Input
    # ===========================================
    trade_id: int = 1
    valuation_date: str = '2025-01-07'  # YYYY-MM-DD
    simulation_button: bool = False
    price_button: bool = True

    # ===========================================
    # END REGION: Input
    # ===========================================

    # ===========================================
    # REGION: Simulation
    # ===========================================
    if simulation_button:
        gbm = GeometricBrownianMotion()

        gbm.fetch_parameters_from_db(trade_id=trade_id)
        simulations_arr: np.array = gbm.model_equity_dynamic(simulation_schema='exact_solution')
        simulations_df = gbm.structure_simulation_results(simulation_array=simulations_arr,
                                                          index=gbm.define_grid()[0],
                                                          columns=range(AppSettings.NUMBER_OF_SIMULATIONS))
        quantile_df = gbm.structure_simulation_results(
            simulation_array=gbm.calculate_quantile(simulations_df, quantile=0.975),
            index=gbm.define_grid()[0],
            columns=["Quantile"])
        mean_df = gbm.structure_simulation_results(simulation_array=gbm.calculate_mean(simulations_df),
                                                   index=gbm.define_grid()[0],
                                                   columns=["Average Path"])
    # ===========================================
    # END REGION: Simulation
    # ===========================================

    # ===========================================
    # REGION: Calculating analytical price
    # ===========================================
    if price_button:
        date_ql = QuantLibToolKit.string_2ql_date(valuation_date)
        european_option = EuropeanPlainVanillaOption(valuation_date=valuation_date, trade_id=trade_id)
        european_option.set_up_market_environment(risk_free_rate=0.03)
        european_option.set_trade_attributes(trade_id=trade_id)
        price = european_option.run_analytical_pricer()
        print(f'Price of option of a trade with id: {trade_id} is equal {round(price, 4)}')
    # ===========================================
    # END REGION: Calculating analytical price
    # ===========================================
