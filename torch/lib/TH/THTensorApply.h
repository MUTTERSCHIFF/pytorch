#ifndef TH_TENSOR_APPLY_INC
#define TH_TENSOR_APPLY_INC

/*
 * The basic strategy for apply is as follows:
 *
 * 1. Starting with the outermost index, loop until we reach a dimension where the
 * data is no longer contiguous, i.e. the stride at that dimension is not equal to
 * the size of the tensor defined by the outer dimensions. Let's call this outer
 * (contiguous) tensor A. Note that if the Tensor is contiguous, then A is equal
 * to the entire Tensor. Let's call the inner tensor B.
 *
 * 2. We loop through the indices in B, starting at its outermost dimension. For
 * example, if B is a 2x2 matrix, then we do:
 *
 * B[0][0]
 * B[0][1]
 * B[1][0]
 * B[1][1]
 *
 * We set the offset into the underlying storage as (storageOffset + stride_B * index_B),
 * i.e. basically we compute the offset into the storage as we would normally for a
 * Tensor. But because we are guaranteed the subsequent data is contiguous in memory, we
 * can simply loop for sizeof(A) iterations and perform the operation, without having to
 * follow the order described by the strides of A.
 *
 * 3. As an optimization, we merge dimensions of A that are contiguous in memory. For
 * example, if A is a 3x3x3x3 tensor narrowed from a 3x3x4x3 tensor, then the first two
 * dimensions can be merged for the purposes of APPLY, reducing the number of nested
 * loops.
 */

