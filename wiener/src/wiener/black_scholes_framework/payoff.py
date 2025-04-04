from abc import ABC, abstractmethod
import numpy as np

from typing import Union, Tuple, Optional
import matplotlib.pyplot as plt


class Payoff(ABC):
    """
    Pure Payoff class with:
    - Single constructor parameter (__spot_price)
    - Fallback logic ONLY in getter
    - Strict encapsulation
    - No external dependencies
    """

    def __init__(self):
        """Initialize with only direct spot price"""
        pass

    def get_market_data(self) -> dict:
        """
        Get spot price with fallback logic:
        1. Return directly set spot price
        2. Try to get from attached option's market data
        3. Return None if no spot available
        """
        try:
            # Try to get from owner's market environment
            hasattr(self, '_owner') and hasattr(self._owner, 'market_environment')
            return self._owner.market_environment.market_data
            # Fallback to owner's trade attributes

        except (AttributeError, KeyError):
            print("Object doe snot have properties that you are trying to transfer")

        return None

    def get_trade_attributes(self) -> dict:
        """
        Get spot price with fallback logic:
        1. Return directly set spot price
        2. Try to get from attached option's market data
        3. Return None if no spot available
        """
        try:
            hasattr(self, '_owner') and hasattr(self._owner, 'trade_attributes')
            return self._owner.trade_attributes
        except (AttributeError, KeyError):
            pass

        return None

    def set_spot_price(self, spot_price: float) -> None:
        """Setter with validation"""
        if not isinstance(spot_price, (int, float)):
            raise TypeError("Spot price must be numeric")
        if spot_price <= 0:
            raise ValueError("Spot price must be positive")
        self._spot_price = float(spot_price)

    def _attach_owner(self, owner: object):
        """Protected method for owner reference (used by EuropeanOption)"""
        self._owner = owner

    @abstractmethod
    def calculate_payoff(self, spot: Union[float, np.ndarray]) -> Union[float, np.ndarray]:
        """Calculate payoff at given spot price(s)"""
        pass

    @abstractmethod
    def payoff_description(self) -> str:
        """String description of payoff"""
        pass

    def plot_payoff(self,
                    spot_range: Tuple[float, float] = None,
                    range_pct: float = 0.3,
                    num_points: int = 100,
                    **plot_kwargs) -> plt.Figure:
        """
        Plot payoff diagram using get_spot_price() with fallback
        """
        ref_spot = self.get_spot_price()
        if spot_range is None:
            if ref_spot is None:
                raise ValueError("No spot price available")
            spot_min = ref_spot * (1 - range_pct)
            spot_max = ref_spot * (1 + range_pct)
        else:
            spot_min, spot_max = spot_range

        spots = np.linspace(spot_min, spot_max, num_points)
        payoffs = self.calculate_payoff(spots)

        plt.figure(figsize=plot_kwargs.pop('figsize', (10, 6)))
        plt.plot(spots, payoffs, **plot_kwargs)
        plt.title(f"Payoff Diagram\n{self.payoff_description()}")
        plt.xlabel("Spot Price")
        plt.ylabel("Payoff")
        plt.grid(True)

        if ref_spot is not None:
            plt.axvline(x=ref_spot, color='r', linestyle='--',
                        label=f'Spot: {ref_spot:.2f}')
            plt.legend()

        plt.tight_layout()
        return plt


class PlainVanillaPayoff(Payoff):
    """Vanilla option payoff implementation"""

    def __init__(self,
                 strike: float | None = None,
                 option_type: str | None = None,
                 spot_price: Optional[float] | None = None):
        super().__init__()
        self.intrinsic_value = None
        self._spot_price = spot_price # protected becasue i want to set this value using method define in parent class.
        self.__strike = strike
        self.__option_type = option_type

    def get_strike(self) -> float:
        return self.__strike

    def set_strike(self, strike: float):
        if not isinstance(strike, (int, float)):
            raise TypeError("Strike must be numeric")
        if strike <= 0:
            raise ValueError("Strike must be positive")
        self.__strike = float(strike)

    def get_option_type(self) -> str:
        return self.__option_type

    def set_option_type(self, option_type: str):
        if not isinstance(option_type, str):
            raise TypeError("Option type must be string")
        option_type = option_type.lower()
        if option_type not in ['call', 'put']:
            raise ValueError("Option type must be 'call' or 'put'")
        self.__option_type = option_type

    def calculate_payoff(self) -> Union[float, np.ndarray]:
        if self.__option_type == 'call':
            self.intrinsic_value=np.maximum(self._spot_price - self.__strike, 0)
            return self.instrinsic_value

        self.intrinsic_value=np.maximum(self.__strike - self._spot_price, 0)
        return self.intrinsic_value

    def __repr__(self) -> str:
        return (f"{self.__class__.__name__}("
                f"option_type='{self.__option_type}', "
                f"strike={self.__strike}, "
                f"spot_price={self._spot_price}, "
                f"intrinsic_value={self.intrinsic_value})")
