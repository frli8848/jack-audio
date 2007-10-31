/***
*
* Copyright (C) 2007 Fredrik Lingvall
*
* This file is part of the ...
*
* The .... is free software; you can redistribute it and/or modify 
* it under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2, or (at your option) any
* later version.
*
* The .... is distributed in the hope that it will be useful, but 
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
* for more details.
*
* You should have received a copy of the GNU General Public License
* along with the DREAM Toolbox; see the file COPYING.  If not, write to the 
* Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
* 02110-1301, USA.
*
***/

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>


//
// Octave headers.
//

#include <octave/oct.h>

#include <octave/config.h>

#include <iostream>
using namespace std;

#include <octave/defun-dld.h>
#include <octave/error.h>
#include <octave/oct-obj.h>
#include <octave/pager.h>
#include <octave/symtab.h>
#include <octave/variables.h>

#define TRUE 1
#define FALSE 0



/***
 * Name and date (of revisions):
 * 
 * Fredrik Lingvall 2007-10-31 : File created.
 *
 ***/



//
// typedef:s
//

typedef struct
{
  dream_idx_type line_start;
  dream_idx_type line_stop;
  double *A;
  dream_idx_type A_M;
  dream_idx_type A_N;
  double *B;
  dream_idx_type B_M;
  dream_idx_type B_N;
  double *Y;
} DATA;

typedef void (*sighandler_t)(int);

//
// Function prototypes.
//



/***
 *
 * Signal handlers.
 *
 ***/

void sighandler(int signum) {
  //printf("Caught signal SIGTERM.\n");
  running = FALSE;
}

void sig_abrt_handler(int signum) {
  //printf("Caught signal SIGABRT.\n");
}

void sig_keyint_handler(int signum) {
  //printf("Caught signal SIGINT.\n");
}

/***
 * 
 * conv_p.c -  Octave (oct) gateway function for CONV_P.
 *
 ***/

DEFUN_DLD (aplay, args, nlhs,
	   "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {}  [Y] = aplay(A).\n\
\n\
APLAY Computes one dimensional convolutions of the columns in the matrix A and the matrix (or vector) B.\n\
\n\
Input parameters:\n\
\n\
@copyright{2007-08-08 Fredrik Lingvall}.\n\
@seealso {play, record}\n\
@end deftypefn")
{
  double *A,*B, *Y; 
  sighandler_t   old_handler, old_handler_abrt, old_handler_keyint;
  pthread_t *threads;
  dream_idx_type line_start, line_stop, A_M, A_N, B_M, B_N, n;
  int    thread_n, N, err;    
  char   *the_str = NULL;
  int    buflen, is_set = FALSE;
  void   *retval;
  DATA   *D;
  octave_value_list oct_retval; 

  in_place = FALSE;

  int nrhs = args.length ();

  // Check for proper inputs arguments.

  switch (nrhs) {
    
  case 0:
  case 1:
  case 2:
    error("aplay requires 3 to 5 input arguments!");
    return oct_retval;
    break;
    
  case 3:
    if (nlhs > 1) {
      error("Too many output arguments for aplay!");
      return oct_retval;
    }    
    break;

  case 4:
    if (nlhs > 0) {
      error("No output arguments required for aplay in in-place operating mode!");
      return oct_retval;
    }    
    break;

  case 5:
    if ( args(4).is_string() ) { // 5:th arg is a fftw wisdom string.
      std::string strin = args(4).string_value(); 
      buflen = strin.length();
      the_str = (char*) malloc(buflen * sizeof(char));
      for ( n=0; n<buflen; n++ ) {
	the_str[n] = strin[n];
      }

      // Valid strings are:
      //  '='  : In-place replace mode.
      //  '+=' : In-place add mode.
      //  '-=' : In-place sub mode.
      
      is_set = FALSE;
      
      if (strcmp(the_str,"=") == 0) {
	mode = EQU; 
	is_set = TRUE;
      }
      
      if (strcmp(the_str,"+=") == 0) {
	mode = SUM; 
	is_set = TRUE;
      }
      
      if (strcmp(the_str,"-=") == 0) {
	mode = NEG; 
	is_set = TRUE;
      }
      
      if (is_set == FALSE) {
	error("Non-valid string in arg 5!");
	return oct_retval;
      }
    }
    free(the_str);
    break;

  default:
    error("aplay requires 3 to 5 input arguments!");
    return oct_retval;
    break;
  }



  const Matrix tmp = args(0).matrix_value();
  A_M = tmp.rows();
  A_N = tmp.cols();
  A = (double*) tmp.fortran_vec();

  const Matrix tmp2 = args(1).matrix_value();
  B_M = tmp2.rows();
  B_N = tmp2.cols();
  B = (double*) tmp2.fortran_vec();

  // Check that arg 2. 
  if ( B_M != 1 && B_N !=1 && B_N != A_N) {
    error("Argument 2 must be a vector or a matrix with the same number of rows as arg 1!");
    return oct_retval;
  }

  if (  B_M == 1 || B_N == 1 ) { // B is a vector.
    B_M = B_M*B_N;
    B_N = 1;
  }
    
  //
  // Number of threads.
  //


  // Check that arg 3 is a scalar.
  if (  args(2).matrix_value().rows()  * args(2).matrix_value().cols() !=1) {
    error("Argument 3 must be a scalar!");
    return oct_retval;
  }

  N = (int) args(2).matrix_value().fortran_vec()[0];
  if (N < 1) {
    error("Argument 3 must be larger or equal to 1 (min number of CPUs)!");
    return oct_retval;
  }

  
  //
  // Create/get output matrix.
  //
  
  Matrix Ymat(A_M+B_M-1, A_N);
  Y = Ymat.fortran_vec();
  
  //
  // Call the play subroutine.
  //
  
  
  playit(A);
  
  
  
  return oct_retval;
  
}
