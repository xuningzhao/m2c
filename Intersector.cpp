#include<Intersector.h>
#include<GeoTools.h>
using std::pair;
using std::vector;

extern int verbose;
extern double domain_diagonal;
//-------------------------------------------------------------------------

Intersector::Intersector(MPI_Comm &comm_, DataManagers3D &dms_, EmbeddedSurfaceData &iod_surface_,
                         TriangulatedSurface &surface_, SpaceVariable3D &coordinates_, 
                         SpaceVariable3D &delta_xyz_, SpaceVariable3D &volume_,
                         vector<GhostPoint> &ghost_nodes_inner_, vector<GhostPoint> &ghost_nodes_outer_)
           : comm(comm_), iod_surface(iod_surface_), surface(surface_), tree(NULL),
             coordinates(coordinates_), delta_xyz(delta_xyz_), volume(volume_),
             ghost_nodes_inner(ghost_nodes_inner_), ghost_nodes_outer(ghost_nodes_outer_),
             BBmin(comm_, &(dms_.ghosted1_3dof)),
             BBmax(comm_, &(dms_.ghosted1_3dof)),
             TMP(comm_, &(dms_.ghosted1_1dof)),
             TMP2(comm_, &(dms_.ghosted1_1dof)),
             CandidatesIndex(comm_, &(dms_.ghosted1_1dof)),
             XForward(comm_, &(dms_.ghosted1_3dof)),
             XBackward(comm_, &(dms_.ghosted1_3dof)),
             Phi(comm_, &(dms_.ghosted1_1dof)),
             Sign(comm_, &(dms_.ghosted1_1dof)),
             floodfiller(comm_, dms_, ghost_nodes_inner_, ghost_nodes_outer_)
{

  CandidatesIndex.SetConstantValue(-1, true);
  XForward.SetConstantValue(-1, true);
  XBackward.SetConstantValue(-1, true);

  half_thickness = 0.5*iod_surface.tracker.surface_thickness;

  coordinates.GetCornerIndices(&i0, &j0, &k0, &imax, &jmax, &kmax);
  coordinates.GetGhostedCornerIndices(&ii0, &jj0, &kk0, &iimax, &jjmax, &kkmax);
  coordinates.GetInternalGhostedCornerIndices(&ii0_in, &jj0_in, &kk0_in, &iimax_in, &jjmax_in, &kkmax_in);
  coordinates.GetGlobalSize(&NX, &NY, &NZ);

  // Set the capacity of internal vectors, so we don't frequently reallocate memory
  int capacity = (imax-i0)*(jmax-j0)*(kmax-k0)/4; //should be big enough
  intersections.reserve(capacity);
  occluded.reserve(capacity); 
  candidates.reserve(capacity*2);
  scope.reserve(surface.elems.size());

  //sanity checks on the triangulated surface
  if(surface.degenerate) {
    print_error("*** Error: Intersector cannot track a degenerate surface.\n");
    exit(-1);
  }
  assert(!(surface.node2node.empty() || 
           surface.node2elem.empty() || 
           surface.elem2elem.empty()));

  closed_surface = surface.CheckSurfaceOrientationAndClosedness();

  surface.CalculateNormalsAndAreas();

  //build nodal bounding boxes
  BuildNodalBoundingBoxes();

}

//-------------------------------------------------------------------------

Intersector::~Intersector()
{
  if(tree)
    delete tree;
}

//-------------------------------------------------------------------------

void
Intersector::Destroy()
{
  floodfiller.Destroy();

  BBmin.Destroy();
  BBmax.Destroy();
  TMP.Destroy();
  TMP2.Destroy();
  CandidatesIndex.Destroy();
  XForward.Destroy();
  XBackward.Destroy();
  Phi.Destroy();
  Sign.Destroy();
}

//-------------------------------------------------------------------------

