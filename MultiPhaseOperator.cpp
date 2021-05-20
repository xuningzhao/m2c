#include<MultiPhaseOperator.h>
#include<SpaceOperator.h>
#include<LevelSetOperator.h>
#include<Vector5D.h>
#include<RiemannSolutions.h>
using std::cout;
using std::endl;
//-----------------------------------------------------

MultiPhaseOperator::MultiPhaseOperator(MPI_Comm &comm_, DataManagers3D &dm_all_, IoData &iod_,
                                       SpaceOperator &spo, vector<LevelSetOperator*> &lso)
                  : comm(comm_), iod_multiphase(iod_.multiphase),
                    coordinates(spo.GetMeshCoordinates()),
                    delta_xyz(spo.GetMeshDeltaXYZ()),
                    Tag(comm_, &(dm_all_.ghosted1_1dof))
{
  coordinates.GetCornerIndices(&i0, &j0, &k0, &imax, &jmax, &kmax);
  coordinates.GetGhostedCornerIndices(&ii0, &jj0, &kk0, &iimax, &jjmax, &kkmax);

  for(int i=0; i<lso.size(); i++)
    ls2matid[i] = lso[i]->GetMaterialID();
}

//-----------------------------------------------------

MultiPhaseOperator::~MultiPhaseOperator()
{ }

//-----------------------------------------------------

void
MultiPhaseOperator::Destroy()
{
  Tag.Destroy();
}

//-----------------------------------------------------

void 
MultiPhaseOperator::UpdateMaterialID(vector<SpaceVariable3D*> &Phi, SpaceVariable3D &ID)
{
  // reset tag to 0
  Tag.SetConstantValue(0, true/*workOnGhost*/);
  ID.SetConstantValue(0, true/*workOnGhost*/);
  int overlap = 0;

  double*** tag = (double***)Tag.GetDataPointer();
  double*** id  = (double***)ID.GetDataPointer();

  for(int ls = 0; ls<Phi.size(); ls++) {//loop through all the level set functions
  
    double*** phi = (double***)Phi[ls]->GetDataPointer();
    int matid = ls2matid[ls];

    for(int k=kk0; k<kkmax; k++)
      for(int j=jj0; j<jjmax; j++)
        for(int i=ii0; i<iimax; i++) {
          if(phi[k][j][i]<0) {
            if(id[k][j][i] != 0) {
              overlap++;
              tag[k][j][i] = 1;
            } 
            id[k][j][i] = matid; 
          }

        }

    Phi[ls]->RestoreDataPointerToLocalVector(); //no changes made
  }

  MPI_Allreduce(MPI_IN_PLACE, &overlap, 1, MPI_INT, MPI_SUM, comm);


  if(overlap) {
    print_error("*** Error: Found overlapping material interfaces. Number of overlapped cells: %d.\n", overlap);
    exit_mpi();
  } 


  if(overlap) 
    Tag.RestoreDataPointerAndInsert();
  else
    Tag.RestoreDataPointerToLocalVector();
}

//-----------------------------------------------------
// Section 4.2.4 of Arthur Rallu's thesis
void
MultiPhaseOperator::UpdateStateVariablesAfterInterfaceMotion(SpaceVariable3D &IDn, 
                        SpaceVariable3D &ID, SpaceVariable3D &V, RiemannSolutions &riemann_solutions)
{
  switch (iod_multiphase.phasechange_type) {

    case MultiPhaseData::RIEMANN_SOLUTION :
      UpdateStateVariablesByRiemannSolutions(IDn, ID, V, riemann_solutions);
      break;

    case MultiPhaseData::EXTRAPOLATION :
      UpdateStateVariablesByExtrapolation(IDn, ID, V);
      break;

    default :
      print_error("*** Error: Specified method for phase-change update (%d) has not been implemented.\n", 
                  (int)iod_multiphase.phasechange_type);
  }
}

//-----------------------------------------------------

