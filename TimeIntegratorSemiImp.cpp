/************************************************************************
 * Copyright © 2020 The Multiphysics Modeling and Computation (M2C) Lab
 * <kevin.wgy@gmail.com> <kevinw3@vt.edu>
 ************************************************************************/

#include<TimeIntegratorSemiImp.h>

//----------------------------------------------------------------------------
// SIMPLE 
//----------------------------------------------------------------------------

TimeIntegratorSIMPLE::TimeIntegratorSIMPLE(MPI_Comm &comm_, IoData& iod_, DataManagers3D& dms_,
                                           SpaceOperator& spo_, IncompressibleOperator &inco_,
                                           vector<LevelSetOperator*>& lso_, MultiPhaseOperator &mpo_,
                                           LaserAbsorptionSolver* laser_, EmbeddedBoundaryOperator* embed_,
                                           HyperelasticityOperator* heo_, PrescribedMotionOperator* pmo_)
                    : TimeIntegratorBase(comm_, iod_, dms_, spo_, lso_, mpo_, laser_, embed_, heo_, pmo_),
                      inco(inco_), Homo(comm_, &(dms_.ghosted1_1dof)), VXstar(comm_, &(dms_.ghosted1_1dof)),
                      VYstar(comm_, &(dms_.ghosted1_1dof)), VZstar(comm_, &(dms_.ghosted1_1dof)),
                      Pprime(comm_, &(dms_.ghosted1_1dof)), B(comm_, &(dms_.ghosted1_1dof)),
                      DX(comm_, &(dms_.ghosted1_1dof)), DY(comm_, &(dms_.ghosted1_1dof)),
                      DZ(comm_, &(dms_.ghosted1_1dof)),
                      vlin_solver(comm_, dms_.ghosted1_1dof, iod.ts.semi_impl.velocity_linear_solver),
                      plin_solver(comm_, dms_.ghosted1_1dof, iod.ts.semi_impl.pressure_linear_solver)
{
  type = SIMPLE;

  Homo.SetConstantValue(1, true); //default

  if(iod.ts.semi_impl.E<=0.0) {
    print_error("*** Error: In the SIMPLE family of methods, E must be set to a positive value.\n");
    exit_mpi();
  }
  Efactor = iod.ts.semi_impl.E;
  
  if(iod.ts.semi_impl.alphaP<=0.0) {
    print_error("*** Error: In the SIMPLE family of methods, alphaP must be set to a positive value "
                "(usually less than 1).\n");
    exit_mpi();
  }
  alphaP = iod.ts.semi_impl.alphaP;

  ijk_zero_p = FindCornerFixedPressure();
}

//----------------------------------------------------------------------------

TimeIntegratorSIMPLE::~TimeIntegratorSIMPLE()
{ }

//----------------------------------------------------------------------------

void
TimeIntegratorSIMPLE::Destroy()
{
  VXstar.Destroy();
  VYstar.Destroy();
  VZstar.Destroy();
  Pprime.Destroy();
  B.Destroy();
  Homo.Destroy();
  DX.Destroy();
  DY.Destroy();
  DZ.Destroy();

  vlin_solver.Destroy();
  plin_solver.Destroy();

  TimeIntegratorBase::Destroy();
}

//----------------------------------------------------------------------------

