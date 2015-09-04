/*
 *
 *   PURPOSE: To implement class IntegralEngine, which calculates, stores,
 *            and processes the molecular integrals needed in ab initio
 *            quantum chemistry calculation.
 *
 *   DATE         AUTHOR           CHANGES
 *   ======================================================================
 *   02/09/15     Robert Shaw      Original code.
 *   03/09/15     Robert Shaw      Merged overlap and kinetic integrals.
 *   04/09/15     Robert Shaw      Multipole integrals added.
 */

#include "error.hpp"
#include "integrals.hpp"
#include "mathutil.hpp"
#include "basis.hpp"
#include "logger.hpp"
#include <cmath>
#include <iostream>

// Constructor
IntegralEngine::IntegralEngine(Molecule& m) : molecule(m)
{
  // Calculate sizes
  int natoms = molecule.getNAtoms();
  int N = 0; // No. of cartesian basis functions
  for (int i = 0; i < natoms; i++){
    N += m.getAtom(i).getNbfs();
  }
  // Cartesian is easy - there are (N^2+N)/2
  // unique 1e integrals and ([(N^2+N)/2]^2 + (N^2+N)/2)/2
  // unique 2e integrals
  int ones = (N*(N+1))/2;
  sizes.resize(4);
  sizes[0] = ones;
  sizes[1] = (ones*(ones+1))/2;
  
  // Spherical is much harder - tbc
  sizes[2] = ones;
  sizes[3] = sizes(1);

  formOverlapKinetic();
  formNucAttract();
}

// Accessors

// Return estimates of the memory that will be needed by the 
// one and two electron integrals. Returns as:
// [1e cart, 2e cart, 1e spher, 2e spher]
Vector IntegralEngine::getEstimates() const
{
  Vector estimates(4);
  // The amount of memory is roughly the number of integrals times the size
  // of a double in memory
  estimates[0] = sizeof(double)*sizes(0);
  estimates[1] = sizeof(double)*sizes(1);
  estimates[2] = sizeof(double)*sizes(2);
  estimates[3] = sizeof(double)*sizes(3);
  return estimates;
}

// Make a matrix of electron-electron repulsion integrals
Matrix IntegralEngine::makeERI() const
{
  Matrix eri;
  return eri;
} 

// Utility functions needed to calculate integrals

// Calculates the centre-of-charge coordinates(P), total(p) and reduced(u) exponents, 
// relative coordinates(X, Y, Z), and pre-exponential factors(K) between basis functions
// with exponents a and b, and centres A, B. Vector returned contains:
// (p, u, Px, Py, Pz, X, Y, Z, Kx, Ky, Kz) 
Vector IntegralEngine::getVals(double a, double b, const Vector& A, const Vector& B) const
{
  Vector vals(11); // Return vector
  
  // Calculate p and u
  double p = a+b; 
  double u = (a*b)/(a+b);
  vals[0] = p;
  vals[1] = u;
  
  // Calculate the Ps, XYZ, and Ks
  for (int i = 0; i < 3; i++){
    vals[i+2] = (a*A(i) + b*B(i)) / p; // P
    vals[i+5] = A(i) - B(i); // X, Y, Z
    vals[i+8] = std::exp(-1.0*u*vals(i+5)*vals(i+5)); // K
  }
  
  return vals;
}

// Calculate the spherical normalisation constant for angular and magnetic
// quantum numbers l, m. See Helgaker, Jorgensen, Olsen, Molecular Electronic
// Structure Theory, Chapter 9 pg 338 for formulae.
double IntegralEngine::getN(int l, int m) const
{
  int mabs = std::abs(m);
  double lfact = (double)(fact(l));
  double lplusmfact = (double)(fact(l+mabs));
  double lminmfact = (double)(fact(l-mabs));
  double zerom = (m == 0 ? 2.0 : 1.0);
  
  double N = 1.0/(std::pow(2.0, mabs)*lfact);
  N = N*std::sqrt((2.0*lplusmfact*lminmfact)/zerom);
  return N;
}

// Similarly, get the Clebsch-Gordon coefficient
double IntegralEngine::getC(int l, int m, int t, int u, double v) const
{
  int mabs = std::abs(m);
  double vm = (m < 0 ? 0.5 : 0.0);
  double premult = std::pow(-1.0, t+v-vm) * std::pow(4.0, t);
  double blt = (double)(binom(l, t));
  double blmt = (double)(binom(l-t, mabs+t));
  double btu = (double)(binom(t, u));
  double bm2v = (double)(binom(mabs, 2*v));
  
  return premult*blt*blmt*btu*bm2v;
}

