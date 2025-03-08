from typing import TypeVar, List, NewType

import QuantLib as ql
import pandas as pd

from tool_kit.quantlib_tool_kit import QuantLibToolKit
from tool_kit.market_data_extractor import MarketDataExtractor
from wiener.models import TradeBook

QL = NewType("QL", ql)

DT = TypeVar('DT')


class TradeCalendarSchedule:

    def __init__(self,
                 valuation_date: str = "2023-4-22",
                 termination_date: str = "2023-7-22",
                 calendar: str = "theUK",
                 year_fraction_convention: str = "Actual365",
                 frequency: str = "monthly",
                 **kwargs):
        """
        Description
        -----------
        This class sets up generic the environment for pricing different financial instruments.

        Parameters
        ----------
        valuation_date : str
            The date you want to price financial instrument. Format Y-m-d.

        termination_date : str
            Date after which your trade expires. Format Y-m-d.
        calendar : str, optional
            Name of the specific calendar of dates to follow, by default "theUK". It corresponds with name of country.
        Possible calendar to chose:
        - theUk
        - USA
        - Poland
        - Switzerland
        year_fraction_convention : str, optional
            Name of year count convention that sets rule how year fraction to be calculated, by default "Actual365"
            Another possible:
            - Actual360, 
            - Actual365,
            - ActualActual,
            - Thirty360
            - Business252 
        frequency : str, optional
            Name of frequency according to with tenors are generated. In particular daily if we want to have business days in our schedule,
              by default "monthly"
            Possible parameters:
            - daily
            - once
            - monthly
            - quarterly
            - annual
            - semiannual


        Note
        ----
        Please note that within this class as well as QuantLibToolKid are defined different static methods where variable are converted into.
        This interface allows to User do not take care about it.

        """

        # -----------------------
        # Region: Constructors
        # -----------------------
        self._valuation_date = valuation_date
        self._termination_date = termination_date
        self._s_calendar = calendar
        self._s_year_fraction_conv = year_fraction_convention
        self._s_frequency = frequency
        # -----------------------
        # END Region: Constructors
        # -----------------------

        # ----------------------------
        # Region: Check format of dates
        # ----------------------------
        if not isinstance(self._termination_date, str):
            # probably we have datetime type and we need to convert to str to unify.
            self._termination_date = self._termination_date.strftime("%Y-%m-%d")

        if not isinstance(self._valuation_date, str):
            # probably we have datetime type and we need to convert to str to unify.
            self._valuation_date = self._valuation_date.strftime("%Y-%m-%d")

        # ----------------------------
        # End region: Check format of dates
        # ----------------------------

        # ----------------------------
        # Region: Processing input data
        # ----------------------------

        self.pricing_dictionary = self.process_trade_info_data(trade_id=1)

        # -----------------------
        # END Processing input data
        # -----------------------

        # -----------------------
        # Region: QuantLib Converter
        # -----------------------
        ql_valuation_date = QuantLibToolKit.string_2ql_date(
            date=self._valuation_date)
        ql_termination_date = QuantLibToolKit.string_2ql_date(
            date=self._termination_date)
        ql_year_fraction_conv = QuantLibToolKit.set_year_fraction_convention(
            year_fraction_conv=self._s_year_fraction_conv)

        ql_schedule = QuantLibToolKit.define_schedule(valuation_date=ql_valuation_date,
                                                      termination_date=ql_termination_date,
                                                      calendar='theUK',
                                                      correction_rule="Following",
                                                      freq_period=self._s_frequency)
        # -----------------------
        # Region: QuantLib Converter
        # -----------------------

        # -----------------------
        # Region: Attributes
        # -----------------------
        self.scheduled_dates = list(ql_schedule)
        self.year_fractions = QuantLibToolKit.consecutive_year_fraction(schedule=ql_schedule)
        self.days_until_expiration = self.scheduled_dates[-1] - self.scheduled_dates[0]
        # -----------------------
        # End Region: Attributes
        # -----------------------

    # -----------------------
    # Region: getters
    # -----------------------
    def get_valuation_date(self):
        return self._valuation_date

    def get_termination_date(self):
        return self._termination_date

    def get_calendar_name(self):
        return self._s_calendar

    def get_year_fraction_convention_name(self):
        return self._s_year_fraction_conv

    def get_frequency(self):
        return self._s_frequency

    # -----------------------
    # End Region: getters
    # -----------------------

    # -----------------------
    # Region: setters
    # -----------------------        
    def set_valuation_date(self, valuation_date):
        self._valuation_date = valuation_date

    def set_termination_date(self, termination_date):
        self._termination_date = termination_date

    def set_calendar(self, calendar):
        self._s_calendar = calendar

    def set_year_fraction_convention_name(self, year_fraction_convention):
        self._s_year_fraction_conv = year_fraction_convention

    def set_frequency(self, frequency):
        self._s_frequency = frequency

    # -----------------------
    # END Region: setters
    # -----------------------

    def process_trade_info_data(self, trade_id=None, *args, **kwargs):
        if trade_id is None:
            # valuation_date     
            valuation_date = args[0] if len(args) > 0 else kwargs[
                "valuation_date"] if "valuation_date" in kwargs else self.get_valuation_date()
            # termination_date
            termination_date = args[1] if len(args) > 1 else kwargs[
                "valuation_date"] if "valuation_date" in kwargs else self.get_termination_date()
            # calendar_name
            calendar_name = args[2] if len(args) > 2 else kwargs[
                "calendar_name"] if "calendar_name" in kwargs else self.get_calendar_name()
            # year_convention
            year_convention = args[3] if len(args) > 3 else kwargs[
                "year_convention"] if "year_convention" in kwargs else self.get_year_fraction_convention_name()
            # frequency
            year_frequency = args[4] if len(args) > 4 else kwargs[
                "year_frequency"] if "year_frequency" in kwargs else self.get_frequency()

            params_set = {"trade_id": None,
                          "valuation_date": valuation_date,
                          "termination_date": termination_date,
                          "calendar_name": calendar_name,
                          "year_convention": year_convention,
                          "year_frequency": year_frequency,
                          "strike": None,
                          "underlying_ticker": "dummy_compnay"}

            return params_set
        else:
            # we derive some trade info from data base
            self.parse_trade_attributes_from_db(trade_id=trade_id)
            valuation_date = args[0] if len(args) > 0 else kwargs[
                "valuation_date"] if "valuation_date" in kwargs else self.get_valuation_date()

    @staticmethod
    def parse_trade_attributes_from_db(trade_id: int):

        one_trade_from_db = TradeBook.objects.get(pk=trade_id)

        # valuation_date

        params_set = {"trade_id": one_trade_from_db.trade_id,
                      "termination_date": one_trade_from_db.trade_maturity,
                      "calendar_name": None,
                      "year_convention": None,
                      "year_frequency": None,
                      "strike": one_trade_from_db.strike,
                      "underlying_ticker": one_trade_from_db.underlying_ticker,
                      "payoff": one_trade_from_db.payoff,
                      "product_type": one_trade_from_db.product_type,
                      "dividend": one_trade_from_db.dividend
                      }

        return params_set