void
MultiPhaseOperator::UpdateStateVariablesByRiemannSolutions(SpaceVariable3D &IDn, 
                        SpaceVariable3D &ID, SpaceVariable3D &V, RiemannSolutions &riemann_solutions)
{
  // extract info
  double*** idn = (double***)IDn.GetDataPointer();
  double*** id  = (double***)ID.GetDataPointer();
  Vec5D***  v   = (Vec5D***) V.GetDataPointer();

  Int3 ind;
  std::map<Int3, std::pair<Vec5D,int> >::iterator it;

  // work inside the real domain
  int counter = 0;
  for(int k=k0; k<kmax; k++)
    for(int j=j0; j<jmax; j++)
      for(int i=i0; i<imax; i++) {

        if(id[k][j][i] == idn[k][j][i]) //id remains the same. Skip
          continue;
        
        // re-set its state variables to 0
        v[k][j][i] = 0.0;

        counter = LocalUpdateByRiemannSolutions(i, j, k, id[k][j][i], v[k][j][i-1], v[k][j][i+1], 
                      v[k][j-1][i], v[k][j+1][i], v[k-1][j][i], v[k+1][j][i], riemann_solutions,
                      v[k][j][i], true);
        if(counter==0)
          LocalUpdateByRiemannSolutions(i, j, k, id[k][j][i], v[k][j][i-1], v[k][j][i+1], 
              v[k][j-1][i], v[k][j+1][i], v[k-1][j][i], v[k+1][j][i], riemann_solutions,
              v[k][j][i], false);
      }  

  ID.RestoreDataPointerToLocalVector();
  IDn.RestoreDataPointerToLocalVector();

  V.RestoreDataPointerAndInsert(); //insert data & communicate with neighbor subd's

} 

//-----------------------------------------------------

int
MultiPhaseOperator::LocalUpdateByRiemannSolutions(int i, int j, int k, int id, Vec5D &vl, Vec5D &vr, 
                        Vec5D &vb, Vec5D &vt, Vec5D &vk, Vec5D &vf, RiemannSolutions &riemann_solutions, 
                        Vec5D &v, bool upwind)
{
  int counter = 0;
  double weight = 0, sum_weight = 0;
  Int3 ind(k,j,i);

  // left
  auto it = riemann_solutions.left.find(ind);
  if(it != riemann_solutions.left.end()) {
    if(it->second.second/*ID*/ == id && (!upwind || vl[1] > 0)) {
      Vec3D v1(vl[1], vl[2], vl[3]);
      weight = upwind ? vl[1]/v1.norm() : 1.0;
      sum_weight += weight;
      v += weight*it->second.first; /*riemann solution*/
      counter++;
    }
  }

  // right
  it = riemann_solutions.right.find(ind);
  if(it != riemann_solutions.right.end()) {
    if(it->second.second/*ID*/ == id && (!upwind || vr[1] < 0)) {
      Vec3D v1(vr[1], vr[2], vr[3]);
      weight = upwind ? -vr[1]/v1.norm() : 1.0;
      sum_weight += weight;
      v += weight*it->second.first; /*riemann solution*/
      counter++;
    }
  }

  // bottom
  it = riemann_solutions.bottom.find(ind);
  if(it != riemann_solutions.bottom.end()) {
    if(it->second.second/*ID*/ == id && (!upwind || vb[2] > 0)) {
      Vec3D v1(vb[1], vb[2], vb[3]);
      weight = upwind ? vb[2]/v1.norm() : 1.0;
      sum_weight += weight;
      v += weight*it->second.first; /*riemann solution*/
      counter++;
    }
  }

  // top
  it = riemann_solutions.top.find(ind);
  if(it != riemann_solutions.top.end()) {
    if(it->second.second/*ID*/ == id && (!upwind || vt[2] < 0)) {
      Vec3D v1(vt[1], vt[2], vt[3]);
      weight = upwind ? -vt[2]/v1.norm() : 1.0;
      sum_weight += weight;
      v += weight*it->second.first; /*riemann solution*/
      counter++;
    }
  }

  // back
  it = riemann_solutions.back.find(ind);
  if(it != riemann_solutions.back.end()) {
    if(it->second.second/*ID*/ == id && (!upwind || vk[3] > 0)) {
      Vec3D v1(vk[1], vk[2], vk[3]);
      weight = upwind ? vk[3]/v1.norm() : 1.0;
      sum_weight += weight;
      v += weight*it->second.first; /*riemann solution*/
      counter++;
    }
  }

  // front
  it = riemann_solutions.front.find(ind);
  if(it != riemann_solutions.front.end()) {
    if(it->second.second/*ID*/ == id && (!upwind || vf[3] < 0)) {
      Vec3D v1(vf[1], vf[2], vf[3]);
      weight = upwind ? -vf[3]/v1.norm() : 1.0;
      sum_weight += weight;
      v += weight*it->second.first; /*riemann solution*/
      counter++;
    }
  }

  if(sum_weight > 0.0)
    v /= sum_weight;
  else if(upwind)
    fprintf(stderr,"*** Warning: Unable to update phase change at (%d,%d,%d) based on upwinding.\n", i,j,k);
  else 
    fprintf(stderr,"*** Error: Unable to update phase change at (%d,%d,%d) by averaging Riemann solutions.\n", i,j,k);

  return counter;
}

