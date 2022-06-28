#include<ViscoelasticityOperator.h>

//------------------------------------------------------------

ViscoelasticityOperator::ViscoelasticityOperator(MPI_Comm &comm_, DataManagers3D &dm_all_, 
                             IoData &iod_, SpaceVariable3D &coordinates_,
                             SpaceVariable3D &delta_xyz_, SpaceVariable3D &volume_,
                             std::vector<GhostPoint> &ghost_nodes_inner_,
                             std::vector<GhostPoint> &ghost_nodes_outer_)
                       : comm(comm_), iod(iod_),
                         refmap(comm_, dm_all_, iod_, coordinates_, delta_xyz_,
                                volume_, ghost_nodes_inner_, ghost_nodes_outer_)
{
  coordinates.GetCornerIndices(&i0, &j0, &k0, &imax, &jmax, &kmax);
  coordinates.GetGhostedCornerIndices(&ii0, &jj0, &kk0, &iimax, &jjmax, &kkmax);
}

//------------------------------------------------------------

ViscoelasticityOperator::~ViscoelasticityOperator()
{ }

//------------------------------------------------------------

void
ViscoelasticityOperator::Destroy()
{
  refmap.Destroy();
}

//------------------------------------------------------------

void
ViscoelasticityOperator::InitializeReferenceMap(SpaceVariable3D &Xi)
{
  refmap.SetInitialCondition(Xi);
}

//------------------------------------------------------------

void
ViscoelasticityOperator::ApplyBoundaryConditionsToReferenceMap(SpaceVariable3D &Xi)
{
  refmap.ApplyBoundaryConditions(Xi);
}

//------------------------------------------------------------

void
ViscoelasticityOperator::ComputeReferenceMapResidual(SpaceVariable3D &V, 
                                                     SpaceVariable3D &Xi, SpaceVariable3D &R)
{
  refmap.ComputeResidual(V,Xi,R);
}

//------------------------------------------------------------















//------------------------------------------------------------