// Contract a set of 1e- integrals
// Assumes that integrals are ordered as: 00, 01, 02, ..., 10, 11, 12, ...,
// and so on, where the first number refers to the index of c1, and the second, 
// that of c2.
double IntegralEngine::makeContracted(Vector& c1, Vector& c2, Vector& ints) const
{
  double integral = 0.0;
  int N1 = c1.size();
  int N2 = c2.size();
  // Loop over contraction coefficients
  for (int i = 0; i < N1; i++){
    for (int j = 0; j < N2; j++){
      integral += c1(i)*c2(j)*ints(i*N2+j);
    }
  }
  return integral;
}

// Do the same but for 2e- integrals
// Assumes integrals are stored as:
// 0000 0001 0002 ... 0010 0011 ....
// 0100 0101 0102 ... 1010 1011 ....
// 0200
// .
// .
// .
// 1000 and so on
double IntegralEngine::makeContracted(Vector& c1, Vector& c2, Vector& c3,
				      Vector& c4, Matrix& ints) const
{
  double integral = 0.0;
  int N1 = c1.size();
  int N2 = c2.size();
  int N3 = c3.size();
  int N4 = c4.size();
  
  // Loop over contraction coefficients
  for (int i = 0; i < N1; i++){
    for (int j = 0; j < N2; j++){
      for (int k = 0; k < N3; k++){
	for (int l = 0; l < N4; l++){
	  integral += c1(i)*c2(j)*c3(k)*c4(l)*ints(i*N2+j, k*N4+l);
	}
      }
    }
  }
  return integral;
}

// Sphericalise a set of 1e- integrals
double IntegralEngine::makeSpherical(int l1, int m1, int l2, int m2, Matrix& ints) const
{
  double integral = 0.0;

  // Get the normalisation 
  double N = getN(l1, m1)*getN(l2, m2);

  // Get the summation limits
  int m1abs = std::abs(m1);
  int m2abs = std::abs(m2);
  double vm1 = (m1 < 0 ? 0.5 : 0);
  double vm2 = (m2 < 0 ? 0.5 : 0);
  double v1lim = std::floor(((double)(m1abs))/2.0 - vm1) + vm1;
  double v2lim = std::floor(((double)(m2abs))/2.0 - vm2) + vm2;
  int t1lim = std::floor((double)(l1-m1abs)/2.0);
  int t2lim = std::floor((double)(l2-m2abs)/2.0);
  
  // Loop over first set of indices
  double C;
  for (int t = 0; t <= t1lim; t++){
    for (int u = 0; u <= t; u++){
      double v = vm1;
      while ( vm1 <= v1lim ){
      }
    }
  }
  return integral;
}

// Same but for 2e- integrals
double IntegralEngine::makeSpherical(int l1, int m1, int l2, int m2,
				     int l3, int m3, int l4, int m4, Matrix& ints) const
{
  double integral = 0.0;
  return integral;
}