void
TimeIntegratorSIMPLE::AdvanceOneTimeStep(SpaceVariable3D &V, SpaceVariable3D &ID,
                                         vector<SpaceVariable3D*>& Phi, vector<SpaceVariable3D*> &NPhi,
                                         vector<SpaceVariable3D*> &KappaPhi,
                                         SpaceVariable3D *L, SpaceVariable3D *Xi, SpaceVariable3D *LocalDt,
                                         [[maybe_unused]] double time, double dt, 
                                         [[maybe_unused]] int time_step, int subcycle, double dts)
{

  if(mpo.NumberOfMaterials()>1) {
    print_error("*** Error: Need to update homogeneity. Currently, the incompressible flow solver does not allow"
                " more than one material.\n");
    exit_mpi();
  }
  if(Phi.size()>0 || NPhi.size()>0 || KappaPhi.size()>0 || L || Xi || subcycle>0 || dts != dt) {
    print_error("*** Error: Problem setup is not supported by TimeIntegratorSIMPLE(or SIMPLEC).\n");
    exit_mpi();
  }


  GlobalMeshInfo &global_mesh(spo.GetGlobalMeshInfo());

  double*** id = ID.GetDataPointer();
  double*** homo = Homo.GetDataPointer();

  int iter, maxIter = time_step == 1 ? 10*iod.ts.semi_impl.maxIts : iod.ts.semi_impl.maxIts;
  vector<double> lin_rnorm; 
  bool lin_success, converged(false);
  double rel_err;

  if(type == SIMPLEC)
    print("  o Running the iterative SIMPLEC procedure.\n");
  else
    print("  o Running the iterative SIMPLE procedure.\n");

  for(iter = 0; iter < maxIter; iter++) {

    Vec5D*** v = (Vec5D***)V.GetDataPointer();

    ExtractVariableComponents(v, VXstar, VYstar, VZstar, Pprime);

    //-----------------------------------------------------
    // Step 1: Solve the momentum equations for u*, v*, w*
    //-----------------------------------------------------

    // Solve the x-momentum equation
    inco.BuildVelocityEquationSIMPLE(0, v, id, homo, vlin_rows, B, DX, type==SIMPLEC, Efactor, dt, LocalDt);
    vlin_solver.SetLinearOperator(vlin_rows);
    lin_success = vlin_solver.Solve(B, VXstar, NULL, NULL, &lin_rnorm);
    if(!lin_success) {
      print_warning("  x Warning: Linear solver for the x-momentum equation failed to converge.\n");
      for(int i=0; i<(int)lin_rnorm.size(); i++)
        print_warning("    > It. %d: residual = %e.\n", i+1, lin_rnorm[i]);
    }

    // Solve the y-momentum equation
    if(!global_mesh.IsMesh1D()) {
      inco.BuildVelocityEquationSIMPLE(1, v, id, homo, vlin_rows, B, DY, type==SIMPLEC, Efactor, dt, LocalDt);
      vlin_solver.SetLinearOperator(vlin_rows);
      vlin_solver.Solve(B, VYstar, NULL, NULL, &lin_rnorm);
    }
    if(!lin_success) {
      print_warning("  x Warning: Linear solver for the y-momentum equation failed to converge.\n");
      for(int i=0; i<(int)lin_rnorm.size(); i++)
        print_warning("      > It. %d: residual = %e.\n", i+1, lin_rnorm[i]);
    }

    // Solve the z-momentum equation
    if(!global_mesh.IsMesh1D() && !global_mesh.IsMesh2D()) {
      inco.BuildVelocityEquationSIMPLE(2, v, id, homo, vlin_rows, B, DZ, type==SIMPLEC, Efactor, dt, LocalDt);
      vlin_solver.SetLinearOperator(vlin_rows);
      vlin_solver.Solve(B, VZstar, NULL, NULL, &lin_rnorm);
    }
    if(!lin_success) {
      print_warning("  x Warning: Linear solver for the z-momentum equation failed to converge.\n");
      for(int i=0; i<(int)lin_rnorm.size(); i++)
        print_warning("    > It. %d: residual = %e.\n", i+1, lin_rnorm[i]);
    }

    
    //-----------------------------------------------------
    // Step 2: Solve the p' equation
    //-----------------------------------------------------
    inco.BuildPressureEquationSIMPLE(v, homo, VXstar, VYstar, VZstar, DX, DY, DZ, plin_rows, B, &ijk_zero_p);
    plin_solver.SetLinearOperator(plin_rows);
    plin_solver.Solve(B, Pprime, NULL, NULL, &lin_rnorm);
    if(!lin_success) {
      print_warning("  x Warning: Linear solver for the pressure correction equation failed to converge.\n");
      for(int i=0; i<(int)lin_rnorm.size(); i++)
        print_warning("    > It. %d: residual = %e.\n", i+1, lin_rnorm[i]);
    }


    //-----------------------------------------------------
    // Step 3: Update p, u, v, w, and compute relative error in velocity
    //         Rescale p and Pprime 
    //-----------------------------------------------------
    rel_err = UpdateStates(v, Pprime, DX, DY, DZ, VXstar, VYstar, VZstar, alphaP); 

    V.RestoreDataPointerAndInsert();

    if(rel_err<iod.ts.semi_impl.convergence_tolerance) {
      converged = true;
      break; 
    }

    print("  o It. %d: Relative error in velocity (2-norm): %e.\n", iter+1, rel_err);

  }
  if(converged)
    print("  o Converged after %d iterations. Relative error in velocity (2-norm): %e.\n", iter+1, rel_err);
  else
    print_warning("  o Failed to converge. Relative error in velocity (2-norm): %e.\n", rel_err);
    

  ID.RestoreDataPointerToLocalVector();
  Homo.RestoreDataPointerToLocalVector();
}

