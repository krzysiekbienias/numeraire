import yfinance as yf
import matplotlib.pyplot as plt
from typing import TypeVar, Iterable, Tuple, Dict, List
import QuantLib as ql
import pandas as pd
from tool_kit.config_loader import CONFIG
import pandas_datareader.data as web


FRED_SERIES_IDS = {
    # Treasury Yields
    "10Y": "DGS10",  # 10-Year Treasury Constant Maturity Rate
    "5Y": "DGS5",    # 5-Year Treasury Constant Maturity Rate
    "2Y": "DGS2",    # 2-Year Treasury Constant Maturity Rate
    "1Y": "DGS1",    # 1-Year Treasury Constant Maturity Rate
    "6M": "DGS6MO",  # 6-Month Treasury Constant Maturity Rate
    "3M": "DGS3MO",  # 3-Month Treasury Constant Maturity Rate
    "1M": "DGS1MO",  # 1-Month Treasury Constant Maturity Rate

    # Treasury Bills
    "3M_TBILL": "DTB3",  # 3-Month Treasury Bill Secondary Market Rate
    "6M_TBILL": "DTB6",  # 6-Month Treasury Bill Secondary Market Rate

    # Federal Funds Rate
    "FEDFUNDS": "FEDFUNDS",  # Effective Federal Funds Rate

    # Inflation Rates
    "CPI": "CPIAUCSL",  # Consumer Price Index for All Urban Consumers
    "CORE_CPI": "CPILFESL",  # Core Consumer Price Index (excluding food and energy)

    # Unemployment Rates
    "UNRATE": "UNRATE",  # Unemployment Rate

    # Exchange Rates
    "EURUSD": "DEXUSEU",  # US Dollar to Euro Exchange Rate
    "GBPUSD": "DEXUSUK",  # US Dollar to British Pound Exchange Rate
    "JPYUSD": "DEXJPUS",  # US Dollar to Japanese Yen Exchange Rate

    # Commodities
    "GOLD": "GOLDAMGBD228NLBM",  # Gold Fixing Price in London (USD per Troy Ounce)
    "SILVER": "SLVPRUSD",  # Silver Price (USD per Troy Ounce)
    "OIL": "DCOILWTICO",  # Crude Oil Prices: West Texas Intermediate (WTI)

    # Stock Market Indices
    "SP500": "SP500",  # S&P 500 Index
    "DJIA": "DJIA",    # Dow Jones Industrial Average
    "NASDAQ": "NASDAQCOM",  # NASDAQ Composite Index
}