// Form the overlap integral matrix sints using the Obara-Saika recurrence relations
// Algorithm:
//  - generate list of basis functions
//  - for each cgbf m
//      - for each cgbf n>=m
//         - loop over prims (u) on m
//            -loop over prims (v) on n
//                form Si0x,y,z
//                calculate Sij in each cartesian direction
//                Suv = Sij,x*Sij,y*Sij,z
//                form Ti0x,y,z
//                calculate Tij in each direction
//                Tuv = Tij,x*Sij,y*Sij,z + Sij,x*Tij,y*Sij,z + Sij,x*Sij,y*Tij,z
//            end
//         end
//         contract Suv into Smn, and Tuv into Tmn
//      end
//  end 
void IntegralEngine::formOverlapKinetic()
{
  // Get the number of basis functions
  int natoms = molecule.getNAtoms();
  int N = 0; // No. of cgbfs
  for (int i = 0; i < natoms; i++){
    N += molecule.getAtom(i).getNbfs();
  }
  sints.resize(N, N); // Resize the matrix
  tints.resize(N, N);
  
  // Form a list of basis functions, and the atoms they're on
  Vector atoms(N); Vector bfs(N);
  int k = 0;
  for (int i = 0; i < natoms; i++){
    int nbfs = molecule.getAtom(i).getNbfs();
    for (int j = 0; j < nbfs; j++){
      atoms[k] = i;
      bfs[k] = j;
      k++;
    }
  }

  // Loop over cgbfs
  BF mbf; BF nbf; Vector mcoords; Vector ncoords;
  PBF mpbf; PBF npbf;
  for (int m = 0; m < N; m++){
    // Retrieve basis function and coordinates
    mbf = molecule.getAtom(atoms(m)).getBF(bfs(m));
    mcoords = molecule.getAtom(atoms(m)).getCoords();
    
    // Get contraction coefficients and no. of prims
    Vector mcoeffs;
    mcoeffs = mbf.getCoeffs();
    int mN = mbf.getNPrims();

    // Loop over cgbfs greater or equal to m
    for (int n = m; n < N; n++){
      nbf = molecule.getAtom(atoms(n)).getBF(bfs(n));
      ncoords = molecule.getAtom(atoms(n)).getCoords();

      // Contraction coefficients and no. prims
      Vector ncoeffs;
      ncoeffs = nbf.getCoeffs();
      int nN = nbf.getNPrims();

      // Store primitive integrals
      Vector overlapPrims(mN*nN);
      Vector kineticPrims(mN*nN);
      // Loop over primitives
      for (int u = 0; u < mN; u++){
	mpbf = mbf.getPBF(u);

	for (int v = 0; v < nN; v++){
	  npbf = nbf.getPBF(v);

	  // Calculate primitive overlap and kinetic integrals
	  Vector temp;
	  temp = overlapKinetic(mpbf, npbf, mcoords, ncoords);

	  // Store in prim vectors
	  overlapPrims[u*nN+v] = temp(0);
	  kineticPrims[u*nN+v] = temp(1);
	} // End prims loop on n
      } // End prims loop on m

      // Contract
      //if(m==35 && n==41){ overlapPrims.print(); }
      sints(m, n) = makeContracted(mcoeffs, ncoeffs, overlapPrims);
      sints(n, m) = sints(m, n);
      tints(m, n) = makeContracted(mcoeffs, ncoeffs, kineticPrims);
      tints(n, m) = tints(m, n);

    } // End of cgbf loop n
  } // End of cgbf loop m
}

