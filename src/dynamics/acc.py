"""Adaptive Cruise Control plant dynamics."""

from __future__ import annotations

import math

import numpy as np

from .base import OdeDynamics


class ACCDynamics(OdeDynamics):
    """Continuous ACC plant used by the SHARC experiments."""

    def setup_system(self) -> None:
        system = self.config["system_parameters"]
        self.beta = float(system["beta"])
        self.gamma = float(system["gamma"])
        self.mass = float(system["mass"])

    def system_derivative(
        self,
        t: float,
        x: np.ndarray,
        u: np.ndarray,
        w: np.ndarray | None,
    ) -> np.ndarray:
        assert w is not None, "ACCDynamics expects an exogenous input w"

        position = float(x[0, 0])
        headway = float(x[1, 0])
        velocity = float(x[2, 0])
        _ = position, headway  # documented state unpacking for readability

        accel_force = float(u[0, 0])
        brake_force = float(u[1, 0])
        front_velocity = float(w[0, 0])

        friction_force = self.beta + self.gamma * velocity**2

        dxdt = np.zeros((self.n, 1), dtype=float)
        dxdt[0, 0] = velocity
        dxdt[1, 0] = front_velocity - velocity
        dxdt[2, 0] = (accel_force - brake_force - friction_force) / self.mass
        return dxdt

    def get_exogenous_input(self, t: float) -> np.ndarray:
        front_velocity = 11.0 + 2.0 * math.sin(t) + math.sin(3.23 * t) + 0.4 * math.sin(12.1 * t)
        return np.array([[front_velocity], [1.0]], dtype=float)
