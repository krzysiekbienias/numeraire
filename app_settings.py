from dataclasses import dataclass


@dataclass
class AppSettings:
    ROUNDING: int = 3
    TOLERANCE: float = 0.0001
    MAX_ITERATION: int = 100
    NUMBER_OF_SIMULATIONS: int = 1000
    CALENDAR: str = 'theUK'
    DEFAULT_YEAR_FRACTION_CONVENTION: str = 'Actual365'
