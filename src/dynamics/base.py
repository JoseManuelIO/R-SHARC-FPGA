"""Base classes for local plant simulation in R-SHARC."""

from __future__ import annotations

from abc import ABC, abstractmethod

import numpy as np


def assert_is_column_vector(array: np.ndarray, label: str = "array") -> None:
    """Validate the column-vector convention used across the simulator."""
    assert isinstance(array, np.ndarray), f"{label} must be a numpy array, got {type(array)}"
    assert array.ndim == 2, f"{label} must be two-dimensional, got shape {array.shape}"
    assert array.shape[1] == 1, f"{label} must be a column vector, got shape {array.shape}"


class Dynamics(ABC):
    """Abstract plant interface."""

    def __init__(self, config: dict):
        self.config = config
        system = self.config["system_parameters"]
        self.n = int(system["state_dimension"])
        self.m = int(system["input_dimension"])
        self.p = int(system["output_dimension"])
        self.w_dim = int(system["exogenous_input_dimension"])
        self.setup_system()

    def setup_system(self) -> None:
        """Hook used by subclasses to cache system parameters."""

    @abstractmethod
    def evolve_state(
        self,
        t0: float,
        x0: np.ndarray,
        u: np.ndarray,
        w: np.ndarray | None,
        tf: float,
    ) -> tuple[float, np.ndarray]:
        """Advance the plant state from t0 to tf."""

    def get_exogenous_input(self, t: float) -> np.ndarray:
        """Default exogenous input is zero."""
        return np.zeros((self.w_dim, 1), dtype=float)


class OdeDynamics(Dynamics):
    """Base class for plants expressed as ODEs and integrated with solve_ivp."""

    @abstractmethod
    def system_derivative(
        self,
        t: float,
        x: np.ndarray,
        u: np.ndarray,
        w: np.ndarray | None,
    ) -> np.ndarray:
        """Return dx/dt as a column vector with the same shape as x."""

    def evolve_state(
        self,
        t0: float,
        x0: np.ndarray,
        u: np.ndarray,
        w: np.ndarray | None,
        tf: float,
    ) -> tuple[float, np.ndarray]:
        assert_is_column_vector(x0, "x0")
        assert_is_column_vector(u, "u")
        if w is not None:
            assert_is_column_vector(w, "w")
        t0 = float(t0)
        tf = float(tf)
        if tf < t0:
            raise ValueError(f"evolve_state expects tf >= t0, got t0={t0}, tf={tf}")
        if np.isclose(tf, t0):
            # Zero-duration evolution is valid and should preserve state exactly.
            return tf, x0.copy()

        n = x0.shape[0]
        x0_flat = x0.reshape(-1)
        assert x0_flat.ndim == 1, f"x0 must flatten to a 1D vector, got {x0_flat.shape}"

        def f(t: float, x: np.ndarray) -> np.ndarray:
            x_col = x.reshape((n, 1))
            dxdt = self.system_derivative(t, x_col, u, w)
            assert dxdt.shape == x_col.shape, (
                f"system_derivative returned {dxdt.shape}, expected {x_col.shape}"
            )
            dxdt_flat = dxdt.reshape(-1)
            assert dxdt_flat.ndim == 1, (
                f"system_derivative must flatten to 1D, got {dxdt_flat.shape}"
            )
            return dxdt_flat

        try:
            from scipy.integrate import solve_ivp
        except ImportError as err:
            raise RuntimeError(
                "scipy is required to evolve plant dynamics. Install the R-SHARC "
                "Python dependencies, for example with 'pip install -e .'."
            ) from err

        sol = solve_ivp(f, [t0, tf], x0_flat, t_eval=np.linspace(t0, tf, 100))
        xf = sol.y[:, -1].reshape((n, 1))
        return float(sol.t[-1]), xf
