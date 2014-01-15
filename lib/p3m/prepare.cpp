/*
  Copyright (C) 2011,2012,2013 Olaf Lenz
  
  This file is part of ScaFaCoS.
  
  ScaFaCoS is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  ScaFaCoS is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>. 
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "types.hpp"
#include "prepare.hpp"
#include <cstdlib>
#include <cstdio>
#include "utils.hpp"
#include "caf.hpp"
#include "influence_function.hpp"
#include "FCSCommon.h"

namespace ScaFaCoS {
  namespace P3M {
    /***************************************************/
    /* FORWARD DECLARATIONS OF INTERNAL FUNCTIONS */
    /***************************************************/
    static void 
    calc_send_grid(data_struct *d);

    static void 
    prepare_a_ai_cao_cut(data_struct *d);

    static void 
    calc_lm_ld_pos(data_struct *d);

    static void 
    calc_local_ca_grid(data_struct *d);

    static void 
    calc_differential_operator(data_struct *d);

#ifdef P3M_ENABLE_DEBUG
    static void 
    print_local_grid(local_grid_t l);

    static void 
    print_send_grid(send_grid_t sm);
#endif

    /***************************************************/
    /* IMPLEMENTATION */
    /***************************************************/
    /** Prepare the data structures and constants of the P3M algorithm.
        All parameters have to be set. */
    void prepare(data_struct *d, fcs_int max_charges) {
      P3M_DEBUG(printf("  prepare() started... \n"));

      /* initializes the (inverse) grid constant d->a
         (d->ai) and the cutoff for charge assignment
         d->cao_cut */ 
      prepare_a_ai_cao_cut(d);
      calc_local_ca_grid(d);
      calc_send_grid(d);
      P3M_DEBUG(print_local_grid(d->local_grid));
      P3M_DEBUG(print_send_grid(d->sm));
      d->send_grid = (fcs_float *) realloc(d->send_grid, sizeof(fcs_float)*d->sm.max);
      d->recv_grid = (fcs_float *) realloc(d->recv_grid, sizeof(fcs_float)*d->sm.max);

      P3M_DEBUG(printf("    Interpolating charge assignment function...\n"));
      d->caf = P3M::CAF::create(d->cao, d->n_interpol);
      d->cafx = d->caf->createCache();
      d->cafy = d->caf->createCache();
      d->cafz = d->caf->createCache();
#ifdef P3M_AD
      d->caf_d = P3M::CAF::create(d->cao, d->n_interpol, true);
      d->cafx_d = d->caf_d->createCache();
      d->cafy_d = d->caf_d->createCache();
      d->cafz_d = d->caf_d->createCache();
#endif  

      /* position offset for calc. of first gridpoint */
      d->pos_shift = (fcs_float)((d->cao-1)/2) - (d->cao%2)/2.0;
      P3M_DEBUG(printf("    pos_shift=" FFLOAT "\n",d->pos_shift)); 
  
      /* FFT */
      P3M_INFO(printf("    Preparing FFTs...\n"));
      fft_prepare(&d->fft, &d->comm, 
                       &d->rs_grid, &d->ks_grid,
                       d->local_grid.dim,d->local_grid.margin,
                       d->grid, d->grid_off,
                       &d->ks_pnum);
  
      /* k-space part */
      calc_differential_operator(d);
      P3M_INFO(printf("    Calculating influence function...\n"));
#if !defined(P3M_INTERLACE) && defined(P3M_IK)
      calc_influence_function_ik(d);
#elif defined(P3M_INTERLACE) && defined(P3M_IK)
      calc_influence_function_iki(d);
#else
      calc_influence_function_adi(d);
#endif

      P3M_DEBUG(printf("  prepare() finished.\n"));
    }

    /** Initializes the (inverse) grid constant \ref struct::a (\ref
        struct::ai) and the cutoff for charge assignment \ref
        struct::cao_cut, which has to be done by \ref init_charges
        once and by \ref scaleby_box_l whenever the \ref box_l
        changed.  */
    static void prepare_a_ai_cao_cut(data_struct *d) {
      P3M_DEBUG(printf("    prepare_a_ai_cao_cut() started... \n"));
      for (fcs_int i=0; i<3; i++) {
        d->ai[i]      = (fcs_float)d->grid[i]/d->box_l[i]; 
        d->a[i]       = 1.0/d->ai[i];
        d->cao_cut[i] = 0.5*d->a[i]*d->cao;
      }
      P3M_DEBUG(printf("    prepare_a_ai_cao_cut() finished. \n"));
    }

    /** Calculate the spacial position of the left down grid point of the
        local grid, to be stored in \ref local_grid::ld_pos; function
        called by \ref calc_local_ca_grid once and by \ref
        scaleby_box_l whenever the \ref box_l changed. */
    static void calc_lm_ld_pos(data_struct *d) {
      fcs_int i; 
      /* spacial position of left bottom grid point */
      for(i=0;i<3;i++) {
        d->local_grid.ld_pos[i] = 
          (d->local_grid.ld_ind[i]+ d->grid_off[i])*d->a[i];
      }
    }

    /** Calculates properties of the local FFT grid for the 
        charge assignment process. */
    static void calc_local_ca_grid(data_struct *d) {
      fcs_int i;
      fcs_int ind[3];
      /* total skin size */
      fcs_float full_skin[3];
  
      P3M_DEBUG(printf("    calc_local_ca_grid() started... \n"));
      for(i=0;i<3;i++)
        full_skin[i]= d->cao_cut[i]+d->skin+d->additional_grid[i];

      /* inner left down grid point (global index) */
      for(i=0;i<3;i++) 
        d->local_grid.in_ld[i] = 
          (fcs_int)ceil(d->comm.my_left[i]*d->ai[i]-d->grid_off[i]);
      /* inner up right grid point (global index) */
      for(i=0;i<3;i++) 
        d->local_grid.in_ur[i] = 
          (fcs_int)floor(d->comm.my_right[i]*d->ai[i]-d->grid_off[i]);
  
      /* correct roundof errors at boundary */
      for(i=0;i<3;i++) {
        if (fcs_float_is_zero((d->comm.my_right[i] * d->ai[i] - d->grid_off[i]) 
                              - d->local_grid.in_ur[i])) 
          d->local_grid.in_ur[i]--;
        if (fcs_float_is_zero(1.0+(d->comm.my_left[i] * d->ai[i] - d->grid_off[i]) 
                              - d->local_grid.in_ld[i])) 
          d->local_grid.in_ld[i]--;
      }
      /* inner grid dimensions */
      for(i=0; i<3; i++) 
        d->local_grid.inner[i] = d->local_grid.in_ur[i] - d->local_grid.in_ld[i] + 1;
      /* index of left down grid point in global grid */
      for(i=0; i<3; i++) 
        d->local_grid.ld_ind[i] = 
          (fcs_int)ceil((d->comm.my_left[i]-full_skin[i])*d->ai[i]-d->grid_off[i]);
      /* spatial position of left down grid point */
      calc_lm_ld_pos(d);
      /* left down margin */
      for(i=0;i<3;i++) 
        d->local_grid.margin[i*2] = d->local_grid.in_ld[i]-d->local_grid.ld_ind[i];
      /* up right grid point */
      for(i=0;i<3;i++) 
        ind[i] =
          (fcs_int)floor((d->comm.my_right[i]+full_skin[i])*d->ai[i]-d->grid_off[i]);
      /* correct roundof errors at up right boundary */
      for(i=0;i<3;i++)
        if (((d->comm.my_right[i]+full_skin[i])*d->ai[i]-d->grid_off[i])-ind[i]==0) 
          ind[i]--;
      /* up right margin */
      for(i=0;i<3;i++) d->local_grid.margin[(i*2)+1] = ind[i] - d->local_grid.in_ur[i];

      /* grid dimension */
      d->local_grid.size=1; 
      for(i=0;i<3;i++) {
        d->local_grid.dim[i] = ind[i] - d->local_grid.ld_ind[i] + 1; 
        d->local_grid.size *= d->local_grid.dim[i];
      }
      /* reduce inner grid indices from global to local */
      for(i=0;i<3;i++) 
        d->local_grid.in_ld[i] = d->local_grid.margin[i*2];
      for(i=0;i<3;i++) 
        d->local_grid.in_ur[i] = d->local_grid.margin[i*2]+d->local_grid.inner[i];

      d->local_grid.q_2_off  = d->local_grid.dim[2] - d->cao;
      d->local_grid.q_21_off = d->local_grid.dim[2] * (d->local_grid.dim[1] - d->cao);

      P3M_DEBUG(printf("    calc_local_ca_grid() finished. \n"));
    }

    /** Calculates the properties of the send/recv sub-grides of the local
     *  FFT grid.  In order to calculate the recv sub-grides there is a
     *  communication of the margins between neighbouring nodes. */ 
    static  void calc_send_grid(data_struct *d) {
      fcs_int i,j, evenodd;
      fcs_int done[3]={0,0,0};
      MPI_Status status;

      P3M_DEBUG(printf("    calc_send_grid() started... \n"));
      /* send grids */
      for (i=0; i<3; i++) {
        for (j=0; j<3; j++) {
          /* left */
          d->sm.s_ld[i*2][j] = 0 + done[j]*d->local_grid.margin[j*2];
          if (j==i) d->sm.s_ur[i*2][j] = d->local_grid.margin[j*2]; 
          else     d->sm.s_ur[i*2][j] = d->local_grid.dim[j]-done[j]*d->local_grid.margin[(j*2)+1];
          /* right */
          if (j==i) d->sm.s_ld[(i*2)+1][j] = d->local_grid.in_ur[j];
          else     d->sm.s_ld[(i*2)+1][j] = 0 + done[j]*d->local_grid.margin[j*2];
          d->sm.s_ur[(i*2)+1][j] = d->local_grid.dim[j] - done[j]*d->local_grid.margin[(j*2)+1];
        }   
        done[i]=1;
      }

      d->sm.max=0;
      for (i=0; i<6; i++) {
        d->sm.s_size[i] = 1;
        for (j=0; j<3; j++) {
          d->sm.s_dim[i][j] = d->sm.s_ur[i][j]-d->sm.s_ld[i][j];
          d->sm.s_size[i] *= d->sm.s_dim[i][j];
        }
        if (d->sm.s_size[i]>d->sm.max) d->sm.max=d->sm.s_size[i];
      }

      /* communication */
      for (i=0; i<6; i++) {
        if (i%2 == 0) j = i+1;
        else       j = i-1;
        if (d->comm.node_neighbors[i] != d->comm.rank) {
          /* two step communication: first all even positions than all odd */
          for (evenodd=0; evenodd<2; evenodd++) {
            if ((d->comm.node_pos[i/2]+evenodd)%2 == 0) {
              P3M_DEBUG(printf("      %d: sending local_grid.margin to %d\n", \
                               d->comm.rank, d->comm.node_neighbors[i]));
              MPI_Send(&(d->local_grid.margin[i]), 1, FCS_MPI_INT, 
                       d->comm.node_neighbors[i], 0, d->comm.mpicomm);
            } else {
              P3M_DEBUG(printf("      %d: receiving local_grid.margin from %d\n", \
                               d->comm.rank, d->comm.node_neighbors[j]));
              MPI_Recv(&(d->local_grid.r_margin[j]), 1, FCS_MPI_INT,
                       d->comm.node_neighbors[j], 0, d->comm.mpicomm, &status);    
            }
          }
        } else {
          d->local_grid.r_margin[j] = d->local_grid.margin[i];
        }
      }

      /* /\* communication *\/ */
      /* for (i = 0; i < 3; i++) { */
      /*   /\* upshift *\/ */
      /*   MPI_Sendrecv(&(d->local_grid.margin[2*i]), 1, FCS_MPI_INT, */
      /* 		 d->comm.node_neighbors[2*i+1], 0, */
      /* 		 &(d->local_grid.r_margin[2*i]), 1, FCS_MPI_INT, */
      /* 		 d->comm.node_neighbors[2*i], 0, */
      /* 		 d->comm.mpicomm, &status); */
      /*   /\* downshift *\/ */
      /*   MPI_Sendrecv(&(d->local_grid.margin[2*i+1]), 1, FCS_MPI_INT, */
      /* 		 d->comm.node_neighbors[2*i], 0, */
      /* 		 &(d->local_grid.r_margin[2*i+1]), 1, FCS_MPI_INT, */
      /* 		 d->comm.node_neighbors[2*i+1], 0, */
      /* 		 d->comm.mpicomm, &status); */
      /* } */

      /* recv grids */
      for (i=0; i<3; i++) 
        for (j=0; j<3; j++) {
          if (j==i) {
            d->sm.r_ld[ i*2   ][j] = d->sm.s_ld[ i*2   ][j] + d->local_grid.margin[2*j];
            d->sm.r_ur[ i*2   ][j] = d->sm.s_ur[ i*2   ][j] + d->local_grid.r_margin[2*j];
            d->sm.r_ld[(i*2)+1][j] = d->sm.s_ld[(i*2)+1][j] - d->local_grid.r_margin[(2*j)+1];
            d->sm.r_ur[(i*2)+1][j] = d->sm.s_ur[(i*2)+1][j] - d->local_grid.margin[(2*j)+1];
          } else {
            d->sm.r_ld[ i*2   ][j] = d->sm.s_ld[ i*2   ][j];
            d->sm.r_ur[ i*2   ][j] = d->sm.s_ur[ i*2   ][j];
            d->sm.r_ld[(i*2)+1][j] = d->sm.s_ld[(i*2)+1][j];
            d->sm.r_ur[(i*2)+1][j] = d->sm.s_ur[(i*2)+1][j];
          }
        }
      for (i=0; i<6; i++) {
        d->sm.r_size[i] = 1;
        for (j=0;j<3;j++) {
          d->sm.r_dim[i][j] = d->sm.r_ur[i][j]-d->sm.r_ld[i][j];
          d->sm.r_size[i] *= d->sm.r_dim[i][j];
        }
        if (d->sm.r_size[i]>d->sm.max) d->sm.max=d->sm.r_size[i];
      }
      P3M_DEBUG(printf("    calc_send_grid() finished. \n"));
    }

    /** Calculates the Fourier transformed differential operator.  
     *  Remark: This is done on the level of n-vectors and not k-vectors,
     *           i.e. the prefactor i*2*PI/L is missing! */
    void calc_differential_operator(data_struct *d) {
      for (fcs_int i=0;i<3;i++) {
        d->d_op[i] = static_cast<fcs_int *>(realloc(d->d_op[i], d->grid[i]*sizeof(fcs_int)));
        d->d_op[i][0] = 0;
        d->d_op[i][d->grid[i]/2] = 0;

        for (fcs_int j = 1; j < d->grid[i]/2; j++) {
          d->d_op[i][j] = j;
          d->d_op[i][d->grid[i] - j] = -j;
        }
      }
    }

