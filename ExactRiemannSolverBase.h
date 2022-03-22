#ifndef _EXACT_RIEMANN_SOLVER_BASE_H_
#define _EXACT_RIEMANN_SOLVER_BASE_H_

#include <VarFcnBase.h>
#include <vector>
/*****************************************************************************************
 * Base class for solving one-dimensional, single- or two-material Riemann problems
 *****************************************************************************************/
#define PRINT_RIEMANN_SOLUTION 1

class ExactRiemannSolverBase {

protected:
  vector<VarFcnBase*>& vf; 

  int maxIts_main, maxIts_bracket, maxIts_shock;
  int numSteps_rarefaction;
  double tol_main;
  double tol_shock;
  double tol_rarefaction; //has the dimension of pressure, should be specified as a "pressure tolerance"
  double min_pressure, failure_threshold, pressure_at_failure;

public:
  ExactRiemannSolverBase(std::vector<VarFcnBase*> &vf_, ExactRiemannSolverData &iod_riemann);
  virtual ~ExactRiemannSolverBase() {}

  virtual int ComputeRiemannSolution(double *dir/*unit normal*/, double *Vm, int idm /*"left" state*/, 
                                     double *Vp, int idp /*"right" state*/, 
                                     double *Vs, int &id /*solution at xi = 0 (i.e. x=0) */,
                                     double *Vsm /*left 'star' solution*/,
                                     double *Vsp /*right 'star' solution*/);

  void PrintStarRelations(double rhol, double ul, double pl, int idl,
                          double rhor, double ur, double pr, int idr,
                          double pmin, double pmax, double dp,
                          size_t& It_1wave, size_t& It_3wave,
                          std::vector<std::vector<double>>& integrationPath1, std::vector<std::vector<double>>& integrationPath3);

#if PRINT_RIEMANN_SOLUTION == 1
  vector<vector<double> > sol1d;
#endif

protected: //internal functions

//! Nested class / functor: Hugoniot eq. (across a shock wave) as a function of rho_K* (K = l,r)
  struct HugoniotEquation {

    HugoniotEquation(VarFcnBase* vf_, double rho_, double p_, double ps_)
                    : vf(vf_), rho(rho_), p(p_), ps(ps_) { 
      e = vf->GetInternalEnergyPerUnitMass(rho_,p_);
      pavg = 0.5*(p_ + ps_);
      one_over_rho = 1.0/rho_;
    }

    inline double operator() (double rhos) {
      es = vf->GetInternalEnergyPerUnitMass(rhos, ps);
      return es - e  + pavg*(1.0/rhos - one_over_rho);
    }

    private:
    VarFcnBase* vf;
    double rho, p, e, ps, es, pavg, one_over_rho;
  };

  bool FindInitialInterval(size_t& It_1wave, size_t& It_3wave, std::vector<std::vector<double>>& integrationPath1, std::vector<std::vector<double>>& integrationPath3,
           double rhol, double ul, double pl, double el, double cl, int idl,
           double rhor, double ur, double pr, double er, double cr, int idr, /*inputs*/
           double &p0, double &rhol0, double &rhor0, double &ul0, double &ur0,
           double &p1, double &rhol1, double &rhor1, double &ul1, double &ur1/*outputs*/);

  bool FindInitialFeasiblePoints(size_t& It_1wave, size_t& It_3wave, std::vector<std::vector<double>>& integrationPath1, std::vector<std::vector<double>>& integrationPath3,
           double rhol, double ul, double pl, double el, double cl, int idl,
           double rhor, double ur, double pr, double er, double cr, int idr, /*inputs*/
           double &p0, double &rhol0, double &rhor0, double &ul0, double &ur0,
           double &p1, double &rhol1, double &rhor1, double &ul1, double &ur1/*outputs*/);

  int FindInitialFeasiblePointsByAcousticTheory(size_t& It_1wave, size_t& It_3wave, std::vector<std::vector<double>>& integrationPath1, std::vector<std::vector<double>>& integrationPath3, /*for acceleration*/
           double rhol, double ul, double pl, double el, double cl, int idl,
           double rhor, double ur, double pr, double er, double cr, int idr, /*inputs*/
           double &p0, double &rhol0, double &rhor0, double &ul0, double &ur0,
           double &p1, double &rhol1, double &rhor1, double &ul1, double &ur1/*outputs*/);

  virtual bool ComputeRhoUStar(int wavenumber /*1 or 3*/,
                   size_t& It_wave /* for acceleration, count how many times this function has been invoked for either 1-wave or 3-wave */,
		   std::vector<std::vector<double>>& integrationPath /*3 by n, first index: 1-pressure, 2-density, 3-velocity*/,
                   double rho, double u, double p, double ps, int id/*inputs*/,
                   double rhos0, double rhos1/*initial guesses for Hugo. eq.*/,
                   double &rhos, double &us/*outputs*/,
                   bool *trans_rare = NULL, double *Vrare_x0 = NULL/*filled only if found tran rf*/);

  virtual bool Rarefaction_OneStepRK4_ODEtest(int wavenumber/*1 or 3*/, int id,
                   double rho_0, double u_0, double p_0 /*start state*/,
                   double drho /*step size*/,
                   double &rho, double &u, double &p, double &xi /*output*/);

  virtual bool Rarefaction_OneStepRK4(int wavenumber/*1 or 3*/, int id,
                            double rho_0, double u_0, double p_0 /*start state*/, 
                            double dp /*step size*/,
                            double &rho, double &u, double &p, double &xi /*output*/,
                            double & uErr /*output: absolute error in us*/);

  void FinalizeSolution(double *dir, double *Vm, double *Vp,
           double rhol, double ul, double pl, int idl,
           double rhor, double ur, double pr, int idr,
           double rhol2, double rhor2, double u2, double p2,
           bool trans_rare, double Vrare_x0[3], /*inputs*/
           double *Vs, int &id, double *Vsm, double *Vsp /*outputs*/);

};

#endif
