#include "MPC.h"
#include <cppad/cppad.hpp>
#include <cppad/ipopt/solve.hpp>
#include "Eigen-3.3/Eigen/Core"
#include <limits>


using CppAD::AD;

// TODO: Set the timestep length and duration
size_t N  = 10;
double dt = 0.1;

const double Lf = 2.67;
double ref_v = 30;

int seq = 0;
size_t x_start     = N * (seq++);
size_t y_start     = N * (seq++);
size_t psi_start   = N * (seq++);
size_t v_start     = N * (seq++);
size_t cte_start   = N * (seq++);
size_t epsi_start  = N * (seq++);
size_t delta_start = N * (seq++);
size_t a_start     = N * (seq++) - 1;

class FG_eval {
 public:
  
  Eigen::VectorXd coeffs;

  FG_eval(Eigen::VectorXd coeffs){
    this->coeffs = coeffs;
  }
  
  // polynomial evaluation
  AD<double> polyeval(AD<double> x) {
    AD<double> result = 0.0;
    for (int i = 0; i < coeffs.size(); i++) {
      result += coeffs[i] * CppAD::pow(x, i);
    }
    return result;
  }
  
  // derivative of polynomial evaluation
  AD<double> d_polyeval(AD<double> x) {
    AD<double> result = 0.0;
    for (int i = 1; i < coeffs.size(); i++) {
      result += coeffs[i] * CppAD::pow(x, i - 1) * i;
    }
    return result;
  }

  typedef CPPAD_TESTVECTOR(AD<double>) ADvector;
  
  void operator()(ADvector& fg, const ADvector& vars) {
    // `fg` a vector of the cost constraints, `vars` is a vector of variable values (state & actuators)
    // The cost is stored is the first element of `fg`.
    // Any additions to the cost should be added to `fg[0]`.
    fg[0] = 0;
    
    // Reference State Cost
    for (int i = 0; i < N; i++) {
      fg[0] += CppAD::pow(vars[cte_start + i], 2);
      fg[0] += CppAD::pow(vars[epsi_start + i], 2);
      fg[0] += CppAD::pow(vars[v_start + i] - ref_v, 2);
    }
    
    // Actuators
    for (int i = 0; i < N - 1; i++) {
      fg[0] += CppAD::pow(vars[delta_start + i], 2);
      fg[0] += CppAD::pow(vars[a_start + i], 2);
    }
    
    // Sequential actuations
    for (int i = 0; i < N - 2; i++) {
      fg[0] += 200 * CppAD::pow(vars[delta_start + i + 1] - vars[delta_start + i], 2);
      fg[0] +=       CppAD::pow(vars[a_start + i + 1]     - vars[a_start + i], 2);
    }
    
    // Initial constraints
    //
    // We add 1 to each of the starting indices due to cost being located at
    // index 0 of `fg`.
    // This bumps up the position of all the other values.
    fg[1 + x_start]    = vars[x_start];
    fg[1 + y_start]    = vars[y_start];
    fg[1 + psi_start]  = vars[psi_start];
    fg[1 + v_start]    = vars[v_start];
    fg[1 + cte_start]  = vars[cte_start];
    fg[1 + epsi_start] = vars[epsi_start];
    
    // The rest of the constraints
    for (int t = 1; t < N; t++) {
      AD<double> x1      = vars[x_start + t];
      AD<double> x0      = vars[x_start + t - 1];
      
      AD<double> y1      = vars[y_start + t];
      AD<double> y0      = vars[y_start + t - 1];
      
      AD<double> psi1    = vars[psi_start + t];
      AD<double> psi0    = vars[psi_start + t - 1];
      
      AD<double> v1      = vars[v_start + t];
      AD<double> v0      = vars[v_start + t - 1];
      
      AD<double> cte1    = vars[cte_start + t];
      AD<double> cte0    = vars[cte_start + t - 1];
      
      AD<double> epsi1   = vars[epsi_start + t];
      AD<double> epsi0   = vars[epsi_start + t - 1];
      
      AD<double> a0      = vars[a_start + t - 1];
      AD<double> delta0  = vars[delta_start + t - 1];
      
      fg[1 + x_start + t]    = x1 - (x0 + v0 * CppAD::cos(psi0) * dt);
      fg[1 + y_start + t]    = y1 - (y0 + v0 * CppAD::sin(psi0) * dt);
      fg[1 + psi_start + t]  = psi1 - (psi0 + v0 * delta0 * dt / Lf);
      fg[1 + v_start + t]    = v1 - (v0 + a0 * dt);
      fg[1 + cte_start + t]  = cte1 - ((polyeval(x0) - y0) + (v0 * CppAD::sin(epsi0) * dt));
      fg[1 + epsi_start + t] = epsi1 - ((psi0 - CppAD::atan(d_polyeval(x0))) + v0 * delta0 * dt / Lf);
    }
  }
};