#ifdef P3M_ENABLE_DEBUG
    /** Debug function printing p3m structures */
    void print_local_grid(local_grid_t l) {
      printf( "    local_grid:\n");
      printf( "      dim=" F3INT ", size=" FINT "\n",
              l.dim[0], l.dim[1], l.dim[2], l.size);
      printf("      ld_ind=" F3INT ", ld_pos=" F3FLOAT "\n",
             l.ld_ind[0],l.ld_ind[1],l.ld_ind[2],
             l.ld_pos[0],l.ld_pos[1],l.ld_pos[2]);
      printf("      inner=" F3INT "[" F3INT "-" F3INT "]\n",
             l.inner[0],l.inner[1],l.inner[2],
             l.in_ld[0],l.in_ld[1],l.in_ld[2],
             l.in_ur[0],l.in_ur[1],l.in_ur[2]);
      printf("      margin=(" FINT "," FINT " ," FINT "," FINT " ," FINT "," FINT ")\n",
             l.margin[0],l.margin[1],l.margin[2],l.margin[3],l.margin[4],l.margin[5]);
      printf("      r_margin=(" FINT "," FINT " ," FINT "," FINT " ," FINT "," FINT ")\n",
             l.r_margin[0],l.r_margin[1],
             l.r_margin[2],l.r_margin[3],
             l.r_margin[4],l.r_margin[5]);
    }

    /** Debug function printing p3m structures */
    void print_send_grid(send_grid_t sm) {
      int i;
      printf( "    send_grid:\n");
      printf( "      max=%d\n",sm.max);
      for (i=0;i<6;i++) {
        printf("      dir=%d: s_dim (%d,%d,%d)  s_ld (%d,%d,%d) s_ur (%d,%d,%d) s_size=%d\n",
               i, sm.s_dim[i][0], sm.s_dim[i][1], sm.s_dim[i][2], 
               sm.s_ld[i][0], sm.s_ld[i][1], sm.s_ld[i][2],
               sm.s_ur[i][0], sm.s_ur[i][1], sm.s_ur[i][2], sm.s_size[i]);
        printf("             r_dim (%d,%d,%d)  r_ld (%d,%d,%d) r_ur (%d,%d,%d) r_size=%d\n",
               sm.r_dim[i][0], sm.r_dim[i][1], sm.r_dim[i][2], 
               sm.r_ld[i][0], sm.r_ld[i][1], sm.r_ld[i][2], 
               sm.r_ur[i][0], sm.r_ur[i][1], sm.r_ur[i][2], sm.r_size[i]);
      }
    }
#endif
  }
}
