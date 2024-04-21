from dataclasses import dataclass


@dataclass

class AppSettings:

    rounding:int
    tolerance:float
    max_iterations:int
    number_of_simulations:int