class MarketDataExtractor:
    """
    Market Data Extraction Utility.

    This class provides an interface for retrieving market data from various sources,
    such as Yahoo Finance and the Federal Reserve Economic Data (FRED) system.

    Features:
    ---------
    - Extract historical equity prices from Yahoo Finance.
    - Fetch interest rate data from FRED.
    - Support for both single and multiple tickers.
    - Provides statistical analysis utilities.

    Parameters:
    -----------
    equity_tickers : str or List[str], optional, default="TSLA"
        The ticker symbol(s) of the equities to fetch data for.

    instrument_id : str, optional, default='1Y'
        The identifier for economic indicators (e.g., Treasury yields, inflation rates).

    start_period : str, optional, default=None
        The start date for data retrieval (YYYY-MM-DD format).

    end_period : str, optional, default=None
        The end date for data retrieval (YYYY-MM-DD format). If `None`, fetches the latest available data.

    Methods:
    --------
    __init__(equity_tickers: str | List[str], instrument_id: str, start_period: str, end_period: str)
        Initializes the extractor with the specified tickers, instrument ID, and time range.

    extract_equity_price(column_name="Close") -> Dict[str, pd.DataFrame | List]
        Extracts historical market data for a given ticker or list of tickers.

    fetch_interest_rate_data() -> float | pd.DataFrame
        Fetches interest rate or economic indicator data from the FRED database.

    df_info(df: pd.DataFrame) -> pd.DataFrame
        Displays and returns basic statistical information about the extracted time series.

    basic_statistic(df: pd.DataFrame)
        Prints metadata and summary statistics of the provided DataFrame.

    Raises:
    -------
    NotImplementedError:
        - If unsupported data retrieval scenarios are encountered.

    RuntimeError:
        - If data fetching fails due to network issues or incorrect API keys.

    Usage:
    ------
    ```python
    extractor = MarketDataExtractor(equity_tickers=["AAPL", "MSFT"], start_period="2023-01-01", end_period="2023-12-31")
    stock_data = extractor.extract_equity_price()

    interest_rate = MarketDataExtractor(instrument_id="FEDFUNDS", start_period="2023-01-01", end_period="2023-01-01")
    rate_value = interest_rate.fetch_interest_rate_data()
    ```
    """

    def __init__(self,
                 equity_tickers: (str, List[str]) = "TSLA",
                 instrument_id: str='1Y',
                 start_period: str = None,
                 end_period: str = None):
        """
        Initializes the MarketDataExtractor.

        Parameters:
        -----------
        equity_tickers : str or List[str], optional, default="TSLA"
            The ticker symbol(s) of the equities to fetch data for.

        instrument_id : str, optional, default='1Y'
            The identifier for economic indicators (e.g., Treasury yields, inflation rates).

        start_period : str, optional, default=None
            The start date for data retrieval (YYYY-MM-DD format).

        end_period : str, optional, default=None
            The end date for data retrieval (YYYY-MM-DD format).
        """
        self._instrument_id = instrument_id
        self._tickers = equity_tickers
        self._start_period = start_period
        self._end_period = end_period

    def extract_equity_price(self,
                     column_name="Close",
                             offset='1d')->Dict[str, pd.DataFrame | List]:
        # TODO refactor this method!
        """
        Extracts historical market data from Yahoo Finance.

        This method fetches the specified column of historical market data (e.g., "Close" prices)
        over the defined time period. It supports different cases:

        - Multiple tickers over a time range.
        - A single ticker at a specific date.
        - Fetching the latest available price from a start date.

        Parameters:
        -----------
        column_name : str, optional, default="Close"
            The column name to extract from Yahoo Finance data.

        Returns:
        --------
        dict :
            A dictionary where keys are ticker symbols and values are either:
            - DataFrames (for historical ranges).
            - Lists containing a date and a price (for single timestamps).

        Raises:
        -------
        NotImplementedError:
            If an unsupported extraction case is encountered.
        """
        if isinstance(self._tickers, str):
            tickers = [self._tickers]
        else:
            tickers = self._tickers

        underlier_prices_dict = {}

        # Fetching data for each ticker
        for ticker in tickers:
            if self._start_period and self._end_period and self._start_period != self._end_period:
                df = yf.download(ticker, start=self._start_period, end=self._end_period)[[column_name]]
                underlier_prices_dict[ticker] = df

            elif self._start_period and (self._end_period is None or self._start_period == self._end_period):
                # Determine the actual end period only if None using QuantLib
                if self._end_period is None:
                    ql_start_date = ql.DateParser.parseISO(self._start_period)
                    # if we choose days as offset we move one day forward
                    if offset.endswith("d"):
                        days_back = int(offset[:-1])
                        ql_end_date = ql_start_date + ql.Period(days_back, ql.Days)
                    # if we choose months as offset we move backwards
                    elif offset.endswith("mo"):
                        months_back = int(offset[:-2])
                        ql_end_date = ql_start_date - ql.Period(months_back, ql.Months)
                    else:
                        raise ValueError("Unsupported offset format. Use 'Xd' for days or 'Xmo' for months.")

                    computed_end_period = ql_end_date.ISO()
                else:
                    computed_end_period = self._end_period
                if offset.endswith("d"):
                    df = yf.Ticker(ticker).history(start=self._start_period, end=computed_end_period)
                else:
                    df = yf.Ticker(ticker).history(start=computed_end_period, end=self._start_period)

                if not df.empty:
                    underlier_prices_dict[ticker] = [df.index[0].date(), df.iloc[0][column_name]]
                else:
                    underlier_prices_dict[ticker] = None  # Handle case where data isn't available
            else:
                raise NotImplementedError("Unsupported extraction case.")

            return underlier_prices_dict

    def fetch_interest_rate_data(self)->float|pd.DataFrame:
        """
        Fetches interest rate data from FRED.

        This method retrieves economic indicators such as Treasury Yields, CPI, and Federal Funds Rate.

        Returns:
        --------
        float or pd.DataFrame :
            - If fetching data for a single date, returns a float value.
            - Otherwise, returns a DataFrame containing time-series data.

        Raises:
        -------
        RuntimeError:
            If data retrieval fails due to incorrect API configuration or network issues.
        """

        try:
            if self._start_period ==self._end_period:

                # Fetch data from FRED
                data = web.DataReader(FRED_SERIES_IDS[self._instrument_id], "fred", self._start_period, self._end_period,
                                      api_key=CONFIG['FRED_API_KEY'])
                return data.values[0][0]/100
            else:
                NotImplementedError("Another cases are not implemented yet")
        except Exception as e:
            raise RuntimeError(f"Failed to fetch data: {e}")


    @staticmethod
    def df_info(df: pd.DataFrame):
        """
        Displays and returns basic statistical information about the extracted time series.

        Parameters:
        -----------
        df : pd.DataFrame
            Data Frame to display info.

        Returns:
        --------
        pd.DataFrame :
            A DataFrame containing summary statistics.
        """
        print(df.describe())
        return df.describe()

    @staticmethod
    def basic_statistic(df: pd.DataFrame):
        """
        Prints metadata and summary statistics of the provided DataFrame.

        Parameters:
        -----------
        df : pd.DataFrame
            The DataFrame containing market data.
        """

        print(df.info())
