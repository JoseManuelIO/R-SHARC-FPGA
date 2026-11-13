"""CartPole plant dynamics."""

from __future__ import annotations

import numpy as np

from .base import OdeDynamics


class CartPoleDynamics(OdeDynamics):
    """Nonlinear cart-pole plant used by the original SHARC examples."""

    def setup_system(self) -> None:
        system = self.config["system_parameters"]
        self.M = float(system["M"])
        self.m_cart = float(system["m"])
        self.J = float(system["J"])
        self.length = float(system["l"])
        self.c = float(system["c"])
        self.gamma = float(system["gamma"])
        self.g = float(system["g"])
        # SHARC's original CartPole implementation reuses `self.m` for both
        # pendulum mass and input dimension, and the latter assignment wins.
        # We keep this explicit compatibility value to lock equation parity.
        self.sharc_m = float(system["input_dimension"])

    def system_derivative(
        self,
        t: float,
        x: np.ndarray,
        u: np.ndarray,
        w: np.ndarray | None,
    ) -> np.ndarray:
        del t, w

        m = self.sharc_m
        total_mass = self.M + m
        total_inertia = self.J + m * self.length**2

        x_pos = float(x[0, 0])
        theta = float(x[1, 0])
        x_dot = float(x[2, 0])
        theta_dot = float(x[3, 0])
        _ = x_pos

        sin_theta = np.sin(theta)
        cos_theta = np.cos(theta)
        theta_dot_sq = theta_dot**2
        length_sq = self.length**2
        force = float(u[0, 0])

        dxdt = np.zeros((4, 1), dtype=float)
        dxdt[0, 0] = x_dot
        dxdt[1, 0] = theta_dot
        dxdt[2, 0] = (
            -m * self.length * sin_theta * theta_dot_sq
            + m * self.g * (m * length_sq / total_inertia) * sin_theta * cos_theta
            - self.c * x_dot
            - (self.gamma / total_inertia) * m * self.length * cos_theta * theta_dot
            + force
        ) / (total_mass - m * (m * length_sq / total_inertia) * cos_theta * cos_theta)
        dxdt[3, 0] = (
            -m * length_sq * sin_theta * cos_theta * theta_dot_sq
            + total_mass * self.g * self.length * sin_theta
            - self.c * self.length * cos_theta * x_dot
            - self.gamma * (total_mass / m) * theta_dot
            + self.length * cos_theta * force
        ) / (total_inertia * (total_mass / m) - m * (self.length * cos_theta) ** 2)
        return dxdt
