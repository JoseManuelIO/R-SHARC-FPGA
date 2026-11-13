// Runtime-specific CartPole NLMPC controller implementation.
// Keeps the original dynamics/objective equations while allowing optimizer
// settings to be configured from JSON in the standalone CVA6 runtime flow.
#include "NLMPCController.h"

#include "debug_levels.hpp"

// constants need to be declared globally or segmentation fault happens (why?)
double M, m, J, l, c, my_gamma, g, sample_time, input_cost_weight;

namespace {

template <typename T>
void assign_if_present(const nlohmann::json& node, const char* key, T& out) {
    if (node.contains(key) && !node.at(key).is_null()) {
        out = node.at(key).get<T>();
    }
}

}  // namespace

void NLMPCController::setup(const nlohmann::json& json_data) {
    // Load system parameters
    M = json_data.at("system_parameters").at("M");
    m = json_data.at("system_parameters").at("m");
    J = json_data.at("system_parameters").at("J");
    l = json_data.at("system_parameters").at("l");
    c = json_data.at("system_parameters").at("c");
    my_gamma = json_data.at("system_parameters").at("gamma");
    g = json_data.at("system_parameters").at("g");
    sample_time = json_data.at("system_parameters").at("sample_time");
    input_cost_weight = json_data.at("system_parameters").at("mpc_options").at("input_cost_weight");

    nlmpc.setLoggerLevel(mpc::Logger::log_level::NONE);
    nlmpc.setDiscretizationSamplingTime(sample_time);

    // Dynamics differential equation
    auto stateEq = [](xVec& x_dot_, const xVec& x_, const uVec& u) {
        // Constants
        double M_t = M + m;
        double J_t = J + m * std::pow(l, 2);
        // Recover state parameters
        double x = x_(0);      // position of the base
        double theta = x_(1);  // angle of the pendulum
        double vx = x_(2);     // velocity of the base
        double omega = x_(3);  // angular rate of the pendulum

        // Compute common terms
        double s_t = std::sin(theta);
        double c_t = std::cos(theta);
        double o_2 = std::pow(omega, 2);
        double l_2 = std::pow(l, 2);

        // Calculate derivatives
        x_dot_(0) = vx;
        x_dot_(1) = omega;
        x_dot_(2) = (-m * l * s_t * o_2 + m * g * (m * l_2 / J_t) * s_t * c_t - c * vx -
                     (my_gamma / J_t) * m * l * c_t * omega + u(0)) /
                    (M_t - m * (m * l_2 / J_t) * c_t * c_t);

        x_dot_(3) = (-m * l_2 * s_t * c_t * o_2 + M_t * g * l * s_t - c * l * c_t * vx -
                     my_gamma * (M_t / m) * omega + l * c_t * u(0)) /
                    (J_t * (M_t / m) - m * (l * c_t) * (l * c_t));
    };

    nlmpc.setStateSpaceFunction(
        [stateEq](xVec& dx, const xVec& x, const uVec& u, const unsigned int&) { stateEq(dx, x, u); });
    nlmpc.setObjectiveFunction([json_data](const mpc::mat<prediction_horizon + 1, TNX>& x,
                                   const mpc::mat<prediction_horizon + 1, TNY>&,
                                   const mpc::mat<prediction_horizon + 1, TNU>& u,
                                   double) {
        std::vector<double> w =
            json_data.at("system_parameters").at("mpc_options").at("state_cost_weights").get<std::vector<double>>();
        Eigen::VectorXd state_cost_weights =
            Eigen::Map<Eigen::VectorXd, Eigen::Unaligned>(w.data(), w.size());  // create a weight vector
        return (x * state_cost_weights.asDiagonal()).array().square().sum() + u.array().square().sum() * input_cost_weight;
    });

    // Runtime-local optimizer configuration (keeps defaults if fields are absent).
    mpc::NLParameters params;
    const auto& system = json_data.at("system_parameters");
    bool warm_start_set = false;
    if (system.contains("nlopt_options") && system.at("nlopt_options").is_object()) {
        const auto& nl = system.at("nlopt_options");
        assign_if_present<int>(nl, "maximum_iteration", params.maximum_iteration);
        assign_if_present<double>(nl, "time_limit", params.time_limit);
        assign_if_present<double>(nl, "relative_ftol", params.relative_ftol);
        assign_if_present<double>(nl, "relative_xtol", params.relative_xtol);
        assign_if_present<double>(nl, "absolute_ftol", params.absolute_ftol);
        assign_if_present<double>(nl, "absolute_xtol", params.absolute_xtol);
        assign_if_present<bool>(nl, "hard_constraints", params.hard_constraints);
        if (nl.contains("enable_warm_start") && !nl.at("enable_warm_start").is_null()) {
            params.enable_warm_start = nl.at("enable_warm_start").get<bool>();
            warm_start_set = true;
        }
    }
    if (!warm_start_set && system.contains("mpc_options") && system.at("mpc_options").is_object()) {
        const auto& mpc_opts = system.at("mpc_options");
        if (mpc_opts.contains("enable_mpc_warm_start") && !mpc_opts.at("enable_mpc_warm_start").is_null()) {
            params.enable_warm_start = mpc_opts.at("enable_mpc_warm_start").get<bool>();
        }
    }
    nlmpc.setOptimizerParameters(params);
}

void NLMPCController::calculateControl(int k, double t, const xVec& x, const wVec& w) {
    state = x;
    // Call NLMPC control calculation here
    nlmpc_step_result = nlmpc.optimize(state, control);
    control = nlmpc_step_result.cmd;

    latest_metadata.clear();
    latest_metadata["solver_status"] = nlmpc_step_result.solver_status;
    latest_metadata["is_feasible"] = nlmpc_step_result.is_feasible;
    const bool cost_available = std::isfinite(nlmpc_step_result.cost);
    latest_metadata["cost_available"] = cost_available;
    latest_metadata["cost"] = cost_available
        ? nlohmann::json(nlmpc_step_result.cost)
        : nlohmann::json(nullptr);
    latest_metadata["status"] =
        mpc::SolutionStats::resultStatusToString(nlmpc_step_result.status);
}

// Register the controller
REGISTER_CONTROLLER("NLMPCController", NLMPCController)
