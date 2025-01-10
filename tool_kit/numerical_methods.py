class RootFinding:

    def __init__(self):
        pass

    @staticmethod
    def newton_raphson(func, derivative, initial_guess, tolerance=1e-7, max_iterations=1000):
        """
        Generic Newton-Raphson method to find the root of a differentiable function.

        Parameters:
        - func: The differentiable function `f(x)`.
        - derivative: The derivative of the function `f'(x)`.
        - initial_guess: Initial guess for the root.
        - tolerance: Stopping criterion for the method. Default is 1e-7.
        - max_iterations: Maximum number of iterations to perform. Default is 1000.

        Returns:
        - root: Approximation of the root.
        - iterations: Number of iterations performed.

        Raises:
        - ValueError: If the derivative becomes zero (division by zero).
        - Exception: If the maximum number of iterations is reached without convergence.

        """
        x = initial_guess
        for iteration in range(max_iterations):
            f_x = func(x)
            f_prime_x = derivative(x)

            if f_prime_x == 0:
                raise ValueError("Derivative is zero. Newton-Raphson method fails.")

            # Update the guess using Newton-Raphson formula
            x_new = x - f_x / f_prime_x

            # Check for convergence
            if abs(x_new - x) < tolerance:
                return x_new, iteration + 1

            x = x_new

        raise Exception("Newton-Raphson method did not converge within the maximum number of iterations.")