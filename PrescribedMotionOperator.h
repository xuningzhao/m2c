/************************************************************************
 * Copyright © 2020 The Multiphysics Modeling and Computation (M2C) Lab
 * <kevin.wgy@gmail.com> <kevinw3@vt.edu>
 ************************************************************************/

#ifndef _PRESCRIBED_MOTION_OPERATOR_H_
#define _PRESCRIBED_MOTION_OPERATOR_H_

#include<IoData.h>

/*********************************************************************
 * class PrescribedMotionOperator enforces user-prescribed velocities
 * for specific material subdomains
 *********************************************************************
*/
class PrescribedMotionOperator {

  struct VelocityTimeHistory {
    std::vector<double> time;
    std::vector<Vec3D> velocity;
  };

  //! maps material id to velocity time history (if constant ==> just one entry)
  std::map<int,VelocityTimeHistory> velo;

  PrescribedMotionOperator( 
  I AM HERE

};

#endif
