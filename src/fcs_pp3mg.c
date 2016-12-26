/*
  Copyright (C) 2011-2012 Rene Halver
  Copyright (C) 2016 Michael Hofmann

  This file is part of ScaFaCoS.

  ScaFaCoS is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  ScaFaCoS is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser Public License for more details.

  You should have received a copy of the GNU Lesser Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>. 
*/



#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <mpi.h>

#include "common/gridsort/gridsort.h"

#include "fcs_pp3mg.h"


FCSResult fcs_pp3mg_check(FCS handle)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

	return FCS_RESULT_SUCCESS;
}


FCSResult fcs_pp3mg_init(FCS handle)
{
  FCSResult result;
  fcs_pp3mg_context_t *ctx;

  FCS_DEBUG_FUNC_INTRO(__func__);

  handle->shift_positions = 1;

  handle->destroy = fcs_pp3mg_destroy;
  handle->set_parameter = fcs_pp3mg_set_parameter;
  handle->print_parameters = fcs_pp3mg_print_parameters;
  handle->tune = fcs_pp3mg_tune;
  handle->run = fcs_pp3mg_run;

  handle->pp3mg_param = malloc(sizeof(*handle->pp3mg_param));
  handle->pp3mg_param->m = -1;
  handle->pp3mg_param->n = -1;
  handle->pp3mg_param->o = -1;
  handle->pp3mg_param->ghosts = -1;
  handle->pp3mg_param->max_particles = -1;
  handle->pp3mg_param->degree = -1;
  handle->pp3mg_param->maxiter = -1;
  handle->pp3mg_param->tol = -1.0;
  handle->pp3mg_param->distribution = 0;
  handle->pp3mg_param->discretization = 0;

  ctx = malloc(sizeof(fcs_pp3mg_context_t));
  ctx->data = malloc(sizeof(pp3mg_data));
  ctx->parameters = malloc(sizeof(pp3mg_parameters));

  fcs_set_method_context(handle, ctx);
  
  /* 
   * Default parameters:
   * pp3mg_cells_x = pp3mg_cells_y = pp3mg_cells_z = 128
   * h = 1/128 => h^4/(12*5*6)*max(f^(4)) = h^4/360*3840
   */

  result = fcs_pp3mg_setup(handle, 128, 128, 128, 6, 3, 10000, 50, 3.9736e-8, 1, 1);
  CHECK_RESULT_RETURN(result);

  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}


FCSResult fcs_pp3mg_destroy(FCS handle)
{
  fcs_pp3mg_context_t *ctx;

  FCS_DEBUG_FUNC_INTRO(__func__);

  ctx = fcs_get_method_context(handle);

  pp3mg_free(ctx->data,ctx->parameters);

  free(ctx->data);
  free(ctx->parameters);
  free(ctx);

  fcs_set_method_context(handle, NULL);

  free(handle->pp3mg_param);

  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}


FCSResult fcs_pp3mg_tune(FCS handle, fcs_int local_particles, fcs_float *positions, fcs_float *charges)
{
  MPI_Comm comm;
  fcs_pp3mg_context_t *ctx;
  const fcs_float *box_a, *box_b, *box_c;
  fcs_float x, y, z;
  FCSResult result;

  FCS_DEBUG_FUNC_INTRO(__func__);

  comm = fcs_get_communicator(handle);

  box_a = fcs_get_box_a(handle);
  box_b = fcs_get_box_b(handle);
  box_c = fcs_get_box_c(handle);
#define MAX( a, b ) ((a>b)?a:b)
  x = MAX(box_a[0],MAX(box_b[0],box_c[0]));
  y = MAX(box_a[1],MAX(box_b[1],box_c[1]));
  z = MAX(box_a[2],MAX(box_b[2],box_c[2]));

  fcs_int max_local_particles = fcs_get_max_local_particles(handle);
  if (local_particles > max_local_particles) max_local_particles = local_particles;

  result = fcs_pp3mg_set_max_particles(handle, max_local_particles);
  CHECK_RESULT_RETURN(result);

  ctx = fcs_get_method_context(handle);

  pp3mg_init(x,y,z,
	     handle->pp3mg_param->m,
	     handle->pp3mg_param->n,
	     handle->pp3mg_param->o,
	     handle->pp3mg_param->ghosts,
	     handle->pp3mg_param->degree,
	     handle->pp3mg_param->max_particles,
	     handle->pp3mg_param->maxiter,
	     handle->pp3mg_param->tol,
	     handle->pp3mg_param->distribution,
	     handle->pp3mg_param->discretization,
	     comm,
	     ctx->data,
	     ctx->parameters);

  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}


