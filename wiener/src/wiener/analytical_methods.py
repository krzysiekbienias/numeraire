from .pricing_environment import MarketEnvironmentHandler, TradeCalendarSchedule

from ...models import TradeBook

import QuantLib as ql
from datetime import datetime
import numpy as np
import json
import os
from scipy.stats import norm


class AnalyticalPricingEnginesInterface:
    def upload_trade_attributes_from_booking_system(self, trade_id: int) -> dict:
        #this function will create trades from our simple data base
        pass

    def create_new_trade(self):
        pass

    def set_up_market_environment(self) -> MarketEnvironmentHandler:
        pass
        # final object that we pass for pricing engine. 

    def create_trade(self):
        pass

    def prepare_priceable_dictionary(self) -> dict:
        pass

    def validate_pricing_data(self) -> bool:
        pass

    def run_analytical_pricer(self) -> float:
        pass


# Use defult parameters to be able to implement class using
class EuropeanPlainVanillaOption(AnalyticalPricingEnginesInterface):
    def __init__(self, valuation_date: str, trade_id: (int, None) = None,**kwargs):
        self._valuation_date = valuation_date
        self._trade_id = trade_id

        self._market_environment = self.set_up_market_environment()
        self._calendar_schedule = TradeCalendarSchedule(valuation_date=self._valuation_date,
                                                        termination_date=kwargs.get('trade_maturity'),frequency='once') \
                                                        if 'trade_maturity' in kwargs\
                                                        else TradeCalendarSchedule(valuation_date=self._valuation_date,
                                                                                termination_date=
                                                                                TradeBook.objects.get(pk=trade_id).trade_maturity,
                                                                                frequency='once')
        self.priceable_dict = self.prepare_priceable_dictionary()

    def set_up_market_environment(self, **kwargs):

        return MarketEnvironmentHandler(valuation_date=self._valuation_date, trade_id=self._trade_id)

    def set_trade_attributes(self, trade_id: int,**kwargs) -> dict:
        """
        Description
        -----------
        This method sets up the attributes of the trade. If we pass kwargs it will populate parameters directly, otherwise,
        it will fetch parameters from data base, according to trade id.
        Parameters
        ----------
        trade_id
        kwargs

        Returns
        -------
        dict

        """
        trade_info = {'underlier': None,
                         "product_type": None,
                         "payoff": None,
                         "trade_date": None,
                         "trade_maturity": None,
                         "strike": None,
                         "dividend": None,
                         "tau": None}

        trade_info['underlier'] = kwargs['underlier'] if 'underlier' in kwargs else TradeBook.objects.get(pk=trade_id).underlier_ticker
        trade_info['product_type'] =kwargs['product_type'] if 'product_type' in kwargs else TradeBook.objects.get(pk=trade_id).product_type
        trade_info['payoff'] = kwargs['payoff'] if 'payoff' in kwargs else TradeBook.objects.get(pk=trade_id).payoff
        trade_info['trade_maturity'] =kwargs['trade_maturity'] if 'trade_maturity' in kwargs else TradeBook.objects.get(pk=trade_id).trade_maturity
        trade_info['strike'] =kwargs['strike'] if 'strike' in kwargs else TradeBook.objects.get(pk=trade_id).strike
        trade_info['dividend'] = kwargs['dividend'] if 'dividend' in kwargs else TradeBook.objects.get(pk=trade_id).dividend
        trade_info["tau"] = kwargs['tau'] if 'tau' in kwargs else self._calendar_schedule.year_fractions
        return trade_info

    def prepare_priceable_dictionary(self, **kwargs) -> dict:
        """
        Description
        -----------
        Priceable dictionary it is a dictionary that contains information that is combination of trade attributes
        and market data parameters.

        Parameters
        ----------
        kwargs

        Returns
        -------
        dict

        """
        market_data_dict = {'underlier_price': None,
                            'volatility': None,
                            'risk_free_rate': None,
                            'discount_factor': None}
        priceable_dict={}
        market_data_dict['underlier_price'] = kwargs['underlier_price'] if 'underlier_price' in kwargs else \
            self._market_environment.extract_underlier_quotation()[
                TradeBook.objects.get(pk=self._trade_id).underlier_ticker][1]
        market_data_dict['volatility'] = kwargs[
            'volatility'] if 'volatility' in kwargs else self._market_environment.get_volatility()
        market_data_dict['risk_free_rate'] = kwargs[
            'risk_free_rate'] if 'risk_free_rate' in kwargs else self._market_environment.get_risk_free_rate()
        # trade attributes part
        if self._trade_id is not None:
            trade_attributes = self.set_trade_attributes(trade_id=self._trade_id)
            priceable_dict.update(trade_attributes)
            priceable_dict.update(market_data_dict)
            priceable_dict['discount_factor'] = kwargs[
                'discount_factor'] if 'discount_factor' in kwargs else self._market_environment.extract_discount_factors(
                maturity_date=priceable_dict['trade_maturity'])
            return market_data_dict
        else:
            trade_attributes=self.set_trade_attributes(trade_id=None,underlier=kwargs['underlier'],
                                                       product_type=kwargs['product_type'],payoff=kwargs['payoff'],
                                                       trade_maturity=kwargs['trade_maturity'],
                                                       strike=kwargs['strike'],
                                                       dividend=kwargs['dividend'],
                                                       tau=kwargs['tau'])
            priceable_dict.update(trade_attributes)
            priceable_dict.update(market_data_dict)
            priceable_dict['discount_factor'] = kwargs[
                'discount_factor'] if 'discount_factor' in kwargs else self._market_environment.extract_discount_factors(
                maturity_date=priceable_dict['trade_maturity'])

            return trade_attributes
            # here we will run create trade function where we build dummy trade from ground


    def d1(self,
           strike: float,
           underlying_price: float,
           time_to_maturity: float,
           risk_free_rate: float,
           volatility: float,
           dividend: float = 0) -> float:
        """d1_helper
        Description
        -----------

        Parameters
        ----------
        strike : float
            _description_
        underlying_price : float
            _description_
        time_to_maturity : float
            _description_
        risk_free_rate : float
            _description_
        volatility : float
            _description_
        dividend : str, optional
            _description_, by default 0

        Returns
        -------
        float
            _description_
        """

        d1 = (np.log(underlying_price / strike) + (risk_free_rate
                                                   - dividend + 0.5 * volatility ** 2) * time_to_maturity) / (
                     np.sqrt(time_to_maturity) * volatility)
        return d1

    def d2(self,
           strike: float,
           underlying_price: float,
           time_to_maturity: float,
           risk_free_rate: float,
           volatility: float,
           dividend: float = 0) -> float:
        """d1_helper
        Description
        -----------

        Parameters
        ----------
        strike : float
            _description_
        underlying_price : float
            _description_
        time_to_maturity : float
            _description_
        risk_free_rate : float
            _description_
        volatility : float
            _description_
        dividend : str, optional
            _description_, by default 0

        Returns
        -------
        float
            _description_
        """

        d2 = (np.log(underlying_price / strike) + (risk_free_rate
                                                   - dividend + 0.5 * volatility ** 2) * time_to_maturity) / (
                     np.sqrt(time_to_maturity) * volatility)
        return d2

    def run_analytical_pricer(self):
        d1 = self.d1(underlying_price=self.priceable_dict["underlier_price"],
                     strike=self.priceable_dict["strike"],
                     time_to_maturity=self.priceable_dict["tau"],
                     risk_free_rate=self.priceable_dict["risk_free_rate"],
                     volatility=self.priceable_dict["volatility"])
        d2 = self.d2(underlying_price=self.priceable_dict["underlier_price"],
                     strike=self.priceable_dict["strike"],
                     time_to_maturity=self.priceable_dict["tau"],
                     risk_free_rate=self.priceable_dict["risk_free_rate"],
                     volatility=self.priceable_dict["volatility"])
        if self.priceable_dict['payoff'] == "call":
            return self.priceable_dict["underlier_price"] * norm.cdf(d1, 0, 1) - self.priceable_dict["strike"] * \
                self.priceable_dict["discount_factor"] * norm.cdf(d2, 0, 1)
        else:
            return self.priceable_dict["strike"] * self.priceable_dict["discount_factor"] * norm.cdf(-d2, 0, 1) - \
                self.priceable_dict["underlier_price"] * norm.cdf(-d1, 0, 1)