//----------------------------------------------------------------------------

void
TimeIntegratorSIMPLE::ExtractVariableComponents(Vec5D*** v, SpaceVariable3D &VX, SpaceVariable3D &VY,
                                                SpaceVariable3D &VZ, SpaceVariable3D &P)
{
  double*** vxstar = VX.GetDataPointer();
  double*** vystar = VY.GetDataPointer();
  double*** vzstar = VZ.GetDataPointer();
  double*** pp     = P.GetDataPointer();

  int ii0, jj0, kk0, iimax, jjmax, kkmax;
  VXstar.GetGhostedCornerIndices(&ii0, &jj0, &kk0, &iimax, &jjmax, &kkmax);

  for(int k=kk0; k<kkmax; k++)
    for(int j=jj0; j<jjmax; j++)
      for(int i=ii0; i<iimax; i++) {
        vxstar[k][j][i] = v[k][j][i][1];
        vystar[k][j][i] = v[k][j][i][2];
        vzstar[k][j][i] = v[k][j][i][3];
        pp[k][j][i]     = v[k][j][i][4];
      }

  VX.RestoreDataPointerToLocalVector(); //no need to exchange, as we have covered ghost layers
  VY.RestoreDataPointerToLocalVector();
  VZ.RestoreDataPointerToLocalVector();
  P.RestoreDataPointerToLocalVector();
}

//----------------------------------------------------------------------------

Int3
TimeIntegratorSIMPLE::FindCornerFixedPressure()
{
  GlobalMeshInfo &global_mesh(spo.GetGlobalMeshInfo());
  return Int3(global_mesh.NX-1, global_mesh.NY-1, global_mesh.NZ-1);
}

//----------------------------------------------------------------------------

double
TimeIntegratorSIMPLE::UpdateStates(Vec5D*** v, SpaceVariable3D &P, SpaceVariable3D &DX,
                                   SpaceVariable3D &DY, SpaceVariable3D &DZ,
                                   SpaceVariable3D &VX, SpaceVariable3D &VY,
                                   SpaceVariable3D &VZ, double prelax)
{

  double*** diagx = DX.GetDataPointer();
  double*** diagy = DY.GetDataPointer();
  double*** diagz = DZ.GetDataPointer();
  double*** ustar = VX.GetDataPointer();
  double*** vstar = VY.GetDataPointer();
  double*** wstar = VZ.GetDataPointer();
  double*** pp    = P.GetDataPointer();

  double uerr  = 0.0;
  double unorm = 0.0;
  double ucorr, vcorr, wcorr, unew, vnew, wnew;

  for(int k=k0; k<kmax; k++)
    for(int j=j0; j<jmax; j++)
      for(int i=i0; i<imax; i++) {

        ucorr = i>0 ? diagx[k][j][i]*(pprime[k][j][i-1] - pprime[k][j][i]) : 0.0;
        vcorr = j>0 ? diagy[k][j][i]*(pprime[k][j-1][i] - pprime[k][j][i]) : 0.0;
        wcorr = k>0 ? diagz[k][j][i]*(pprime[k-1][j][i] - pprime[k][j][i]) : 0.0;
     
        if(i>0) v[k][j][i][1] = ustar[k][j][i] + ucorr;
        if(j>0) v[k][j][i][2] = vstar[k][j][i] + vcorr;
        if(k>0) v[k][j][i][3] = wstar[k][j][i] + wcorr;
        v[k][j][i][4] += prelax*pp[k][j][i];
        pp[k][j][i] = 0.0; //reset to 0 for next iteration

        unew = v[k][j][i][1];
        vnew = v[k][j][i][2];
        wnew = v[k][j][i][2];

        unorm += unew*unew + vnew*vnew + wnew*wnew;
        uerr  += ucorr*ucorr + vcorr*vcorr + wcorr*wcorr;
      }

  MPI_Allreduce(MPI_IN_PLACE, &unorm, 1, MPI_DOUBLE, MPI_SUM, comm);
  MPI_Allreduce(MPI_IN_PLACE, &uerr, 1, MPI_DOUBLE, MPI_SUM, comm);

  DX.RestoreDataPointerToLocalVector();
  DY.RestoreDataPointerToLocalVector();
  DZ.RestoreDataPointerToLocalVector();
  VX.RestoreDataPointerToLocalVector();
  VY.RestoreDataPointerToLocalVector();
  VZ.RestoreDataPointerToLocalVector();
  P.RestoreDataPointerToLocalVector();
   
  return sqrt(uerr/unorm);

}


