#ifdef DE

#include"global.h"
#include"grid3D.h"
#include<stdio.h>
#include<stdlib.h>
#include"math.h"
#include <iostream>
#include"io.h"
#include<stdarg.h>
#include<string.h>
#include <iostream>
#include <fstream>
#include<ctime>

using namespace std;

#ifdef PARALLEL_OMP
#include"parallel_omp.h"
#endif

#ifdef MPI_CHOLLA
#include"mpi_routines.h"
#endif



void Grid3D::Write_DE_Eta_Beta_File( ){
  
  string file_name ( "dual_energy_eta_beta.dat" );
  chprintf( "\nCreating Eta-Beta File: %s \n\n", file_name.c_str() );
  
  bool file_exists = false;
  if (FILE *file = fopen(file_name.c_str(), "r")){
    file_exists = true;
    chprintf( "  File exists, appending values: %s \n\n", file_name.c_str() );
    fclose( file );
  } 
  
  // current date/time based on current system
  time_t now = time(0);
  // convert now to string form
  char* dt = ctime(&now);
  
  int i, n_steps;
  n_steps = 100;
  
  Real E, U, eta, beta, delta_U;
  E = 1.0;
  U = E * DUAL_ENERGY_ETA_0;
  delta_U = ( E - U ) / ( n_steps - 1 );
    
  ofstream out_file;
  if ( procID == 0 ){
    out_file.open(file_name.c_str() );
    out_file << "# eta beta" << endl;
    for ( i=0; i<n_steps; i++ ){
      eta = U / E;
      beta = Get_Dual_Energy_Beta( E, U );      
      out_file << eta << " " << beta << endl;
      U += delta_U;
    }
    out_file.close();
  }
}

int Grid3D::Select_Internal_Energy_From_DE( Real E, Real U_total, Real U_advected ){

  Real eta = DE_LIMIT;
  
  if( U_total / E > eta ) return 0;
  else return 1;  
}

Real Grid3D::Get_Dual_Energy_Beta( Real E, Real U_total ){
  
  Real x_0, x_1;
  Real eta_0, eta_1, beta_0, beta_1, eta, beta;
  eta_0 = DUAL_ENERGY_ETA_0;
  eta_1 = DUAL_ENERGY_ETA_1;
  beta_0 = DUAL_ENERGY_BETA_0;
  beta_1 = DUAL_ENERGY_BETA_1;
  
  eta = U_total / E;
  
  x_0 = log10( eta_0 );
  x_1 = log10( eta_1 );
  
  beta = ( beta_1 - beta_0 ) / ( x_1 - x_0 ) * ( log10(eta) - x_0 )  + beta_0;
  return beta;  
}

void Grid3D::Sync_Energies_3D_CPU(){
  #ifndef PARALLEL_OMP
  Sync_Energies_3D_CPU_function( 0, H.nz_real );
  #ifdef TEMPERATURE_FLOOR
  Apply_Temperature_Floor_CPU_function( 0, H.nz_real );
  #endif
  #else
  #pragma omp parallel num_threads( N_OMP_THREADS )
  {
    int omp_id, n_omp_procs;
    int g_start, g_end;

    omp_id = omp_get_thread_num();
    n_omp_procs = omp_get_num_threads();
    Get_OMP_Grid_Indxs( H.nz_real, n_omp_procs, omp_id, &g_start, &g_end  );

    Sync_Energies_3D_CPU_function( g_start, g_end );
    #ifdef TEMPERATURE_FLOOR
    #pragma omp barrier
    Apply_Temperature_Floor_CPU_function( g_start, g_end );
    #endif
  }
  #endif
  
  
}

Real Grid3D::Get_Pressure_From_Energy( int indx ){
  
  Real d, d_inv, vx, vy, vz, E, Ek, p;
  d = C.density[indx];
  d_inv = 1/d;
  vx = C.momentum_x[indx] * d_inv;
  vy = C.momentum_y[indx] * d_inv;
  vz = C.momentum_z[indx] * d_inv;
  E = C.Energy[indx];
  Ek = 0.5*d*(vx*vx + vy*vy + vz*vz);
  p = (gama - 1) * ( E - Ek );
  return p;  
}

bool Get_Pressure_Jump( Real gamma, Real rho_l, Real rho_r, Real p_l, Real p_r ){
  bool pressure_jump = false;
  if ( ( fabs( p_r - p_l ) / fmin( p_r, p_l) ) > ( 0.1 * gamma *  fabs( rho_r - rho_l ) / fmin( rho_r, rho_l)  ) ) pressure_jump = true;
  return pressure_jump;
}

