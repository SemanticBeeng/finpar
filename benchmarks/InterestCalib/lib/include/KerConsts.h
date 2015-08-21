#ifndef KERNEL_CONSTANTS
#define KERNEL_CONSTANTS

/**
 * If set then Sobol's pseudo-rand num gen is used,
 * Otherwise (if GPU_VERSION!=2) `rand' from C is used.
 */
#define WITH_SOBOL 1 

/** 
 * 0 -> Multi-Core Execution
 * 1 -> Only the pricing is done on GPU,
 *        mutation, crossover done on CPU
 * 2 -> Everything on GPU using Sobol's 
 *        independent formula for pseudo-
 *        random numbers.
 */
#define GPU_VERSION 2

/*********************************************/
/*********************************************/
/*********************************************/

#if REAL_IS_FLOAT
    #define EPS0   (0.3e-2F)
    #define EPS    (0.2e-4F)
    #define PI     (3.14159265358F)

#else
    #define EPS0   (1.0e-3)
    #define EPS    (1.0e-5)
    #define PI     (3.1415926535897932384626433832795)

#endif

#define WARP   (1<<lgWARP)

#define LWG_EG 256

// ugaussian_Pinv(KKK)=1.0e~4
#define KKK -3.71901648545568   

#define R     0.03

#endif // KERNEL_CONSTANTS