//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
// SIMPLER
//----------------------------------------------------------------------------

TimeIntegratorSIMPLER::TimeIntegratorSIMPLER(MPI_Comm &comm_, IoData& iod_, DataManagers3D& dms_,
                                             SpaceOperator& spo_, IncompressibleOperator &inco_,
                                             vector<LevelSetOperator*>& lso_, MultiPhaseOperator &mpo_,
                                             LaserAbsorptionSolver* laser_, EmbeddedBoundaryOperator* embed_,
                                             HyperelasticityOperator* heo_, PrescribedMotionOperator* pmo_)
                     : TimeIntegratorSIMPLE(comm_, iod_, dms_, spo_, inco_, lso_, mpo_, laser_, embed_, 
                                            heo_, pmo_)
{
  type = SIMPLER;

}

//----------------------------------------------------------------------------

TimeIntegratorSIMPLER::~TimeIntegratorSIMPLER()
{ }

//----------------------------------------------------------------------------

void
TimeIntegratorSIMPLER::Destroy()
{
  TimeIntegratorSIMPLE::Destroy();
}

//----------------------------------------------------------------------------

void
TimeIntegratorSIMPLER::AdvanceOneTimeStep(SpaceVariable3D &V, SpaceVariable3D &ID,
                                          vector<SpaceVariable3D*>& Phi, vector<SpaceVariable3D*> &NPhi,
                                          vector<SpaceVariable3D*> &KappaPhi,
                                          SpaceVariable3D *L, SpaceVariable3D *Xi, SpaceVariable3D *LocalDt,
                                          double time, double dt, int time_step, int subcycle, double dts)
{

  if(mpo.NumberOfMaterials()>1) {
    print_error("*** Error: Need to update homogeneity. Currently, the incompressible flow solver does not allow"
                " more than one material.\n");
    exit_mpi();
  }
  if(Phi.size()>0 || NPhi.size()>0 || KappaPhi.size()>0 || L || Xi || subcycle>0 || dts != dt) {
    print_error("*** Error: Problem setup is not supported by TimeIntegratorSIMPLEC.\n");
    exit_mpi();
  }


  GlobalMeshInfo &global_mesh(spo.GetGlobalMeshInfo());

  double*** id = ID.GetDataPointer();
  double*** homo = Homo.GetDataPointer();

  int iter, maxIter = time_step == 1 ? 10*iod.ts.semi_impl.maxIts : iod.ts.semi_impl.maxIts;
  vector<double> lin_rnorm; 
  bool lin_success, converged(false);
  double rel_err;

  print("  o Running the iterative SIMPLER procedure.\n");

  for(iter = 0; iter < maxIter; iter++) {

    Vec5D*** v = (Vec5D***)V.GetDataPointer();

    ExtractVariableComponents(v, VXstar, VYstar, VZstar, Pprime);

    
    //-----------------------------------------------------
    // Step 1: Solve the pressure (not p') equation
    //-----------------------------------------------------
    B.SetConstantValue(0.0);
    inco.EstimateVelocityForPressureSIMPLER(0, v, id, homo, VXstar, DX, Efactor, dt, LocalDt); //"Uhat"
    inco.EstimateVelocityForPressureSIMPLER(1, v, id, homo, VYstar, DY, Efactor, dt, LocalDt); //"Vhat"
    inco.EstimateVelocityForPressureSIMPLER(2, v, id, homo, VZstar, DZ, Efactor, dt, LocalDt); //"What"
    inco.BuildPressureEquationSIMPLE(v, homo, VXstar, VYstar, VZstar, DX, DY, DZ, plin_rows, B, &ijk_zero_p);
    plin_solver.SetLinearOperator(plin_rows);
    plin_solver.Solve(B, Pprime, NULL, NULL, &lin_rnorm);
    if(!lin_success) {
      print_warning("  x Warning: Linear solver for the pressure equation failed to converge.\n");
      for(int i=0; i<(int)lin_rnorm.size(); i++)
        print_warning("    > It. %d: residual = %e.\n", i+1, lin_rnorm[i]);
    }
    UpdatePressure(v, Pprime); //!< Pprime --> v[k][j][i][4]


    //-----------------------------------------------------
    // Step 2: Solve the momentum equations for u*, v*, w*
    // TODO: The equations can be built faster by reusing info in Estimate...
    //-----------------------------------------------------

    // Solve the x-momentum equation
    inco.BuildVelocityEquationSIMPLE(0, v, id, homo, vlin_rows, B, DX, Efactor, dt, LocalDt);
    vlin_solver.SetLinearOperator(vlin_rows);
    lin_success = vlin_solver.Solve(B, VXstar, NULL, NULL, &lin_rnorm);
    if(!lin_success) {
      print_warning("  x Warning: Linear solver for the x-momentum equation failed to converge.\n");
      for(int i=0; i<(int)lin_rnorm.size(); i++)
        print_warning("    > It. %d: residual = %e.\n", i+1, lin_rnorm[i]);
    }

    // Solve the y-momentum equation
    if(!global_mesh.IsMesh1D()) {
      inco.BuildVelocityEquationSIMPLE(1, v, id, homo, vlin_rows, B, DY, Efactor, dt, LocalDt);
      vlin_solver.SetLinearOperator(vlin_rows);
      vlin_solver.Solve(B, VYstar, NULL, NULL, &lin_rnorm);
    }
    if(!lin_success) {
      print_warning("  x Warning: Linear solver for the y-momentum equation failed to converge.\n");
      for(int i=0; i<(int)lin_rnorm.size(); i++)
        print_warning("      > It. %d: residual = %e.\n", i+1, lin_rnorm[i]);
    }

    // Solve the z-momentum equation
    if(!global_mesh.IsMesh1D() && !global_mesh.IsMesh2D()) {
      inco.BuildVelocityEquationSIMPLE(2, v, id, homo, vlin_rows, B, DZ, Efactor, dt, LocalDt);
      vlin_solver.SetLinearOperator(vlin_rows);
      vlin_solver.Solve(B, VZstar, NULL, NULL, &lin_rnorm);
    }
    if(!lin_success) {
      print_warning("  x Warning: Linear solver for the z-momentum equation failed to converge.\n");
      for(int i=0; i<(int)lin_rnorm.size(); i++)
        print_warning("    > It. %d: residual = %e.\n", i+1, lin_rnorm[i]);
    }

    
    //-----------------------------------------------------
    // Step 2: Solve the p' equation
    //-----------------------------------------------------
    inco.BuildPressureEquationSIMPLE(v, homo, VXstar, VYstar, VZstar, DX, DY, DZ, plin_rows, B, &ijk_zero_p);
    plin_solver.UsePreviousPreconditioner(true);
    plin_solver.SetLinearOperator(plin_rows);
    plin_solver.Solve(B, Pprime, NULL, NULL, &lin_rnorm);
    if(!lin_success) {
      print_warning("  x Warning: Linear solver for the pressure correction equation failed to converge.\n");
      for(int i=0; i<(int)lin_rnorm.size(); i++)
        print_warning("    > It. %d: residual = %e.\n", i+1, lin_rnorm[i]);
    }


    //-----------------------------------------------------
    // Step 3: Update p, u, v, w, and compute relative error in velocity
    //-----------------------------------------------------
    rel_err = UpdateVelocity(v); //Uses DX, DY, DZ, VXstar, VYstar, VZstar

    V.RestoreDataPointerAndInsert();

    if(rel_err<iod.ts.semi_impl.convergence_tolerance) {
      converged = true;
      break; 
    }

    print("  o It. %d: Relative error in velocity (2-norm): %e.\n", iter+1, rel_err);

  }
  if(converged)
    print("  o Converged after %d iterations. Relative error in velocity (2-norm): %e.\n", iter+1, rel_err);
  else
    print_warning("  o Failed to converge. Relative error in velocity (2-norm): %e.\n", rel_err);
    

  ID.RestoreDataPointerToLocalVector();
  Homo.RestoreDataPointerToLocalVector();
}