//
// MPC class definition implementation.
//
MPC::MPC()  {}
MPC::~MPC() {}

vector<double> MPC::Solve(Eigen::VectorXd state, Eigen::VectorXd coeffs, unsigned int latency) {
  bool ok = true;
  typedef CPPAD_TESTVECTOR(double) Dvector;
  
  int seq = 0;
  double x    = state[seq++];
  double y    = state[seq++];
  double psi  = state[seq++];
  double v    = state[seq++];
  double cte  = state[seq++];
  double epsi = state[seq++];

  size_t n_vars        = N * seq + (N - 1) * 2;
  size_t n_constraints = N * seq;

  Dvector vars(n_vars);
  for (int i = 0; i < n_vars; i++) {
    vars[i] = 0;
  }

  Dvector vars_lowerbound(n_vars);
  Dvector vars_upperbound(n_vars);
  vars[x_start]    = x;
  vars[y_start]    = y;
  vars[psi_start]  = psi;
  vars[v_start]    = v;
  vars[cte_start]  = cte;
  vars[epsi_start] = epsi;
  
  // Set all non-actuators upper and lowerlimits
  // to the max negative and positive values.
  for (int i = 0; i < delta_start; i++) {
    vars_lowerbound[i] = std::numeric_limits<double>::lowest();
    vars_upperbound[i] = std::numeric_limits<double>::max();
  }
  
  // The upper and lower limits of delta are set to -25 and 25
  // degrees (values in radians).
  for (int i = delta_start; i < a_start; i++) {
    vars_lowerbound[i] = - 25 * M_PI / 180;
    vars_upperbound[i] =   25 * M_PI / 180;
  }
  
  // Acceleration/decceleration upper and lower limits.
  for (int i = a_start; i < n_vars; i++) {
    vars_lowerbound[i] = -1.0;
    vars_upperbound[i] =  1.0;
  }
  
  // Lower and upper limits for the constraints
  // Should be 0 besides initial state.
  Dvector constraints_lowerbound(n_constraints);
  Dvector constraints_upperbound(n_constraints);
  for (int i = 0; i < n_constraints; i++) {
    constraints_lowerbound[i] = 0;
    constraints_upperbound[i] = 0;
  }

  
  constraints_lowerbound[x_start]    = x;
  constraints_lowerbound[y_start]    = y;
  constraints_lowerbound[psi_start]  = psi;
  constraints_lowerbound[v_start]    = v;
  constraints_lowerbound[cte_start]  = cte;
  constraints_lowerbound[epsi_start] = epsi;
  
  constraints_upperbound[x_start]    = x;
  constraints_upperbound[y_start]    = y;
  constraints_upperbound[psi_start]  = psi;
  constraints_upperbound[v_start]    = v;
  constraints_upperbound[cte_start]  = cte;
  constraints_upperbound[epsi_start] = epsi;
  
  // object that computes objective and constraints
  FG_eval fg_eval(coeffs);

  //
  // NOTE: You don't have to worry about these options
  //
  // options for IPOPT solver
  std::string options;
  // Uncomment this if you'd like more print information
  options += "Integer print_level  0\n";
  // NOTE: Setting sparse to true allows the solver to take advantage
  // of sparse routines, this makes the computation MUCH FASTER. If you
  // can uncomment 1 of these and see if it makes a difference or not but
  // if you uncomment both the computation time should go up in orders of
  // magnitude.
  options += "Sparse  true        forward\n";
  options += "Sparse  true        reverse\n";
  
  // NOTE: Currently the solver has a maximum time limit of 0.5 seconds.
  // Change this as you see fit.
  options += "Numeric max_cpu_time          0.5\n";

  // place to return solution
  CppAD::ipopt::solve_result<Dvector> solution;

  // solve the problem
  CppAD::ipopt::solve<Dvector, FG_eval>(
      options, vars, vars_lowerbound, vars_upperbound, constraints_lowerbound,
      constraints_upperbound, fg_eval, solution);

  // Check some of the solution values
  ok &= solution.status == CppAD::ipopt::solve_result<Dvector>::success;

  vector<double> actuations;

  if (ok) {
  
    // yield actuations offseted by latency.
    // works OK when latency is multiple of dt, would need to re-eval
    // poly otherwise
    size_t latency_offset = unsigned(latency / (1000* dt));
    
    assert(latency_offset < (N-1));
    
    actuations.push_back(solution.x[delta_start + latency_offset]);
    actuations.push_back(solution.x[a_start     + latency_offset]);
  
    for (int i = 0; i < N-1; i++) {
      actuations.push_back(solution.x[x_start + i + 1]);
      actuations.push_back(solution.x[y_start + i + 1]);
    }
  }
  
  return actuations;
}
