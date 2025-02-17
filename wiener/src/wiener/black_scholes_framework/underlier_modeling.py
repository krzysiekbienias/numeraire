import pandas as pd
import os
from datetime import datetime
from wiener.src.pricing_environment import TradeCalendarSchedule
from app_settings import AppSettings
import numpy as np
from typing import List
from wiener.models import TradeBook
from tool_kit.yahoo_data_extractor import YahooDataExtractor
from tool_kit.config_loader import CONFIG


class SimulationInterface:
    def define_grid(self):
        pass

    def chose_simulation_schema(self):
        pass

    def model_equity_dynamic(self, simulation_schema: str):
        pass

    def path_metrics(self):
        pass

    def plot_paths(self):
        pass


class GeometricBrownianMotion(SimulationInterface):

    def __init__(self,
                 drift: float = 0.3,
                 volatility: float = 0.2,
                 initialisation_point: float = 100,
                 grid_time_step: str = 'daily',
                 start_simulation_date: str = '2025-01-02',
                 end_simulation_date: str = '2025-04-02',
                 simulation_schema: str = AppSettings.SIMULATION_SCHEMA):
        self._start_simulation_date = start_simulation_date
        self._end_simulation_date = end_simulation_date
        self._drift = drift
        self._volatility = volatility
        self._simulation_schema = simulation_schema
        self._initialisation_point = initialisation_point
        self._grid_time_step = grid_time_step

    # --------------
    # Region getters
    # --------------
    def get_drift(self):
        return self._drift

    def get_volatility(self):
        return self._volatility

    def get_initialisation_point(self):
        return self._initialisation_point

    def get_grid_time_step(self):
        return self._grid_time_step

    # --------------
    # End  Region getters
    # --------------

    # --------------
    # Region setters
    # --------------
    def set_drift(self, drift):
        self._drift = drift

    def set_volatility(self, volatility):
        self._volatility = volatility

    def set_initialisation_point(self, initialisation_point):
        self._initialisation_point = initialisation_point

    def set_grid_time_step(self, grid_time_step):
        self._grid_time_step = grid_time_step

    def set_start_simulation_date(self, start_simulation_date):
        self._start_simulation_date = start_simulation_date

    def set_end_simulation_date(self, end_simulation_date):
        self._end_simulation_date = end_simulation_date

    # --------------
    # End Region setters
    # --------------

    # --------------
    # Region methods
    # --------------
    def define_grid(self) -> tuple:
        """
        Description
        -----------

        Define the grid shape as tuple. First dimension is the number of grid points. Second dimension are
        incremental list.

        Returns
        -------
        tuple

        """
        calendar_schedule = TradeCalendarSchedule(valuation_date=self._start_simulation_date,
                                                  termination_date=self._end_simulation_date,
                                                  frequency=self._grid_time_step,
                                                  calendar=AppSettings.CALENDAR,
                                                  year_fraction_convention=AppSettings.DEFAULT_YEAR_FRACTION_CONVENTION)
        return calendar_schedule.scheduled_dates, calendar_schedule.year_fractions

    def fetch_parameters_from_db(self, trade_id):
        """Description
           -----------
            Fetches and sets key parameters from the database based on a given trade ID.

            This function retrieves the trade maturity and underlying ticker from the database
            using the `TradeBook` model. It then:
            - Sets the end simulation date based on the trade maturity.
            - Fetches the initial price of the underlying asset using the `YahooDataExtractor`.
            - Sets the initialization point for the simulation.

            Parameters:
            ----------
            trade_id : int
                The unique identifier of the trade, used to fetch relevant data from the `TradeBook` database.

            Raises:
            ------
            TradeBook.DoesNotExist
                If the given `trade_id` does not exist in the database.

            Example:
            -------
            >>> model.fetch_parameters_from_db(trade_id=12345)
            """
        self.set_end_simulation_date(TradeBook.objects.get(pk=trade_id).trade_maturity)
        # we need only one date extracted to get initial price of the underlying, this is why start_period and
        # end_period are equal
        yd_object = YahooDataExtractor(tickers=TradeBook.objects.get(pk=trade_id).underlying_ticker,
                                       start_period=self._start_simulation_date,
                                       end_period=self._start_simulation_date)
        self.set_initialisation_point(initialisation_point=
                                      yd_object.extract_data()[TradeBook.objects.get(pk=trade_id).underlying_ticker][1])

    def euler_discretization_schema(self,
                                    simulation_dates,
                                    incremental: List[float],
                                    paths_number: int = AppSettings.NUMBER_OF_SIMULATIONS) -> np.array:
        """euler_discretization_schema

        Description
        -----------
        Define the euler discretization schema.

        Parameters
        ----------
        incremental
        simulation_dates
        paths_number

        Returns
        -------

        """

        dts = incremental
        x_ip1 = np.zeros((paths_number, len(simulation_dates)))
        x_ip1[:, 0] = self._initialisation_point
        for t, dt in zip(range(1, len(x_ip1[0])), dts):
            z = np.random.standard_normal(paths_number)
            x_ip1[:, t] = x_ip1[:, t - 1] + self._drift * dt + self._volatility * z * np.sqrt(dt)
        return np.transpose(x_ip1)

    def milstein_discretization_schema(self,
                                       simulation_dates,
                                       incremental: List[float],
                                       paths_number: int = AppSettings.NUMBER_OF_SIMULATIONS) -> np.array:
        """
        Description
        -----------
        Define the euler discretization schema.

        Parameters
        ----------
        incremental
        simulation_dates
        paths_number

        Returns
        -------

        """

        dts = incremental
        x_ip1 = np.zeros((paths_number, len(simulation_dates)))
        x_ip1[:, 0] = self._initialisation_point
        for t, dt in zip(range(1, len(x_ip1[0])), dts):
            z = np.random.standard_normal(paths_number)
            x_ip1[:, t] = x_ip1[:, t - 1] + self._drift * x_ip1[:, t - 1] * dt + \
                          self._volatility * x_ip1[:, t - 1] * np.sqrt(dt) * z + \
                          0.5 * self._volatility ** 2 * x_ip1[:, t - 1] * (dt * z ** 2 * np.sqrt(dt) - dt)
        return np.transpose(x_ip1)

    def exact_solution_discretization_schema(self,
                                             simulation_dates,
                                             incremental: List[float],
                                             paths_number: int = AppSettings.NUMBER_OF_SIMULATIONS) -> np.array:
        """
        Description
        -----------
        Define the euler discretization schema.

        Parameters
        ----------
        incremental
        simulation_dates
        paths_number

        Returns
        -------

        """

        dts = incremental
        x_ip1 = np.zeros((paths_number, len(simulation_dates)))
        x_ip1[:, 0] = self._initialisation_point
        for t, dt in zip(range(1, len(x_ip1[0])), dts):
            z = np.random.standard_normal(paths_number)
            x_ip1[:, t] = x_ip1[:, t - 1] * np.exp(
                (self._volatility - 0.5 * self._volatility ** 2) * dt +
                self._volatility * np.sqrt(dt) * z)
        return np.transpose(x_ip1)

    # --------------
    # End Region methods
    # --------------

    def model_equity_dynamic(self, simulation_schema: str):
        """
            Description
            -----------
            Simulates the equity dynamics based on the specified discretization schema.

            Parameters:
            ----------
            simulation_schema : str
                The discretization schema to use for the simulation. Options are:
                - "euler" : Uses the Euler discretization schema.
                - "milstein" : Uses the Milstein discretization schema.
                - "exact_solution" : Uses the exact solution discretization schema.

            Returns:
            -------
            np.ndarray
                A 2D NumPy array representing the simulated equity paths.

            Raises:
            ------
            ValueError
                If an invalid `simulation_schema` is provided.

            Example:
            -------
            >>> model.model_equity_dynamic("euler")
            >>> model.model_equity_dynamic("milstein")
            >>> model.model_equity_dynamic("exact_solution")
            """
        if simulation_schema == "euler":
            return self.euler_discretization_schema(simulation_dates=self.define_grid()[0],
                                                    incremental=self.define_grid()[1])
        elif simulation_schema == "milstein":
            return self.milstein_discretization_schema(simulation_dates=self.define_grid()[0],
                                                       incremental=self.define_grid()[1])
        elif simulation_schema == "exact_solution":
            return self.exact_solution_discretization_schema(simulation_dates=self.define_grid()[0],
                                                             incremental=self.define_grid()[1])
        else:
            raise ValueError("This schema does not exist! Please chose 'euler', 'milstein', 'exact_solution")

    @staticmethod
    def calculate_quantile(simulated_paths: np.ndarray, quantile):
        """Calculate the 97.5% quantile from a set of simulated equity paths.

            Parameters:
            simulated_paths (np.ndarray): A 2D NumPy array where rows represent days
                                  and columns represent different scenarios.

            Returns:
            np.ndarray: A 1D array containing the 97.5% quantile for each day."""
        return np.quantile(simulated_paths, quantile, axis=1)

    @staticmethod
    def calculate_mean(simulated_paths):
        """
        Calculate the mean from a set of simulated equity paths.

        Parameters:
        simulated_paths (np.ndarray): A 2D NumPy array where rows represent days
                                      and columns represent different scenarios.

        Returns:
        np.ndarray: A 1D array containing the mean for each day.

        """
        return np.mean(simulated_paths, axis=1)

    @staticmethod
    def structure_simulation_results(simulation_array: np.ndarray, index, columns, save: bool=False):
        simulation_df=pd.DataFrame(simulation_array, index=index, columns=columns)
        if save:
            simulation_df.to_csv(os.path.join(CONFIG['csv_drop_path'],CONFIG['equity'],
                                              datetime.now().strftime("%Y-%m-%d %H:%M:%S"),'.csv'))
        return simulation_df