//----------------------------------------------------------------------------

void
TimeIntegratorSIMPLER::UpdatePressure(Vec5D*** v, SpaceVariable3D &P)
{

  double*** pp = P.GetDataPointer();

  for(int k=k0; k<kmax; k++)
    for(int j=j0; j<jmax; j++)
      for(int i=i0; i<imax; i++)
        v[k][j][i][4] = pp[k][j][i]; //reset to 0 for next iteration

  P.RestoreDataPointerToLocalVector();
   
}

//----------------------------------------------------------------------------
// SIMPLEC
//----------------------------------------------------------------------------

TimeIntegratorSIMPLEC::TimeIntegratorSIMPLEC(MPI_Comm &comm_, IoData& iod_, DataManagers3D& dms_,
                                             SpaceOperator& spo_, IncompressibleOperator &inco_,
                                             vector<LevelSetOperator*>& lso_, MultiPhaseOperator &mpo_,
                                             LaserAbsorptionSolver* laser_, EmbeddedBoundaryOperator* embed_,
                                             HyperelasticityOperator* heo_, PrescribedMotionOperator* pmo_)
                     : TimeIntegratorSIMPLE(comm_, iod_, dms_, spo_, inco_, lso_, mpo_, laser_, embed_, 
                                            heo_, pmo_)
{
  type = SIMPLEC;
  alphaP = 1.0; //!< fixed to 1.0 in the SIMPLEC algorithm
}

