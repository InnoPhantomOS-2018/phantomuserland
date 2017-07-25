/**
 *
 * Phantom OS
 *
 * Copyright (C) 2005-2011 Dmitry Zavalishin, dz@dz.ru
 *
 * Atomic funcs.
 *
 *
**/

#ifndef ATOMIC_H
#define ATOMIC_H

#include <phantom_types.h>

//#ifdef ARCH_ia32
// arch dependen operand size = 2x pointer size -- made it pvm_object_int_t, defined in arch-types.h

// TODO used __ATOMIC_ACQ_REL for any case. relax?

// Must be used on any pvm_object_t which is possibly shared and accessed not in spinlock/mutex
#define ATOMIC_PVM_LOAD( __dest, __src ) ( *((pvm_object_int_t*)(&__dest)) = __atomic_load_n( (pvm_object_int_t*)&(__src), __ATOMIC_ACQUIRE ) )
#define ATOMIC_PVM_STORE( __dest, __src ) (__atomic_store_n( (pvm_object_int_t*)&(__dest), *((pvm_object_int_t*)(&(__src))), __ATOMIC_RELEASE ) )
//#endif

#if (!defined(ATOMIC_PVM_LOAD)) || (!defined(ATOMIC_PVM_STORE))
#  error need atomic ops for current arch
#endif

#define ATOMIC_ADD_AND_FETCH( __ptr, __val ) __sync_add_and_fetch( __ptr, __val )

// atomic_set can be used for non-intel?
#define ATOMIC_FETCH_AND_SET( __ptr, __val ) __sync_lock_test_and_set( __ptr, __val )

int atomic_add(int *val, int incr);
int atomic_or(int *val, int incr);
#if 0
/*
** Copyright 2001, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/

int atomic_and(int *val, int incr);
int atomic_set(volatile int *val, int set_to);
int test_and_set(int *val, int set_to, int test_val);

#endif

#endif // ATOMIC_H

