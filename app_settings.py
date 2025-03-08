from dataclasses import dataclass


@dataclass
class AppSettings:
    """
        A configuration class for global application settings.

        This class defines constants used throughout the application, including numerical precision,
        simulation settings, and financial conventions.

        Attributes:
        -----------
        ROUNDING : int
            The number of decimal places used for rounding calculations (default: 3).

        TOLERANCE : float
            The numerical tolerance level for iterative algorithms and convergence checks (default: 0.0001).

        MAX_ITERATION : int
            The maximum number of iterations allowed in numerical procedures (default: 100).

        NUMBER_OF_SIMULATIONS : int
            The default number of Monte Carlo simulations used in pricing models (default: 1000).

        CALENDAR : str
            The default market calendar for financial calculations (default: 'theUK').

        DEFAULT_YEAR_FRACTION_CONVENTION : str
            The default day count convention for computing year fractions (default: 'Actual365').

        SIMULATION_SCHEMA : str
            The schema used for simulating asset price evolution (default: 'exact_solution').
    """
    ROUNDING: int = 3
    TOLERANCE: float = 0.0001
    MAX_ITERATION: int = 100
    NUMBER_OF_SIMULATIONS: int = 1000
    CALENDAR: str = 'theUK'
    DEFAULT_YEAR_FRACTION_CONVENTION: str = 'Actual365'
    SIMULATION_SCHEMA: str = 'exact_solution'
