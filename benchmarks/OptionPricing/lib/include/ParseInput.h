#ifndef PARSE_INPUT
#define PARSE_INPUT

#include "ParserC.h"
#include "Util.h"
#include "Constants.h"
#include <assert.h>
#include <math.h>
#include <algorithm>


/*******************************/
/***     Macros Helpers      ***/
/*******************************/  
#define EPS             0.0005

#if REAL_IS_FLOAT
    #define read_real read_float
#else
    #define read_real read_double
#endif

/************************************/
/***      Read-Only Arrays        ***/           
/************************************/
typedef struct {
    int*    sobol_dirvcts;  // [sobol_bit_count][num_under*num_dates]
    int*    sobol_dirvctsT; // [num_under*num_dates][sobol_bit_count]
    UCHAR*  sobol_fix_ind;  // [chunk-1]

    void cleanup() {
        free(sobol_dirvctsT);
        free(sobol_dirvcts);  // allocated together with sobol_fix_ind
//      free(sobol_fix_ind);
    }
} SobolArrays __attribute__ ((aligned (16)));

typedef struct {
    int * bb_inds;    // [3, num_dates], i.e., bi, li, ri
    real_t* bb_data;    // [3, num_dates], i.e., sd, lw, rw

    void cleanup() {
        free(bb_inds);  free(bb_data);
    }
} BrowBridgeArrays  __attribute__ ((aligned (16)));


typedef struct {
    real_t* md_c;       // [num_models, num_under, num_under]
    real_t* md_vols;    // [num_models, num_dates, num_under]
    real_t* md_drifts;  // [num_models, num_dates, num_under]
    real_t* md_starts;  // [num_models, num_under]
    real_t* md_discts;  // [num_models, num_cash_flows]
    real_t* md_detvals; // [num_models, num_det_pricers]

    void cleanup() {
        free(md_c); // all allocated together, hence free the first one         
//        free(md_vols); free(md_drifts); free(md_starts); free(md_discts); free(md_detvals);
    }
} ModelArrays  __attribute__ ((aligned (16)));


/************************************/
/*** Various Utility Functions    ***/
/************************************/
UINT do_padding(const UINT& n) {
    return (((n / 64) * 64) + 64);
}

void initROscals(LoopROScalars& scals) { 
    scals.chunk = 64; // must be a power of two!
    scals.sob_norm_fact = 1.0 / (1<<scals.sobol_bits); 
    scals.sobol_count_ini = 0;
}