FCSResult fcs_pp3mg_run(FCS handle, fcs_int local_particles, fcs_float *positions, fcs_float *charges, fcs_float *field, fcs_float *potentials)
{
  fcs_pp3mg_context_t *ctx;

  FCS_DEBUG_FUNC_INTRO(__func__);

  ctx = fcs_get_method_context(handle);
  
  ctx->last_runtime = MPI_Wtime();
  
  {
    fcs_int i;
    fcs_float *x, *y, *z, *fx, *fy, *fz;
    
    fcs_float box_a[3] = { ctx->parameters->x, 0, 0 };
    fcs_float box_b[3] = { 0, ctx->parameters->y, 0 };
    fcs_float box_c[3] = { 0, 0, ctx->parameters->z };
    fcs_float box_base[3] = { 0, 0, 0 };
    fcs_float lower_bound[3] = { ctx->parameters->x_start, ctx->parameters->y_start, ctx->parameters->z_start };
    fcs_float upper_bound[3] = { ctx->parameters->x_end, ctx->parameters->y_end, ctx->parameters->z_end };

    fcs_int max_local_particles = fcs_get_max_local_particles(handle);
    if (local_particles > max_local_particles) max_local_particles = local_particles;

    fcs_gridsort_t gridsort;
    
    fcs_int sorted_num_particles;
    fcs_float *sorted_charges, *sorted_positions;
    fcs_float *sorted_field, *sorted_potentials;
    fcs_gridsort_index_t *sorted_indices;
    
    fcs_gridsort_create(&gridsort);
    fcs_gridsort_set_system(&gridsort, box_base, box_a, box_b, box_c, NULL);
    fcs_gridsort_set_bounds(&gridsort, lower_bound, upper_bound);
    fcs_gridsort_set_particles(&gridsort, local_particles, max_local_particles, positions, charges);
    fcs_gridsort_sort_forward(&gridsort, 0.0, ctx->parameters->mpi_comm_cart);
    fcs_gridsort_get_real_particles(&gridsort, &sorted_num_particles, &sorted_positions, &sorted_charges, &sorted_indices);
    
    x = (fcs_float*) malloc(sorted_num_particles*sizeof(fcs_float));
    y = (fcs_float*) malloc(sorted_num_particles*sizeof(fcs_float));
    z = (fcs_float*) malloc(sorted_num_particles*sizeof(fcs_float));
    fx = (fcs_float*) malloc(sorted_num_particles*sizeof(fcs_float));
    fy = (fcs_float*) malloc(sorted_num_particles*sizeof(fcs_float));
    fz = (fcs_float*) malloc(sorted_num_particles*sizeof(fcs_float));
    sorted_field = (fcs_float*) malloc(3*sorted_num_particles*sizeof(fcs_float));
    sorted_potentials = (fcs_float*) malloc(sorted_num_particles*sizeof(fcs_float));
    
    for (i=0; i<sorted_num_particles; i++) {
      x[i] = sorted_positions[3*i+0];
      y[i] = sorted_positions[3*i+1];
      z[i] = sorted_positions[3*i+2];
    }
    
    pp3mg(x, y, z, sorted_charges, sorted_potentials, fx, fy, fz, sorted_num_particles, ctx->data, ctx->parameters);
    
    for (i=0; i<sorted_num_particles; i++) {
      sorted_field[3*i+0] = fx[i]/(1.0/(4.0*FCS_PI)*sorted_charges[i]);
      sorted_field[3*i+1] = fy[i]/(1.0/(4.0*FCS_PI)*sorted_charges[i]);
      sorted_field[3*i+2] = fz[i]/(1.0/(4.0*FCS_PI)*sorted_charges[i]);
      sorted_potentials[i] = sorted_potentials[i]/(1.0/(4.0*FCS_PI)*sorted_charges[i]);
    }

    fcs_gridsort_set_sorted_results(&gridsort, sorted_num_particles, sorted_field, sorted_potentials);
    fcs_gridsort_set_results(&gridsort, max_local_particles, field, potentials);
    fcs_gridsort_sort_backward(&gridsort, ctx->parameters->mpi_comm_cart);
    
    if (x) free(x);
    if (y) free(y);
    if (z) free(z);
    if (fx) free(fx);
    if (fy) free(fy);
    if (fz) free(fz);
    if (sorted_field) free(sorted_field);
    if (sorted_potentials) free(sorted_potentials);
    
    fcs_gridsort_free(&gridsort);
    fcs_gridsort_destroy(&gridsort);
    
  }
  ctx->last_runtime = MPI_Wtime() - ctx->last_runtime;

  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}