void
Intersector::BuildNodalBoundingBoxes()
{
  double tol = 0.01;  //i.e. 1% tolerance

  Vec3D*** coords = (Vec3D***) coordinates.GetDataPointer();
  Vec3D*** bbmin  = (Vec3D***) BBmin.GetDataPointer();
  Vec3D*** bbmax  = (Vec3D***) BBmax.GetDataPointer();

  double delta;
  for(int k=k0; k<kmax; k++)
    for(int j=j0; j<jmax; j++)
      for(int i=i0; i<imax; i++) {

        delta = tol*(coords[k][j][i][0] - coords[k][j][i-1][0]); 
        bbmin[k][j][i][0] = i-1>=0 ? coords[k][j][i-1][0] - tol : coords[k][j][i][0] - tol;
        delta = tol*(coords[k][j][i+1][0] - coords[k][j][i][0]); 
        bbmax[k][j][i][0] = i+1<NX ? coords[k][j][i+1][0] + tol : coords[k][j][i][0] + tol;

        delta = tol*(coords[k][j][i][1] - coords[k][j-1][i][1]); 
        bbmin[k][j][i][1] = j-1>=0 ? coords[k][j-1][i][1] - tol : coords[k][j][i][1] - tol;
        delta = tol*(coords[k][j+1][i][1] - coords[k][j][i][1]); 
        bbmax[k][j][i][1] = j+1<NY ? coords[k][j+1][i][1] + tol : coords[k][j][i][1] + tol;

        delta = tol*(coords[k][j][i][2] - coords[k-1][j][i][2]); 
        bbmin[k][j][i][2] = k-1>=0 ? coords[k-1][j][i][2] - tol : coords[k][j][i][2] - tol;
        delta = tol*(coords[k+1][j][i][2] - coords[k][j][i][2]); 
        bbmax[k][j][i][2] = k+1<NZ ? coords[k+1][j][i][2] + tol : coords[k][j][i][2] + tol;

      }

  //subD_bb includes the ghost boundary
  subD_bbmin[0] = coords[kk0][jj0][ii0][0]             - tol*(coords[k0][j0][i0][0]           - coords[k0][j0][i0-1][0]);
  subD_bbmax[0] = coords[kkmax-1][jjmax-1][iimax-1][0] + tol*(coords[kmax-1][jmax-1][imax][0] - coords[kmax-1][jmax-1][imax-1][0]);
  subD_bbmin[1] = coords[kk0][jj0][ii0][1]             - tol*(coords[k0][j0][i0][1]           - coords[k0][j0-1][i0][1]);
  subD_bbmax[1] = coords[kkmax-1][jjmax-1][iimax-1][1] + tol*(coords[kmax-1][jmax][imax-1][1] - coords[kmax-1][jmax-1][imax-1][1]);
  subD_bbmin[2] = coords[kk0][jj0][ii0][2]             - tol*(coords[k0][j0][i0][2]           - coords[k0-1][j0][i0][2]);
  subD_bbmax[2] = coords[kkmax-1][jjmax-1][iimax-1][2] + tol*(coords[kmax][jmax-1][imax-1][2] - coords[kmax-1][jmax-1][imax-1][2]);

  coordinates.RestoreDataPointerToLocalVector();
  BBmin.RestoreDataPointerAndInsert();
  BBmax.RestoreDataPointerAndInsert();
}

//-------------------------------------------------------------------------

void
Intersector::BuildSubdomainScopeAndKDTree()
{
  scope.clear();

  vector<Vec3D>& Xs(surface.X);
  vector<Int3>&  Es(surface.elems);
  for(auto it = Es.begin(); it != Es.end(); it++) {
    MyTriangle tri(it - Es.begin(), Xs[(*it)[0]], Xs[(*it)[1]], Xs[(*it)[2]]);
    bool inside = true;
    for(int i=0; i<3; i++) {
      if(tri.val[i] > subD_bbmax[i] || tri.val[i] + tri.width[i] < subD_bbmin[i]) {
        inside = false;
        break;
      }
    }
    if(inside) 
      scope.push_back(tri); //creating a copy of "tri" and store it in scope
  }

  // build the tree
  if(tree)
    delete tree;
  tree = new KDTree<MyTriangle,3>(scope.size(), scope.data());
}

//-------------------------------------------------------------------------

void
Intersector::FindNodalCandidates()
{
  assert(tree);

  candidates.clear();

  int nMaxCand = 500; //will increase if necessary

  Vec3D*** coords  = (Vec3D***) coordinates.GetDataPointer();
  Vec3D*** bbmin   = (Vec3D***) BBmin.GetDataPointer();
  Vec3D*** bbmax   = (Vec3D***) BBmax.GetDataPointer();
  double*** candid = CandidatesIndex.GetDataPointer();

  MyTriangle *tmp = new MyTriange[nMaxCand];
  
  // Work on all nodes inside the physical domain, including internal ghost layer
  for(int k=kk0_in; k<kkmax_in; k++)
    for(int j=jj0_in; j<jjmax_in; j++)
      for(int i=ii0_in; i<iimax_in; i++) {

        // find candidates
        int nFound = FindCandidatesInBox(tree, bbmin[k][j][i], bbmax[k][j][i], tmp, nMaxCand);

        // update candidates and CandidatesIndex
        if(nFound==0) {
          candid[k][j][i] = -1;
        } else {
          candidates.push_back(std::make_pair(Int3(i,j,k), vector<MyTriangle>())); 
          vector<MyTriangle>& tri(candidates.back().second);
          tri.reserve(nFound);
          for(int i=0; i<nFound; i++)
            tri.push_back(tmp[i]); 
          candid[k][j][i] = candidates.size() - 1; //index of this node in candidates
        }

      }

  coordinates.RestoreDataPointerToLocalVector();
  BBmin.RestoreDataPointerToLocalVector();
  BBmax.RestoreDataPointerToLocalVector();
  CandidatesIndex.RestoreDataPointerToLocalVector(); //can NOT communicate, because "candidates" do not.

  delete [] tmp;
}

