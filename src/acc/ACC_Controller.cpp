#include "ACC_Controller.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

void ACC_Controller::setup(const nlohmann::json &json_data){
    lmpc.setLoggerLevel(mpc::Logger::log_level::NONE);

    // Load generic system parameters
    sample_time              = json_data.at("system_parameters").at("sample_time");
    
    // Load system-specific parameters.
    mass                     = json_data.at("system_parameters").at("mass");
    v_des                    = json_data.at("system_parameters").at("v_des");
    d_min                    = json_data.at("system_parameters").at("d_min");
    v_max                    = json_data.at("system_parameters").at("v_max");
    F_accel_max              = json_data.at("system_parameters").at("F_accel_max");
    F_brake_max              = json_data.at("system_parameters").at("F_brake_max");
    max_brake_acceleration       = json_data.at("system_parameters").at("max_brake_acceleration");
    max_brake_acceleration_front = json_data.at("system_parameters").at("max_brake_acceleration_front");
    beta                     = json_data.at("system_parameters").at("beta");
    gamma                    = json_data.at("system_parameters").at("gamma");

    // Check the values are reasonable.
    assert(mass  > 0);
    assert(v_des > 0);
    assert(d_min > 0);
    assert(v_max > 0);
    assert(F_accel_max > 0);
    assert(F_brake_max > 0);
    assert(max_brake_acceleration > 0);
    assert(max_brake_acceleration_front > 0);
    assert(beta  > 0);
    assert(gamma > 0) ;

    // Cost weights
    output_cost_weight       = json_data.at("system_parameters").at("mpc_options").at("output_cost_weight");
    input_cost_weight        = json_data.at("system_parameters").at("mpc_options").at("input_cost_weight");
    delta_input_cost_weight  = json_data.at("system_parameters").at("mpc_options").at("delta_input_cost_weight");

    updateStateSpaceMatrices(0);
    setOptimizerParameters(json_data);
    setWeights(json_data);
    setConstraints();
    setReferences(json_data);

    // Check that the initial condition does not violate the constraints.
    xVec x0;
    uVec u0;
    yVec y0;
    if (json_data.at("x0").size() != Tnx || json_data.at("u0").size() != Tnu) {
        throw std::invalid_argument("Initial state/control dimensions do not match");
    }
    for (int i = 0; i < Tnx; ++i) x0(i) = json_data.at("x0").at(i).get<double>();
    for (int i = 0; i < Tnu; ++i) u0(i) = json_data.at("u0").at(i).get<double>();
    control = u0;
    y0 = C * x0;

    assert(v_max >= x0[v_index]);
    assert(v_max >= y0[0]);

}

ACCStepResult ACC_Controller::calculateStep(const ACCStepInput &input) {
    xVec x;
    x << input.x[0],
         input.x[1],
         input.x[2];

    wVec w;
    w << input.w[0],
         input.w[1];

    uVec u_previous;
    u_previous << input.u_previous[0],
                  input.u_previous[1];

    setPreviousControl(u_previous);
    calculateControlCore(input.k, input.t, x, w);

    ACCStepResult result;
    result.u = {control(0), control(1)};
    result.controller_status = static_cast<int>(lmpc_step_result.status);
    result.solver_status = lmpc_step_result.solver_status;
    result.iterations = lmpc_step_result.num_iterations;
    result.is_feasible = lmpc_step_result.is_feasible;
    result.objective = lmpc_step_result.cost;
    result.constraint_error = lmpc_step_result.primal_residual;
    result.dual_residual = lmpc_step_result.dual_residual;
    result.adaptive_rho_interval = lmpc_step_result.adaptive_rho_interval;
    result.rho_updates = lmpc_step_result.rho_updates;
    result.rho_estimate = lmpc_step_result.rho_estimate;
    result.rho_final = lmpc_step_result.rho_final;

    return result;
}

void ACC_Controller::calculateControl(
    int k,
    double t,
    const xVec &x,
    const wVec &w) {
    calculateControlCore(k, t, x, w);
}

void ACC_Controller::calculateControlCore(int k, double t, const xVec &x, const wVec &w){
    // Calculate the control value "u" based on the current state (or state estimate) "x" and the 
    // current exogenuous input (or its estimate) "w".

    state            = x;
    exogenous_input = w;

    // Update the state matrices to linearize around the current velocity.
    double v = x[v_index];
    updateStateSpaceMatrices(v);

    // Update terminal constraint based on the measurement of the front vehicle's velocity.
    double v_front_underestimate = w[0];
    updateTerminalConstraint(v_front_underestimate);

    for (int col = 0; col < prediction_horizon; col++){
      w_series(0, col) = std::max(0.0, v_front_underestimate - col * sample_time * max_brake_acceleration_front);
      w_series(1, col) = 1.0; 
    }
    lmpc.setExogenousInputs(w_series);

    // Call LMPC control calculation here
    lmpc_step_result = lmpc.optimize(state, control);
    control = lmpc_step_result.cmd;

}

