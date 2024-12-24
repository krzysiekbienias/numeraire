from .pricing_environment import TradeCalendarSchedule
from app_settings import AppSettings
import numpy as np
from typing import List


class SimulationInterface:
    def define_grid(self):
        pass

    def chose_simulation_schema(self):
        pass

    def model_underlier(self,simulation_schema):
        pass

    def path_metrics(self):
        pass

    def plot_paths(self):
        pass


class GeometricBrownianMotion(SimulationInterface):

    def __init__(self,
                 drift: float,
                 volatility: float,
                 initialisation_point: float,
                 grid_time_step: str,
                 start_simulation_date: str,
                 end_simulation_date: str,
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

    def euler_discretization_schema(self,
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

    def model_underlier(self, simulation_schema: str):
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