//-------------------------------------------------------------------------

void
Intersector::FindIntersections(bool with_nodal_cands) //also finds occluded and first layer nodes
{

  Vec3D*** coords  = (Vec3D***) coordinates.GetDataPointer();
  Vec3D*** xf      = (Vec3D***) XForward.GetDataPointer();
  Vec3D*** xb      = (Vec3D***) XBackward.GetDataPointer();
  double*** candid = with_nodal_cands ? CandidatesIndex.GetDataPointer() : NULL;
  double*** sign   = Sign.GetDataPointer();
  
  double*** occid  = TMP.GetDataPointer(); //occluding triangle id
  double*** layer  = TMP2.GetDataPointer(); //"layer" of each node: 0(occluded), 1, or -1 (unknown)

  //Clear previous values
  intersections.clear();

  //Preparation
  Vec3D tol(half_thickness*5, half_thickness*5, half_thickness*5); //a tolerance, more than enough

  int max_left   = 500; //will increase if necessary
  int max_bottom = 500; 
  int max_back   = 500; 

  MyTriangle *tmp_left = new MyTriangle[max_left];
  int found_left;
  MyTriangle *tmp_bottom = new MyTriangle[max_bottom];
  int found_bottom;
  MyTriangle *tmp_back = new MyTriangle[max_back];
  int found_back;

  IntersectionPoint xp0, xp1;

  // ----------------------------------------------------------------------------
  // Find occluded nodes and intersections. Build occluded and firstLayer 
  // ----------------------------------------------------------------------------
  firstLayer.clear();
  // We only deal with edges whose vertices are both in the real domain
  for(int k=k0; k<kkmax_in; k++)
    for(int j=j0; j<jjmax_in; j++)
      for(int i=i0; i<iimax_in; i++) {
  
        // start with assuming it is outside
        sign[k][j][i] = 1;

        // start with a meaningless triangle id
        occid[k][j][i] = -1;

        layer[k][j][i] = -1;


        if(candid && k<kmax && j<jmax && i<imax && //candid has a valid value @ i,j,k
           candid[k][j][i] < 0)  //no nodal candidates, intersection impossible
          continue;
 

        //--------------------------------------------
        // Find candidates for left/bottom/back edges
        //--------------------------------------------
        found_left = found_bottom = found_back = 0;

        if(i-1>=0) { //the edge [k][j][i-1] -> [k][j][i] is inside the physical domain
          if(candid && k<kmax && j<jmax && //candid has a valid value @ i-1,j,k
             candid[k][j][i-1] < 0) {
            //DO NOTHING. No nodal candidates
          } else
            found_left = FindCandidatesInBox(tree, coords[k][j][i-1] - tol, coords[k][j][i] + tol, tmp_left, max_left);
        }

        if(j-1>=0) { //the edge [k][j-1][i] -> [k][j][i] is inside the physical domain
          if(candid && k<kmax && i<imax && //candid has a valid value @ i,j-1,k
             candid[k][j-1][i] < 0) {
            //DO NOTHING. No nodal candidates
          } else
            found_bottom = FindCandidatesInBox(tree, coords[k][j-1][i] - tol, coords[k][j][i] + tol, tmp_bottom, max_bottom);
        }

        if(k-1>=0) { //the edge [k-1][j][i] -> [k][j][i] is inside the physical domain
          if(candid && j<jmax && i<imax && //candid has a valid value @ i,j,k-1
             candid[k-1][j][i] < 0) {
            //DO NOTHING. No nodal candidates
          } else
            found_back = FindCandidatesInBox(tree, coords[k-1][j][i] - tol, coords[k][j][i] + tol, tmp_back, max_back);
        }


        //--------------------------------------------
        // Check if (i,j,k) is occluded
        //--------------------------------------------
        int tid(-1);
        if ((found_left>0   && IsPointOccludedByTriangles(coords[k][j][i], tmp_left,   found_left,   half_thickness, tid)) ||
            (found_bottom>0 && IsPointOccludedByTriangles(coords[k][j][i], tmp_bottom, found_bottom, half_thickness, tid)) ||
            (found_back>0   && IsPointOccludedByTriangles(coords[k][j][i], tmp_back,   found_back,   half_thickness, tid))) {
          sign[k][j][i] = 0;
          occid[k][j][i] = tid;
          layer[k][j][i] = 0;          
        }

        //--------------------------------------------
        // Find intersections (left, bottom, and back edges)
        //--------------------------------------------
        // left
        if(found_left) {
          int count = FindEdgeIntersectionsWithTriangles(coords[k][j][i-1], i-1, j, k, 0, 
                          coords[k][j][i][0] - coords[k][j][i-1][0], tmp_left, found_left, xp0, xp1);
          if(count==0) {
            xf[k][j][i][0] = xb[k][j][i][0] = -1;
          } else if(count==1) {
            intersections.push_back(xp0);
            xf[k][j][i][0] = xb[k][j][i][0] = intersections.size() - 1;
            layer[k][j][i-1] = layer[k][j][i] = 1;
          } else {//more than one intersections
            intersections.push_back(xp0);
            xf[k][j][i][0] = intersections.size() - 1;
            intersections.push_back(xp1);
            xb[k][j][i][0] = intersections.size() - 1;
            layer[k][j][i-1] = layer[k][j][i] = 1;
          }
        } else {
          xf[k][j][i][0] = xb[k][j][i][0] = -1;
        }

        // bottom 
        if(found_bottom) {
          int count = FindEdgeIntersectionsWithTriangles(coords[k][j-1][i], i, j-1, k, 1, 
                          coords[k][j][i][1] - coords[k][j-1][i][1], tmp_bottom, found_bottom, xp0, xp1);
          if(count==0) {
            xf[k][j][i][1] = xb[k][j][i][1] = -1;
          } else if(count==1) {
            intersections.push_back(xp0);
            xf[k][j][i][1] = xb[k][j][i][1] = intersections.size() - 1;
            layer[k][j-1][i] = layer[k][j][i] = 1;
          } else {//more than one intersections
            intersections.push_back(xp0);
            xf[k][j][i][1] = intersections.size() - 1;
            intersections.push_back(xp1);
            xb[k][j][i][1] = intersections.size() - 1;
            layer[k][j-1][i] = layer[k][j][i] = 1;
          } else {//more than one intersections
          }
        } else {
          xf[k][j][i][1] = xb[k][j][i][1] = -1;
        }

        // back 
        if(found_back) {
          int count = FindEdgeIntersectionsWithTriangles(coords[k-1][j][i], i, j, k-1, 2, 
                          coords[k][j][i][2] - coords[k-1][j][i][2], tmp_back, found_back, xp0, xp1);
          if(count==0) {
            xf[k][j][i][2] = xb[k][j][i][2] = -1;
          } else if(count==1) {
            intersections.push_back(xp0);
            xf[k][j][i][2] = xb[k][j][i][2] = intersections.size() - 1;
            layer[k-1][j][i] = layer[k][j][i] = 1;
          } else {//more than one intersections
            intersections.push_back(xp0);
            xf[k][j][i][2] = intersections.size() - 1;
            intersections.push_back(xp1);
            xb[k][j][i][2] = intersections.size() - 1;
            layer[k-1][j][i] = layer[k][j][i] = 1;
          }
        } else {
          xf[k][j][i][2] = xb[k][j][i][2] = -1;
        }

      }

  // Exchange Sign and TMP so internal ghost nodes are accounted for
  if(candid) CandidatesIndex.RestoreDataPointerToLocalVector();
  Sign.RestoreDataPointerAndInsert();
  TMP.RestoreDataPointerAndInsert();
  TMP2.RestoreDataPointerAndInsert();

  occid  = TMP.GetDataPointer();

  // ----------------------------------------------------------------------------
  // Make sure all edges connected to occluded nodes have intersections
  // ----------------------------------------------------------------------------
  bool ijk_occluded;
  for(int k=k0; k<kkmax_in; k++)
    for(int j=j0; j<jjmax_in; j++)
      for(int i=i0; i<iimax_in; i++) {
 
        ijk_occluded = (occid[k][j][i]>=0);
        if(i-1>=0) { //left edge within physical domain
          
          if(occid[k][j][i-1]<0 && !ijk_occluded) { //neither (i-1,j,k) nor (i,j,k) occluded
            //nothing to be done   
          } 
          else { //at least one of the two vertices is occluded

            // insert "imposed" intersection points
            if(occid[k][j][i-1]>=0) { //(i-1,j,k) is occluded
              Vec3D xi;
              int trif = occid[k][j][i-1];
              Int3& nf(surface.elems[trif]);
              // re-run the checker to get xi
              bool is_occluded = GeoTools::IsPointInThickenedTriangle(coords[k][j][i-1], surface.X[nf[0]],
                                            surface.X[nf[1]], surface.X[nf[2]], half_thickness,
                                            surface.elemArea[trif], surface.elemNorm[trif], xi);
              assert(is_occluded);
              intersections.push_back(IntersectionPoint(i-1,j,k, 0, 0.0, trif, xi));
            }
            if(ijk_occluded) { //(i-1,j,k) is occluded
              Vec3D xi;
              int trif = occid[k][j][i];
              Int3& nf(surface.elems[trif]);
              // re-run the checker to get xi
              bool is_occluded = GeoTools::IsPointInThickenedTriangle(coords[k][j][i], surface.X[nf[0]],
                                            surface.X[nf[1]], surface.X[nf[2]], half_thickness,
                                            surface.elemArea[trif], surface.elemNorm[trif], xi);
              assert(is_occluded);
              intersections.push_back(IntersectionPoint(i-1,j,k, 0, coords[k][j][i][0] - coords[k][j][i-1][0], trif, xi));
            }

            // update xb and xf
            if(xf[k][j][i][0]<0) {
              if(occid[k][j][i-1]>=0 && ijk_occluded) { //both vertices are occluded
                xf[k][j][i][0] = intersections.size() - 2;
                xb[k][j][i][0] = intersections.size() - 1;
              } else {// only one of the two vertices is occluded 
                xf[k][j][i][0] = xb[k][j][i][0] = intersections.size() - 1;
              }
            }
            else { //intersection already found. 
              //ensure that the occluded node(s) is the intersection point closest to the occluded node.
              if(occid[k][j][i-1]>=0 && ijk_occluded) {
                xf[k][j][i][0] = intersections.size() - 2;
                xb[k][j][i][0] = intersections.size() - 1;
              } else if(occid[k][j][i-1]>=0) {
                xf[k][j][i][0] = intersections.size() - 1;
                IntersectionPoint &p(intersections[xb[k][j][i][0]]);
                if(p.dist<=half_thickness) {//this is essentially the first vertex
                  xb[k][j][i][0] = intersections.size() - 1;
                }
              } else { //ijk_occluded
                xb[k][j][i][0] = intersections.size() - 1;
                IntersectionPoint &p(intersections[xf[k][j][i][0]]);
                if(p.dist >= coords[k][j][i][0] - coords[k][j][i-1][0] - half_thickness) {//this is essentially the second vertex
                  xf[k][j][i][0] = intersections.size() - 1;
                }
              }
            }
          }
        }


        if(j-1>=0) { //bottom edge within physical domain
          
          if(occid[k][j-1][i]<0 && !ijk_occluded) { //neither (i,j-1,k) nor (i,j,k) occluded
            //nothing to be done   
          } 
          else { //at least one of the two vertices is occluded

            // insert "imposed" intersection points
            if(occid[k][j-1][i]>=0) { //(i,j-1,k) is occluded
              Vec3D xi;
              int trif = occid[k][j-1][i];
              Int3& nf(surface.elems[trif]);
              // re-run the checker to get xi
              bool is_occluded = GeoTools::IsPointInThickenedTriangle(coords[k][j-1][i], surface.X[nf[0]],
                                            surface.X[nf[1]], surface.X[nf[2]], half_thickness,
                                            surface.elemArea[trif], surface.elemNorm[trif], xi);
              assert(is_occluded);
              intersections.push_back(IntersectionPoint(i,j-1,k, 1, 0.0, trif, xi));
            }
            if(ijk_occluded) { //(i-1,j,k) is occluded
              Vec3D xi;
              int trif = occid[k][j][i];
              Int3& nf(surface.elems[trif]);
              // re-run the checker to get xi
              bool is_occluded = GeoTools::IsPointInThickenedTriangle(coords[k][j][i], surface.X[nf[0]],
                                            surface.X[nf[1]], surface.X[nf[2]], half_thickness,
                                            surface.elemArea[trif], surface.elemNorm[trif], xi);
              assert(is_occluded);
              intersections.push_back(IntersectionPoint(i,j-1,k, 1, coords[k][j][i][1] - coords[k][j-1][i][1], trif, xi));
            }

            // update xb and xf
            if(xf[k][j][i][1]<0) {
              if(occid[k][j-1][i]>=0 && ijk_occluded) { //both vertices are occluded
                xf[k][j][i][1] = intersections.size() - 2;
                xb[k][j][i][1] = intersections.size() - 1;
              } else {// only one of the two vertices is occluded 
                xf[k][j][i][1] = xb[k][j][i][1] = intersections.size() - 1;
              }
            }
            else { //intersection already found. 
              //ensure that the occluded node(s) is the intersection point closest to the occluded node.
              if(occid[k][j-1][i]>=0 && ijk_occluded) {
                xf[k][j][i][1] = intersections.size() - 2;
                xb[k][j][i][1] = intersections.size() - 1;
              } else if(occid[k][j-1][i]>=0) {
                xf[k][j][i][1] = intersections.size() - 1;
                IntersectionPoint &p(intersections[xb[k][j][i][1]]);
                if(p.dist<=half_thickness) {//this is essentially the first vertex
                  xb[k][j][i][1] = intersections.size() - 1;
                }
              } else { //ijk_occluded
                xb[k][j][i][1] = intersections.size() - 1;
                IntersectionPoint &p(intersections[xf[k][j][i][1]]);
                if(p.dist >= coords[k][j][i][1] - coords[k][j-1][i][1] - half_thickness) {//this is essentially the second vertex
                  xf[k][j][i][1] = intersections.size() - 1;
                }
              }
            }
          }
        }


        if(k-1>=0) { //back edge within physical domain
          
          if(occid[k-1][j][i]<0 && !ijk_occluded) { //neither (i,j,k-1) nor (i,j,k) occluded
            //nothing to be done   
          } 
          else { //at least one of the two vertices is occluded

            // insert "imposed" intersection points
            if(occid[k-1][j][i]>=0) { //(i,j-1,k) is occluded
              Vec3D xi;
              int trif = occid[k-1][j][i];
              Int3& nf(surface.elems[trif]);
              // re-run the checker to get xi
              bool is_occluded = GeoTools::IsPointInThickenedTriangle(coords[k-1][j][i], surface.X[nf[0]],
                                            surface.X[nf[1]], surface.X[nf[2]], half_thickness,
                                            surface.elemArea[trif], surface.elemNorm[trif], xi);
              assert(is_occluded);
              intersections.push_back(IntersectionPoint(i,j,k-1, 2, 0.0, trif, xi));
            }
            if(ijk_occluded) { //(i-1,j,k) is occluded
              Vec3D xi;
              int trif = occid[k][j][i];
              Int3& nf(surface.elems[trif]);
              // re-run the checker to get xi
              bool is_occluded = GeoTools::IsPointInThickenedTriangle(coords[k][j][i], surface.X[nf[0]],
                                            surface.X[nf[1]], surface.X[nf[2]], half_thickness,
                                            surface.elemArea[trif], surface.elemNorm[trif], xi);
              assert(is_occluded);
              intersections.push_back(IntersectionPoint(i,j,k-1, 2, coords[k][j][i][2] - coords[k-1][j][i][2], trif, xi));
            }

            // update xb and xf
            if(xf[k][j][i][2]<0) {
              if(occid[k-1][j][i]>=0 && ijk_occluded) { //both vertices are occluded
                xf[k][j][i][2] = intersections.size() - 2;
                xb[k][j][i][2] = intersections.size() - 1;
              } else {// only one of the two vertices is occluded 
                xf[k][j][i][2] = xb[k][j][i][2] = intersections.size() - 1;
              }
            }
            else { //intersection already found. 
              //ensure that the occluded node(s) is the intersection point closest to the occluded node.
              if(occid[k-1][j][i]>=0 && ijk_occluded) {
                xf[k][j][i][2] = intersections.size() - 2;
                xb[k][j][i][2] = intersections.size() - 1;
              } else if(occid[k-1][j-1][i]>=0) {
                xf[k][j][i][2] = intersections.size() - 1;
                IntersectionPoint &p(intersections[xb[k][j][i][2]]);
                if(p.dist<=half_thickness) {//this is essentially the first vertex
                  xb[k][j][i][2] = intersections.size() - 1;
                }
              } else { //ijk_occluded
                xb[k][j][i][2] = intersections.size() - 1;
                IntersectionPoint &p(intersections[xf[k][j][i][2]]);
                if(p.dist >= coords[k][j][i][2] - coords[k-1][j][i][2] - half_thickness) {//this is essentially the second vertex
                  xf[k][j][i][2] = intersections.size() - 1;
                }
              }
            }
          }
        }

      }

  TMP.RestoreDataPointerToLocalVector();

  XForward.RestoreDataPointerToLocalVector(); //Cannot exchange data, because "intersections" does not communicate
  XBackward.RestoreDataPointerToLocalVector(); //Cannot exchange data, because "intersections" does not communicate

  coordinates.RestoreDataPointerToLocalVector();


  // ----------------------------------------------------------------------------
  // Build the sets of occluded and firstLayer nodes. Include internal ghost nodes
  // ----------------------------------------------------------------------------
  occluded.clear();
  firstLayer.clear();

  layer  = TMP2.GetDataPointer(); //"layer" of each node: 0(occluded), 1, or -1 (unknown)

  for(int k=kk0_in; k<kkmax_in; k++)
    for(int j=jj0_in; j<jjmax_in; j++)
      for(int i=ii0_in; i<iimax_in; i++) {
        if(layer[k][j][i]==0) {
          occluded.insert(Int3(i,j,k));         
          firstLayer.insert(Int3(i,j,k));
        } else if(layer[k][j][i]==1) 
          firstLayer.insert(Int3(i,j,k));
      }

  TMP2.RestoreDataPointerToLocalVector();

}