//----------------------------------------------------------------------------

TimeIntegratorSIMPLEC::~TimeIntegratorSIMPLEC()
{ }

//----------------------------------------------------------------------------


//----------------------------------------------------------------------------
// PISO 
//----------------------------------------------------------------------------

TimeIntegratorPISO::TimeIntegratorPISO(MPI_Comm &comm_, IoData& iod_, DataManagers3D& dms_,
                                       SpaceOperator& spo_, IncompressibleOperator &inco_,
                                       vector<LevelSetOperator*>& lso_, MultiPhaseOperator &mpo_,
                                       LaserAbsorptionSolver* laser_, EmbeddedBoundaryOperator* embed_,
                                       HyperelasticityOperator* heo_, PrescribedMotionOperator* pmo_)
                     : TimeIntegratorSIMPLE(comm_, iod_, dms_, spo_, inco_, lso_, mpo_, laser_, embed_, 
                                            heo_, pmo_)
{


}

//----------------------------------------------------------------------------

TimeIntegratorPISO::~TimeIntegratorPISO()
{ }

//----------------------------------------------------------------------------

void
TimeIntegratorPISO::Destroy()
{ 


  TimeIntegratorSIMPLE::Destroy();
}

//----------------------------------------------------------------------------

void
TimeIntegratorPISO::AdvanceOneTimeStep(SpaceVariable3D &V, SpaceVariable3D &ID,
                                       vector<SpaceVariable3D*>& Phi, vector<SpaceVariable3D*> &NPhi,
                                       vector<SpaceVariable3D*> &KappaPhi,
                                       SpaceVariable3D *L, SpaceVariable3D *Xi, SpaceVariable3D *LocalDt,
                                       double time, double dt, int time_step, int subcycle, double dts)
{
  print_error("*** Error: TimeIntegratorPISO::AdvanceOneTimeStep has not been implemented yet.\n");
}

//----------------------------------------------------------------------------








//----------------------------------------------------------------------------




//----------------------------------------------------------------------------







