/* SPDX-License-Identifier: MIT */

/*
 * util/lock.cpp
 * 
 * InfOS
 * Copyright (C) University of Edinburgh 2016.  All Rights Reserved.
 * 
 * Tom Spink <tspink@inf.ed.ac.uk>
 */
#include <infos/util/lock.h>
#include <infos/kernel/kernel.h>
#include <infos/kernel/thread.h>
#include <infos/kernel/syscall.h>
#include <infos/kernel/log.h>
#include <arch/arch.h>
#include <arch/x86/pio.h>

using namespace infos::util;
using namespace infos::kernel;

void Mutex::lock()
{
	while (__sync_lock_test_and_set(&_locked, 1)) {
//	    infos::arch::x86::__outb(0xe9, 0x41);
//		infos::kernel::sys.arch().invoke_kernel_syscall(1);
        asm volatile ("nop");
	}
	
//	_owner = &Thread::current(); // todo: this is the problem
}

void Mutex::unlock()
{
	__sync_lock_release(&_locked);
}

bool Mutex::locked_by_me()
{
	return locked() && _owner == &Thread::current();
}

//void Spinlock::lock()
//{
//    while (__sync_lock_test_and_set(&_locked, 1)) { asm volatile ("nop"); }
//}
//
//void Spinlock::unlock()
//{
//    __sync_lock_release(&_locked);
//}

void ConditionVariable::wait(Mutex& mtx)
{
	assert(mtx.locked_by_me());
}

void ConditionVariable::notify_all()
{
//
}

void ConditionVariable::notify_one()
{
//
}



IRQLock::IRQLock() : _were_interrupts_enabled(false)
{

}

void IRQLock::lock()
{
	_were_interrupts_enabled = infos::kernel::sys.arch().interrupts_enabled();
	if (_were_interrupts_enabled) {
		infos::kernel::sys.arch().disable_interrupts();
	}
	
	assert(!infos::kernel::sys.arch().interrupts_enabled());
}

void IRQLock::unlock()
{
	if (_were_interrupts_enabled) {
		infos::kernel::sys.arch().enable_interrupts();
		assert(infos::kernel::sys.arch().interrupts_enabled());
	}
}