void computeSobolFixIndex( SobolArrays& sob_arrs, const UINT& chunk ) {
    // this should have been allocated in ParseInput!
    assert( (sob_arrs.sobol_fix_ind != NULL) && "Array sobol_fix_ind should be already allocated in ParseInput!" );
    
    // Given `chunk', the most-significant zero of iterations 
    // {1 .. chunk-1} mod chunk is the same.
    for( int k = 1; k < chunk-1; k ++ ) {
        UINT gs  = k;
        UINT ell = 0;
        while(gs & 1) {
            ell++;
            gs >>= 1;
        }
        sob_arrs.sobol_fix_ind[k] = ell;
	}
}
/************************************/
/*** Parsing The Current DataSet  ***/           
/************************************/
void readDataSet(   LoopROScalars& scals, SobolArrays&      sob_arrs,
                    ModelArrays& md_arrs, BrowBridgeArrays& bb_arrs
) {
    if( read_int( &scals.contract   ) ||
        read_int( &scals.num_mcits  ) ||
        read_int( &scals.num_dates  ) ||
        read_int( &scals.num_under  ) ||
        read_int( &scals.num_models ) ||
        read_int( &scals.sobol_bits )   ) {
        
        fprintf(stderr, "Syntax error when reading the first four ints params.\n");
        exit(1);
    }
    initROscals(scals);

    int64_t shape[3];

    { // reading sobol's arrays

        // 1. sobol's direction vectors
        int* sob_mat;
        if (read_array(sizeof(int), read_int, (void**)&sob_mat, shape, 2) ) {
            fprintf(stderr, "Syntax error when reading sobol direction-vector matrix of dim [%d,%d].\n",
                            scals.num_under*scals.num_dates, scals.sobol_bits);
            exit(1);
        }

        bool ok = ( shape[1] == scals.sobol_bits ) && 
                  ( shape[0] == scals.num_under*scals.num_dates );
        assert(ok && "Incorrect shape of sobol direction vectors!");
        sob_arrs.sobol_dirvctsT = sob_mat;

        // 2. Transpose the sobol direction vectors!
        int sob_dim    = scals.num_under * scals.num_dates;
        int alloc_size = do_padding ( (sob_dim * scals.sobol_bits) + (1 << logMAX_CHUNK) );
        sob_arrs.sobol_dirvcts = static_cast<int*> ( malloc( do_padding( alloc_size ) * sizeof(int) ) );
        for( int j = 0; j < scals.sobol_bits; j++ ) {
            for( int i = 0; i< sob_dim; i++ ) {
                sob_arrs.sobol_dirvcts[j*sob_dim + i] = sob_mat[i*scals.sobol_bits + j];
            }
        }

        sob_arrs.sobol_fix_ind = (UCHAR*) (sob_arrs.sobol_dirvcts + sob_dim * scals.sobol_bits);
        for ( int j = 0; j < (1<<logMAX_CHUNK); j ++ ) sob_arrs.sobol_fix_ind[j] = 0;
    }

    { // reading the market (models) data
        bool ok;

        // md_c
        if (read_array(sizeof(real_t), read_real, (void**)&md_arrs.md_c, shape, 3) ) {
            fprintf(stderr, "Syntax error when reading md_c [%d,%d,%d].\n",
                            scals.num_models, scals.num_under, scals.num_under);
            exit(1);
        }
        ok = ( shape[0] == scals.num_models) && 
             ( shape[1] == scals.num_under ) && 
             ( shape[2] == scals.num_under )  ;
        assert(ok && "Incorrect shape of md_c!");

        // md_vols (volatility)
        if (read_array(sizeof(real_t), read_real, (void**)&md_arrs.md_vols, shape, 3) ) {
            fprintf(stderr, "Syntax error when reading md_vols [%d,%d,%d].\n",
                            scals.num_models, scals.num_dates, scals.num_under);
            exit(1);
        }
        ok = ( shape[0] == scals.num_models) && 
             ( shape[1] == scals.num_dates ) && 
             ( shape[2] == scals.num_under )  ;
        assert(ok && "Incorrect shape of md_vols!");

        // md_drifts
        if (read_array(sizeof(real_t), read_real, (void**)&md_arrs.md_drifts, shape, 3) ) {
            fprintf(stderr, "Syntax error when reading md_drifts [%d,%d,%d].\n",
                            scals.num_models, scals.num_dates, scals.num_under);
            exit(1);
        }
        ok = ( shape[0] == scals.num_models) && 
             ( shape[1] == scals.num_dates ) && 
             ( shape[2] == scals.num_under )  ;
        assert(ok && "Incorrect shape of md_drifts!");

        // md_starts
        if (read_array(sizeof(real_t), read_real, (void**)&md_arrs.md_starts, shape, 2) ) {
            fprintf(stderr, "Syntax error when reading md_drifts [%d,%d].\n",
                            scals.num_models, scals.num_under);
            exit(1);
        }
        ok = ( shape[0] == scals.num_models) && 
             ( shape[1] == scals.num_under )  ;
        assert(ok && "Incorrect shape of md_starts!");

        // md_detvals (Model deterministic values)
        if (read_array(sizeof(real_t), read_real, (void**)&md_arrs.md_detvals, shape, 2) ) {
            fprintf(stderr, "Syntax error when reading md_detvals [%d,..].\n",
                            scals.num_models);
            exit(1);
        }
        ok = ( shape[0] == scals.num_models );
        assert(ok && "Incorrect shape of md_detvals!");
        scals.num_det_pricers = shape[1];

        // md_discts (Model discounts)
        if (read_array(sizeof(real_t), read_real, (void**)&md_arrs.md_discts, shape, 2) ) {
            fprintf(stderr, "Syntax error when reading md_discts [%d,..].\n",
                            scals.num_models);
            exit(1);
        }
        ok = ( shape[0] == scals.num_models );
        assert(ok && "Incorrect shape of md_discts!");
        scals.num_cash_flows  = shape[1];

        { // put all of them in a contiguous memory:
            int   alloc_size, offset;
            real_t* flat_arr;
            alloc_size  = scals.num_under * ( scals.num_under + 2*scals.num_dates + 1);
            alloc_size += scals.num_cash_flows + scals.num_det_pricers;
            alloc_size *= scals.num_models;
            alloc_size  = do_padding( alloc_size );

            flat_arr = static_cast<real_t*> ( malloc( alloc_size * sizeof(real_t) ) );

            // copy md_c
            offset     = 0; 
            alloc_size = scals.num_models * scals.num_under * scals.num_under;
            for( int k = 0; k < alloc_size; k ++ ) {
                flat_arr[k+offset] = md_arrs.md_c[k];
            }
            free(md_arrs.md_c);     md_arrs.md_c = flat_arr + offset;
            // copy md_vols
            offset    += alloc_size; 
            alloc_size = scals.num_models * scals.num_under * scals.num_dates;
            for( int k = 0; k < alloc_size; k ++ ) {
                flat_arr[k+offset] = md_arrs.md_vols[k];
            }
            free(md_arrs.md_vols);  md_arrs.md_vols = flat_arr + offset;
            // copy md_drifts
            offset    += alloc_size; 
            for( int k = 0; k < alloc_size; k ++ ) {
                flat_arr[k+offset] = md_arrs.md_drifts[k];
            }
            free(md_arrs.md_drifts); md_arrs.md_drifts = flat_arr + offset;
            // copy md_starts
            offset    += alloc_size; 
            alloc_size = scals.num_models * scals.num_under;
            for( int k = 0; k < alloc_size; k ++ ) {
                flat_arr[k+offset] = md_arrs.md_starts[k];
            }
            free(md_arrs.md_starts); md_arrs.md_starts = flat_arr + offset;
            // copy md_discts
            offset    += alloc_size; 
            alloc_size = scals.num_models * scals.num_cash_flows;
            for( int k = 0; k < alloc_size; k ++ ) {
                flat_arr[k+offset] = md_arrs.md_discts[k];
            }
            free(md_arrs.md_discts); md_arrs.md_discts = flat_arr + offset;
            // copy md_detvals
            offset    += alloc_size; 
            alloc_size = scals.num_models * scals.num_det_pricers;
            for( int k = 0; k < alloc_size; k ++ ) {
                flat_arr[k+offset] = md_arrs.md_detvals[k];
            }
            free(md_arrs.md_detvals); md_arrs.md_detvals = flat_arr + offset;        
        }
    }

    { // Brownian Bridge Indirect Arrays and Data
        if (read_array(sizeof(int), read_int, (void**)&bb_arrs.bb_inds, shape, 2) ) {
            fprintf(stderr, "Syntax error when reading Brownian Bridge indirect arrays [3,%d].\n",
                            scals.num_dates);
            exit(1);
        }
        bool ok = ( shape[0] == 3 ) && ( shape[1] == scals.num_dates );
        assert(ok && "Incorrect shape of bb_inds (brownian bridge indirect arrays)!");

                // md_starts
        if (read_array(sizeof(real_t), read_real, (void**)&bb_arrs.bb_data, shape, 2) ) {
            fprintf(stderr, "Syntax error when reading md_drifts [3,%d].\n",
                            scals.num_dates );
            exit(1);
        }
        ok = ( shape[0] == 3 ) && ( shape[1] == scals.num_dates )  ;
        assert(ok && "Incorrect shape of bb_data (brownian bridge data arrays)!");
    }
}

#endif // PARSE_INPUT
