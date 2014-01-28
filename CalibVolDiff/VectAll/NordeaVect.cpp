#include "Constants.h"
#include "DataStructConst.h"
#include "NordeaInit.h"
#include "NordeaVect_CPU.h"
#include "NordeaVect_GPU.h"


void whole_loop_nest (
        REAL*       res,
        const REAL* strikes,
        const REAL  s0,
        const REAL  t,
        const REAL  alpha,
        const REAL  nu,
        const REAL  beta
) {
    // loop index
    unsigned int i;
    //const unsigned int NUM_XY = NUM_X*NUM_Y;

    // arrays:
    REAL *a = NULL, *b = NULL, *c = NULL,
         *y = NULL, *u = NULL, *v = NULL,
         *scan_tmp = NULL;

    { // allocate arrays
        a = new REAL __attribute__ ((aligned (32))) [OUTER_LOOP_COUNT*NUM_XY];
        b = new REAL[OUTER_LOOP_COUNT*NUM_XY];
        c = new REAL[OUTER_LOOP_COUNT*NUM_XY];

        y = new REAL[OUTER_LOOP_COUNT*NUM_XY];
        u = new REAL[OUTER_LOOP_COUNT*NUM_XY];
        v = new REAL[OUTER_LOOP_COUNT*NUM_XY];

        scan_tmp = new REAL[4*OUTER_LOOP_COUNT*NUM_XY];
    }

    // parallel!
    whole_loop_nest_init ( s0, t, alpha, nu, beta );

    // parallel!
    for( i=0; i<OUTER_LOOP_COUNT; ++i ) {
        REAL* res_arr = myResArr+i*NUM_X*NUM_Y;
        setPayoff_expanded(strikes[i], res_arr);
    }

    if( IS_GPU > 0 ) {
        RWScalars         ro_scal;
        NordeaArrays      cpu_arrs;
        oclNordeaArrays   ocl_arrs;

        { // init arrays
            cpu_arrs.myX = myX; cpu_arrs.myDx = myDx; cpu_arrs.myDxx = myDxx;
            cpu_arrs.myY = myY; cpu_arrs.myDy = myDy; cpu_arrs.myDyy = myDyy;
            cpu_arrs.timeline = myTimeline;
            cpu_arrs.a = a; cpu_arrs.b = b; cpu_arrs.c = c;
            cpu_arrs.y = y; cpu_arrs.u = u; cpu_arrs.v = v;
            cpu_arrs.tmp = scan_tmp;
            cpu_arrs.res_arr  = &myResArr[0];
        }

        { // init scalars
            ro_scal.NUM_X = NUM_X; ro_scal.NUM_Y = NUM_Y; ro_scal.NUM_XY = NUM_X * NUM_Y;
            ro_scal.alpha = alpha; ro_scal.beta  = beta;  ro_scal.nu     = nu;
        }

        { // SAFETY CHECK!
            bool is_safe =  (NUM_X <= WORKGROUP_SIZE) && (NUM_Y <= WORKGROUP_SIZE) &&
                            is_pow2(NUM_X) && is_pow2(NUM_Y) &&
                            (WORKGROUP_SIZE % NUM_X == 0) && (WORKGROUP_SIZE % NUM_Y == 0);
            assert(is_safe && "NOT SAFE TO PARALLELISE ON GPU!");
        }

        runOnGPU ( ro_scal, cpu_arrs, ocl_arrs );
    } else {

        mlfi_timeb  t_start, t_end;
        unsigned long int elapsed;
        mlfi_ftime(&t_start);

        // parallel!
        for(int t_ind = NUM_T-2; t_ind>=0; --t_ind) {
    //        iteration_expanded_GPU (
    //                t_ind, alpha, beta, nu,
    //                a, b, c, y, u, v, scan_tmp
    //            );

            iteration_expanded_CPU (
                    t_ind, alpha, beta, nu,
                    a, b, c, y, u, v, scan_tmp
                );
        }

        mlfi_ftime(&t_end);
        elapsed = mlfi_diff_time(t_end,t_start);
        printf("\n\nCPU Run Time: %lu !\n\n", elapsed);

    }

    for( i=0; i<OUTER_LOOP_COUNT; ++i ) {
        REAL* res_arr = myResArr+i*NUM_X*NUM_Y;
        res[i] = res_arr[myYindex*NUM_X+myXindex];
        if(DEBUG) { printf("(res[%d]: %f)\n", i, res[i]); }
    }

    { // de-allocate the arrays
        delete[] a;
        delete[] b;
        delete[] c;

        delete[] y;
        delete[] u;
        delete[] v;

        //delete[] yy;
        //delete[] res;
        delete[] scan_tmp;
    }
}




int main() {
    const REAL s0 = 0.03, strike = 0.03, t = 5.0, alpha = 0.2, nu = 0.6, beta = 0.5;

    REAL strikes[OUTER_LOOP_COUNT];
    REAL res    [OUTER_LOOP_COUNT];

    for(unsigned i=0; i<OUTER_LOOP_COUNT; ++i) {
        strikes[i] = 0.001*i;
    }

    whole_loop_nest( res, strikes, s0, t, alpha, nu, beta );

    return 0;
}