//-------------------------------------------------------------------------

//optional
void
Intersector::FindShortestDistanceForFirstLayer //don't forget to subtract half_distance

void
Intersector::FindShortestDistanceForOtherNodes //call level set reinitializer


//-------------------------------------------------------------------------

bool
Intersector::IsPointOccludedByTriangles(Vec3D &x0, MyTriangle* tri, int nTri, double my_half_thickness,
                                        int &tid, double* xi) 
{
  vector<Vec3D>&  Xs(surface.X);
  vector<Int3>&   Es(surface.elems);
  vector<Vec3D>&  Ns(surface.elemNorm);
  vector<double>& As(surface.elemArea); 

  for(int i=0; i<nTri; i++) {
    int id = tri[i].trId();
    Int3& nodes(Es[id]);
    if(GeoTools::IsPointInThickenedTriangle(x0, Xs[nodes[0]], Xs[nodes[1]], Xs[nodes[2]], my_half_thickness,
                                            &As[id], &Ns[id], xi)) {
      tid = id;
      return true;
    }
  }

  return false;
}

//-------------------------------------------------------------------------

int
Intersector::FloodFill(bool &hasInlet, bool &hasOutlet, bool &hasOcc, int &nClosures)
{
  // ----------------------------------------------------------------
  // Call floodfiller to do the work.
  // ----------------------------------------------------------------
  int nColors = floodfiller.FillBasedOnEdgeObstructions(XForward, -1/*xf==-1 means no intersection*/, Sign, occluded);

  // ----------------------------------------------------------------
  // Now we need to convert the color map to what we want: 0~occluded, 1, 2,...: regions connected to Dirichlet
  // boundaries (inlet/outlet/farfield). 1=inlet, 2=outlet. -1,-2,...: inside enclosures
  // ----------------------------------------------------------------
  double*** sign   = Sign.GetDataPointer();

  // First, we need to find the current colors for inlet, outlet
  std::set<int> inlet_color; 
  std::set<int> outlet_color;  //In most cases, both sets should only contain 0 or 1 member. Otherwise, it means 
                               //one type of boundary (e.g., inlet) is separated by the embedded surface into multiple
                               //disconnected regions. In this case, we have to merge the colors into one.
  for(auto it = ghost_nodes_outer.begin(); it != ghost_nodes_outer.end(); it++) {
    if(it->type_projection != GhostPoint::FACE)
      continue;
    if(it->bcType == MeshData::INLET) {
      Int3& ijk(it->image_ijk);
      inlet_color.insert(sign[ijk[2]][ijk[1]][ijk[0]]);
    } else if(it->bcType == MeshData::OUTLET) {
      Int3& ijk(it->image_ijk);
      outlet_color.insert(sign[ijk[2]][ijk[1]][ijk[0]]);
    }
  }

  int max_inlet_color(-1);
  for(auto it = inlet_color.begin(); it != inlet_color.end(); it++)
    max_inlet_color = std::max(max_inlet_color, *it);
  MPI_Allreduce(MPI_IN_PLACE, &max_inlet_color, 1, MPI_INT, MPI_MAX, comm)  

  vector<int> in_colors(max_inlet_color+1, -1);
  for(auto it = inlet_color.begin(); it != inlet_color.end(); it++)
    in_colors[*it] = 1;
  MPI_Allreduce(MPI_IN_PLACE, in_colors.data(), in_colors.size(), MPI_INT, MPI_MAX, comm)  

  if(in_colors[0] == 1 && verbose>1)
    print_warning("Warning: Found occluded node(s) near an inlet or farfield boundary.");

  int max_outlet_color(-1);
  for(auto it = outlet_color.begin(); it != outlet_color.end(); it++) 
    max_outlet_color = std::max(max_outlet_color, *it);
  MPI_Allreduce(MPI_IN_PLACE, &max_outlet_color, 1, MPI_INT, MPI_MAX, comm)  

  vector<int> out_colors(max_outlet_color+1, -1);
  for(auto it = outlet_color.begin(); it != outlet_color.end(); it++)
    out_colors[*it] = 1;
  MPI_Allreduce(MPI_IN_PLACE, out_colors.data(), out_colors.size(), MPI_INT, MPI_MAX, comm)  
  
  if(out_colors[0] == 1 && verbose>1)
    print_warning("Warning: Found occluded node(s) near an outlet or farfield boundary.");

  // Convert colors 
  std::map<int,int> old2new;
  for(int i=0; i<in_colors.size(); i++)
    if(in_colors[i] == 1)
     old2new[i] = 1; //inlet_color;
  for(int i=0; i<out_colors.size(); i++)
    if(out_colors[i] == 1) {
     assert(old2new.find(i) == old2new.end());
     old2new[i] = 2; //outlet_color;
    }
  int tmp_counter = 0;
  for(int i=1; i<nColors+1; i++)
    if(old2new.find(i) == old2new.end())
      old2new[i] = --tmp_counter;

  int total_occluded = 0;

  for(int k=k0; k<kmax; k++)
    for(int j=j0; j<jmax; j++)
      for(int i=i0; i<imax; i++) {
        if(sign[k][j][i] != 0)
          sign[k][j][i] = old2new[sign[k][j][i]];
        else
          total_occluded++;
      }

  // statistics
  MPI_Allreduce(MPI_IN_PLACE, &total_occluded, 1, MPI_INT, MPI_SUM, comm)  

  if(total_occluded>0) 
    hasOcc = true; //i.e. one zero color (for occluded)

  hasInlet = hasOutlet = false;
  nClosures = 0;
  for(it = old2new.begin(); it != old2new.end(); it++)
    if(it->second==1)
      hasInlet = true;
    else if(it->second==2)
      hasOutlet = true;
    else if(it->second<0)
      nClosures++;

  Sign.RestoreDataPointerAndInsert();

  return int(hasInlet) + int(hasOutlet) + int(hasOcc) + nClosures;
}

