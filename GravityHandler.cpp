/************************************************************************
 * Copyright © 2020 The Multiphysics Modeling and Computation (M2C) Lab
 * <kevin.wgy@gmail.com> <kevinw3@vt.edu>
 ************************************************************************/

#include <GravityHandler.h>
#include <LevelSetOperator.h>
#include <EmbeddedBoundaryDataSet.h>
#include <FloodFill.h>
#include <GeoTools.h>
#include <Vector5D.h>

using std::vector;

//------------------------------------------------------------------------

GravityHandler::GravityHandler(MPI_Comm &comm_, DataManagers3D &dm_all_, IoData &iod_, SpaceVariable3D &coordinates_,
                               vector<GhostPoint> &ghost_nodes_inner_, vector<GhostPoint> &ghost_nodes_outer_,
                               GlobalMeshInfo &global_mesh_)
              : comm(comm_), dm_all(dm_all_), iod(iod_), coordinates(coordinates_),
                ghost_nodes_inner(ghost_nodes_inner_), ghost_nodes_outer(ghost_nodes_outer_),
                global_mesh(global_mesh_)
{
  coordinates.GetCornerIndices(&i0, &j0, &k0, &imax, &jmax, &kmax);
  coordinates.GetGhostedCornerIndices(&ii0, &jj0, &kk0, &iimax, &jjmax, &kkmax);
  coordinates.GetGlobalSize(&NX, &NY, &NZ);
}

//------------------------------------------------------------------------

GravityHandler::~GravityHandler()
{ }

//------------------------------------------------------------------------

void
GravityHandler::Destroy()
{ }

//------------------------------------------------------------------------