Real Get_Second_Derivative( int i, int j, int k, int direction, int nx, int ny, int nz, Real dx, Real dy, Real dz, Real *field ){
  
  Real delta_x;
  int id_c, id_l, id_r;
  id_c = (i) + (j)*nx + (k)*ny*nx;
  if ( direction == 0 ){
    id_l = (i-1) + (j)*nx + (k)*ny*nx;
    id_r = (i+1) + (j)*nx + (k)*ny*nx; 
    delta_x = dx; 
  }
  if ( direction == 1 ){
    id_l = (i) + (j-1)*nx + (k)*ny*nx;
    id_r = (i) + (j+1)*nx + (k)*ny*nx;
    delta_x = dy;  
  }
  if ( direction == 2 ){
    id_l = (i) + (j)*nx + (k-1)*ny*nx;
    id_r = (i) + (j)*nx + (k+1)*ny*nx;
    delta_x = dz;  
  }
  
  Real val_c, val_l, val_r, d2_val;
  val_c = field[id_c];
  val_l = field[id_l];
  val_r = field[id_r];
  
  //Finite Difference First Order Second Derivative:
  d2_val = ( val_r - 2*val_c + val_l ) / ( delta_x * delta_x );
  return d2_val;  
}

void Grid3D::Sync_Energies_3D_CPU_function( int g_start, int g_end ){
  
  int nx_grid, ny_grid, nz_grid, nGHST_grid;
  nGHST_grid = H.n_ghost;
  nx_grid = H.nx;
  ny_grid = H.ny;
  nz_grid = H.nz;

  int nx, ny, nz;
  nx = H.nx_real;
  ny = H.ny_real;
  nz = H.nz_real;

  int nGHST = nGHST_grid ;
  Real d, d_inv, vx, vy, vz, E, Ek, ge_total, ge_advected, Emax, U;
  int k, j, i, id;
  int k_g, j_g, i_g;
  int flag_DE;
  
  // Real eta = DE_LIMIT;
  // Real Beta_DE = BETA_DUAL_ENERGY;
  
  Real eta, Beta_DE;
  eta = DUAL_ENERGY_ETA_0;
  
  Real eta_val,  beta_val;

  Real v_l, v_r, delta_vx, delta_vy, delta_vz, delta_v2, ge_trunc;

  //Shock detection
  Real p_l, p_r, rho_l, rho_r;
  Real d2_rho_l, d2_rho_r;
  bool pressure_jump, density_curvature_same;

  int imo, ipo, jmo, jpo, kmo, kpo;
  for ( k_g=g_start; k_g<g_end; k_g++ ){
    for ( j_g=0; j_g<ny; j_g++ ){
      for ( i_g=0; i_g<nx; i_g++ ){

        i = i_g + nGHST;
        j = j_g + nGHST;
        k = k_g + nGHST;

        id  = (i) + (j)*nx_grid + (k)*ny_grid*nx_grid;
        imo = (i-1) + (j)*nx_grid + (k)*ny_grid*nx_grid;
        ipo = (i+1) + (j)*nx_grid + (k)*ny_grid*nx_grid;
        jmo = (i) + (j-1)*nx_grid + (k)*ny_grid*nx_grid;
        jpo = (i) + (j+1)*nx_grid + (k)*ny_grid*nx_grid;
        kmo = (i) + (j)*nx_grid + (k-1)*ny_grid*nx_grid;
        kpo = (i) + (j)*nx_grid + (k+1)*ny_grid*nx_grid;

        d = C.density[id];
        d_inv = 1/d;
        vx = C.momentum_x[id] * d_inv;
        vy = C.momentum_y[id] * d_inv;
        vz = C.momentum_z[id] * d_inv;
        E = C.Energy[id];

        Ek = 0.5*d*(vx*vx + vy*vy + vz*vz);
        ge_total = E - Ek;
        ge_advected = C.GasEnergy[id];

        //New Dual Enegy Condition from Teyssier 2015
        //Get delta velocity
        
        //X direcction
        v_l = C.momentum_x[imo] / C.density[imo];
        v_r = C.momentum_x[ipo] / C.density[ipo];
        delta_vx = v_r - v_l;
        
        //Y direcction
        v_l = C.momentum_y[jmo] / C.density[jmo];
        v_r = C.momentum_y[jpo] / C.density[jpo];
        delta_vy = v_r - v_l;
        
        //Z direcction
        v_l = C.momentum_z[kmo] / C.density[kmo];
        v_r = C.momentum_z[kpo] / C.density[kpo];
        delta_vz = v_r - v_l;
        
        delta_v2 = delta_vx*delta_vx + delta_vy*delta_vy + delta_vz*delta_vz; 
        
        //Get the truncation error (Teyssier 2015)
        ge_trunc = 0.5 * d * delta_v2;
        
        //Get Beta as a function of the internal energy fraction
        Beta_DE = Get_Dual_Energy_Beta( E, ge_total );
        
        eta_val = ge_total / E;
        beta_val = ge_total / ge_trunc;
        #ifdef COOLING_GRACKLE
        Cool.eta_value[id] = eta_val;
        Cool.beta_value[id] = beta_val;
        #endif
                
        if (ge_total > 0.0 && E > 0.0 && ge_total/E > eta && ge_total > 0.5*ge_advected && ( ge_total > Beta_DE * ge_trunc ) ){
          U = ge_total;
          flag_DE = 0;
        }            
        else{
          U = ge_advected;
          flag_DE = 1;
        }
        
        // //Syncronize advected internal energy with total internal energy when using total internal energy based on local maxEnergy condition
        // //find the max nearby total energy
        // Emax = E;
        // Emax = std::max(C.Energy[imo], E);
        // Emax = std::max(Emax, C.Energy[ipo]);
        // Emax = std::max(Emax, C.Energy[jmo]);
        // Emax = std::max(Emax, C.Energy[jpo]);
        // Emax = std::max(Emax, C.Energy[kmo]);
        // Emax = std::max(Emax, C.Energy[kpo]);
        // if (ge_total/Emax > 0.1 && ge_total > 0.0 && Emax > 0.0){
        //   U = ge_total;
        //   flag_DE = 0;
        // }
        
        
        //Shock detection: Fryxell, 2000
        //The pressure jump must be sufficiently large
        pressure_jump = false;
        
        //X direction
        rho_l = C.density[imo]; 
        rho_r = C.density[ipo]; 
        p_l = Get_Pressure_From_Energy( imo );
        p_r = Get_Pressure_From_Energy( ipo );
        pressure_jump = Get_Pressure_Jump( gama, rho_l, rho_r, p_l, p_r );
        
        //Y direction
        if ( !pressure_jump ){
          rho_l = C.density[jmo]; 
          rho_r = C.density[jpo]; 
          p_l = Get_Pressure_From_Energy( jmo );
          p_r = Get_Pressure_From_Energy( jpo );
          pressure_jump = Get_Pressure_Jump( gama, rho_l, rho_r, p_l, p_r );
        }
        
        //Z direction
        if ( !pressure_jump ){
          rho_l = C.density[kmo]; 
          rho_r = C.density[kpo]; 
          p_l = Get_Pressure_From_Energy( kmo );
          p_r = Get_Pressure_From_Energy( kpo );
          pressure_jump = Get_Pressure_Jump( gama, rho_l, rho_r, p_l, p_r );
        }
        
        //If the pressure Jump is large enough, use the Total Internal Energy
        if ( pressure_jump ){
          U = ge_total;
          flag_DE = 0;
        }
        
        // The second derivative of the density profile has the same sign on adjacent cells
        density_curvature_same = true;
        
        //X direcction
        d2_rho_l = Get_Second_Derivative( i-1, j, k, 0, nx_grid, ny_grid, nz_grid, H.dx, H.dy, H.dz, C.density );
        d2_rho_r = Get_Second_Derivative( i+1, j, k, 0, nx_grid, ny_grid, nz_grid, H.dx, H.dy, H.dz, C.density );
        if ( d2_rho_l * d2_rho_r < 0 ) density_curvature_same = false;
          
        //Y direcction
        if (!density_curvature_same){
          d2_rho_l = Get_Second_Derivative( i, j-1, k, 1, nx_grid, ny_grid, nz_grid, H.dx, H.dy, H.dz, C.density );
          d2_rho_r = Get_Second_Derivative( i, j+1, k, 1, nx_grid, ny_grid, nz_grid, H.dx, H.dy, H.dz, C.density );
          if ( d2_rho_l * d2_rho_r < 0 ) density_curvature_same = false;
        }
        
        //Z direcction
        if (!density_curvature_same){
          d2_rho_l = Get_Second_Derivative( i, j, k-1, 2, nx_grid, ny_grid, nz_grid, H.dx, H.dy, H.dz, C.density );
          d2_rho_r = Get_Second_Derivative( i, j, k+1, 2, nx_grid, ny_grid, nz_grid, H.dx, H.dy, H.dz, C.density );
          if ( d2_rho_l * d2_rho_r < 0 ) density_curvature_same = false;
        }
        
        //NOTE: If Density Curvature Same and Pressure Change Have to BOTH be satisfied for Shock detection, then they have to be
        // evaluated jointly for each direcction and AND condition satisfied.  
        
        //Set the Internal Energy
        C.Energy[id] = Ek + U;
        C.GasEnergy[id] = U;
        
        //Set the flag for which internal energy was used
        #ifdef COOLING_GRACKLE
        Cool.flags_DE[id] = flag_DE;
        #endif
                 
      }
    }
  }
}

