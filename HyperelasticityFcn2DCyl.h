/************************************************************************
 * Copyright © 2020 The Multiphysics Modeling and Computation (M2C) Lab
 * <kevin.wgy@gmail.com> <kevinw3@vt.edu>
 ************************************************************************/

#ifndef _HYPERELASTICITY_FCN_2DCYL_H_
#define _HYPERELASTICITY_FCN_2DCYL_H_

#include<HypervelocityFcn.h>

/****************************************************
 * Class HyperelasticityFcnBase2DCyl and the derived classes
 * are children of HyperelasticityFcnBase. These classes are
 * designed for problems with cylindrical symmetry solved on
 * a 2D mesh. The "x" and "y" coordinates of the 2D mesh
 * correspond to the "z" and "r" cylindrical coordinates,
 * respectively. These functions compute the fluxes associated
 * with "\sigma_{2D}$ --- see KW's notes. There are additional
 * source terms, which need to be added somewhere else.
 *
 * Note 1: The input matrix F (deformation gradient) is constructed
 *       as follows:
 *       F[0] = dz/dZ;   F[3] = 0.0;   F[6] = dz/dR;
 *       F[1] = 0.0;     F[4] = r/R;   F[7] = 0.0;
 *       F[2] = dr/dZ;   F[5] = 0.0;   F[8] = dr/dR;
 *
 * Note 2: When combining EOS and hyperelasticity models, a
 *         method is to compute pressure from EOS, and *only*
 *         the deviatoric stress from hyperelasticity. In this
 *         class, the "GetCauchyStress" function computes the
 *         complete stress tensor. But the "EvaluateFlux" function
 *         allows the user to make a decision between the full
 *         tensor and the deviatoric part.
 * Note 3: Matrices follow the 'column-major' / 'column-first'
 *         convention. For example, A[1] is the A(2,1) entry.
 * Note 4: The Cauchy stress tensor (\sigma_{2D}) is symmetric, so
 *         only 3 entries are stored: sigma[0] = sigma_zz,
 *         sigma[1] = sigma_zr, sigma[2] = sigma_rr
 ****************************************************/

//---------------------------------------------------------------------------------
//
class HyperelasticityFcnBase2DCyl : public HyperelasticityFcnBase {

protected:

  double M2x2[4], N2x2[4]; //!< for temporary use

public:

  HyperelasticityFcnBase2DCyl(VarFcnBase &vf_) : HyperelasticityFcnBase(vf_) {}
  virtual ~HyperelasticityFcnBase2DCyl() {}

  virtual void GetCauchyStressTensor([[maybe_unused]] double *F, [[maybe_unused]] double *V, double *sigma) {
    for(int i=0; i<3; i++)
      sigma[i] = 0.0;
  }

  //! compute the flux function
  void EvaluateHyperelasticFluxFunction_F(double* flux/*output*/, double* F, double* V/*state var.*/,
                                          bool deviatoric_only = true);
  void EvaluateHyperelasticFluxFunction_G(double* flux/*output*/, double* F, double* V/*state var.*/,
                                          bool deviatoric_only = true);
  void EvaluateHyperelasticFluxFunction_H(double* flux/*output*/, double* F, double* V/*state var.*/,
                                          bool deviatoric_only = true);

protected:

  void ConvertPK2ToCauchy(double* P, double *F, double J, double *sigma); //!< sigma[0-2] (because of symmetry)

};

//---------------------------------------------------------------------------------

class HyperelasticityFcnSaintVenantKirchhoff2DCyl : public HyperelasticityFcnBase2DCyl {

  double lambda, mu; //first and second Lame constants

public:

  HyperelasticityFcnSaintVenantKirchhoff2DCyl(HyperelasticityModelData &hyper, VarFcnBase &vf_);
  ~HyperelasticityFcnSaintVenantKirchhoff2DCyl() {}
  
  void GetCauchyStressTensor(double *F, double *V, double *sigma);

};

//---------------------------------------------------------------------------------

class HyperelasticityFcnModifiedSaintVenantKirchhoff2DCyl : public HyperelasticityFcnBase2DCyl {

  double kappa, mu; //bulk modulus and the second Lame constant (i.e. shear modulus)

public:

  HyperelasticityFcnModifiedSaintVenantKirchhoff2DCyl(HyperelasticityModelData &hyper, VarFcnBase &vf_);
  ~HyperelasticityFcnModifiedSaintVenantKirchhoff2DCyl() {}
  
  void GetCauchyStressTensor(double *F, double *V, double *sigma);

};

//---------------------------------------------------------------------------------

class HyperelasticityFcnNeoHookean2DCyl: public HyperelasticityFcnBase2DCyl {

  double kappa, mu; //bulk modulus and the second Lame constant (i.e. shear modulus)

public:

  HyperelasticityFcnNeoHookean2DCyl(HyperelasticityModelData &hyper, VarFcnBase &vf_);
  ~HyperelasticityFcnNeoHookean2DCyl() {}
  
  void GetCauchyStressTensor(double *F, double *V, double *sigma);

};

//---------------------------------------------------------------------------------

class HyperelasticityFcnMooneyRivlin2DCyl: public HyperelasticityFcnBase2DCyl {

  double kappa, C01, C10; //kappa: bulk modulus, C01+C10 = mu/2

public:

  HyperelasticityFcnMooneyRivlin2DCyl(HyperelasticityModelData &hyper, VarFcnBase &vf_);
  ~HyperelasticityFcnMooneyRivlin2DCyl() {}
  
  void GetCauchyStressTensor(double *F, double *V, double *sigma);

};

//---------------------------------------------------------------------------------


#endif