//-------------------------------------------------------------------------

int
Intersector::RefillAfterSurfaceUpdate(bool &hasInlet, bool &hasOutlet, bool &hasOcc, int &nClosures)
{
//TODO

}

//-------------------------------------------------------------------------

void
Intersector::CalculateUnsignedDistanceNearSurface(int nLayers, bool nodal_cands_calculated)
{

  if(!nodal_cands_calculated)
    FindNodalCandidates();

  double*** candid = CandidatesIndex.GetDataPointer();
  double*** phi    = CandidatesIndex.GetDataPointer();
  
  // set const. value to phi, by default
  double default_distance = 0.5*domain_diagonal;
  for(int k=kk0_in; k<kkmax_in; k++)
    for(int j=jj0_in; j<jjmax_in; j++)
      for(int i=ii0_in; i<iimax_in; i++)
        phi[k][j][i] = default_distance;

  assert(nLayers>=1);

  // if layer>1, build another scope and another tree.
  // XXX

  // loop through layers. 1st layer means nodes connected to intersecting edges (including occluded nodes).
  for(int layer=1; layer<=nLayers; layer++) {
    std::set<Int3> seeds_for_next;
     
  }








  }


 ProjectPointToTriangle(Vec3D& x0, Vec3D& xA, Vec3D& xB, Vec3D& xC, double xi[3],
                              double* area = NULL, Vec3D* dir = NULL,
                              bool return_signed_distance = false);   

}