/* combined setter function for all pp3mg parameters */
FCSResult fcs_pp3mg_setup(FCS handle, fcs_int cells_x, fcs_int cells_y, fcs_int cells_z, fcs_int ghosts, fcs_int degree, fcs_int max_particles, fcs_int max_iterations, fcs_float tol, fcs_int distribution, fcs_int discretization)
{
  FCSResult result;

  FCS_DEBUG_FUNC_INTRO(__func__);

  result = fcs_pp3mg_set_cells_x(handle, cells_x);
  CHECK_RESULT_RETURN(result);

  result = fcs_pp3mg_set_cells_y(handle, cells_y);
  CHECK_RESULT_RETURN(result);

  result = fcs_pp3mg_set_cells_z(handle, cells_z);
  CHECK_RESULT_RETURN(result);

  result = fcs_pp3mg_set_ghosts(handle, ghosts);
  CHECK_RESULT_RETURN(result);

  result = fcs_pp3mg_set_degree(handle, degree);
  CHECK_RESULT_RETURN(result);

  result = fcs_pp3mg_set_max_particles(handle, max_particles);
  CHECK_RESULT_RETURN(result);

  result = fcs_pp3mg_set_max_iterations(handle, max_iterations);
  CHECK_RESULT_RETURN(result);

  result = fcs_pp3mg_set_tol(handle, tol);
  CHECK_RESULT_RETURN(result);

  result = fcs_pp3mg_set_distribution(handle, distribution);
  CHECK_RESULT_RETURN(result);

  result = fcs_pp3mg_set_discretization(handle, distribution);
  CHECK_RESULT_RETURN(result);

  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* setter for parameter cells_x */
FCSResult fcs_pp3mg_set_cells_x(FCS handle, fcs_int cells_x)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  handle->pp3mg_param->m = cells_x;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* getter for parameter cells_x */
FCSResult fcs_pp3mg_get_cells_x(FCS handle, fcs_int *cells_x)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  *cells_x = handle->pp3mg_param->m;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* setter for parameter cells_y */
FCSResult fcs_pp3mg_set_cells_y(FCS handle, fcs_int cells_y)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  handle->pp3mg_param->n = cells_y;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* getter for parameter cells_y */
FCSResult fcs_pp3mg_get_cells_y(FCS handle, fcs_int *cells_y)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  *cells_y = handle->pp3mg_param->n;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* setter for parameter cells_z */
FCSResult fcs_pp3mg_set_cells_z(FCS handle, fcs_int cells_z)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  handle->pp3mg_param->o = cells_z;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* getter for parameter cells_z */
FCSResult fcs_pp3mg_get_cells_z(FCS handle, fcs_int *cells_z)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  *cells_z = handle->pp3mg_param->o;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* setter for parameter ghosts */
FCSResult fcs_pp3mg_set_ghosts(FCS handle, fcs_int ghosts)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  handle->pp3mg_param->ghosts = ghosts;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* getter for parameter ghosts */
FCSResult fcs_pp3mg_get_ghosts(FCS handle, fcs_int *ghosts)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  *ghosts = handle->pp3mg_param->ghosts;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* setter for parameter degree */
FCSResult fcs_pp3mg_set_degree(FCS handle, fcs_int degree)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  handle->pp3mg_param->degree = degree;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* getter for parameter degree */
FCSResult fcs_pp3mg_get_degree(FCS handle, fcs_int *degree)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  *degree = handle->pp3mg_param->degree;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* setter for parameter max_particles */
FCSResult fcs_pp3mg_set_max_particles(FCS handle, fcs_int max_particles)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  handle->pp3mg_param->max_particles = max_particles;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* getter for parameter max_particles */
FCSResult fcs_pp3mg_get_max_particles(FCS handle, fcs_int *max_particles)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  *max_particles = handle->pp3mg_param->max_particles;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* setter for parameter maxiter */
FCSResult fcs_pp3mg_set_max_iterations(FCS handle, fcs_int max_iterations)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  handle->pp3mg_param->maxiter = max_iterations;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* getter for parameter maxiter */
FCSResult fcs_pp3mg_get_max_iterations(FCS handle, fcs_int *max_iterations)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  *max_iterations = handle->pp3mg_param->maxiter;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* setter for parameter tol */
FCSResult fcs_pp3mg_set_tol(FCS handle, fcs_float tol)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  handle->pp3mg_param->tol = tol;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* getter for parameter tol */
FCSResult fcs_pp3mg_get_tol(FCS handle, fcs_float *tol)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  *tol = handle->pp3mg_param->tol;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* setter for parameter distribution */
FCSResult fcs_pp3mg_set_distribution(FCS handle, fcs_int distribution)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  handle->pp3mg_param->distribution = distribution;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* getter for parameter distribution */
FCSResult fcs_pp3mg_get_distribution(FCS handle, fcs_int *distribution)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  *distribution = handle->pp3mg_param->distribution;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* setter for parameter discretization */
FCSResult fcs_pp3mg_set_discretization(FCS handle, fcs_int discretization)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  handle->pp3mg_param->discretization = discretization;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}

