#ifndef GENERIC_UTILITIES
#define GENERIC_UTILITIES

/*******************************************************/
/*****  Utilities Related to Time Instrumentation  *****/
/*******************************************************/
//#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
//#include <sys/timeb.h>
#include <assert.h>
#include <omp.h>


#define TIME_RESOLUTION_MICROSECOND

#ifdef TIME_RESOLUTION_MICROSECOND
// CHR: added helper function for computing time differences at microsecond resolution
int timeval_subtract(struct timeval *result, struct timeval *t2, struct timeval *t1)
{
    unsigned int resolution=1000000;
    long int diff = (t2->tv_usec + resolution * t2->tv_sec) - (t1->tv_usec + resolution * t1->tv_sec);
    result->tv_sec = diff / resolution;
    result->tv_usec = diff % resolution;
    return (diff<0);
}

#endif

#if 0
typedef struct timeb mlfi_timeb;
#define mlfi_ftime ftime

#define mlfi_diff_time(t1,t2) \
  (t1.time - t2.time) * 1000 + (t1.millitm - t2.millitm)
#endif
/*******************************************************/
/*****   Utilities Related to Read/Write to File   *****/
/*******************************************************/

int get_CPU_num_threads() {
    int procs;

#pragma omp parallel shared(procs)
    {
        int th_id = omp_get_thread_num();
        if(th_id == 0) { procs = omp_get_num_threads(); }
    }

    bool valid_procs = (procs > 0) && (procs <= 1024);
    assert(valid_procs && "Number of threads NOT in {1, ..., 1024}");
    return procs;
}

int get_GPU_num_threads() {
    return 1024;
}

#endif //GENERIC_UTILITIES
