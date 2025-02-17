import numpy as np
from scipy.stats import norm

from wiener.src.pricing_environment import MarketEnvironmentHandler, TradeCalendarSchedule
from ...models import TradeBook
from tool_kit.numerical_methods import RootFinding


class AnalyticalPricingEnginesInterface:
    def upload_trade_attributes_from_booking_system(self, trade_id: int) -> dict:
        """
        Description:
        Uploads trade attributes from booking system.
        Parameters
        ----------
        trade_id
        """
        pass

    def create_new_trade(self):
        pass

    def set_up_market_environment(self) -> MarketEnvironmentHandler:
        pass
        # final object that we pass for pricing engine. 

    def create_trade(self):
        pass

    def prepare_priceable_dictionary(self, market_data: MarketEnvironmentHandler, trade_atributes: dict) -> dict:
        """
        Description
        -----------
        Priceable dictionary it is a dictionary that contains information that is combination of trade attributes
        and market data parameters.

       Parameters
       ----------

       Returns
       -------
       dict

               """
        pass

    def validate_pricing_data(self) -> bool:
        pass

    def run_analytical_pricer(self) -> float:
        pass


# Use defult parameters to be able to implement class using
class EuropeanOptionPricer(AnalyticalPricingEnginesInterface):
    def __init__(self, valuation_date: str, trade_id: (int, None) = None, **kwargs):
        self._valuation_date = valuation_date
        self._trade_id = trade_id

        self._calendar_schedule = TradeCalendarSchedule(valuation_date=self._valuation_date,
                                                        termination_date=kwargs.get('trade_maturity'),
                                                        frequency='once') \
            if 'trade_maturity' in kwargs \
            else TradeCalendarSchedule(valuation_date=self._valuation_date,
                                       termination_date=
                                       TradeBook.objects.get(pk=trade_id).trade_maturity,
                                       frequency='once')
        self.market_environment = None
        self.trade_attributes = None

    def set_up_market_environment(self, trade_id=None, **kwargs):
        if self.market_environment is None:
            self.market_environment = MarketEnvironmentHandler(valuation_date=self._valuation_date,
                                                               trade_id=self._trade_id)
            self.market_environment.upload_market_data(**kwargs)
            self.market_environment.extract_discount_factors()
        return self.market_environment

    def set_trade_attributes(self, trade_id: int, **kwargs) -> dict:
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
        self.trade_attributes = dict(underlying=None, product_type=None, payoff=None, trade_date=None,
                                     trade_maturity=None,
                                     strike=None, dividend=None, tau=None)

        self.trade_attributes['underlying'] = kwargs['underlying'] if 'underlying' in kwargs else TradeBook.objects.get(
            pk=trade_id).underlying_ticker
        self.trade_attributes['product_type'] = kwargs[
            'product_type'] if 'product_type' in kwargs else TradeBook.objects.get(
            pk=trade_id).product_type
        self.trade_attributes['payoff'] = kwargs['payoff'] if 'payoff' in kwargs else TradeBook.objects.get(
            pk=trade_id).payoff
        self.trade_attributes['trade_maturity'] = kwargs[
            'trade_maturity'] if 'trade_maturity' in kwargs else TradeBook.objects.get(pk=trade_id).trade_maturity
        self.trade_attributes['strike'] = kwargs['strike'] if 'strike' in kwargs else TradeBook.objects.get(
            pk=trade_id).strike
        self.trade_attributes['dividend'] = kwargs['dividend'] if 'dividend' in kwargs else TradeBook.objects.get(
            pk=trade_id).dividend
        self.trade_attributes["tau"] = kwargs['tau'] if 'tau' in kwargs else self._calendar_schedule.year_fractions
        return self.trade_attributes

    @staticmethod
    def d1(strike: float,
           underlying_price: float,
           time_to_maturity: float,
           risk_free_rate: float,
           volatility: float,
           dividend: float = 0) -> float:
        """
        Description
        -----------
        This method calculates d1 ingredient that goes to Black-Scholes price.

        Parameters
        ----------
        strike
        underlying_price
        time_to_maturity
        risk_free_rate
        volatility
        dividend

        Returns
        -------

        """
        d1 = (np.log(underlying_price / strike) + (risk_free_rate
                                                   - dividend + 0.5 * volatility ** 2) * time_to_maturity) / (np.sqrt(time_to_maturity) * volatility)
        return d1

    @staticmethod
    def d2(strike: float,
           underlying_price: float,
           time_to_maturity: float,
           risk_free_rate: float,
           volatility: float,
           dividend: float = 0) -> float:

        d2 = (np.log(underlying_price / strike) + (risk_free_rate
                                                   - dividend - 0.5 * volatility ** 2) * time_to_maturity) / (
                     np.sqrt(time_to_maturity) * volatility)
        return d2

    def plain_vanilla_option(self):
        if self.trade_attributes['payoff'] == "Call":
            return (self.market_environment.market_data['underlying_price'] * norm.cdf(d1, 0, 1) -
                    self.trade_attributes["strike"] * self.market_environment.market_data["discount_factor"]
                    * norm.cdf(d2, 0, 1))
        else:
            return (self.trade_attributes["strike"] * self.market_environment.market_data["discount_factor"] *
                    norm.cdf(-d2, 0, 1) - \
                    self.market_environment.market_data["underlying_price"] * norm.cdf(-d1, 0, 1))


    def digital_option(self):
        pass

    def asian_option(self):
        pass

    def barier_option(self):
        pass

    def chooser_option(self):


    def run_analytical_pricer(self,option_style: str) -> float:
        d1 = self.d1(underlying_price=self.market_environment.market_data['underlying_price'],
                     strike=self.trade_attributes['strike'],
                     time_to_maturity=self.trade_attributes["tau"][0],
                     risk_free_rate=self.market_environment.market_data["risk_free_rate"],
                     volatility=self.market_environment.market_data["volatility"])
        d2 = self.d2(underlying_price=self.market_environment.market_data['underlying_price'],
                     strike=self.trade_attributes['strike'],
                     time_to_maturity=self.trade_attributes["tau"][0],
                     risk_free_rate=self.market_environment.market_data["risk_free_rate"],
                     volatility=self.market_environment.market_data["volatility"])
        if option_style=='plain_vanilla':
            self.plain_vanilla_option()
        elif option_style=='digital_option':
            pass

    def vega(self, volatility: float) -> float:
        """
        Calculates Vega, the sensitivity of the option price to volatility.
        Vega is the derivative of the option price w.r.t volatility.
        """
        d1 = self.d1(strike=self.trade_attributes['strike'],
                     underlying_price=self.market_environment.market_data['underlying_price'],
                     time_to_maturity=self.trade_attributes["tau"][0],
                     risk_free_rate=self.market_environment.market_data["risk_free_rate"],
                     volatility=volatility)

        return self.market_environment.market_data['underlying_price'] * norm.pdf(d1, 0, 1) * np.sqrt(
            self.trade_attributes["tau"][0])

    def implied_volatility(self, market_price: float, initial_guess: float = 0.2, tolerance: float = 1e-7,
                           max_iterations: int = 100):
        """
        Uses Newton-Raphson method to solve for implied volatility.

        Parameters:
        - market_price: The observed market price of the option.
        - initial_guess: Initial volatility estimate (default 20%).
        - tolerance: Convergence tolerance.
        - max_iterations: Maximum iterations for Newton-Raphson.

        Returns:
        - implied_vol: The calculated implied volatility.
        - iterations: Number of iterations used.
        """

        def price_difference(vol):
            """ Difference between the market price and BS price with given vol. """
            self.market_environment.market_data["volatility"] = vol  # Temporarily update volatility
            return self.run_analytical_pricer() - market_price

        def vega_derivative(vol):
            """ Computes Vega as the derivative of the price function w.r.t volatility. """
            return self.vega(vol)

        # Use Newton-Raphson method to find the implied volatility
        try:
            implied_vol, iterations = RootFinding.newton_raphson(price_difference, vega_derivative, initial_guess,
                                                                 tolerance, max_iterations)
            return implied_vol, iterations
        except Exception as e:
            print(f"Newton-Raphson did not converge: {e}")
            return None, None


    class BinaryOptionPricer(AnalyticalPricingEnginesInterface):