class MarketEnvironmentInterface:
    def extract_underlying_quotation(self):
        pass

    def extract_volatility(self):
        pass

    def extract_discount_factors(self):
        pass

    def validate_market_data(self):
        pass

    def process_parameters(self):
        pass


class MarketEnvironmentHandler:
    """
    A class to manage and handle market-related data for financial modeling and option pricing.

    The `MarketEnvironmentHandler` class is designed to store, retrieve, and manipulate market parameters
    such as risk-free rates, volatility, discount factors, and underlying asset prices. It also provides
    functionality to interact with external data sources (e.g., Yahoo Finance) and databases to fetch
    trade-related information.

    Attributes
    ----------
    __valuation_date : str
        The valuation date in 'YYYY-MM-DD' format.
    __underlying_price : float
        The price of the underlying asset.
    __underlying_name : str
        The name or ticker symbol of the underlying asset.
    __risk_free_rate : float
        The risk-free interest rate.
    __volatility : float
        The volatility of the underlying asset.
    __discount_factor : float
        The discount factor for valuation.
    __trade_id : int or None
        The trade ID associated with the market data (if applicable).
    market_data : dict
        A dictionary to store processed market data.
    """

    def __init__(self,
                 valuation_date: str = "2023-10-02",  #remember to add zero for day
                 underlying_name: str = "TSLA",
                 underlying_price: float = 100,
                 risk_free_rate: float = 0.06,
                 volatility: float = 0.32,
                 discount_factor: float = 0.99,
                 trade_id: (int, None) = None,
                 risk_free_rate_id:str|None="1Y"
                 ):

        self.__valuation_date = valuation_date
        self.__underlying_price = underlying_price
        self.__underlying_name = underlying_name
        self.__risk_free_rate = risk_free_rate
        self.__volatility = volatility
        self.__discount_factor = discount_factor,
        self.__trade_id = trade_id
        self.__risk_free_rate_id=risk_free_rate_id

        self.market_data: dict = dict()

    # --------------
    # Region setters
    # --------------
    def set_valuation_date(self, valuation_date):
        """
        Set the valuation date with validation.

        Parameters
        ----------
        valuation_date : str
            The valuation date in 'YYYY-MM-DD' format.

        Raises
        ------
        ValueError
            If the valuation date is not in the correct format or is not a valid date.
        """
        # Check if the input is a string
        if not isinstance(valuation_date, str):
            raise ValueError("Valuation date must be a string in 'YYYY-MM-DD' format.")

        # Check the format using a regular expression
        import re
        if not re.match(r"^\d{4}-\d{2}-\d{2}$", valuation_date):
            raise ValueError("Valuation date must be in 'YYYY-MM-DD' format.")

        # Validate that the date is a valid calendar date

        # Validate that the date is a valid calendar date using QuantLib
        try:
            # Convert the string to a QuantLib date
            ql_date = ql.DateParser.parseISO(valuation_date)
        except RuntimeError:
            raise ValueError("Valuation date is not a valid calendar date.")

        # If all checks pass, set the valuation date
        self.__valuation_date = valuation_date

    def set_underlying_name(self, underlying_name):
        """
        Set the name or ticker symbol of the underlying asset.

        Parameters
        ----------
        underlying_name : str
            The name or ticker symbol of the underlying asset.
        """
        if self.__trade_id is None:
            self.__underlying_name = underlying_name
        else:
            self.__underlying_name = TradeBook.objects.get(pk=self.__trade_id).underlying_ticker

    def set_underlying_price(self, underlying_price):

        if self.__trade_id is None:
            self.__underlying_price = underlying_price
        else:
            self.__underlying_price = self.parse_underlying_price_from_yahoo_finance(trade_id=self.__trade_id)[
                'underlying_price']

    def set_risk_free_rate(self, risk_free_rate=None):
        """
        Set the risk-free interest rate.

        Parameters
        ----------
        risk_free_rate : float, optional
            The risk-free interest rate. If None, fetches from Yahoo Finance.
        """
        if risk_free_rate is None:

            self.__risk_free_rate = MarketDataExtractor(equity_tickers=self.__underlying_name,
                                                        instrument_id=self.__risk_free_rate_id,
                                                        start_period=self.__valuation_date,
                                                        end_period=self.__valuation_date).fetch_interest_rate_data()
        else:
            self.__risk_free_rate = risk_free_rate

    def set_volatility(self, volatility=None):
        if volatility is None:
            raise NotImplementedError("It might be implemented later as implied volatility")
        else:
            self.__volatility = volatility

    # --------------
    # End  Region getters
    # --------------

    # --------------
    # Region getters
    # --------------

    def get_trade_id(self):
        """
        Returns the trade ID associated with the market data.

        Returns:
            int or None: The trade ID if set, otherwise None.
        """
        return self.__trade_id

    def get_valuation_date(self):

        return self.__valuation_date

    def get_underlying_name(self):
        return self.__underlying_name

    def get_risk_free_rate(self):
        return self.__risk_free_rate

    def get_volatility(self):
        return self.__volatility

    # --------------
    # End  Region getters
    # --------------

    def display_market_data(self):
        def __str__(self) -> str:
            return (f'Market data:\n underlying Price :{self.__underlying_price} \n '
                    f'Risk free rate for pricing :{self.__risk_free_rate}\n'
                    f' Implied volatility: {self.__implied_volatility}\n')

    @staticmethod
    def parse_trade_ticker_by_trade_id(trade_id: (int, None)) -> str:
        """
        Parse the underlying ticker symbol from a database based on the trade ID.

        Parameters
        ----------
        trade_id : int or None
            The trade ID to look up in the database.

        Returns
        -------
        str
            The ticker symbol of the underlying asset if found, otherwise None.
        """
        if trade_id is not None and TradeBook.objects.filter(pk=trade_id).exists():
            underlying_for_trade = TradeBook.objects.get(pk=trade_id).underlying_ticker
            return underlying_for_trade
        else:
            return None

    def parse_underlying_price_from_yahoo_finance(self, trade_id: (int, None)) -> dict|None:
        """
        Fetch the underlying asset price from Yahoo Finance based on the trade ID.

        Parameters
        ----------
        trade_id : int or None
            The trade ID to look up in the database.

        Returns
        -------
        dict
            A dictionary containing the underlying asset name and price if found, otherwise None.
        """

        if TradeBook.objects.filter(pk=trade_id).exists():
            print(f"Trade {trade_id} exists ")
            print(f"You might extract underlying price for {self.__underlying_name}")
            underlying_price = self.extract_underlying_quotation()
            return {"db_underlying": self.__underlying_name, "underlying_price": underlying_price}
        else:
            print(f"Thre is no trade with id {trade_id}")
            return None

    @staticmethod
    def create_ql_rate(risk_free_rate: float = 0.05, year_fraction_conv: ql.DayCounter = ql.Actual365Fixed(),
                       compounding_type: int = ql.Continuous, frequency: int = ql.Once) -> ql.InterestRate:
        """
        Create a QuantLib interest rate object.

        Parameters
        ----------
        risk_free_rate : float, optional
            The risk-free interest rate. Defaults to 0.05.
        year_fraction_conv : ql.DayCounter, optional
            The day count convention. Defaults to ql.Actual365Fixed().
        compounding_type : int, optional
            The compounding type. Defaults to ql.Continuous.
        frequency : int, optional
            The compounding frequency. Defaults to ql.Once.

        Returns
        -------
        ql.InterestRate
            A QuantLib interest rate object.
        """

        return ql.InterestRate(risk_free_rate, year_fraction_conv, compounding_type, frequency)

    def extract_underlying_quotation(self) -> dict:
        """
        Extract the underlying asset price for a given time window.

        Returns
        -------
        dict
            A dictionary containing the underlying asset price and related data.
        """
        if self.__trade_id is not None:
            valuation_date_ql = QuantLibToolKit.string_2ql_date(self.__valuation_date)
            maturity_date_ql = QuantLibToolKit.date_object_2ql_date(
                TradeBook.objects.get(pk=self.__trade_id).trade_maturity)
            start_contract_date = QuantLibToolKit.date_object_2ql_date(
                TradeBook.objects.get(pk=self.__trade_id).trade_date)
            if valuation_date_ql > maturity_date_ql:
                raise ValueError("Valuation date must be earlier than maturity date")
            elif valuation_date_ql < start_contract_date:
                raise ValueError("Valuation date must NOT be earlier than the contract starts.")
        if self.__trade_id is None:
            print(f'Price of underlying as of {self.__valuation_date} for underlier {self.get_underlying_name()} is'
                  f'equal {MarketDataExtractor(equity_tickers=self.__underlying_name,start_period=self.__valuation_date).extract_equity_price()[self.__underlying_price][1]}')
            return MarketDataExtractor(equity_tickers=self.__underlying_name,
                                       start_period=self.__valuation_date).extract_equity_price()
        else:
            print(f'Price of underlying as of {self.__valuation_date} for underlier {self.get_underlying_name()} is'
                  f' equal {MarketDataExtractor(equity_tickers=self.__underlying_name, start_period=self.__valuation_date).extract_equity_price()[self.__underlying_name][1]}')
            return MarketDataExtractor(equity_tickers=self.parse_trade_ticker_by_trade_id(trade_id=self.__trade_id),
                                       start_period=self.__valuation_date).extract_equity_price()

    def extract_discount_factors(self, maturity_date: str | None = None) -> float:
        """
        Description
        ----------
        This method calculates discount factors based on maturity date and valuation date.

        Parameters
        ----------
        maturity_date
        """
        rate = self.create_ql_rate(risk_free_rate=self.__risk_free_rate)
        if self.__trade_id is not None:
            valuation_date_ql = ql.DateParser.parseISO(self.__valuation_date)
            maturity_date_ql = QuantLibToolKit.date_object_2ql_date(
                TradeBook.objects.get(pk=self.__trade_id).trade_maturity)
            start_contract_date = QuantLibToolKit.date_object_2ql_date(
                TradeBook.objects.get(pk=self.__trade_id).trade_date)
            if maturity_date_ql < valuation_date_ql < start_contract_date:
                raise ValueError("Valuation date must be earlier than maturity date")

            self.__discount_factor = rate.discountFactor(QuantLibToolKit.string_2ql_date(self.__valuation_date),
                                                         QuantLibToolKit.date_object_2ql_date(
                                                             TradeBook.objects.get(pk=self.__trade_id).trade_maturity))
            return self.__discount_factor
        else:
            self.__discount_factor = rate.discountFactor(QuantLibToolKit.string_2ql_date(self.__valuation_date),
                                                         QuantLibToolKit.string_2ql_date(maturity_date))
            return self.__discount_factor

    def upload_market_data(self, **kwargs):
        """
        Description
        -----------
         method to parse option price that comes from market. It supports defining parameters by user when the
        method is called as well as feeding parameters from diverse data providers. ** kwargs passed to process

        - underlying_ticker
        - underlying_price
        - market_date
        - risk_free_rate
        - volatility
        Returns
        -------
        dict
        """

        # risk-free rate

        risk_free_rate = kwargs['risk_free_rate'] if "risk_free_rate" in kwargs else self.set_risk_free_rate()
        risk_free_rate=self.get_risk_free_rate()
        # volatility
        volatility = kwargs['volatility'] if "volatility" in kwargs else self.set_volatility()

        # underlying name
        if self.__trade_id is not None:
            underlying_name = self.parse_trade_ticker_by_trade_id(trade_id=self.__trade_id)
            # ticker name set using setter based on trade id.
            self.set_underlying_name(underlying_name=underlying_name)

        else:
            underlying_name = self.get_underlying_name()
        # underlying price at valuation date
        underlying_price = kwargs["underlying_price"] if "underlying_price" in kwargs \
            else self.extract_underlying_quotation()[underlying_name][1]

        discount_factor = kwargs["discount_factor"] if "discount_factor" in kwargs else self.extract_discount_factors()

        self.market_data = dict(underlying_price=underlying_price,
                                risk_free_rate=risk_free_rate,
                                volatility=volatility,
                                underlying_name=underlying_name,
                                discount_factor=discount_factor)

    def validate_market_data(self):
        raise NotImplementedError()
