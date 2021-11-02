#ifndef _SPACEOPERATOR_H_
#define _SPACEOPERATOR_H_
#include <ExactRiemannSolverBase.h>
#include <SymmetryOperator.h>
#include <ViscosityOperator.h>
#include <SmoothingOperator.h>
#include <FluxFcnBase.h>
#include <Reconstructor.h>
#include <RiemannSolutions.h>

/*******************************************
 * class SpaceOperator drives computations
 * that require domain/mesh information
 ******************************************/
class SpaceOperator
{
  MPI_Comm&                 comm;
  DataManagers3D&           dm_all;
  IoData&                   iod;
  FluxFcnBase&              fluxFcn;

  vector<VarFcnBase*>& varFcn; //!< each material has a varFcn

  //! Exact Riemann problem solver (multi-phase)
  ExactRiemannSolverBase &riemann;

  //! Mesh info
  SpaceVariable3D coordinates;
  SpaceVariable3D delta_xyz;
  SpaceVariable3D volume; //!< volume of node-centered control volumes
  
  vector<GhostPoint> ghost_nodes_inner; //!< ghost nodes inside the physical domain (shared with other subd)
  vector<GhostPoint> ghost_nodes_outer; //!< ghost nodes outside the physical domain

  int i0, j0, k0, imax, jmax, kmax; //!< corners of the real subdomain
  int ii0, jj0, kk0, iimax, jjmax, kkmax; //!< corners of the ghosted subdomain

  //! Class for spatial reconstruction
  Reconstructor rec;

  //! Class for imposing spherical or cylindrical symmetry (sink terms placed on the left-hand-side!)
  SymmetryOperator* symm;

  //! Class for calculating spatial gradients of variables
  ViscosityOperator* visco;

  //! Class for smoothing the solution
  SmoothingOperator* smooth;

  //! Reconstructed primitive state variables at cell boundaries
  SpaceVariable3D Vl, Vr, Vb, Vt, Vk, Vf;

  //! For temporary variable
  SpaceVariable3D Utmp;

public:
  SpaceOperator(MPI_Comm &comm_, DataManagers3D &dm_all_, IoData &iod_,
                vector<VarFcnBase*> &varFcn_, FluxFcnBase &fluxFcn_,
                ExactRiemannSolverBase &riemann_,
                vector<double> &x, vector<double> &y, vector<double> &z,
                vector<double> &dx, vector<double> &dy, vector<double> &dz,
                bool screenout = true); 
  ~SpaceOperator();

  //! Reset the coords of ghost layer nodes  (a NULL pointer means that value does not need to be reset)
  void ResetGhostLayer(double* xminus, double* xplus, double* yminus,  double* yplus,
                       double* zminus, double* zplus, double* dxminus, double* dxplus, double* dyminus,
                       double* dyplus, double* dzminus, double* dzplus);

  void ConservativeToPrimitive(SpaceVariable3D &U, SpaceVariable3D &ID, SpaceVariable3D &V,
                               bool workOnGhost = false);
  void PrimitiveToConservative(SpaceVariable3D &V, SpaceVariable3D &ID, SpaceVariable3D &U,
                               bool workOnGhost = false);
  int  ClipDensityAndPressure(SpaceVariable3D &V, SpaceVariable3D &ID, 
                              bool workOnGhost = false, bool checkState = true);

  void SetupViscosityOperator(InterpolatorBase *interpolator_, GradientCalculatorBase *grad_);

  void SetInitialCondition(SpaceVariable3D &V, SpaceVariable3D &ID);
    
  void ApplyBoundaryConditions(SpaceVariable3D &V);

  void ApplySmoothingFilter(double time, double dt, int time_step, SpaceVariable3D &V, SpaceVariable3D &ID);

  void FindExtremeValuesOfFlowVariables(SpaceVariable3D &V, SpaceVariable3D &ID,
                                        double *Vmin, double *Vmax, double &cmin, 
                                        double &cmax, double &Machmax, double &char_speed_max,
                                        double &dx_over_char_speed_min);

  void ComputeTimeStepSize(SpaceVariable3D &V, SpaceVariable3D &ID, double &dt, double &cfl);

  //! Compute the RHS of the ODE system (Only for cells inside the physical domain)
  void ComputeResidual(SpaceVariable3D &V, SpaceVariable3D &ID, SpaceVariable3D &R, 
                       RiemannSolutions *riemann_solutions = NULL);

  SpaceVariable3D& GetMeshCoordinates() {return coordinates;}
  SpaceVariable3D& GetMeshDeltaXYZ()    {return delta_xyz;}
  SpaceVariable3D& GetMeshCellVolumes() {return volume;}

  vector<GhostPoint>* GetPointerToInnerGhostNodes() {return &ghost_nodes_inner;}
  vector<GhostPoint>* GetPointerToOuterGhostNodes() {return &ghost_nodes_outer;}

  void Destroy();

private:
  void SetupMesh(vector<double> &x, vector<double> &y, vector<double> &z,
                 vector<double> &dx, vector<double> &dy, vector<double> &dz);
  void SetupMeshUniformRectangularDomain();
  void PopulateGhostBoundaryCoordinates();

  void CreateGhostNodeLists(bool screenout);

  void ApplyBoundaryConditionsGeometricEntities(Vec5D*** v);

  void CheckReconstructedStates(SpaceVariable3D &V,
                                SpaceVariable3D &Vl, SpaceVariable3D &Vr, SpaceVariable3D &Vb,
                                SpaceVariable3D &Vt, SpaceVariable3D &Vk, SpaceVariable3D &Vf,
                                SpaceVariable3D &ID);

  void ComputeAdvectionFluxes(SpaceVariable3D &V, SpaceVariable3D &ID, SpaceVariable3D &F,
                              RiemannSolutions *riemann_solutions = NULL);

};


#endif