#ifdef TEMPERATURE_FLOOR
void Grid3D::Apply_Temperature_Floor_CPU_function( int g_start, int g_end ){

  Real temp_floor = H.temperature_floor;
  
  #ifdef COOLING_GRACKLE
  if ( Cosmo.current_a > Cool.scale_factor_UVB_on ) temp_floor = 1;
  #endif 
  
  Real U_floor = temp_floor / (gama - 1) / MP * KB * 1e-10;
  
  

  #ifdef COSMOLOGY
  U_floor /=  Cosmo.v_0_gas * Cosmo.v_0_gas / Cosmo.current_a / Cosmo.current_a;
  #endif

  int nx_grid, ny_grid, nz_grid, nGHST_grid;
  nGHST_grid = H.n_ghost;
  nx_grid = H.nx;
  ny_grid = H.ny;
  nz_grid = H.nz;

  int nx, ny, nz;
  nx = H.nx_real;
  ny = H.ny_real;
  nz = H.nz_real;
  
  Real U_floor_local, mu;

  int nGHST = nGHST_grid ;
  Real d, vx, vy, vz, Ekin, E, U, GE;
  int k, j, i, id;
  for ( k=g_start; k<g_end; k++ ){
    for ( j=0; j<ny; j++ ){
      for ( i=0; i<nx; i++ ){
        id  = (i+nGHST) + (j+nGHST)*nx_grid + (k+nGHST)*ny_grid*nx_grid;

        d = C.density[id];
        vx = C.momentum_x[id] / d;
        vy = C.momentum_y[id] / d;
        vz = C.momentum_z[id] / d;
        Ekin = 0.5 * d * (vx*vx + vy*vy + vz*vz);
        E = C.Energy[id];
        
        #ifdef COOLING_GRACKLE
        mu = Cool.Get_Mean_Molecular_Weight( id );
        U_floor_local = U_floor / mu ;
        #else
        U_floor_local = U_floor;
        #endif
        
        U = ( E - Ekin ) / d;
        if ( U < U_floor_local ) C.Energy[id] = Ekin + d*U_floor_local;
        
        #ifdef DE
        GE = C.GasEnergy[id];
        U = GE / d;
        if ( U < U_floor_local ) C.GasEnergy[id] = d*U_floor_local;
        #endif
      }
    }
  }
}
#endif //TEMPERATURE_FLOOR