void
GravityHandler::UpdateInitialConditionByFlooding(SpaceVariable3D &V, SpaceVariable3D &ID, 
                                                 vector<LevelSetOperator*> lso, vector<SpaceVariable3D*>& Phi,
                                                 std::unique_ptr<vector<std::unique_ptr<EmbeddedBoundaryDataSet> > >
                                                 EBDS)
{

  if(iod.ic.floodIc.source_x == DBL_MAX || iod.ic.floodIc.source_y == DBL_MAX ||
     iod.ic.floodIc.source_z == DBL_MAX)
    return; //user did not specify this

  if(iod.ic.floodIc.gx == 0.0 && iod.ic.floodIc.gy == 0.0 && iod.ic.floodIc.gz == 0.0) {
    print_error("*** Error: In InitialCondition.Flood, gravitational acceleration vector is 0.\n");
    exit_mpi();
  }


  // Find the material id of "water" and the level set function (phi) that tracks it
  int water_matid = iod.ic.floodIc.waterline_ic.materialid;
  int water_lsid = -1;
  if(water_matid>0) {//should have a lso that tracks water_matid
    for(int i=0; i<(int)lso.size(); i++)
      if(lso[i]->GetMaterialID() == water_matid) {
        water_lsid = i;
        break;
      }
    if(water_lsid == -1) {
      print_error("*** Error: A level set function should be defined to track material %d (flooded).\n",
                  water_matid);
      exit_mpi();
    }
  }
 

  // Get user-specified parameter values
  Vec3D source(iod.ic.floodIc.source_x, iod.ic.floodIc.source_y, iod.ic.floodIc.source_z);
  Vec3D wl(iod.ic.floodIc.waterline_x, iod.ic.floodIc.waterline_y, iod.ic.floodIc.waterline_z);
  Vec3D gravity(iod.ic.floodIc.gx, iod.ic.floodIc.gy, iod.ic.floodIc.gz);
  double gnorm = gravity.norm();
  Vec3D gdir = gravity / gnorm;
  double p0 = iod.ic.floodIc.waterline_ic.pressure;
  double rho0 = iod.ic.floodIc.waterline_ic.density;
  Vec3D v0(iod.ic.floodIc.waterline_ic.velocity_x, iod.ic.floodIc.waterline_ic.velocity_y,
           iod.ic.floodIc.waterline_ic.velocity_z);


  // Extract data
  double*** phi = NULL;
  if(water_lsid>=0) 
    phi = Phi[water_lsid]->GetDataPointer();

  Vec5D***      v = (Vec5D***) V.GetDataPointer();
  double***    id = ID.GetDataPointer();

  // Get intersection data (if any)
  vector<Vec3D***> xf;
  if(EBDS)
    for(auto&& ebds : *EBDS)
      xf.push_back((Vec3D***)ebds->XForward_ptr->GetDataPointer());


  // Create temporary variables
  SpaceVariable3D Obs(comm, &(dm_all.ghosted1_3dof)); //default value is 0
  SpaceVariable3D Dist(comm, &(dm_all.ghosted1_1dof)); //distance to surface (signed)
  SpaceVariable3D Color(comm, &(dm_all.ghosted1_1dof));

  Vec3D***    ob = (Vec3D***)Obs.GetDataPointer();
  double*** dist = Dist.GetDataPointer();


  // Calculate signed distance to water surface (need to include ghosts, used below)
  Vec3D*** coords = (Vec3D***) coordinates.GetDataPointer();
  for(int k=kk0; k<kkmax; k++)
    for(int j=jj0; j<jjmax; j++)
      for(int i=ii0; i<iimax; i++)
        dist[k][j][i] = GeoTools::ProjectPointToPlane(coords[k][j][i], wl, gdir, true);
  coordinates.RestoreDataPointerToLocalVector();

  // Separate regions
  for(int k=k0; k<kmax; k++)
    for(int j=j0; j<jmax; j++)
      for(int i=i0; i<imax; i++) {

        if(i>=0 && id[k][j][i] != id[k][j][i-1])   //Does not add edge obstructions at boundaries
          ob[k][j][i][0] = 1;
        if(j>=0 && id[k][j][i] != id[k][j-1][i])
          ob[k][j][i][1] = 1;
        if(k>=0 && id[k][j][i] != id[k-1][j][i])
          ob[k][j][i][2] = 1;

        for(auto&& myxf : xf)
          for(int p=0; p<3; p++)
            if(myxf[k][j][i][p] >= 0)  //-1 means no intersection
              ob[k][j][i][p] += 10; //the number can be used for debugging

        if(i>=0 && dist[k][j][i]*dist[k][j][i-1]<=0.0)
          ob[k][j][i][0] += 100;
        if(j>=0 && dist[k][j][i]*dist[k][j-1][i]<=0.0)
          ob[k][j][i][1] += 100;
        if(k>=0 && dist[k][j][i]*dist[k-1][j][i]<=0.0)
          ob[k][j][i][2] += 100;

      }

  Obs.RestoreDataPointerAndInsert();


  // Create & run flood-filler
  FloodFill floodfiller(comm, dm_all, ghost_nodes_inner, ghost_nodes_outer);
  std::set<Int3> occluded;
  floodfiller.FillBasedOnEdgeObstructions(Obs, 0, occluded, Color); //0 is the non-obstructing tag
  double*** color = Color.GetDataPointer();


  // Figure out the "color" of flooded area
  Int3 source_ijk = global_mesh.FindClosestNodeToPoint(source, false); //false means not including ext ghosts
  int flood_color = -INT_MAX;
  if(source_ijk[0]>=i0 && source_ijk[0]<imax && source_ijk[1]>=j0 && source_ijk[1]<jmax &&
     source_ijk[2]>=k0 && source_ijk[2]<jmax)
    flood_color = color[source_ijk[2]][source_ijk[1]][source_ijk[0]];
  MPI_Allreduce(MPI_IN_PLACE, &flood_color, 1, MPI_INT, MPI_MAX, comm);
  if(flood_color == -INT_MAX) {
    print_error("*** Error: The flood source point seems to be outside the computational domain.\n");
    exit_mpi();
  }

  
  // --------------------------------------------------------
  // Update V, ID, and Phi
  for(int k=k0; k<kmax; k++)
    for(int j=j0; j<jmax; j++)
      for(int i=i0; i<imax; i++) {
        // first, update phi
        if(phi) {
          double dxmin_half = 0.5*global_mesh.GetMinDXYZ(Int3(i,j,k));
          if(color[k][j][i] == flood_color)
            phi[k][j][i] = -dxmin_half;
          else
            phi[k][j][i] = dxmin_half;
/*
          if(fabs(dist[k][j][i])<fabs(phi[k][j][i])) {
            if(color[k][j][i] == flood_color)
              phi[k][j][i] = -fabs(dist[k][j][i]); //negative inside the material subdomain
            else { // does not change the sign
              if(phi[k][j][i]>=0)
                phi[k][j][i] = fabs(dist[k][j][i]);
              else
                phi[k][j][i] = -fabs(dist[k][j][i]);
            }
          }
          else {
            if(color[k][j][i] == flood_color)
              phi[k][j][i] = -fabs(phi[k][j][i]);
            // if color is not flood_color, it may still be the same (water) material, i.e. inside the subdomain.
          }
*/
        }

        // update ID and V
        if(color[k][j][i] == flood_color) {
          id[k][j][i] = water_matid;
          v[k][j][i][0] = rho0;
          v[k][j][i][1] = v0[0];
          v[k][j][i][2] = v0[1];
          v[k][j][i][3] = v0[2];
          v[k][j][i][4] = p0 + rho0*gnorm*dist[k][j][i];
        }
      }

  // --------------------------------------------------------


  // Restore data
  if(xf.size()>0)
    for(auto&& ebds : *EBDS)
      ebds->XForward_ptr->RestoreDataPointerToLocalVector();


  V.RestoreDataPointerAndInsert();
  ID.RestoreDataPointerAndInsert();
  if(water_lsid>=0)
    Phi[water_lsid]->RestoreDataPointerAndInsert();

  lso[water_lsid]->ApplyBoundaryConditions(*Phi[water_lsid]);
  lso[water_lsid]->Reinitialize(0.0, 1.0, 0.0, *Phi[water_lsid], 600, true);
  MPI_Barrier(comm);
  print("Done Done\n");

  Color.RestoreDataPointerToLocalVector();
  print("Done Done 2\n");
  print("Done Done 3\n");
  Dist.RestoreDataPointerToLocalVector();

  print("Done Done 4\n");
  //Destroy locally created objects
  Obs.Destroy();
  Dist.Destroy();
  Color.Destroy();
  floodfiller.Destroy();

  print("Done Done 5\n");
  V.StoreMeshCoordinates(coordinates);
  V.WriteToVTRFile("V.vtr", "V");
  ID.StoreMeshCoordinates(coordinates);
  ID.WriteToVTRFile("ID.vtr", "ID");
  print("Done Done 6\n");
  Phi[water_lsid]->StoreMeshCoordinates(coordinates);
  Phi[water_lsid]->WriteToVTRFile("Phi.vtr", "Phi");
  exit_mpi();
}

//------------------------------------------------------------------------




//------------------------------------------------------------------------




//------------------------------------------------------------------------