//-----------------------------------------------------

void
MultiPhaseOperator::UpdateStateVariablesByExtrapolation(SpaceVariable3D &IDn, 
                        SpaceVariable3D &ID, SpaceVariable3D &V)
{
  // extract info
  double*** idn = (double***)IDn.GetDataPointer();
  double*** id  = (double***)ID.GetDataPointer();
  Vec5D***  v   = (Vec5D***) V.GetDataPointer();

  Vec3D*** coords = (Vec3D***)coordinates.GetDataPointer();

  double weight, sum_weight;
  Vec3D v1, x1x0;
  double v1norm;

  // work inside the real domain
  for(int k=k0; k<kmax; k++)
    for(int j=j0; j<jmax; j++)
      for(int i=i0; i<imax; i++) {

        if(id[k][j][i] == idn[k][j][i]) //id remains the same. Skip
          continue;

        // re-set its state variables to 0
        v[k][j][i] = 0.0;

        // coordinates of this node
        Vec3D& x0(coords[k][j][i]);

        sum_weight = 0.0;

        //go over the neighboring nodes 
        for(int neighk = k-1; neighk <= k+1; neighk++)         
          for(int neighj = j-1; neighj <= j+1; neighj++)
            for(int neighi = i-1; neighi <= i+1; neighi++) {

              if(id[neighk][neighj][neighi] != id[k][j][i])
                continue; //this neighbor has a different ID. Skip it.

              if(id[neighk][neighj][neighi] != idn[neighk][neighj][neighi])
                continue; //this neighbor also changed ID. Skip it. (Also skipping node [k][j][i])

              if(ID.OutsidePhysicalDomain(neighi, neighj, neighk))
                continue; //this neighbor is outside the physical domain. Skip.

              // coordinates and velocity at the neighbor node
              Vec3D& x1(coords[neighk][neighj][neighi]);
              v1[0] = v[neighk][neighj][neighi][1];
              v1[1] = v[neighk][neighj][neighi][2];
              v1[2] = v[neighk][neighj][neighi][3];

              // compute weight
              v1norm = v1.norm();
              if(v1norm != 0)
                v1 /= v1norm;
              x1x0 = x0 - x1; 
              x1x0 /= x1x0.norm();

              weight = max(0.0, x1x0*v1);

              // add weighted s.v. at neighbor node
              if(weight>0) {
                sum_weight += weight;
                v[k][j][i] += weight*v[neighk][neighj][neighi];
              }
            }

        v[k][j][i] /= sum_weight; 
      }

  coordinates.RestoreDataPointerToLocalVector();
  ID.RestoreDataPointerToLocalVector();
  IDn.RestoreDataPointerToLocalVector();

  V.RestoreDataPointerAndInsert(); //insert data & communicate with neighbor subd's

}

//-----------------------------------------------------