// Calculate the overlap and kinetic energy integrals between two primitive
// cartesian gaussian basis functions, given the coordinates of their centres
Vector IntegralEngine::overlapKinetic(const PBF& u, const PBF& v, 
				      const Vector& ucoords, const Vector& vcoords) const
{
  Vector rvals(2); // Vector to return answer in

  // Get exponents, norms, and angular momenta
  int ulx = u.getLx(); int uly = u.getLy(); int ulz = u.getLz();
  int vlx = v.getLx(); int vly = v.getLy(); int vlz = v.getLz();
  double unorm = u.getNorm(); double uexp = u.getExponent();
  double vnorm = v.getNorm(); double vexp = v.getExponent();

  // Get the necessary values from getVals
  Vector vals;
  vals = getVals(uexp, vexp, ucoords, vcoords);

  // Store the overlap intermediates for later use
  Matrix Sijx(ulx+1, vlx+1); Matrix Sijy(uly+1, vly+1); Matrix Sijz(ulz+1, vlz+1);

  // Calculate the S00 values in each direction
  double premult = std::sqrt(M_PI/vals(0)); // sqrt(PI/p)
  Sijx(0, 0) = premult*vals(8); // vals(8-10) are the K values
  Sijy(0, 0) = premult*vals(9);
  Sijz(0, 0) = premult*vals(10);

  // Loop to form Si0 in each cartesian direction

  // Use the Obara-Saika recursion formula:
  // S(i+1)j = XPA*Sij + (1/2p)*(i*S(i-1)j + j*Si(j-1))
  // to calculate Si0. 

  // First calculate XPA, YPA, ZPA, 1/2p
  double XPA = vals(2) - ucoords(0);
  double YPA = vals(3) - ucoords(1);
  double ZPA = vals(4) - ucoords(2);
  double one2p = 1.0/(2.0*vals(0)); // 1/2p                                                                                                    

  // Then loop
  double Snext, Scurr, Slast;
  Slast = 0.0;
  Scurr = Sijx(0, 0);
  for (int i = 1; i < ulx+1; i++){
    Snext = XPA*Scurr + one2p*(i-1)*Slast;
    Sijx(i, 0) = Snext;
    Slast = Scurr; Scurr = Snext;
  }
  Slast = 0.0; Scurr = Sijy(0, 0);
  for (int i = 1; i < uly+1; i++){
    Snext = YPA*Scurr + one2p*(i-1)*Slast;
    Sijy(i, 0) = Snext;
    Slast = Scurr; Scurr = Snext;
  }
  Slast = 0.0; Scurr = Sijz(0, 0);
  for (int i = 1; i < ulz+1; i++){
    Snext = ZPA*Scurr + one2p*(i-1)*Slast;
    Sijz(i, 0) = Snext;
    Slast = Scurr; Scurr = Snext;
  }

  // Next we increment the j using the equivalent recursion formula
  // First, calculate XPB, YPB, ZPB
  double XPB = vals(2) - vcoords(0);
  double YPB = vals(3) - vcoords(1);
  double ZPB = vals(4) - vcoords(2);

  // Get the Si1 before looping, if needed
  if(vlx>0){
    for (int k = 0; k < ulx+1; k++){
      int ktemp = (k > 0 ? k-1 : 0); // Avoid out of bounds errors
      Sijx(k, 1) = XPB*Sijx(k,0) + one2p*k*Sijx(ktemp, 0); 
    
      // Then loop
      for (int j = 2; j < vlx+1; j++)
	Sijx(k, j) = XPB*Sijx(k, j-1) + one2p*(k*Sijx(ktemp, j-1) +
					       (j-1)*Sijx(k, j-2));
    }
  }
  // Repeat for y, z
  if(vly>0){
    for (int k = 0; k < uly+1; k++){
      int ktemp= (k > 0 ? k-1 : 0); // Avoid out of bounds errors                                                                                                                                               
      Sijy(k, 1) = YPB*Sijy(k,0) + one2p*k*Sijy(ktemp, 0);

      // Then loop                                                                                                                                                                  
      for (int j = 2; j < vly+1; j++)
        Sijy(k, j) = YPB*Sijy(k, j-1) + one2p*(k*Sijy(ktemp, j-1) +
                                               (j-1)*Sijy(k, j-2));
    }
  }
  if(vlz>0){
    for (int k = 0; k < ulz+1; k++){
      int ktemp= (k > 0 ? k-1 : 0); // Avoid out of bounds errors                                                                                                                                               
      Sijz(k, 1) = ZPB*Sijz(k,0) + one2p*k*Sijz(ktemp, 0);

      // Then loop                                                                                                                                                                             
      for (int j = 2; j < vlz+1; j++)
        Sijz(k, j) = ZPB*Sijz(k, j-1) + one2p*(k*Sijz(ktemp, j-1) +
                                               (j-1)*Sijz(k, j-2));
    }
  }

  // Get final overlap integral
  rvals[0] = unorm*vnorm*Sijx(ulx, vlx)*Sijy(uly, vly)*Sijz(ulz, vlz);
  
  // Now compute the kinetic energy integral
  
  // Start by calculating T00 in each direction
  Matrix Tijx(ulx+1, vlx+1); Matrix Tijy(uly+1, vly+1); Matrix Tijz(ulz+1, vlz+1);
  Tijx(0, 0) = (uexp - 2*uexp*uexp*(XPA*XPA + one2p))*Sijx(0, 0);
  Tijy(0, 0) = (uexp - 2*uexp*uexp*(YPA*YPA + one2p))*Sijy(0, 0);
  Tijz(0, 0) = (uexp - 2*uexp*uexp*(ZPA*ZPA + one2p))*Sijz(0, 0);

  // A couple of repeatedly used multipliers
  double vp = vexp/vals(0); // vexp/p
  double up = uexp/vals(0); // uexp/p

  // Form the Ti0 values
  if (ulx > 0) {
    // Get T10 first
    Tijx(1, 0) = XPA*Tijx(0, 0) + vp*2*uexp*Sijx(1, 0);

    // Loop for rest
    for (int i = 2; i < ulx+1; i++){
      Tijx(i, 0) = XPA*Tijx(i-1, 0) + one2p*(i-1)*Tijx(i-2, 0) + 
	vp*(2*uexp*Sijx(i, 0) - (i-1)*Sijx(i-2, 0));
    }
  }
  // Repeat for y and z components
  if (uly > 0) {
    // Get T10 first
    Tijy(1, 0) = YPA*Tijy(0, 0) + vp*2*uexp*Sijy(1, 0);

    // Loop for rest
    for(int i = 2; i <uly+1; i++){
      Tijy(i, 0) = YPA*Tijy(i-1, 0) + one2p*(i-1)*Tijy(i-2, 0) +
        vp*(2*uexp*Sijy(i, 0) - (i-1)*Sijy(i-2, 0));
    }
  }
  if (ulz > 0) {
    // Get T10 first
    Tijz(1, 0) = ZPA*Tijz(0, 0) + vp*2*uexp*Sijz(1, 0);

    // Loop for rest
    for(int i = 2; i <ulz+1; i++){
      Tijz(i, 0) = ZPA*Tijz(i-1, 0) + one2p*(i-1)*Tijz(i-2, 0) +
        vp*(2*uexp*Sijz(i, 0) - (i-1)*Sijz(i-2, 0));
    }
  }
  
  // Now increment j

  if (vlx > 0){
    for (int k = 0; k < ulx+1; k++){
      int ktemp = (k > 0 ? k-1 : 0);
      Tijx(k, 1) = XPB*Tijx(k,0) + one2p*k*Tijx(ktemp, 0)
	+ up*2*vexp*Sijx(k, 1);
      
      for (int j = 2; j < vlx+1; j++){
	Tijx(k, j) = XPB*Tijx(k, j-1) + one2p*(k*Tijx(ktemp, j-1) + (j-1)*Tijx(k, j-2))
	  + up*(2*vexp*Sijx(k, j) - (j-1)*Sijx(k, j-2));
      }
    }
  }
  // Repeat for y and z
  if (vly > 0){
    for(int k = 0; k <uly+1; k++){
      int ktemp = (k > 0 ? k-1 : 0);
      Tijy(k, 1) = YPB*Tijy(k,0) + one2p*k*Tijy(ktemp, 0)
	+ up*2*vexp*Sijy(k, 1);
      
      for (int j = 2; j< vly+1; j++){
	Tijy(k,j) = YPB*Tijy(k, j-1) +one2p*(k*Tijy(ktemp, j-1) + (j-1)*Tijy(k, j-2))
          + up*(2*vexp*Sijy(k, j) - (j-1)*Sijy(k, j-2));
      } 
    }
  }
  if (vlz > 0){
    for(int k = 0; k <ulz+1; k++){
      int ktemp = (k > 0 ? k-1 : 0);
      Tijz(k, 1) = ZPB*Tijz(k,0) + one2p*k*Tijz(ktemp, 0)
	+ up*2*vexp*Sijz(k, 1);
      
      for (int j = 2; j< vlz+1; j++){
	Tijz(k,j) = ZPB*Tijz(k, j-1) +one2p*(k*Tijz(ktemp, j-1) + (j-1)*Tijz(k, j-2))
          + up*(2*vexp*Sijz(k, j) - (j-1)*Sijz(k, j-2));
      } 
    }
  }

  // Construct the final kinetic energy integral as:
  // Tuv = Tijx*Sijy*Sijz + Sijx*Tijy*Sijz + Sijx*Sijy*Tijz
  rvals[1] = unorm*vnorm*(Tijx(ulx, vlx)*Sijy(uly, vly)*Sijz(ulz, vlz)
    + Sijx(ulx, vlx)*Tijy(uly, vly)*Sijz(ulz, vlz) 
			 + Sijx(ulx, vlx)*Sijy(uly, vly)*Tijz(ulz, vlz));
  
  return rvals;
}