Real Grid3D::Get_Average_Kinetic_Energy_function( int g_start, int g_end ){

  int nx_grid, ny_grid, nz_grid, nGHST;
  nGHST = H.n_ghost;
  nx_grid = H.nx;
  ny_grid = H.ny;
  nz_grid = H.nz;

  int nx, ny, nz;
  nx = H.nx_real;
  ny = H.ny_real;
  nz = H.nz_real;

  Real Ek_sum = 0;
  Real d, d_inv, vx, vy, vz, E, Ek;

  int k, j, i, id;
  for ( k=g_start; k<g_end; k++ ){
    for ( j=0; j<ny; j++ ){
      for ( i=0; i<nx; i++ ){

        id  = (i+nGHST) + (j+nGHST)*nx_grid + (k+nGHST)*ny_grid*nx_grid;

        d = C.density[id];
        d_inv = 1/d;
        vx = C.momentum_x[id] * d_inv;
        vy = C.momentum_y[id] * d_inv;
        vz = C.momentum_z[id] * d_inv;
        E = C.Energy[id];
        Ek = 0.5*d*(vx*vx + vy*vy + vz*vz);
        Ek_sum += Ek;
      }
    }
  }
  return Ek_sum;
}

void Grid3D::Get_Average_Kinetic_Energy(){
  Real Ek_sum;

  #ifndef PARALLEL_OMP
  Ek_sum = Get_Average_Kinetic_Energy_function(  0, H.nz_real );
  #else
  Ek_sum = 0;
  Real Ek_sum_all[N_OMP_THREADS];
  #pragma omp parallel num_threads( N_OMP_THREADS )
  {
    int omp_id, n_omp_procs;
    int g_start, g_end;

    omp_id = omp_get_thread_num();
    n_omp_procs = omp_get_num_threads();
    Get_OMP_Grid_Indxs( H.nz_real, n_omp_procs, omp_id,  &g_start, &g_end  );
    Ek_sum_all[omp_id] = Get_Average_Kinetic_Energy_function(  g_start, g_end );

  }
  for ( int i=0; i<N_OMP_THREADS; i++ ){
    Ek_sum += Ek_sum_all[i];
  }
  #endif
  
  #ifdef MPI_CHOLLA
  Ek_sum /=  ( H.nx_real * H.ny_real * H.nz_real);
  H.Ekin_avrg = ReduceRealAvg(Ek_sum);
  #else
  H.Ekin_avrg = Ek_sum / ( H.nx_real * H.ny_real * H.nz_real);
  #endif
  
  
  
  
}








#endif