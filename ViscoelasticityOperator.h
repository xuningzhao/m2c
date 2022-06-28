#ifndef _VISCOELASTICITY_OPERATOR_H_
#define _VISCOELASTICITY_OPERATOR_H_
#include<ReferenceMapOperator.h>

/*********************************************************
 * class ViscoelasticityOperator is responsible for
 * computing the elastic and viscoelastic stresses for
 * solid and solid-like materials.
 *********************************************************/

class ViscoelasticityOperator
{
  MPI_Comm& comm;
  IoData &iod;

  int i0, j0, k0, imax, jmax, kmax; //!< corners of the real subdomain
  int ii0, jj0, kk0, iimax, jjmax, kkmax; //!< corners of the ghosted subdomain

  //! Solver of the reference map
  ReferenceMapOperator refmap;

public:

  ViscoelasticityOperator(MPI_Comm &comm_, DataManagers3D &dm_all_, IoData &iod_, SpaceVariable3D &coordinates_,
                          SpaceVariable3D &delta_xyz_, SpaceVariable3D &volume_,
                          std::vector<GhostPoint> &ghost_nodes_inner_,
                          std::vector<GhostPoint> &ghost_nodes_outer_);
  ~ViscoelasticityOperator();

  void Destroy();

  void InitializeReferenceMap(SpaceVariable3D &Xi);

  void ApplyBoundaryConditionsToReferenceMap(SpaceVariable3D &Xi);

  void ComputeReferenceMapResidual(SpaceVariable3D &V, SpaceVariable3D &Xi, SpaceVariable3D &R);

};

#endif
