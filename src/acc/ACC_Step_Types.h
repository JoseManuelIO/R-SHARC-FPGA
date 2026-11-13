#pragma once

#include <array>

struct ACCStepInput {
    int k = 0;
    double t = 0.0;

    std::array<double, 3> x{};
    std::array<double, 2> w{};
    std::array<double, 2> u_previous{};
};

struct ACCStepResult {
    std::array<double, 2> u{};

    int controller_status = 0;
    int solver_status = -1;
    int iterations = 0;
    bool is_feasible = false;

    double objective = 0.0;
    double constraint_error = 0.0;
    double dual_residual = 0.0;

    int adaptive_rho_interval = 0;
    int rho_updates = 0;
    double rho_estimate = 0.0;
    double rho_final = 0.0;
};
