import os

import django
import numpy as np
import QuantLib as ql

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'base.settings')
django.setup()
from wiener.src.wiener.black_scholes_framework.pricer import (PlainVanillaOption, DigitalOption,
                                                              AssetOrNothingOption, AsianOption)
from wiener.src.wiener.black_scholes_framework.payoff import PlainVanillaPayoff,DigitalOptionPayoff,AsianOptionPayoff,AssetOrNothingOptionPayoff
from tool_kit.quantlib_tool_kit import QuantLibToolKit

from wiener.src.wiener.black_scholes_framework.underlier_modeling import GeometricBrownianMotion
from app_settings import AppSettings
from wiener.models import TradeBook, DerivativePrice
from logger import logger


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
    valuation_date : str
        The valuation date in string format (YYYY-MM-DD).
    trade_id : int
        The ID of the trade being priced.
    country : str, optional
        The country to use for the financial calendar. Defaults to "theUK".

    Notes
    -----
    - Uses `QuantLibToolKit.set_calendar()` to ensure the valuation date is a business day.
    - If not a business day, suggests the next valid business day.
    - Ensures trades are not re-priced if they have already been priced for the given date.
    """
    # Convert the date if needed
    date_ql = QuantLibToolKit.string_2ql_date(valuation_date)
    calendar = QuantLibToolKit.set_calendar(calendar_name)
    # Check if the date is a business day
    if not calendar.isBusinessDay(date_ql):
        next_business_day = calendar.advance(date_ql, ql.Period(1, ql.Days))
        logger.info(f"⚠️ {valuation_date} is not a business day in {calendar_name}. Next valid business day: "
                    f"{next_business_day}")
        return  # Exit early

    # Fetch option style from the database
    trade = TradeBook.objects.get(pk=trade_id)
    product_type = trade.product_type  # Assuming 'product_type' is stored in DB

    # Check if the trade has already been priced for this valuation date
    existing_valuation = DerivativePrice.objects.filter(
        trade_id=trade_id, valuation_date=valuation_date
    ).exists()

    if existing_valuation:
        logger.info(f"Trade {trade_id} has already been priced for valuation date {valuation_date}. Skipping pricing.")
        return  # Exit the function early

    # Mapping option styles to their corresponding pricer classes
    pricer_mapping = {
        'PlainVanillaOption': PlainVanillaOption,
        'DigitalOption': DigitalOption,
        'AssetOrNothingOption': AssetOrNothingOption,
        'AsianOption': AsianOption
    }

    payoff_mapping = {
        'PlainVanillaOption': PlainVanillaPayoff,
        'DigitalOption': DigitalOptionPayoff,
        'AssetOrNothingOption': AssetOrNothingOptionPayoff,
        'AsianOption': AsianOptionPayoff
    }


    # Select the correct pricer class
    pricer_class = pricer_mapping.get(product_type)
    payoff_class = payoff_mapping.get(product_type)

    if not pricer_class:
        logger.error(f"Unknown option style encountered: {product_type}")
        raise ValueError(f"Unknown option style: {product_type}")
    if not payoff_class:
        logger.error(f"Not defined payoff encountered: {product_type}")
        raise ValueError(f"Not possible to calculate payoff: {product_type}")

    # Instantiate and run the pricer
    option_pricer = pricer_class(valuation_date=valuation_date, trade_id=trade_id)
    option_pricer.set_up_market_environment(**kwargs)
    option_pricer.set_trade_attributes(trade_id=trade_id)
    if product_type == 'AsianOption':
        option_pricer.simulate_underlier()
    # maybee here we might initiatet

    payoff=payoff_class()
    payoff._attach_owner(owner=option_pricer)
    payoff.set_strike(strike=payoff._owner.trade_attributes['strike'])
    payoff.set_option_type(option_type=payoff._owner.trade_attributes['payoff'])
    payoff.set_spot_price(spot_price=payoff._owner.market_environment.market_data['underlying_price'])
    if product_type =='DigitalOption':
        payoff.set_cash_amount(cash_amount=payoff._owner.trade_attributes['structured_params']['cash_amount'])
    payoff.calculate_payoff()
    logger.info(payoff)

    option_pricer.create_valuation_results()
    option_pricer.price_deploy()

    #print(f'Price of {product_type} option for trade ID {trade_id} is {round(option_pricer.valuation_results['official_price'], 4)}')


if __name__ == '__main__':
    # ===========================================
    # REGION: Input
    # ===========================================
    # trade_ids=[1,2,3,4,5,6,7,8,9,10,11]
    calendar_name = "USA"
    logger.info(f"Calendar set to: {calendar_name}.")

    trade_id = 4
    logger.info(f"Preparing to price trade with ID: {trade_id}")
    valuation_date: str = '2025-02-10'  # YYYY-MM-DD
    logger.info(f"Valuation date set to: {valuation_date}")
    simulation_button: bool = False
    price_button: bool = True
    volatility = 0.25
    logger.warning(f"No external source for volatility detected. Falling back to user-defined value: {volatility}")
    # ** kwargs for run_pricer:
    # - risk_free_rate
    # - volatility currently I must pass it.

    # ===========================================
    # END REGION: Input
    # ===========================================

    # ===========================================
    # REGION: Simulation
    # ===========================================
    if simulation_button:
        run_simulation(trade_id=trade_id)
    # ===========================================
    # END REGION: Simulation
    # ===========================================

    # ===========================================
    # REGION: Calculating analytical price
    # ===========================================
    if price_button:
        run_pricer(valuation_date, trade_id, volatility=volatility, country=calendar_name)
    # ===========================================
    # END REGION: Calculating analytical price
    # ===========================================
