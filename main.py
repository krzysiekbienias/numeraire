import os

import django
import numpy as np

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'base.settings')
django.setup()
from wiener.src.wiener.black_scholes_framework.analytical_pricer import (PlainVanilaOption, DigitalOption,
                                                                         AssetOrNothingOption)
from tool_kit.quantlib_tool_kit import QuantLibToolKit

from wiener.src.wiener.black_scholes_framework.underlier_modeling import GeometricBrownianMotion
from app_settings import AppSettings
from wiener.models import TradeBook


def run_simulation(trade_id: int):
    """ Runs market simulation using Geometric Brownian Motion. """
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

    print("Simulation completed successfully!")


def run_pricer(valuation_date: str, trade_id: int, **kwargs):
    """ Runs the appropriate option pricer based on trade details.

    Parameters
    ----------
    valuation_date : valuation_date:str
    trade_id : trade_id:int
    """
    # Convert the date if needed
    date_ql = QuantLibToolKit.string_2ql_date(valuation_date)

    # Fetch option style from the database
    trade = TradeBook.objects.get(pk=trade_id)
    option_style = trade.option_style  # Assuming 'option_style' is stored in DB

    # Mapping option styles to their corresponding pricer classes
    pricer_mapping = {
        'PlainVanillaOption': PlainVanilaOption,
        'DigitalOption': DigitalOption,
        'AssetOrNothingOption': AssetOrNothingOption,
    }

    # Select the correct pricer class
    pricer_class = pricer_mapping.get(option_style)

    if not pricer_class:
        raise ValueError(f"Unknown option style: {option_style}")

    # Instantiate and run the pricer
    option_pricer = pricer_class(valuation_date=valuation_date, trade_id=trade_id)
    option_pricer.set_up_market_environment(**kwargs)
    option_pricer.set_trade_attributes(trade_id=trade_id)

    price = option_pricer.run_pricer()

    print(f'Price of {option_style} option for trade ID {trade_id} is {round(price, 4)}')


if __name__ == '__main__':
    # ===========================================
    # REGION: Input
    # ===========================================
    trade_id: int = 1
    valuation_date: str = '2025-01-07'  # YYYY-MM-DD
    simulation_button: bool = False
    price_button: bool = True
    volatility=0.25
    # ** kwargs for run_pricer:
    # - risk_free_rate
    # - volatility currently i must pass it.

    # ===========================================
    # END REGION: Input
    # ===========================================

    # ===========================================
    # REGION: Simulation
    # ===========================================
    if simulation_button:
        run_simulation(trade_id)
    # ===========================================
    # END REGION: Simulation
    # ===========================================

    # ===========================================
    # REGION: Calculating analytical price
    # ===========================================
    if price_button:
        run_pricer(valuation_date, trade_id, volatility=volatility)
    # ===========================================
    # END REGION: Calculating analytical price
    # ===========================================