/* getter for parameter discretization */
FCSResult fcs_pp3mg_get_discretization(FCS handle, fcs_int *discretization)
{
  FCS_DEBUG_FUNC_INTRO(__func__);

  *discretization = handle->pp3mg_param->discretization;
  
  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}


FCSResult fcs_pp3mg_set_parameter(FCS handle, fcs_bool continue_on_errors, char **current, char **next, fcs_int *matched)
{
  char *param = *current;
  char *cur = *next;

  *matched = 0;

  FCS_PARSE_IF_PARAM_THEN_FUNC1_GOTO_NEXT("pp3mg_cells_x",        pp3mg_set_cells_x,        FCS_PARSE_VAL(fcs_int));
  FCS_PARSE_IF_PARAM_THEN_FUNC1_GOTO_NEXT("pp3mg_cells_y",        pp3mg_set_cells_y,        FCS_PARSE_VAL(fcs_int));
  FCS_PARSE_IF_PARAM_THEN_FUNC1_GOTO_NEXT("pp3mg_cells_z",        pp3mg_set_cells_z,        FCS_PARSE_VAL(fcs_int));
  FCS_PARSE_IF_PARAM_THEN_FUNC1_GOTO_NEXT("pp3mg_ghosts",         pp3mg_set_ghosts,         FCS_PARSE_VAL(fcs_int));
  FCS_PARSE_IF_PARAM_THEN_FUNC1_GOTO_NEXT("pp3mg_degree",         pp3mg_set_degree,         FCS_PARSE_VAL(fcs_int));
  FCS_PARSE_IF_PARAM_THEN_FUNC1_GOTO_NEXT("pp3mg_max_particles",  pp3mg_set_max_particles,  FCS_PARSE_VAL(fcs_int));
  FCS_PARSE_IF_PARAM_THEN_FUNC1_GOTO_NEXT("pp3mg_max_iterations", pp3mg_set_max_iterations, FCS_PARSE_VAL(fcs_int));
  FCS_PARSE_IF_PARAM_THEN_FUNC1_GOTO_NEXT("pp3mg_tol",            pp3mg_set_tol,            FCS_PARSE_VAL(fcs_float));
  FCS_PARSE_IF_PARAM_THEN_FUNC1_GOTO_NEXT("pp3mg_distribution",   pp3mg_set_distribution,   FCS_PARSE_VAL(fcs_int));
  FCS_PARSE_IF_PARAM_THEN_FUNC1_GOTO_NEXT("pp3mg_discretization", pp3mg_set_discretization, FCS_PARSE_VAL(fcs_int));

  return FCS_RESULT_SUCCESS;

next_param:
  *current = param;
  *next = cur;

  *matched = 1;

  return FCS_RESULT_SUCCESS;
}


FCSResult fcs_pp3mg_print_parameters(FCS handle)
{
  fcs_int cells_x, cells_y, cells_z;
  fcs_int ghosts;
  fcs_int degree;
  fcs_int max_particles;
  fcs_int max_iterations;
  fcs_float tol;
  fcs_int distribution;
  fcs_int discretization;

  FCS_DEBUG_FUNC_INTRO(__func__);

  fcs_pp3mg_get_cells_x(handle, &cells_x);
  fcs_pp3mg_get_cells_y(handle, &cells_y);
  fcs_pp3mg_get_cells_z(handle, &cells_z);
  fcs_pp3mg_get_ghosts(handle, &ghosts);
  fcs_pp3mg_get_degree(handle, &degree);
  fcs_pp3mg_get_max_particles(handle, &max_particles);
  fcs_pp3mg_get_max_iterations(handle, &max_iterations);
  fcs_pp3mg_get_tol(handle, &tol);
  fcs_pp3mg_get_distribution(handle, &distribution);
  fcs_pp3mg_get_discretization(handle, &discretization);
 
  printf("pp3mg cells x: %" FCS_LMOD_INT "d\n",cells_x);
  printf("pp3mg cells y: %" FCS_LMOD_INT "d\n",cells_y);
  printf("pp3mg cells z: %" FCS_LMOD_INT "d\n",cells_z);
  printf("pp3mg ghosts: %" FCS_LMOD_INT "d\n",ghosts);
  printf("pp3mg degree: %" FCS_LMOD_INT "d\n",degree);
  printf("pp3mg max_particles: %" FCS_LMOD_INT "d\n",max_particles);
  printf("pp3mg max_iterations: %" FCS_LMOD_INT "d\n",max_iterations);
  printf("pp3mg tol: %e\n",tol);
  printf("pp3mg distribution: %" FCS_LMOD_INT "d\n",distribution);
  printf("pp3mg discretization: %" FCS_LMOD_INT "d\n",discretization);

  FCS_DEBUG_FUNC_OUTRO(__func__, FCS_RESULT_SUCCESS);

  return FCS_RESULT_SUCCESS;
}
