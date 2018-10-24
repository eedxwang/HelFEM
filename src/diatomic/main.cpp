#include "../general/cmdline.h"
#include "../general/constants.h"
#include "../general/diis.h"
#include "../general/dftfuncs.h"
#include "../general/timer.h"
#include "../general/elements.h"
#include "../general/scf_helpers.h"
#include "basis.h"
#include "dftgrid.h"
#include <cfloat>
#include <climits>

using namespace helfem;

void classify_orbitals(const arma::mat & C, const arma::ivec & mvals, const std::vector<arma::uvec> & mposidx, const std::vector<arma::uvec> & mnegidx) {
  for(size_t io=0;io<C.n_cols;io++) {
    arma::vec orb(C.col(io));

    arma::vec opchar(mvals.n_elem), onchar(mvals.n_elem);
    for(size_t c=0;c<mvals.n_elem;c++) {
      opchar(c)=arma::norm(orb(mposidx[c]),"fro");
      onchar(c)=arma::norm(orb(mnegidx[c]),"fro");
    }

    // Total character is
    arma::vec ochar(opchar+onchar);

    // Normalize
    opchar/=arma::sum(ochar);
    onchar/=arma::sum(ochar);
    ochar/=arma::sum(ochar);

    // Orbital symmetry is then
    arma::uword oidx;
    double stot=ochar.max(oidx);

    // Symmetry threshold
    double thr=0.999;

    // Classification
    std::ostringstream cl;

    if(stot>=thr) {
      // Orbital symmetry
      char sym=' ';
      if(opchar(oidx)>=thr)
        sym='g';
      else if(onchar(oidx)>=thr)
        sym='u';

      printf("Orbital %2i: m=%+i %c\n",(int) io+1,(int) mvals(oidx),sym);
    } else {
      printf("Orbital %2i: unknown\n",(int) io+1);
    }
  }
}

void normalize_matrix(arma::mat & M, const arma::vec & norm) {
  if(M.n_rows != norm.n_elem) throw std::logic_error("Incompatible dimensions!\n");
  if(M.n_cols != norm.n_elem) throw std::logic_error("Incompatible dimensions!\n");
  for(size_t i=0;i<M.n_rows;i++)
    for(size_t j=0;j<M.n_cols;j++)
      M(i,j)*=norm(i)*norm(j);
}