//-------------------------------------------------------------------------

int
Intersector::FindEdgeIntersectionsWithTriangles(Vec3D &x0, int i, int j, int k, int dir, double len, MyTriangles* tri, int nTri,
                                                IntersectionPoint &xf, IntersectionPoint &xb)
{
  vector<Vec3D>&  Xs(surface.X);
  vector<Int3>&   Es(surface.elems);
  vector<Vec3D>&  Ns(surface.elemNorm);
  vector<double>& As(surface.elemArea); 

  vector<pair<double, IntersectionPoint> > X; //pairs distance with intersection point info

  double dist;
  Vec3D xi; //barycentric coords of the projection point
  for(int i=0; i<nTri; i++) {
    int id = tri[i].trId();
    Int3& nodes(Es[id]);
    bool found = GeoTools::LineSegmentIntersectsTriangle(x0, dir, len, Xs[nodes[0]], Xs[nodes[1]], Xs[nodes[2]],
                                                         &dist, NULL, &xi);
    if(found) //store the result
      X.push_back(std::make_pair(dist, IntersectionPoint(i,j,k,dir,dist,id,xi)));
  }

  if(X.empty())
    return 0;

  double x0_2_xf = DBL_MAX; 
  double x0_2_xb = -1.0;
  for(auto it = X.begin(); it != X.end(); it++) {
    if(x0_2_xf > it->first) {
      x0_2_xf = it->first;
      xf = it->second;
    }
    if(x0_2_xb < it->first) {
      x0_2_xb = it->first;
      xb = it->second;
    }
  }

  return X.size();
}

//-------------------------------------------------------------------------