void ACC_Controller::updateStateSpaceMatrices(double v0) {
    /* 
    Update the state space matrices based on the linearization of the dynamics centered at 
    the current velocity "v0".
    */

    // Define continuous-time state matrix A_c
    Ac << 0, 0,                1, // p   (position)
          0, 0,               -1, // h   (headway)
          0, 0, -2*gamma*v0/mass; // v   (velocity)

    // Define continuous-time input matrix B_c
    Bc <<      0,          0, // p   (position)
               0,          0, // h   (headway)
          1/mass,    -1/mass; // v   (velocity)
    //         ^           ^   
    // u:     T^a         T^b    

    // State to output matrix
    C <<   0, 0, 1;
    //     ^  ^  ^
    // x:  p  h  v

    // Constant force from friction: k = β + γv²
    double k = beta - gamma * std::pow(v0, 2); 
    assert(k > 0);

    // Set input disturbance matrix.
    Bc_disturbance << 0,       0, 
                      1,       0, 
                      0, -k/mass;
    
    // ======= Discrete-time Matrices ========                         
    // State disturbance matrix
    Cd_disturbance = mat<Tny, Tndu>::Zero() ; // Output disturbance matrix

    discretization<Tnx, Tnu, Tndu>(Ac, Bc, Bc_disturbance, sample_time, Ad, Bd, Bd_disturbance);

    // Set the state-space model in LMPC
    lmpc.setStateSpaceModel(Ad, Bd, C);
    lmpc.setDisturbances(Bd_disturbance, Cd_disturbance);
}

void ACC_Controller::setOptimizerParameters(const nlohmann::json &json_data) {
    LParameters params;
    params.alpha = 1.6;
    const auto& osqp_options = json_data.at("system_parameters").at("osqp_options");
    params.rho               = osqp_options.value("rho", 1e-6);
    params.adaptive_rho      = osqp_options.value("adaptive_rho", true);
    params.adaptive_rho_interval =
        osqp_options.value("adaptive_rho_interval", 0);
    params.eps_rel           = osqp_options.at("rel_tolerance");
    params.eps_abs           = osqp_options.at("abs_tolerance");
    params.eps_prim_inf      = osqp_options.at("primal_infeasibility_tolerance");
    params.eps_dual_inf      = osqp_options.at("dual_infeasibility_tolerance");
    // Keep the baseline independent of wall-clock time.
    params.time_limit        = 0;
    params.maximum_iteration = osqp_options.at("maximum_iteration");
    params.verbose           = osqp_options.at("verbose");
    params.enable_warm_start = json_data.at("system_parameters").at("mpc_options").at("enable_mpc_warm_start");
    params.polish            = true;

    lmpc.setOptimizerParameters(params);
}

void ACC_Controller::setWeights(const nlohmann::json &json_data) {
    if (output_cost_weight < 0) {
        throw std::invalid_argument("The output weight was negative.");
    }

    yVec outputWeight     = yVec::Ones() * output_cost_weight;
    uVec inputWeight      = uVec::Ones() * input_cost_weight;
    uVec deltaInputWeight = uVec::Ones() * delta_input_cost_weight;

    lmpc.setObjectiveWeights(outputWeight, inputWeight, deltaInputWeight, {-1, -1});
}

void ACC_Controller::setConstraints() {
  const double inf = std::numeric_limits<double>::infinity();

  umin << 0, // acceleration force
          0; // braking force

  umax << F_accel_max, // acceleration force
          F_brake_max; // braking force

  xmin <<       0, // p     (position)
            d_min, // h     (headway)
                0; // v     (velocity)

  xmax <<          inf, // p     (position)
                   inf, // h     (headway)
                 v_max; // v     (velocity)

  ymin <<     0; // velocity

  ymax << v_max; // velocity

  bool are_x_bounds_okay = lmpc.setStateBounds( xmin, xmax, {-1, -1});
  bool are_u_bounds_okay = lmpc.setInputBounds( umin, umax, {-1, -1});
  bool are_y_bounds_okay = lmpc.setOutputBounds(ymin, ymax, {-1, -1});
  assert(are_x_bounds_okay);
  assert(are_u_bounds_okay);
  assert(are_y_bounds_okay);

  // Initially, set a terminal constraint that assume the front vehicle is not moving, 
  // until we have a sensor measurement to the contrary.
  updateTerminalConstraint(0);
}

void ACC_Controller::updateTerminalConstraint(double v_front_underestimate) {
  // ==== Set the terminal constraint ====
  //    h(end) - v(end)*v_max/2a > d_min - v_f(end)^2 / 2a_F

  // Create a vector 'c' for a constraint in the form "cᵀ x ≥ s_min."
  xVec xc;
  xc  << 0, 1, -v_max/(2*max_brake_acceleration);
  //     ^  ^            ^
  // x:  p  h            v

  // Compute the velocity at the prediction horizon if it brakes as fast a possible.
  double worst_case_front_velocity_at_ph 
            = std::max(0.0, v_front_underestimate  - prediction_horizon * max_brake_acceleration_front * sample_time);

  // Calculate "d_min - v_f(end)^2 / 2a_F"
  double constraint_min = d_min - pow(worst_case_front_velocity_at_ph, 2) / (2*max_brake_acceleration_front);

  uVec uc = uVec::Zero();
  // Set the scalar constraint as a terminal constraint (using the slice "{prediction_horizon-1, prediction_horizon}")
  // means this contraint is only applied at the last step in the prediction horizon.
  bool is_okay = lmpc.setScalarConstraint(constraint_min, inf, xc, uc, {prediction_horizon-1, prediction_horizon});
  assert(is_okay);
}

void ACC_Controller::setReferences(const nlohmann::json &json_data) {
    yRef << v_des;
    assert(v_des > 0);
    lmpc.setReferences(yRef, uVec::Zero(), uVec::Zero(), {-1, -1});
}