// Form the matrix of nuclear attraction integrals
void IntegralEngine::formNucAttract()
{
}

// Calculate a multipole integral between two bfs a,b 
// about the point c, to a given set of powers in the
// cartesian coordinates of c.
// Uses Obara-Saika recurrence relations. 
double IntegralEngine::multipole(BF& a, BF& b, const Vector& acoords,
				 const Vector& bcoords, const Vector& ccoords,
				 const Vector& powers) const
{
  double integral; // To return answer in
  
  // Get the number of primitives on each, and contraction coefficients
  Vector acoeffs; Vector bcoeffs;
  acoeffs = a.getCoeffs();
  bcoeffs = b.getCoeffs();
  int aN = a.getNPrims();
  int bN = b.getNPrims(); 
  
  // Need to store primitive integrals
  Vector prims(aN*bN);

  // Loop over primitives
  PBF apbf; PBF bpbf;
  for (int u = 0; u < aN; u++){
    apbf = a.getPBF(u);
    for (int v = 0; v < bN; v++){
      bpbf = b.getPBF(v);

      // Calculate the primitive integral
      prims[u*bN+v] = multipole(apbf, bpbf, acoords, bcoords, ccoords, powers);
    }
  }
  
  // Contract
  integral = makeContracted(acoeffs, bcoeffs, prims);
  return integral;
}

// Calculate the above multipole integral between two primitives
double IntegralEngine::multipole(PBF& u,  PBF& v, const Vector& ucoords,
		 const Vector& vcoords, const Vector& ccoords, 
		 const Vector& powers) const
{
  // To be written
  double integral;
  return integral;
}