#define __TH_TENSOR_APPLYX_PREAMBLE(TYPE, TENSOR, DIM, ALLOW_CONTIGUOUS) \
  TYPE *TENSOR##_data = NULL; \
  int64_t *TENSOR##_counter = NULL, *TENSOR##_sizes = NULL, *TENSOR##_strides = NULL, *TENSOR##_dimOffset = NULL; \
  int64_t TENSOR##_stride = 0, TENSOR##_size = 0, TENSOR##_dim = 0, TENSOR##_i, TENSOR##_n; \
  int TENSOR##_contiguous = ALLOW_CONTIGUOUS && DIM < 0; \
  TENSOR##_n = (TENSOR->nDimension ? 1 : 0); \
  for(TENSOR##_i = 0; TENSOR##_i < TENSOR->nDimension; TENSOR##_i++) \
    TENSOR##_n *= TENSOR->size[TENSOR##_i]; \
\
  if(TENSOR->nDimension == 0) \
    TH_TENSOR_APPLY_hasFinished = 1; \
  else \
  { \
    TENSOR##_data = TENSOR->storage->data+TENSOR->storageOffset; \
    TENSOR##_size = 1; \
    TENSOR##_stride = 1; \
    for(TENSOR##_i = TENSOR->nDimension-1; TENSOR##_i >= 0; TENSOR##_i--) { \
      if(TENSOR->size[TENSOR##_i] != 1) { \
        if(TENSOR->stride[TENSOR##_i] == TENSOR##_size && TENSOR##_i != DIM) \
          TENSOR##_size *= TENSOR->size[TENSOR##_i]; \
        else{ \
          TENSOR##_contiguous = 0; \
          break; \
        } \
      } \
    } \
    if (!TENSOR##_contiguous) { \
      /* Find the dimension of contiguous sections */ \
      TENSOR##_dim = 1; \
      for(TENSOR##_i = TENSOR->nDimension-2; TENSOR##_i >= 0; TENSOR##_i--) \
      { \
        if(TENSOR->stride[TENSOR##_i] != TENSOR->stride[TENSOR##_i+1] * TENSOR->size[TENSOR##_i+1] || TENSOR##_i == DIM || TENSOR##_i+1 == DIM) \
          TENSOR##_dim++; \
      } \
      /* Allocate an array of 3*dim elements, where dim is the number of contiguous sections */ \
      TENSOR##_counter = (int64_t*)THAlloc(sizeof(int64_t)*(3*TENSOR##_dim)); \
      TENSOR##_sizes = TENSOR##_counter + TENSOR##_dim; \
      TENSOR##_strides = TENSOR##_counter + 2*TENSOR##_dim; \
      TH_TENSOR_dim_index = TENSOR##_dim-1; \
      TENSOR##_dimOffset = (DIM == TENSOR->nDimension-1) ? &TENSOR##_i : &TENSOR##_counter[DIM]; \
      TENSOR##_sizes[TH_TENSOR_dim_index] = TENSOR->size[TENSOR->nDimension-1]; \
      TENSOR##_strides[TH_TENSOR_dim_index] = TENSOR->stride[TENSOR->nDimension-1]; \
      /* TENSOR##_counter tracks where we are in the storage. The offset into the */ \
      /* storage is given by storage_offset + (i * j), where i is the stride */ \
      /* vector and j is tensor_counter vector. This sets the starting position for the loop. */ \
      for(TENSOR##_i = TENSOR##_dim-1; TENSOR##_i >= 0; --TENSOR##_i) { \
        TENSOR##_counter[TENSOR##_i] = 0; \
      } \
      for(TENSOR##_i = TENSOR->nDimension-2; TENSOR##_i >= 0; --TENSOR##_i) { \
        if (TENSOR->stride[TENSOR##_i] == TENSOR->stride[TENSOR##_i+1] * TENSOR->size[TENSOR##_i+1] && TENSOR##_i != DIM && TENSOR##_i+1 != DIM) { \
          TENSOR##_sizes[TH_TENSOR_dim_index] = TENSOR->size[TENSOR##_i] * TENSOR##_sizes[TH_TENSOR_dim_index]; \
          if (DIM != TENSOR->nDimension-1 && TENSOR##_i < DIM) \
            TENSOR##_dimOffset--; \
        } else { \
          --TH_TENSOR_dim_index; \
          TENSOR##_sizes[TH_TENSOR_dim_index] = TENSOR->size[TENSOR##_i]; \
          TENSOR##_strides[TH_TENSOR_dim_index] = TENSOR->stride[TENSOR##_i]; \
        } \
      } \
      /* Size of the inner most section */ \
      TENSOR##_size = TENSOR##_sizes[TENSOR##_dim-1]; \
      /* Stride of the inner most section */ \
      TENSOR##_stride = TENSOR##_strides[TENSOR##_dim-1]; \
    } \
    else{\
      TENSOR##_dim = 1;\
      TENSOR##_counter = (int64_t*)THAlloc(sizeof(int64_t)*3);\
      TENSOR##_sizes = TENSOR##_counter + 1;\
      TENSOR##_strides = TENSOR##_counter + 2;\
      TENSOR##_sizes[0] = TENSOR##_n;\
      TENSOR##_strides[0] = 1;\
      TENSOR##_size = TENSOR##_sizes[0];\
      TENSOR##_stride = TENSOR##_strides[0];\
	}\
  } \
  TENSOR##_i = 0;

#define  __TH_TENSOR_APPLYX_UPDATE_COUNTERS(TENSOR, ALWAYS_UPDATE) \
  if(TENSOR##_i == TENSOR##_size || ALWAYS_UPDATE) \
  { \
    if(TENSOR##_contiguous) \
      break; \
\
    if(TENSOR##_dim == 1) \
       break; \
\
    /* Reset pointer to beginning of loop */ \
    TENSOR##_data -= TENSOR##_size*TENSOR##_stride; \
    for(TENSOR##_i = TENSOR##_dim-2; TENSOR##_i >= 0; TENSOR##_i--) \
    { \
      TENSOR##_counter[TENSOR##_i]++; \
      /* Jump ahread by the stride of this dimension */ \
      TENSOR##_data += TENSOR##_strides[TENSOR##_i]; \
\
      if(TENSOR##_counter[TENSOR##_i]  == TENSOR##_sizes[TENSOR##_i]) \
      { \
        if(TENSOR##_i == 0) \
        { \
          TH_TENSOR_APPLY_hasFinished = 1; \
          break; \
        } \
          else \
        { \
          /* Reset the pointer to the beginning of the chunk defined by this dimension */ \
          TENSOR##_data -= TENSOR##_counter[TENSOR##_i]*TENSOR##_strides[TENSOR##_i]; \
          TENSOR##_counter[TENSOR##_i] = 0; \
        } \
      } \
      else \
        break; \
    } \
    TENSOR##_i = 0; \
  } \

#define TH_TENSOR_APPLY3_D(TYPE1, TENSOR1, TYPE2, TENSOR2, TYPE3, TENSOR3, DIM, CODE) \
{ \
  int TH_TENSOR_APPLY_hasFinished = 0; \
  int64_t TH_TENSOR_dim_index = 0; \
  __TH_TENSOR_APPLYX_PREAMBLE(TYPE1, TENSOR1, DIM, 1) \
  __TH_TENSOR_APPLYX_PREAMBLE(TYPE2, TENSOR2, DIM, 1) \
  __TH_TENSOR_APPLYX_PREAMBLE(TYPE3, TENSOR3, DIM, 1) \
                                                                        \
  int elements_equal = 1;                                               \
  if(TENSOR1##_n != TENSOR2##_n) {                                      \
    elements_equal = 0;                                                 \
  }                                                                     \
  else if(TENSOR1##_n != TENSOR3##_n) {                                 \
    elements_equal = 0;                                                 \
  }                                                                     \
  if (elements_equal == 0) {                                            \
    THDescBuff T1buff = _THSizeDesc(TENSOR1->size, TENSOR1->nDimension); \
    THDescBuff T2buff = _THSizeDesc(TENSOR2->size, TENSOR2->nDimension); \
    THDescBuff T3buff = _THSizeDesc(TENSOR3->size, TENSOR3->nDimension); \
    THError("inconsistent tensor size, expected %s %s, %s %s and %s %s to have the same " \
            "number of elements, but got %d, %d and %d elements respectively", \
            #TENSOR1, T1buff.str, #TENSOR2, T2buff.str, #TENSOR3, T3buff.str, \
            TENSOR1##_n, TENSOR2##_n, TENSOR3##_n);                     \
  }                                                                     \
                                                                        \
  while(!TH_TENSOR_APPLY_hasFinished) \
  { \
    /* Loop through the inner most region of the Tensor */ \
    for(; TENSOR1##_i < TENSOR1##_size && TENSOR2##_i < TENSOR2##_size && TENSOR3##_i < TENSOR3##_size; TENSOR1##_i++, TENSOR2##_i++, TENSOR3##_i++, TENSOR1##_data += TENSOR1##_stride, TENSOR2##_data += TENSOR2##_stride, TENSOR3##_data += TENSOR3##_stride) /* 0 et pas TENSOR##_dim! */ \
    { \
      CODE \
    } \
    __TH_TENSOR_APPLYX_UPDATE_COUNTERS(TENSOR1, 0) \
    __TH_TENSOR_APPLYX_UPDATE_COUNTERS(TENSOR2, 0) \
    __TH_TENSOR_APPLYX_UPDATE_COUNTERS(TENSOR3, 0) \
  } \
  if(TENSOR1##_counter != NULL) \
    THFree(TENSOR1##_counter); \
  if(TENSOR2##_counter != NULL) \
    THFree(TENSOR2##_counter); \
  if(TENSOR3##_counter != NULL) \
    THFree(TENSOR3##_counter); \
}

#define TH_TENSOR_APPLY3(TYPE1, TENSOR1, TYPE2, TENSOR2, TYPE3, TENSOR3, CODE) \
  TH_TENSOR_APPLY3_D(TYPE1, TENSOR1, TYPE2, TENSOR2, TYPE3, TENSOR3, -1, CODE)

#define TH_TENSOR_APPLY2_D(TYPE1, TENSOR1, TYPE2, TENSOR2, DIM, CODE) \
{ \
  int TH_TENSOR_APPLY_hasFinished = 0; \
  int64_t TH_TENSOR_dim_index = 0; \
  __TH_TENSOR_APPLYX_PREAMBLE(TYPE1, TENSOR1, DIM, 1) \
  __TH_TENSOR_APPLYX_PREAMBLE(TYPE2, TENSOR2, DIM, 1) \
\
    if(TENSOR1##_n != TENSOR2##_n) {                                    \
      THDescBuff T1buff = _THSizeDesc(TENSOR1->size, TENSOR1->nDimension); \
      THDescBuff T2buff = _THSizeDesc(TENSOR2->size, TENSOR2->nDimension); \
      THError("inconsistent tensor size, expected %s %s and %s %s to have the same " \
              "number of elements, but got %d and %d elements respectively", \
              #TENSOR1, T1buff.str, #TENSOR2, T2buff.str, TENSOR1##_n, TENSOR2##_n); \
    }                                                                   \
  while(!TH_TENSOR_APPLY_hasFinished) \
  { \
    /* Loop through the inner most region of the Tensor */ \
    for(; TENSOR1##_i < TENSOR1##_size && TENSOR2##_i < TENSOR2##_size; TENSOR1##_i++, TENSOR2##_i++, TENSOR1##_data += TENSOR1##_stride, TENSOR2##_data += TENSOR2##_stride) /* 0 et pas TENSOR##_dim! */ \
    { \
      CODE \
    } \
    __TH_TENSOR_APPLYX_UPDATE_COUNTERS(TENSOR1, 0) \
    __TH_TENSOR_APPLYX_UPDATE_COUNTERS(TENSOR2, 0) \
  } \
  if(TENSOR1##_counter != NULL) \
    THFree(TENSOR1##_counter); \
  if(TENSOR2##_counter != NULL) \
    THFree(TENSOR2##_counter); \
}

#define TH_TENSOR_APPLY2(TYPE1, TENSOR1, TYPE2, TENSOR2, CODE) \
  TH_TENSOR_APPLY2_D(TYPE1, TENSOR1, TYPE2, TENSOR2, -1, CODE)

#define TH_TENSOR_APPLY_D(TYPE, TENSOR, DIM, CODE) \
{ \
  int TH_TENSOR_APPLY_hasFinished = 0; \
  int64_t TH_TENSOR_dim_index = 0; \
  __TH_TENSOR_APPLYX_PREAMBLE(TYPE, TENSOR, DIM, 0) \
\
  while(!TH_TENSOR_APPLY_hasFinished) \
  { \
    /* Loop through the inner most region of the Tensor */ \
    for(; TENSOR##_i < TENSOR##_size; TENSOR##_i++, TENSOR##_data += TENSOR##_stride) /* 0 et pas TENSOR##_dim! */ \
    { \
      CODE \
    } \
    __TH_TENSOR_APPLYX_UPDATE_COUNTERS(TENSOR, 1) \
  } \
  THFree(TENSOR##_counter); \
}

#define TH_TENSOR_APPLY(TYPE, TENSOR, CODE) \
  TH_TENSOR_APPLY_D(TYPE, TENSOR, -1, CODE)


#ifdef _OPENMP

#ifndef _WIN32
#define PRAGMA(P) _Pragma(#P)
#else
#define PRAGMA(P) __pragma(P)
#endif

#define TH_OMP_OVERHEAD_THRESHOLD_OMP 5000
#include <omp.h>
#include <x86intrin.h>
#include <stdio.h>

#define TH_TENSOR_APPLY3_OMP(SIZE, CONTIG1, CONTIG2, CONTIG3, TYPE1, TENSOR1, TYPE2, TENSOR2, TYPE3, TENSOR3, CODE) \
{                                                                             \
  /* for adveanced searching index*/                                                                    \
  TYPE1 *rp = THTensor_(data)(TENSOR1);                                                                 \
  TYPE2 *tp = THTensor_(data)(TENSOR2);                                                                 \
  TYPE3 *srcp = THTensor_(data)(TENSOR3);                                                               \
  ptrdiff_t iter = 0;\
  if(CONTIG1 && CONTIG2 && CONTIG3){                                                                    \
    if (rp != tp) { \
      PRAGMA( omp parallel for if (SIZE > TH_OMP_OVERHEAD_THRESHOLD_OMP) )  \
      PRAGMA(ivdep) \
      for (iter = 0; iter < SIZE; iter++) {\
        TYPE1 *TENSOR1##_data = rp+iter;\
        TYPE2 *TENSOR2##_data = tp+iter; \
        TYPE3 *TENSOR3##_data = srcp+iter;\
        CODE                                \
      } \
    } else {\
      PRAGMA( omp parallel for if (SIZE > TH_OMP_OVERHEAD_THRESHOLD_OMP) )  \
      for (iter = 0; iter < SIZE; iter++) {\
        TYPE1 *TENSOR1##_data = rp+iter;\
        TYPE2 *TENSOR2##_data = tp+iter; \
        TYPE3 *TENSOR3##_data = srcp+iter;\
        CODE                                \
      } \
    }\
  } else{              \
    int TH_TENSOR_APPLY_hasFinished = 0;\
    int64_t TH_TENSOR_dim_index = 0;\
    __TH_TENSOR_APPLYX_PREAMBLE(TYPE1, TENSOR1, -1, 1) \
    __TH_TENSOR_APPLYX_PREAMBLE(TYPE2, TENSOR2, -1, 1) \
    __TH_TENSOR_APPLYX_PREAMBLE(TYPE3, TENSOR3, -1, 1) \
    PRAGMA(omp parallel if (SIZE > TH_OMP_OVERHEAD_THRESHOLD_OMP) firstprivate(TENSOR1##_sizes, TENSOR1##_strides, TENSOR1##_dim, TENSOR1##_stride, TENSOR1##_size, TENSOR1##_i, TENSOR2##_sizes, TENSOR2##_strides, TENSOR2##_dim, TENSOR2##_stride, TENSOR2##_size, TENSOR2##_i, TENSOR3##_sizes, TENSOR3##_strides, TENSOR3##_dim, TENSOR3##_stride, TENSOR3##_size, TENSOR3##_i))\
    {\
      size_t num_threads = omp_get_num_threads();\
      size_t tid = omp_get_thread_num();\
      ptrdiff_t line_index_offset = tid * (SIZE/num_threads);\
      ptrdiff_t line_index_end = (tid == num_threads - 1)? SIZE:(line_index_offset + SIZE/num_threads);\
      ptrdiff_t line_seg_len = line_index_end - line_index_offset;\
      __TH_TENSOR_APPLYX_CAL_OFFSET(TENSOR1);\
      __TH_TENSOR_APPLYX_CAL_OFFSET(TENSOR2);\
      __TH_TENSOR_APPLYX_CAL_OFFSET(TENSOR3);\
      TYPE1 *TENSOR1##_data = rp + TENSOR1##_offset;\
      TYPE2 *TENSOR2##_data = tp + TENSOR2##_offset;\
      TYPE3 *TENSOR3##_data = srcp + TENSOR3##_offset;\
      ptrdiff_t count = 0;\
      ptrdiff_t TENSOR1##_start = TENSOR1##_counter_tmp[TENSOR1##_dim - 1];\
      ptrdiff_t TENSOR2##_start = TENSOR2##_counter_tmp[TENSOR2##_dim - 1];\
      ptrdiff_t TENSOR3##_start = TENSOR3##_counter_tmp[TENSOR3##_dim - 1];\
      while(count < line_seg_len){\
        for(TENSOR1##_i=TENSOR1##_start, TENSOR2##_i=TENSOR2##_start,TENSOR3##_i=TENSOR3##_start; (count<line_seg_len)&&(TENSOR1##_i<TENSOR1##_size)&&(TENSOR2##_i<TENSOR2##_size)&&(TENSOR3##_i<TENSOR3##_size); ++TENSOR1##_i,++TENSOR2##_i,++TENSOR3##_i,++count){\
          CODE\
          TENSOR1##_data += TENSOR1##_stride;\
          TENSOR2##_data += TENSOR2##_stride;\
          TENSOR3##_data += TENSOR3##_stride;\
        }\
        if(count < line_seg_len){\
          __TH_TENSOR_APPLYX_UPDATE_COUNTERS_OMP(TENSOR1);\
          __TH_TENSOR_APPLYX_UPDATE_COUNTERS_OMP(TENSOR2);\
          __TH_TENSOR_APPLYX_UPDATE_COUNTERS_OMP(TENSOR3);\
        }\
      }\
      if(TENSOR1##_counter_tmp != NULL) \
        THFree(TENSOR1##_counter_tmp); \
      if(TENSOR2##_counter_tmp != NULL) \
        THFree(TENSOR2##_counter_tmp); \
      if(TENSOR3##_counter_tmp != NULL) \
        THFree(TENSOR3##_counter_tmp);\
    }\
    if(TENSOR1##_counter != NULL)\
      THFree(TENSOR1##_counter);\
    if(TENSOR2##_counter != NULL)\
      THFree(TENSOR2##_counter);\
    if(TENSOR3##_counter != NULL)\
      THFree(TENSOR3##_counter);\
  }\
}


#define __TH_TENSOR_APPLYX_CAL_OFFSET(TENSOR) \
  int64_t *TENSOR##_counter_tmp = (int64_t*)THAlloc(sizeof(int64_t) * TENSOR##_dim);\
  ptrdiff_t TENSOR##_offset = 0;                                                                \
  ptrdiff_t TENSOR##_quot = line_index_offset;                                                           \
  for (TENSOR##_i = TENSOR##_dim-1; TENSOR##_i>=0; --TENSOR##_i) {                              \
    TENSOR##_counter_tmp[TENSOR##_i] = TENSOR##_quot%TENSOR##_sizes[TENSOR##_i];                         \
    TENSOR##_quot /= TENSOR##_sizes[TENSOR##_i];                                                         \
    TENSOR##_offset += TENSOR##_counter_tmp[TENSOR##_i] * TENSOR##_strides[TENSOR##_i];         \
  }

#define __TH_TENSOR_APPLYX_UPDATE_COUNTERS_OMP(TENSOR) \
  if(TENSOR##_i == TENSOR##_size && TENSOR##_dim > 1){ \
    int TENSOR##_carry_coord = 1; \
    TENSOR##_data -= TENSOR##_size * TENSOR##_stride; \
    TENSOR##_start = 0;                               \
    for(TENSOR##_i = TENSOR##_dim - 2; (TENSOR##_i >= 0) && (TENSOR##_carry_coord); TENSOR##_i--){ \
      TENSOR##_counter_tmp[TENSOR##_i]++; \
      TENSOR##_data += TENSOR##_strides[TENSOR##_i]; \
      if(TENSOR##_counter_tmp[TENSOR##_i] == TENSOR##_sizes[TENSOR##_i]){ \
        TENSOR##_data -= TENSOR##_sizes[TENSOR##_i] * TENSOR##_strides[TENSOR##_i]; \
        TENSOR##_counter_tmp[TENSOR##_i] = 0; \
      } else { \
        TENSOR##_carry_coord = 0; \
      } \
    } \
  } else { \
    TENSOR##_start = TENSOR##_i;                               \
  }

#define TH_TENSOR_APPLY2_OMP(SIZE, CONTIG1, CONTIG2, TYPE1, TENSOR1, TYPE2, TENSOR2, CODE) \
{                                                                             \
  /* for advanced searching index*/                                       \
  TYPE1 *rp = THTensor_(data)(TENSOR1);                                    \
  TYPE2 *tp = THTensor_(data)(TENSOR2);                                    \
  ptrdiff_t iter = 0;\
  if( CONTIG1 && CONTIG2 ){                                    \
    if(tp != rp) { \
      PRAGMA( omp parallel for if (SIZE > TH_OMP_OVERHEAD_THRESHOLD_OMP) firstprivate(rp, tp))  \
      PRAGMA(ivdep) \
      for (iter = 0; iter < SIZE; iter++) {                             \
        TYPE2 *TENSOR2##_data = tp+iter;                                \
        TYPE1 *TENSOR1##_data = rp+iter;                                \
        CODE                                                            \
      }\
    } else {\
      PRAGMA( omp parallel for if (SIZE > TH_OMP_OVERHEAD_THRESHOLD_OMP) firstprivate(rp, tp) )  \
      PRAGMA(simd) \
      for (iter = 0; iter < SIZE; iter++) {\
        TYPE2* TENSOR2##_data = tp+iter;\
        TYPE1* TENSOR1##_data = rp+iter;\
        CODE                                \
      }\
    }\
  } else {              \
    int TH_TENSOR_APPLY_hasFinished = 0; \
    int64_t TH_TENSOR_dim_index = 0; \
    __TH_TENSOR_APPLYX_PREAMBLE(TYPE2, TENSOR2, -1, 1) \
    __TH_TENSOR_APPLYX_PREAMBLE(TYPE1, TENSOR1, -1, 1) \
    PRAGMA(omp parallel if (SIZE > TH_OMP_OVERHEAD_THRESHOLD_OMP) firstprivate(TENSOR2##_sizes, TENSOR2##_strides, TENSOR2##_dim, TENSOR2##_stride, TENSOR2##_size, TENSOR2##_i, TENSOR1##_sizes, TENSOR1##_strides, TENSOR1##_dim, TENSOR1##_stride, TENSOR1##_size, TENSOR1##_i)) \
    {                                                                                                    \
      size_t num_threads = omp_get_num_threads();                                                        \
      size_t tid = omp_get_thread_num();                                                                 \
      ptrdiff_t line_index_offset = tid * (SIZE/num_threads);                                            \
      ptrdiff_t line_index_end = (tid == num_threads - 1) ? SIZE:(line_index_offset + SIZE/num_threads); \
      ptrdiff_t line_seg_len = line_index_end - line_index_offset;                                       \
      /*divided segment maybe does not cover the size of the last contiguous dimension*/                 \
      /*calculate each coord of the preamble tensor*/                                                    \
      __TH_TENSOR_APPLYX_CAL_OFFSET(TENSOR2);                                                           \
      __TH_TENSOR_APPLYX_CAL_OFFSET(TENSOR1);                                                           \
      TYPE2 *TENSOR2##_data = tp + TENSOR2##_offset;                                                     \
      TYPE1 *TENSOR1##_data = rp + TENSOR1##_offset;                                                     \
      ptrdiff_t count = 0;                                                                               \
      ptrdiff_t TENSOR2##_start =  TENSOR2##_counter_tmp[TENSOR2##_dim-1];                               \
      ptrdiff_t TENSOR1##_start =  TENSOR1##_counter_tmp[TENSOR1##_dim-1];                               \
      while (count < line_seg_len) {                                                                     \
        for(TENSOR2##_i=TENSOR2##_start, TENSOR1##_i = TENSOR1##_start; ((count < line_seg_len) && (TENSOR2##_i < TENSOR2##_size) && (TENSOR1##_i < TENSOR1##_size)); ++TENSOR2##_i, ++TENSOR1##_i, ++count){ \
          CODE                                                                                               \
          TENSOR2##_data += TENSOR2##_stride;                                                                \
          TENSOR1##_data += TENSOR1##_stride;                                                                \
        }                                                                                                    \
        if (count < line_seg_len){                                                                           \
          __TH_TENSOR_APPLYX_UPDATE_COUNTERS_OMP(TENSOR2);                                                   \
          __TH_TENSOR_APPLYX_UPDATE_COUNTERS_OMP(TENSOR1);                                                   \
        }                                                                                                    \
      }                                                                                                      \
      if(TENSOR1##_counter_tmp != NULL) \
        THFree(TENSOR1##_counter_tmp); \
      if(TENSOR2##_counter_tmp != NULL) \
        THFree(TENSOR2##_counter_tmp); \
    }                                                                                                        \
    if(TENSOR2##_counter != NULL) \
      THFree(TENSOR2##_counter); \
    if(TENSOR1##_counter != NULL) \
      THFree(TENSOR1##_counter);\
    }\
}

#define TH_TENSOR_APPLY_REDUCTION_OMP(TYPE1, TENSOR1, OPERATION, CODE) \
{\
  TYPE1 *rp = THTensor_(data)(TENSOR1);                                      \
  int TENSOR1##Contg = THTensor_(isContiguous)(TENSOR1);                     \
  ptrdiff_t TENSOR1##Size = THTensor_(nElement)(TENSOR1);                     \
  ptrdiff_t iter = 0;\
  if(TENSOR1##Contg){                                    \
    TYPE1 *TENSOR1##_data = NULL;         \
    PRAGMA( omp parallel for if (TENSOR1##Size > TH_OMP_OVERHEAD_THRESHOLD_OMP) private(TENSOR1##_data,  iter) reduction(OPERATION) ) \
    for (iter = 0; iter < TENSOR1##Size; iter++) {\
      TENSOR1##_data = rp+iter;\
      CODE                                \
    }\
  } else { \
    int TH_TENSOR_APPLY_hasFinished = 0;\
    int64_t TH_TENSOR_dim_index = 0;\
    __TH_TENSOR_APPLYX_PREAMBLE(TYPE1, TENSOR1, -1, 1);\
    PRAGMA(omp parallel if (TENSOR1##Size > TH_OMP_OVERHEAD_THRESHOLD_OMP) firstprivate(TENSOR1##_sizes, TENSOR1##_strides, TENSOR1##_dim, TENSOR1##_stride, TENSOR1##_size, TENSOR1##_i) reduction(OPERATION))\
    {\
      size_t num_threads = omp_get_num_threads();\
      size_t tid = omp_get_thread_num();\
      ptrdiff_t line_index_offset = tid * (TENSOR1##Size/num_threads);\
      ptrdiff_t line_index_end = (tid == num_threads - 1)?TENSOR1##Size:(line_index_offset + TENSOR1##Size/num_threads);\
      ptrdiff_t line_seg_len = line_index_end - line_index_offset;\
      __TH_TENSOR_APPLYX_CAL_OFFSET(TENSOR1);\
      TYPE1 *TENSOR1##_data = rp + TENSOR1##_offset;\
      ptrdiff_t count = 0;\
      ptrdiff_t TENSOR1##_start = TENSOR1##_counter_tmp[TENSOR1##_dim - 1];\
      while(count < line_seg_len){\
        for(TENSOR1##_i=TENSOR1##_start; (count < line_seg_len)&&(TENSOR1##_i < TENSOR1##_size); ++TENSOR1##_i, ++count){\
          CODE\
          TENSOR1##_data += TENSOR1##_stride;\
        }\
        if(count < line_seg_len){\
          __TH_TENSOR_APPLYX_UPDATE_COUNTERS_OMP(TENSOR1);\
        }\
      }\
      if(TENSOR1##_counter_tmp != NULL) \
        THFree(TENSOR1##_counter_tmp); \
    }\
    if(TENSOR1##_counter != NULL)\
      THFree(TENSOR1##_counter);\
  }\
}


#endif
#endif
