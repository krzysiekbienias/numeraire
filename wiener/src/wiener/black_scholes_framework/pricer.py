import numpy as np
from scipy.stats import norm

from wiener.src.pricing_environment import MarketEnvironmentHandler, TradeCalendarSchedule
from wiener.models import TradeBook
from tool_kit.numerical_methods import RootFinding
from wiener.src.wiener.black_scholes_framework.underlier_modeling import GeometricBrownianMotion
from app_settings import AppSettings


class PricingEnginesInterface:
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

    def prepare_priceable_dictionary(self, market_data: MarketEnvironmentHandler, trade_attributes: dict) -> dict:
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


# Use default parameters to be able to implement class using
class EuropeanOption(PricingEnginesInterface):
    """
        A class representing a European-style option pricing model.

        The `EuropeanOption` class provides methods to set up trade attributes, configure market environments,
        and compute key components of the Black-Scholes pricing model.

        Parameters:
        -----------
        valuation_date : str
            The date on which the option is being valued.

        trade_id : int, optional, default=None
            The unique identifier of the trade. If not provided, trade details must be passed via kwargs.

        **kwargs : dict, optional
            Additional trade-related parameters such as 'trade_maturity'.

        Attributes:
        -----------
        calendar_schedule : TradeCalendarSchedule
            A schedule that defines the trading period and frequency.

        market_environment : MarketEnvironmentHandler or None
            Stores the market data required for pricing.

        trade_attributes : dict or None
            Stores trade-specific parameters (e.g., strike price, payoff, underlying asset).
    """

    def __init__(self, valuation_date: str, trade_id: (int, None) = None, **kwargs):
        """
        Initializes a European option with a valuation date and trade ID.

        Parameters:
        -----------
        valuation_date : str
            The valuation date of the option.

        trade_id : int, optional
            The unique identifier of the trade.

        **kwargs : dict, optional
            Additional trade parameters such as 'trade_maturity'.
        """
        self.__valuation_date = valuation_date
        self.__trade_id = trade_id

        self.calendar_schedule = TradeCalendarSchedule(valuation_date=self.__valuation_date,
                                                       termination_date=kwargs.get('trade_maturity'),
                                                       frequency='once') \
            if 'trade_maturity' in kwargs \
            else TradeCalendarSchedule(valuation_date=self.__valuation_date,
                                       termination_date=
                                       TradeBook.objects.get(pk=trade_id).trade_maturity,
                                       frequency='once')
        self.market_environment = None
        self.trade_attributes = None

    def set_valuation_date(self, valuation_date):
        """
        Sets a new valuation date for the option.

        Parameters:
        -----------
        valuation_date : str
            The new valuation date in "YYYY-MM-DD" format.
        """
        self.__valuation_date = valuation_date

    def set_trade_id(self, trade_id):
        """
        Sets a new trade ID for the option.

        Parameters:
        -----------
        trade_id : int
            The unique trade identifier.
        """
        self.__trade_id = trade_id

    @property
    def get_valuation_date(self):
        """
        Returns the valuation date of the option.

        Returns:
        --------
        str
            The valuation date.
        """
        return self.__valuation_date

    @property
    def get_trade_id(self):
        """
        Returns the trade ID associated with the option.

        Returns:
        --------
        int
            The trade ID.
        """
        return self.__trade_id

    def set_up_market_environment(self, trade_id=None, **kwargs):
        if self.market_environment is None:
            self.market_environment = MarketEnvironmentHandler(valuation_date=self.__valuation_date,
                                                               trade_id=self.__trade_id)
            self.market_environment.upload_market_data(**kwargs)
            self.market_environment.extract_discount_factors()
        return self.market_environment

    def set_trade_attributes(self, trade_id: int, **kwargs) -> dict:
        """
        Sets up the trade attributes required for pricing the option.

        If keyword arguments (`kwargs`) are provided, trade attributes are populated directly.
        Otherwise, attributes are retrieved from the database using the trade ID.

        Parameters:
        -----------
        trade_id : int
            The unique trade identifier.

        **kwargs : dict, optional
            Optional trade parameters including:

            - 'underlying'
            - 'product_type'
            - 'payoff'
            - 'trade_maturity'
            - 'strike'
            - 'dividend'
            - 'tau'

        Returns:
        --------
        dict
            A dictionary containing trade attributes.
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
        self.trade_attributes["tau"] = kwargs['tau'] if 'tau' in kwargs else self.calendar_schedule.year_fractions
        return self.trade_attributes

    @property
    def d1(self) -> float:
        """
        Computes `d1`, a key parameter in the Black-Scholes option pricing model.

        The formula used is:

        d1 = [ln(S/K) + (r - q + (σ²)/2) * T] / (σ * sqrt(T))
        where:

        - `S` = underlying asset price
        - `K` = strike price
        - `r` = risk-free rate (discount factor)
        - `q` = dividend yield
        - `σ` = volatility
        - `T` = time to maturity (tau)

        Returns:
        --------
        float
            The computed d1 value.
        """

        d1 = (np.log(self.market_environment.market_data['underlying_price'] / self.trade_attributes['strike']) +
              (self.market_environment.market_data['discount_factor'] - self.trade_attributes['dividend'] + 0.5
               * self.market_environment.market_data['volatility'] ** 2) * self.trade_attributes['tau'][0]) / (
                     np.sqrt(self.trade_attributes['tau'][0]) * self.market_environment.market_data['volatility'])
        return d1

    @property
    def d2(self):
        """
        Computes `d2`, a key parameter in the Black-Scholes option pricing model.

        The formula used is:


        d2 = d1 - σ * sqrt(T)

        where:
        - `σ` = volatility
        - `T` = time to maturity (tau)

        Returns:
        --------
        float
            The computed d2 value.
        """
        d2 = self.d1 - self.market_environment.market_data['volatility'] * np.sqrt(self.trade_attributes['tau'])
        return d2

    def vega(self, vol) -> float:
        """
        Calculates Vega, the sensitivity of the option price to volatility.
        Vega is the derivative of the option price w.r.t volatility.
        """

        return self.market_environment.market_data['underlying_price'] * norm.pdf(self.d1, 0, 1) * np.sqrt(
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
            return self.run_analytical_pricer(option_style='plain_vanilla') - market_price  # TODO option_style must
            # come from database.

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


class PlainVanilaOption(EuropeanOption):
    """
    A class for pricing Plain Vanilla European options using the Black-Scholes model.

    This class extends `EuropeanOption` and provides a method to compute the option price
    using the **Black-Scholes formula**.

    Methods:
    --------
    run_pricer():
        Computes the price of a Plain Vanilla European option (Call or Put)
        using the Black-Scholes formula.
    """
    def run_pricer(self):
        """
        Computes the price of a Plain Vanilla European option using the Black-Scholes formula.

        The pricing formula depends on whether the option is a **Call** or **Put** and
        utilizes the standard normal cumulative distribution function (`norm.cdf`).

        Returns:
        --------
        float
            The calculated price of the Plain Vanilla option.

        Raises:
        -------
        KeyError:
            If required market data or trade attributes are missing.
        """
        if self.trade_attributes['payoff'] == "Call":
            return (self.market_environment.market_data['underlying_price'] * norm.cdf(self.d1, 0, 1) -
                    self.trade_attributes["strike"] * self.market_environment.market_data["discount_factor"]
                    * norm.cdf(self.d2, 0, 1))[0]
        else:
            return (self.trade_attributes["strike"] * self.market_environment.market_data["discount_factor"] *
                    norm.cdf(-self.d2, 0, 1) -
                    self.market_environment.market_data["underlying_price"] * norm.cdf(-self.d1, 0, 1))[0]


class DigitalOption(EuropeanOption):
    """
    A class for pricing Digital (Binary) European options using the Black-Scholes model.

    A **Digital option** (also called a Binary option) has a fixed payout of 1 unit if
    the option expires in the money (ITM), and 0 otherwise.

    The class extends `EuropeanOption` and uses the **Black-Scholes closed-form solution**
    for digital option pricing.

    Methods:
    --------
    run_pricer():
        Computes the price of a Digital (Binary) European option using the Black-Scholes formula.
    """
    def run_pricer(self):
        """
        Computes the price of a Digital (Binary) European option using the Black-Scholes formula.

        - A **Call digital option** pays 1 unit if  :math: `S_{T} > K`
        - A **Put digital option** pays 1 unit if :math: (S_{T} < K).

        The price is determined using the cumulative normal distribution function (`norm.cdf`).

        Returns:
        --------
        float
            The computed price of the Digital option.

        Raises:
        -------
        KeyError:
            If required market data or trade attributes are missing.

        Notes:
        ------
        The pricing formulas are:

        - **Call Digital Option Price**:

          :math:`V_{{call}} = e^{-rT} \cdot N(d_2)`

        - **Put Digital Option Price**:

          :math:`V_{{put}} = e^{-rT} \cdot N(-d_2)`
        """
        if self.trade_attributes['payoff'] == "Call":
            return 1 * self.market_environment.market_data["discount_factor"] * norm.cdf(self.d2, 0, 1)[0]

        else:
            return 1 * self.market_environment.market_data["discount_factor"] * norm.cdf(-self.d2, 0, 1)[0]


class AssetOrNothingOption(EuropeanOption):
    def run_pricer(self):
        if self.trade_attributes['payoff'] == "Call":
            return self.market_environment.market_data['underlying_price'] * norm.cdf(self.d1, 0, 1)
        else:
            return self.market_environment.market_data["underlying_price"] * norm.cdf(-self.d1, 0, 1)


class AsianOption(EuropeanOption):
    """
        A class for pricing Asian options using Monte Carlo simulation.

        This class extends `EuropeanOption` and models an Asian option, where the payoff
        depends on the average price of the underlying asset over time. The class utilizes
        a Geometric Brownian Motion (GBM) model to simulate the asset's price dynamics.

        Methods:
        --------
        simulate_underlier():
            Simulates the underlying asset price path using the Geometric Brownian Motion (GBM) model.

        run_pricer(underlying_price):
            Computes the price of the Asian option based on the arithmetic average of the underlying asset price.
    """

    def simulate_underlier(self):
        """
        Simulates the underlying asset price path using a Geometric Brownian Motion (GBM) model.

        The simulation is performed over the trade's lifetime, using market parameters such as
        the risk-free rate and initial asset price.

        Returns:
        --------
        np.ndarray
            A simulated price path of the underlying asset.
        """
        gbm = GeometricBrownianMotion()
        gbm.set_start_simulation_date(start_simulation_date=self.get_valuation_date)
        gbm.set_end_simulation_date(end_simulation_date=self.trade_attributes['trade_maturity'])
        gbm.set_drift(self.market_environment.market_data['risk_free_rate'])
        gbm.set_initialisation_point(self.market_environment.market_data['underlying_price'])
        print(f"Market info and trade attributes updated for trade {self.get_trade_id}")

        print("Grid for simulation prepared!")
        modelled_underlier = gbm.model_equity_dynamic(simulation_schema=AppSettings.SIMULATION_SCHEMA)
        return modelled_underlier

    def run_pricer(self, underlying_price):
        """
        Prices the Asian option using Monte Carlo simulation.

        The method calculates the arithmetic average of the simulated asset prices
        and determines the option payoff based on the option type (Call/Put).
        The discounted expectation of the payoffs is used to estimate the option price.

        Parameters:
        -----------
        underlying_price : np.ndarray
            A set of simulated underlying asset prices.

        Returns:
        --------
        float
            The estimated price of the Asian option.

        Raises:
        -------
        ValueError
            If the option type (payoff) is not recognized.
        """
        arithmetic_average = np.mean(underlying_price, axis=0)
        if self.trade_attributes['payoff'] == "Call":
            payoffs = np.maximum(arithmetic_average - self.trade_attributes['strike'], 0)
        elif self.trade_attributes['payoff'] == "Put":
            payoffs = arithmetic_average - self.trade_attributes['strike']
        else:
            raise ValueError(f"Unrecognized payoff {self.trade_attributes['payoff']}")
        option_price = self.market_environment.market_data['discount_factor'] * np.mean(payoffs)
        return option_price