int main(int argc, char **argv) {
  cmdline::parser parser;

  // full option name, no short option, description, argument required
  parser.add<std::string>("Z1", 0, "first nuclear charge", true);
  parser.add<std::string>("Z2", 0, "second nuclear charge", true);
  parser.add<double>("Rbond", 0, "internuclear distance", true);
  parser.add<bool>("angstrom", 0, "input distances in angstrom", false, false);
  parser.add<int>("nela", 0, "number of alpha electrons", false, 0);
  parser.add<int>("nelb", 0, "number of beta  electrons", false, 0);
  parser.add<int>("Q", 0, "charge state", false, 0);
  parser.add<int>("M", 0, "spin multiplicity", false, 0);
  parser.add<std::string>("lmax", 0, "maximum l quantum number", true, "");
  parser.add<int>("mmax", 0, "maximum m quantum number", false, -1);
  parser.add<int>("lpad", 0, "padding for max l for more accurate Qlm recursion", false, 10);
  parser.add<double>("Rmax", 0, "practical infinity in au", false, 40.0);
  parser.add<int>("grid", 0, "type of grid: 1 for linear, 2 for quadratic, 3 for polynomial, 4 for logarithmic", false, 1);
  parser.add<double>("zexp", 0, "parameter in radial grid", false, 1.0);
  parser.add<int>("nelem", 0, "number of elements", true);
  parser.add<int>("nnodes", 0, "number of nodes per element", false, 15);
  parser.add<int>("nquad", 0, "number of quadrature points", false, 0);
  parser.add<int>("maxit", 0, "maximum number of iterations", false, 50);
  parser.add<double>("convthr", 0, "convergence threshold", false, 1e-7);
  parser.add<double>("Ez", 0, "electric dipole field", false, 0.0);
  parser.add<double>("Qzz", 0, "electric quadrupole field", false, 0.0);
  parser.add<double>("Bz", 0, "magnetic dipole field", false, 0.0);
  parser.add<bool>("diag", 0, "exact diagonalization", false, 1);
  parser.add<std::string>("method", 0, "method to use", false, "HF");
  parser.add<int>("ldft", 0, "theta rule for dft quadrature (0 for auto)", false, 0);
  parser.add<int>("mdft", 0, "phi rule for dft quadrature (0 for auto)", false, 0);
  parser.add<double>("dftthr", 0, "density threshold for dft", false, 1e-12);
  parser.add<int>("restricted", 0, "spin-restricted orbitals", false, -1);
  parser.add<int>("symmetry", 0, "force orbital symmetry", false, 1);
  parser.add<int>("primbas", 0, "primitive radial basis", false, 4);
  parser.add<double>("diiseps", 0, "when to start mixing in diis", false, 1e-2);
  parser.add<double>("diisthr", 0, "when to switch over fully to diis", false, 1e-3);
  parser.add<int>("diisorder", 0, "length of diis history", false, 5);
  parser.add<int>("readocc", 0, "read occupations from file, use until nth build", false, 0);
  parser.add<double>("perturb", 0, "randomly perturb initial guess", false, 0.0);
  parser.add<int>("seed", 0, "seed for random perturbation", false, 0);
  parser.parse_check(argc, argv);

  // Get parameters
  double Rmax(parser.get<double>("Rmax"));
  int igrid(parser.get<int>("grid"));
  double zexp(parser.get<double>("zexp"));
  double Ez(parser.get<double>("Ez"));
  double Qzz(parser.get<double>("Qzz"));
  double Bz(parser.get<double>("Bz"));

  int maxit(parser.get<int>("maxit"));
  double convthr(parser.get<double>("convthr"));

  bool diag(parser.get<bool>("diag"));
  int restr(parser.get<int>("restricted"));
  int symm(parser.get<int>("symmetry"));

  int primbas(parser.get<int>("primbas"));
  // Number of elements
  int Nelem(parser.get<int>("nelem"));
  // Number of nodes
  int Nnodes(parser.get<int>("nnodes"));
  // Order of quadrature rule
  int Nquad(parser.get<int>("nquad"));
  // Angular grid
  std::string lmax(parser.get<std::string>("lmax"));
  int mmax(parser.get<int>("mmax"));
  int lpad(parser.get<int>("lpad"));

  // DFT angular grid
  int ldft(parser.get<int>("ldft"));
  int mdft(parser.get<int>("mdft"));
  double dftthr(parser.get<double>("dftthr"));

  // Nuclear charge
  int Z1(get_Z(parser.get<std::string>("Z1")));
  int Z2(get_Z(parser.get<std::string>("Z2")));
  double Rbond(parser.get<double>("Rbond"));
  // Number of occupied states
  int nela(parser.get<int>("nela"));
  int nelb(parser.get<int>("nelb"));
  int Q(parser.get<int>("Q"));
  int M(parser.get<int>("M"));

  double diiseps=parser.get<double>("diiseps");
  double diisthr=parser.get<double>("diisthr");
  int diisorder=parser.get<int>("diisorder");

  std::string method(parser.get<std::string>("method"));

  double perturb=parser.get<double>("perturb");
  int seed=parser.get<int>("seed");

  // Read occupations from file?
  int readocc=parser.get<int>("readocc");
  if(readocc<0)
    readocc=INT_MAX;
  arma::imat occs;
  if(readocc) {
    occs.load("occs.dat",arma::raw_ascii);
    if(occs.n_cols < 3) {
      throw std::logic_error("Must have at least three columns in occupation data.\n");
    }
  }

  if(parser.get<bool>("angstrom")) {
    // Convert to atomic units
    Rbond*=ANGSTROMINBOHR;
  }

  scf::parse_nela_nelb(nela,nelb,Q,M,Z1+Z2);
  if(restr==-1) {
    // If number of electrons differs then unrestrict
    restr=(nela==nelb);
  }

  std::vector<std::string> rcalc(2);
  rcalc[0]="unrestricted";
  rcalc[1]="restricted";

  printf("Running %s %s calculation with Rmax=%e and %i elements.\n",rcalc[restr].c_str(),method.c_str(),Rmax,Nelem);

  // Get primitive basis
  polynomial_basis::PolynomialBasis *poly(polynomial_basis::get_basis(primbas,Nnodes));

  if(Nquad==0)
    // Set default value
    Nquad=5*poly->get_nbf();
  else if(Nquad<2*poly->get_nbf())
    throw std::logic_error("Insufficient radial quadrature.\n");

  printf("Using %i point quadrature rule.\n",Nquad);

  arma::ivec lmmax;
  if(mmax>=0) {
    lmmax.ones(mmax+1);
    lmmax*=atoi(lmax.c_str());
  } else {
    // Parse list of l values
    std::vector<arma::uword> lmmaxv;
    std::stringstream ss(lmax);
    while( ss.good() ) {
      std::string substr;
      getline( ss, substr, ',' );
      lmmaxv.push_back(atoi(substr.c_str()));
    }
    lmmax=arma::conv_to<arma::ivec>::from(lmmaxv);
  }

  diatomic::basis::TwoDBasis basis(Z1, Z2, Rbond, poly, Nquad, Nelem, Rmax, lmmax, igrid, zexp, lpad);
  printf("Basis set consists of %i angular shells composed of %i radial functions, totaling %i basis functions\n",(int) basis.Nang(), (int) basis.Nrad(), (int) basis.Nbf());

  printf("One-electron matrix requires %s\n",scf::memory_size(basis.mem_1el()).c_str());
  printf("Auxiliary one-electron integrals require %s\n",scf::memory_size(basis.mem_1el_aux()).c_str());
  printf("Auxiliary two-electron integrals require %s\n",scf::memory_size(basis.mem_2el_aux()).c_str());

  double Enucr=Z1*Z2/Rbond;
  printf("Left- and right-hand nuclear charges are %i and %i at distance % .3f\n",Z1,Z2,Rbond);
  printf("Nuclear repulsion energy is %e\n",Enucr);
  printf("Number of electrons is %i %i\n",nela,nelb);

  // Collect basis function indices
  arma::ivec mvals;
  {
    arma::ivec mv(basis.get_m());
    arma::uvec idx(arma::find_unique(mv,true));
    mvals=mv(idx);
  }
  std::vector<arma::uvec> midx(mvals.n_elem), mposidx(mvals.n_elem), mnegidx(mvals.n_elem);
  for(size_t i=0;i<midx.size();i++) {
    midx[i]=basis.m_indices(mvals(i));
    mposidx[i]=basis.m_indices(mvals(i),false);
    mnegidx[i]=basis.m_indices(mvals(i),true);
  }

  // Symmetry indices
  std::vector<arma::uvec> dsym;
  if(symm==2 && Z1!=Z2) {
    printf("Warning - asked for homonuclear symmetry for heteronuclear molecule. Relaxing restriction.\n");
    symm=1;
  }
  if(symm==2 && (Ez!=0.0 || Qzz!=0.0)) {
    printf("Warning - asked for full orbital symmetry in presence of electric field. Relaxing restriction.\n");
    symm=1;
  }
  if(symm==2 && Bz!=0.0) {
    printf("Warning - asked for full orbital symmetry in presence of magnetic field. Relaxing restriction.\n");
    symm=1;
  }
  if(symm)
    dsym=basis.get_sym_idx(symm);

  // Forced occupations?
  arma::ivec occnuma, occnumb;
  std::vector<arma::uvec> occsym;
  if(readocc) {
    // Number of occupied alpha orbitals is first column
    occnuma=occs.col(0);
    // Number of occupied beta orbitals is second column
    occnumb=occs.col(1);

    // Heteronuclear molecule?
    if(Z1 != Z2 && occs.n_cols != 3)
      throw std::logic_error("Heteronuclear molecule orbital occupations must have three columns.\n");
    if(Z1 == Z2 && (occs.n_cols != 3) && (occs.n_cols != 4))
      throw std::logic_error("Homonuclear molecule orbital occupations must have three or four columns.\n");

    if(occs.n_cols == 3) {
      // m value is third column
      for(size_t i=0;i<occs.n_rows;i++)
        occsym.push_back(basis.m_indices(occs(i,2)));
    } else if(occs.n_cols == 4) {
      if(symm!=2)
        throw std::logic_error("For use of homonuclear orbital occupations, must turn on use of full symmetry.\n");
      // m value is third column, parity is fourth column
      for(size_t i=0;i<occs.n_rows;i++) {
        if(occs(i,3)!=1 && occs(i,3)!=-1) {
          std::ostringstream oss;
          oss << "Error on line " << i+1 << " of orbital occupations: parity must be +1 or -1\n";
          throw std::logic_error(oss.str());
        }
        occsym.push_back(basis.m_indices(occs(i,2),(occs(i,3)==-1)));
      }
    }

    // Check consistency of values
    if(arma::sum(occnuma) != nela) {
      std::ostringstream oss;
      oss << "Specified alpha occupations don't match wanted spin state.\n";
      oss << "Occupying " << arma::sum(occnuma) << " orbitals but should have " << nela << " orbitals.\n";
      throw std::logic_error(oss.str());
    }
    if(arma::sum(occnumb) != nelb) {
      std::ostringstream oss;
      oss << "Specified alpha occupations don't match wanted spin state.\n";
      oss << "Occupying " << arma::sum(occnumb) << " orbitals but should have " << nelb << " orbitals.\n";
      throw std::logic_error(oss.str());
    }
  }

  // Functional
  int x_func, c_func;
  ::parse_xc_func(x_func, c_func, method);
  ::print_info(x_func, c_func);

  bool dft=(x_func>0 || c_func>0);
  if(is_range_separated(x_func))
    throw std::logic_error("Range separated functionals are not supported.\n");
  // Fraction of exact exchange
  double kfrac(exact_exchange(x_func));
  if(kfrac!=0.0)
    printf("\nUsing hybrid exchange with % .3f %% of exact exchange.\n",kfrac*100);
  else
    printf("\nA pure exchange functional used, no exact exchange.\n");

  Timer timer;

  // Form overlap matrix
  arma::mat S(basis.overlap());
  // Form kinetic energy matrix
  arma::mat T(basis.kinetic());

  helfem::dftgrid::DFTGrid grid;
  if(dft) {
    if(ldft==0)
      // Default value: we have 2*lmax from the bra and ket and 2 from
      // the volume element, and allow for 2*lmax from the
      // density/potential. Add in 10 more for a bit more accuracy.
      ldft=4*arma::max(lmmax)+12;
    if(ldft<(int) (2*arma::max(lmmax)+2))
      throw std::logic_error("Increase ldft to guarantee accuracy of quadrature!\n");

    if(mdft==0)
      // Default value: we have 2*mmax from the bra and ket, and allow
      // for 2*mmax from the density/potential. Add in 5 to make
      // sure quadrature is still accurate for mmax=0
      mdft=4*lmmax.n_elem+5;
    if(mdft<(int) (2*lmmax.n_elem))
      throw std::logic_error("Increase mdft to guarantee accuracy of quadrature!\n");

    // Form grid
    grid=helfem::dftgrid::DFTGrid(&basis,ldft,mdft);

    // Basis function norms
    arma::vec bfnorm(arma::pow(arma::diagvec(S),-0.5));

    // Check accuracy of grid
    double Sthr=1e-10;
    double Tthr=1e-8;
    bool inacc=false;
    {
      arma::mat Sdft(grid.eval_overlap());
      Sdft-=S;
      normalize_matrix(Sdft,bfnorm);

      double Serr(arma::norm(Sdft,"fro"));
      printf("Error in overlap matrix evaluated through xc grid is %e\n",Serr);
      fflush(stdout);
      if(Serr>=Sthr)
        inacc=true;
    }
    {
      arma::mat Tdft(grid.eval_kinetic());
      // Compute relative error
      for(size_t j=0;j<Tdft.n_cols;j++)
        for(size_t i=0;i<Tdft.n_rows;i++)
          Tdft(i,j)=std::abs(Tdft(i,j)-T(i,j))/(1+std::abs(T(i,j)));

      double Terr(arma::norm(Tdft,"fro"));
      printf("Relative error in kinetic matrix evaluated through xc grid is %e\n",Terr);
      fflush(stdout);
      if(Terr>=Tthr)
        inacc=true;
    }
    if(inacc)
      printf("Warning - possibly inaccurate quadrature!\n");
  }

  // Get half-inverse
  timer.set();
  arma::mat Sinvh(basis.Sinvh(!diag,symm));
  printf("Half-inverse formed in %.6f\n",timer.get());
  {
    arma::mat Smo(Sinvh.t()*S*Sinvh);
    Smo-=arma::eye<arma::mat>(Smo.n_rows,Smo.n_cols);
    printf("Orbital orthonormality deviation is %e\n",arma::norm(Smo,"fro"));
  }
  arma::mat Sh(basis.Shalf(!diag,symm));
  printf("Half-overlap formed in %.6f\n",timer.get());
  {
    arma::mat Smo(Sh.t()*Sinvh);
    Smo-=arma::eye<arma::mat>(Smo.n_rows,Smo.n_cols);
    printf("Half-overlap error is %e\n",arma::norm(Smo,"fro"));
  }

  // Form nuclear attraction energy matrix
  Timer tnuc;
  const arma::mat Vnuc(basis.nuclear());

  // Dipole coupling
  const arma::mat dip(basis.dipole_z());
  // Quadrupole coupling
  const arma::mat quad(basis.quadrupole_zz());

  // Nuclear dipole and quadrupole
  const double nucdip=(Z2-Z1)*Rbond/2.0;
  const double nucquad=(Z1+Z2)*Rbond*Rbond/4.0;

  // Electric field coupling (minus sign cancels one from charge)
  const arma::mat Vel(Ez*dip + Qzz*quad/3.0);
  // Magnetic field coupling
  arma::mat Vmag(basis.Bz_field(Bz));
  const double Enucfield(-Ez*nucdip - Qzz*nucquad/3.0);

  // Form Hamiltonian
  const arma::mat H0(T+Vnuc+Vel+Vmag);

  printf("One-electron matrices formed in %.6f\n",timer.get());

  // Occupied and virtual orbitals
  arma::mat Caocc, Cbocc, Cavirt, Cbvirt;
  arma::vec Ea, Eb;
  // Number of eigenenergies to print
  arma::uword nena(std::min((arma::uword) nela+4,Sinvh.n_cols));
  arma::uword nenb(std::min((arma::uword) nelb+4,Sinvh.n_cols));

  // Guess orbitals
  timer.set();
  {
    // Use core guess
    arma::vec E;
    arma::mat C;
    if(symm)
      scf::eig_gsym_sub(E,C,H0,Sinvh,dsym);
    else
      scf::eig_gsym(E,C,H0,Sinvh);

    Ea=E;
    arma::mat Ca(C);
    Eb=E;
    arma::mat Cb(C);

    // Enforce occupation according to specified symmetry
    if(readocc) {
      scf::enforce_occupations(Ca,Ea,occnuma,occsym);
      if(restr && nela==nelb)
        Cb=Ca;
      else
        scf::enforce_occupations(Cb,Eb,occnumb,occsym);
    }

    // Perturb guess
    if(perturb) {
      // Generate norb x norb rotation matrix
      arma::arma_rng::set_seed(seed);
      Ca*=scf::perturbation_matrix(Ca.n_cols,perturb);
      if(restr && nela==nelb) {
        Cb=Ca;
      } else {
        Cb*=scf::perturbation_matrix(Cb.n_cols,perturb);
      }
      printf("Guess orbitals perturbed by %e\n",perturb);
    }

    // Alpha orbitals
    Caocc=Ca.cols(0,nela-1);
    if(C.n_cols>(size_t) nela)
      Cavirt=Ca.cols(nela,C.n_cols-1);

    // Beta guess
    if(nelb)
      Cbocc=Cb.cols(0,nelb-1);
    if(C.n_cols>(size_t) nelb)
      Cbvirt=Cb.cols(nelb,C.n_cols-1);

    Ea.subvec(0,nena-1).t().print("Alpha orbital energies");
    Eb.subvec(0,nenb-1).t().print("Beta  orbital energies");

    printf("\n");
    printf("Alpha orbital symmetries\n");
    classify_orbitals(Caocc,mvals,mposidx,mnegidx);
    if(nelb>0) {
      printf("\n");
      printf("Beta orbital symmetries\n");
      classify_orbitals(Cbocc,mvals,mposidx,mnegidx);
    }
    printf("\n");
  }
  printf("Initial guess performed in %.6f\n",timer.get());

  printf("Computing two-electron integrals\n");
  fflush(stdout);
  timer.set();
  basis.compute_tei(kfrac!=0.0);
  printf("Done in %.6f\n",timer.get());

  double Ekin, Epot, Ecoul, Exx, Exc, Eefield, Emfield, Etot;
  double Eold=0.0;

  bool usediis=true, useadiis=true, diiscomb=false;
  uDIIS diis(S,Sinvh,diiscomb,usediis,diiseps,diisthr,useadiis,true,diisorder);
  double diiserr;

  // Density matrices
  arma::mat P, Pa, Pb;

  for(int i=1;i<=maxit;i++) {
    printf("\n**** Iteration %i ****\n\n",i);

    // Form density matrix
    Pa=scf::form_density(Caocc,nela);
    Pb=scf::form_density(Cbocc,nelb);
    if(Pb.n_rows == 0)
      Pb.zeros(Pa.n_rows,Pa.n_cols);
    P=Pa+Pb;

    printf("Tr Pa = %f\n",arma::trace(Pa*S));
    if(nelb)
      printf("Tr Pb = %f\n",arma::trace(Pb*S));
    fflush(stdout);

    Ekin=arma::trace(P*T);
    Epot=arma::trace(P*Vnuc);
    Eefield=arma::trace(P*Vel);
    Emfield=arma::trace(P*Vmag)+Bz/2.0*(nela-nelb);

    // Form Coulomb matrix
    timer.set();
    arma::mat J(basis.coulomb(P));
    double tJ(timer.get());
    Ecoul=0.5*arma::trace(P*J);
    printf("Coulomb energy %.10e % .6f\n",Ecoul,tJ);
    fflush(stdout);

    // Form exchange matrix
    timer.set();
    arma::mat Ka, Kb;
    if(kfrac!=0.0) {
      Ka=kfrac*basis.exchange(Pa);

      if(nelb) {
        if(restr && nela==nelb)
          Kb=Ka;
        else
          Kb=kfrac*basis.exchange(Pb);
      } else
        Kb.zeros(Cbocc.n_rows,Cbocc.n_rows);
      double tK(timer.get());
      Exx=0.5*arma::trace(Pa*Ka);
      if(Kb.n_rows == Pb.n_rows && Kb.n_cols == Pb.n_cols)
        Exx+=0.5*arma::trace(Pb*Kb);
      printf("Exchange energy %.10e % .6f\n",Exx,tK);
    } else {
      Exx=0.0;
    }
    fflush(stdout);

    // Exchange-correlation
    Exc=0.0;
    arma::mat XCa, XCb;
    if(dft) {
      timer.set();
      double nelnum;
      double ekin;
      if(restr && nela==nelb) {
        grid.eval_Fxc(x_func, c_func, P, XCa, Exc, nelnum, ekin, dftthr);
        XCb=XCa;
      } else {
        grid.eval_Fxc(x_func, c_func, Pa, Pb, XCa, XCb, Exc, nelnum, ekin, nelb>0, dftthr);
      }
      double txc(timer.get());
      printf("DFT energy %.10e % .6f\n",Exc,txc);
      printf("Error in integrated number of electrons % e\n",nelnum-nela-nelb);
      if(ekin!=0.0)
        printf("Error in integral of kinetic energy density % e\n",ekin-Ekin);
    }
    fflush(stdout);

    // Fock matrices
    arma::mat Fa(H0+J);
    arma::mat Fb(H0+J);
    if(Ka.n_rows == Fa.n_rows) {
      Fa+=Ka;
    }
    if(Kb.n_rows == Fb.n_rows) {
      Fb+=Kb;
    }
    if(dft) {
      Fa+=XCa;
      if(nelb>0) {
        Fb+=XCb;
      }
    }
    if(Bz!=0.0) {
      // Add in the B*Sz term
      Fa+=Bz*S/2.0;
      Fb-=Bz*S/2.0;
    }

    // ROHF update to Fock matrix
    if(restr && nela!=nelb)
      scf::ROHF_update(Fa,Fb,P,Sh,Sinvh,nela,nelb);

    // Update energy
    Etot=Ekin+Epot+Eefield+Emfield+Ecoul+Exx+Exc+Enucr+Enucfield;
    double dE=Etot-Eold;

    printf("Total energy is % .10f\n",Etot);
    if(i>1)
      printf("Energy changed by %e\n",dE);
    Eold=Etot;
    fflush(stdout);

    /*
      S.print("S");
      T.print("T");
      Vnuc.print("Vnuc");
      Ca.print("Ca");
      Pa.print("Pa");
      J.print("J");
      Ka.print("Ka");

      arma::mat Jmo(Ca.t()*J*Ca);
      arma::mat Kmo(Ca.t()*Ka*Ca);
      Jmo.submat(0,0,10,10).print("Jmo");
      Kmo.submat(0,0,10,10).print("Kmo");


      Kmo+=Jmo;
      Kmo.print("Jmo+Kmo");

      Fa.print("Fa");
      arma::mat Fao(Sinvh.t()*Fa*Sinvh);
      Fao.print("Fao");
      Sinvh.print("Sinvh");
    */

    /*
      arma::mat Jmo(Ca.t()*J*Ca);
      arma::mat Kmo(Ca.t()*Ka*Ca);
      arma::mat Fmo(Ca.t()*Fa*Ca);
      Jmo=Jmo.submat(0,0,4,4);
      Kmo=Kmo.submat(0,0,4,4);
      Fmo=Fmo.submat(0,0,4,4);
      Jmo.print("J");
      Kmo.print("K");
      Fmo.print("F");
    */

    // Update DIIS
    timer.set();
    diis.update(Fa,Fb,Pa,Pb,Etot,diiserr);
    printf("DIIS error is %e, update done in %.6f\n",diiserr,timer.get());
    fflush(stdout);

    // Solve DIIS to get Fock update
    timer.set();
    diis.solve_F(Fa,Fb);
    printf("DIIS solution done in %.6f\n",timer.get());
    fflush(stdout);

    // Have we converged? Note that DIIS error is still wrt full space, not active space.
    bool convd=(diiserr<convthr) && (std::abs(dE)<convthr);

    // Diagonalize Fock matrix to get new orbitals
    timer.set();
    arma::mat Ca, Cb;
    if(symm)
      scf::eig_gsym_sub(Ea,Ca,Fa,Sinvh,dsym);
    else
      scf::eig_gsym(Ea,Ca,Fa,Sinvh);
    // Enforce occupation according to specified symmetry
    if(i<readocc) {
      scf::enforce_occupations(Ca,Ea,occnuma,occsym);
    }

    if(restr && nela==nelb) {
      Eb=Ea;
      Cb=Ca;
    } else {
      if(symm)
        scf::eig_gsym_sub(Eb,Cb,Fb,Sinvh,dsym);
      else
        scf::eig_gsym(Eb,Cb,Fb,Sinvh);
    }
    // Enforce occupation according to specified symmetry
    if(i<readocc) {
      scf::enforce_occupations(Cb,Eb,occnumb,occsym);
    }

    Caocc=Ca.cols(0,nela-1);
    if(Ca.n_cols>(size_t) nela)
      Cavirt=Ca.cols(nela,Ca.n_cols-1);
    if(nelb>0)
      Cbocc=Cb.cols(0,nelb-1);
    if(Cb.n_cols>(size_t) nelb)
      Cbvirt=Cb.cols(nelb,Cb.n_cols-1);
    if(symm)
      printf("Subspace diagonalization done in %.6f\n",timer.get());
    else
      printf("Full diagonalization done in %.6f\n",timer.get());

    if(Ea.n_elem>(size_t)nela)
      printf("Alpha HOMO-LUMO gap is % .3f eV\n",(Ea(nela)-Ea(nela-1))*HARTREEINEV);
    if(nelb && Eb.n_elem>(size_t)nelb)
      printf("Beta  HOMO-LUMO gap is % .3f eV\n",(Eb(nelb)-Eb(nelb-1))*HARTREEINEV);
    fflush(stdout);

    printf("\n");
    printf("Alpha orbital symmetries\n");
    classify_orbitals(Caocc,mvals,mposidx,mnegidx);
    if(nelb>0) {
      printf("\n");
      printf("Beta orbital symmetries\n");
      classify_orbitals(Cbocc,mvals,mposidx,mnegidx);
    }
    printf("\n");

    if(convd)
      break;
  }

  printf("%-21s energy: % .16f\n","Kinetic",Ekin);
  printf("%-21s energy: % .16f\n","Nuclear attraction",Epot);
  printf("%-21s energy: % .16f\n","Nuclear repulsion",Enucr);
  printf("%-21s energy: % .16f\n","Coulomb",Ecoul);
  printf("%-21s energy: % .16f\n","Exact exchange",Exx);
  printf("%-21s energy: % .16f\n","Exchange-correlation",Exc);
  printf("%-21s energy: % .16f\n","Electric field",Eefield);
  printf("%-21s energy: % .16f\n","Magnetic field",Emfield);
  printf("%-21s energy: % .16f\n","Nucleus-field",Enucfield);
  printf("%-21s energy: % .16f\n","Total",Etot);
  printf("%-21s energy: % .16f\n","Virial ratio",-Etot/Ekin);

  double eldip=-arma::trace(dip*P);
  double elquad=-arma::trace(quad*P);

  printf("\n");
  printf("Electronic dipole     moment % .16e\n",eldip);
  printf("Nuclear    dipole     moment % .16e\n",nucdip);
  printf("Total      dipole     moment % .16e\n",eldip+nucdip);
  printf("Electronic quadrupole moment % .16e\n",elquad);
  printf("Nuclear    quadrupole moment % .16e\n",nucquad);
  printf("Total      quadrupole moment % .16e\n",elquad+nucquad);

  printf("\n");
  printf("Nuclear electron densities\n");
  arma::vec nucdena(basis.nuclear_density(Pa));
  arma::vec nucdenb(basis.nuclear_density(Pb));
  for(size_t i=0;i<nucdena.size();i++) {
    int Z = (i==0) ? Z1 : Z2;
    if(Z==0)
      continue;
    printf("%-2s: % .10e % .10e % .10e\n",element_symbols[Z].c_str(),nucdena(i),nucdenb(i),nucdena(i)+nucdenb(i));
  }

  // rms sizes
  std::vector<arma::mat> orba, orbb;
  for(int io=0;io<nela;io++)
    orba.push_back(basis.radial_moments(Caocc.col(io)*arma::trans(Caocc.col(io))));
  for(int io=0;io<nelb;io++)
    orbb.push_back(basis.radial_moments(Cbocc.col(io)*arma::trans(Cbocc.col(io))));

  for(int ic=0;ic<3;ic++) {
    // Lh analysis only if lh atom exists
    if(ic==0 && Z1==0)
      continue;
    // Middle analysis only if both atoms exist
    if(ic==1 && (Z1==0 || Z2==0))
      continue;
    // Rh analysis only if rh atom exists
    if(ic==2 && Z2==0)
      continue;

    if(ic==0)
      printf("\nOccupied orbital analysis wrt left atom:\n");
    else if(ic==1)
      printf("\nOccupied orbital analysis wrt geometrical center:\n");
    else
      printf("\nOccupied orbital analysis wrt right atom:\n");

    enum moment {mone,
                 one,
                 two,
                 three};

    if(ic==1) {
      printf("Alpha orbitals\n");
      printf("%2s %13s %12s\n","io","energy","sqrt(<r^2>)");
      for(int io=0;io<nela;io++) {
        printf("%2i % e %e\n",(int) io+1, Ea(io), sqrt(orba[io](two,ic)));
      }
      printf("Beta orbitals\n");
      for(int io=0;io<nelb;io++) {
        printf("%2i % e %e\n",(int) io+1, Eb(io), sqrt(orbb[io](two,ic)));
      }
    } else {
      printf("Alpha orbitals\n");
      printf("%2s %13s %12s %12s %12s %12s\n","io","energy","1/<r^-1>","<r>","sqrt(<r^2>)","cbrt(<r^3>)");
      for(int io=0;io<nela;io++) {
        printf("%2i % e %e %e %e %e\n",(int) io+1, Ea(io), 1.0/orba[io](mone,ic), orba[io](one,ic), sqrt(orba[io](two,ic)), cbrt(orba[io](three,ic)));
      }
      printf("Beta orbitals\n");
      for(int io=0;io<nelb;io++) {
        printf("%2i % e %e %e %e %e\n",(int) io+1, Eb(io), 1.0/orbb[io](mone,ic), orbb[io](one,ic), sqrt(orbb[io](two,ic)), cbrt(orbb[io](three,ic)));
      }
    }
  }

  /*
  // Test orthonormality
  arma::mat Smo(Ca.t()*S*Ca);
  Smo-=arma::eye<arma::mat>(Smo.n_rows,Smo.n_cols);
  printf("Alpha orthonormality deviation is %e\n",arma::norm(Smo,"fro"));
  Smo=(Cb.t()*S*Cb);
  Smo-=arma::eye<arma::mat>(Smo.n_rows,Smo.n_cols);
  printf("Beta orthonormality deviation is %e\n",arma::norm(Smo,"fro"));
  */

  return 0;
}