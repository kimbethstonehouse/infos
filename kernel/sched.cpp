/* SPDX-License-Identifier: MIT */

/*
 * kernel/sched.cpp
 *
 * InfOS
 * Copyright (C) University of Edinburgh 2016.  All Rights Reserved.
 *
 * Tom Spink <tspink@inf.ed.ac.uk>
 */
#include <infos/kernel/sched.h>
#include <infos/kernel/sched-entity.h>
#include <infos/kernel/process.h>
#include <infos/kernel/kernel.h>
#include <infos/util/time.h>
#include <infos/util/cmdline.h>
#include <arch/arch.h>
#include <arch/x86/context.h>
#include <arch/x86/msr.h>

using namespace infos::kernel;
using namespace infos::util;
using namespace infos::arch::x86;

ComponentLog infos::kernel::sched_log(syslog, "sched");

static char sched_algorithm[32];
static char core_algorithm[32];

RegisterCmdLineArgument(SchedAlgorithm, "sched.algorithm") {
	strncpy(sched_algorithm, value, sizeof(sched_algorithm)-1);
}

RegisterCmdLineArgument(CoreAlg, "core.algorithm") {
    strncpy(core_algorithm, value, sizeof(core_algorithm)-1);
}

RegisterCmdLineArgument(SchedDebug, "sched.debug") {
	if (strncmp(value, "1", 1) == 0) {
		sched_log.enable();
	} else {
		sched_log.disable();
	}
}

SchedulingManager::SchedulingManager(Kernel &owner) : Subsystem(owner)
{
}

int SchedulingManager::rdrand16_step(uint16_t *rand)
{
	unsigned char ok;

	asm volatile("rdrand %0; setc %1"
				 : "=r"(*rand), "=qm"(ok));

	return (int)ok;
}

unsigned int prng()
{
	// our initial starting seed is 5323
	static unsigned int nSeed = 5323;

	// Take the current seed and generate a new value from it
	// Due to our use of large constants and overflow, it would be
	// very hard for someone to predict what the next number is
	// going to be from the previous one.
	nSeed = (8253729 * nSeed + 2396403);

	// Take the seed and return a value between 0 and 32767
	return nSeed % 32767;
}

Scheduler *SchedulingManager::next_sched_rand()
{
	UniqueLock<util::Mutex> l(_mtx);

	// Choose a random scheduler
	uint16_t random_idx;
	rdrand16_step(&random_idx);
	random_idx = random_idx % (schedulers_.count());
	//    syslog.messagef(LogLevel::IMPORTANT2, "Scheduler %u chosen from %u", random_idx, schedulers_.count());
	return schedulers_.at(random_idx);
}

Scheduler *SchedulingManager::next_sched_load_bal()
{
	// Multicore, so choose the scheduler with the smallest runqueue!
	int min = schedulers_.first()->algorithm().load();

	Scheduler *next = schedulers_.first();

	for (Scheduler *sched : schedulers_)
	{
		if (sched->algorithm().load() < min)
		{
			min = sched->algorithm().load();
			next = sched;
		}
	}

	return next;
}

Scheduler *SchedulingManager::next_sched_proc_affin(SchedulingEntity &entity) {
    if (entity.proc_affinity()) {
        return entity.affined_scheduler();
    } else return next_sched_load_bal();
}

void SchedulingManager::add_scheduler(Scheduler &scheduler)
{
	UniqueLock<util::Mutex> l(_mtx);
	schedulers_.enqueue(&scheduler);
}

Scheduler *SchedulingManager::get_scheduler()
{
	UniqueLock<util::Mutex> l(_mtx);
	return schedulers_.dequeue();
}

void SchedulingManager::set_entity_state(SchedulingEntity &entity, SchedulingEntityState::SchedulingEntityState state) {
    Scheduler *sched;

    // Something went really wrong if we've no schedulers to run on
    if (schedulers_.count() == 0) arch_abort();
    // Single core!
    if (schedulers_.count() == 1) sched = schedulers_.first();

    if (entity.get_type() == SchedulingEntityType::IDLE) {
        // This is really important. The idle entity must be given to the scheduler
        // of the core that created it because the core forcibly activates it either way
        sched = &infos::drivers::irq::Core::get_current_core()->get_scheduler();
    } else if (strncmp(core_algorithm, "load.bal", strlen(core_algorithm)-1) == 0) {
        sched = next_sched_load_bal();
    } else if (strncmp(core_algorithm, "proc.affin", strlen(core_algorithm)-1) == 0) {
        sched = next_sched_proc_affin(entity);
    } else {
        // Default is random
        sched = next_sched_rand();
    }

    entity.set_affinity(*sched);
    sched->set_entity_state(entity, state);
}

Scheduler::Scheduler(Kernel &owner) : Subsystem(owner), _active(false), _current(NULL)
{
}

/**
 * The idle task thread proc.  It just spins in a loop, relaxing the processor.
 */
static void idle_task()
{
	for (;;)
		asm volatile("pause");
}

bool Scheduler::init()
{
	sys.sched_manager().add_scheduler(*this);

	sched_log.message(LogLevel::INFO, "Creating idle process");
	Process *idle_process = new Process("idle", true, (Thread::thread_proc_t)idle_task);

    _idle_entity = &idle_process->main_thread();
	_idle_entity->set_type(SchedulingEntityType::IDLE);

	SchedulingAlgorithm *algo = acquire_scheduler_algorithm();
	if (!algo)
	{
		syslog.messagef(LogLevel::ERROR, "No scheduling algorithm available");
		return false;
	}

	syslog.messagef(LogLevel::IMPORTANT, "*** USING SCHEDULER ALGORITHM: %s", algo->name());

	// Install the discovered algorithm.
	_algorithm = algo;

	// Start the idle process thread, and forcibly activate it.  This is so that
	// when interrupts are enabled, the idle thread becomes the context that is
	// saved and restored.
	idle_process->start();
	idle_process->main_thread().activate(NULL);

	// But also remove the idle thread from the algorithms run queue, as the
	// scheduler shouldn't be scheduling this as a regular task.
	algo->remove_from_runqueue(idle_process->main_thread());

	return true;
}

void Scheduler::run()
{
	// This is now the point of no return.  Once the scheduler is activated, it will schedule the first
	// eligible process.  Which may or may not be the idle task.  But, non-scheduled control-flow will cease
	// to be, and the kernel will only run processes.
	sched_log.message(LogLevel::INFO, "Activating scheduler...");
	_active = true;

	// Enable interrupts
	syslog.messagef(LogLevel::DEBUG, "Enabling interrupts");
	owner().arch().enable_interrupts();

	// From this point onwards, the scheduler is now live.

	// This infinite loop is required for two reasons:
	// 1. The ::run() method is marked as __noreturn, and gcc can figure out if that's really true, and so will complain
	//    if our "noreturn" method actually returns.
	//
	// 2. Once the scheduler is activated, it will only begin scheduling on the next timer tick -- which, of course,
	//    is asynchronous to this control-flow.
	for (;;)
		asm volatile("nop");
}

/**
 * Called during an interrupt to (possibly) switch processes.
 */
void Scheduler::schedule()
{
	if (!_active)
		return;
	if (!_algorithm)
		return;

	// Ask the scheduling algorithm for the next process.
	SchedulingEntity *next = _algorithm->pick_next_entity();

	// If the algorithm refused to return a process, then schedule
	// the idle entity.
	if (!next) {
		next = _idle_entity;
	}

	// If the next task to run, is NOT the currently running task...
	if (next != _current)
	{
		// Activate the next task.
		if (next->activate(_current))
		{
			// Update the current task pointer.
			_current = next;
		}
		else
		{
			// If the task failed to activate, try and forcibly activate the idle entity.
			if (!_idle_entity->activate(_current))
			{
				// We're in big trouble if even the idle thread won't activate.
				arch_abort();
			}

			// Update the current task pointer.
			_current = _idle_entity;
		}
	}

	// Update the execution start time for the task that's about to run.
	_current->update_exec_start_time(owner().runtime());

	// Debugging output for the scheduler.
	uint8_t apic_id = (*(uint32_t *)(pa_to_vpa((__rdmsr(MSR_APIC_BASE) & ~0xfff) + 0x20))) >> 24;
	sched_log.messagef(LogLevel::DEBUG, "Scheduled %p (%s/%s) on core %u", _current, ((Thread *)_current)->owner().name().c_str(), ((Thread *)_current)->name().c_str(), apic_id);
}

/**
 * Updates process accounting.
 */
void Scheduler::update_accounting()
{
	if (_current)
	{
		auto now = owner().runtime();

		// Calculate the delta.
		SchedulingEntity::EntityRuntime delta = now - _current->_exec_start_time;

		// Increment the CPU runtime.
		_current->increment_cpu_runtime(delta);

		// Update the exec start time.
		_current->update_exec_start_time(now);
	}
}

/**
 * Changes the state of a scheduling entity.
 * @param entity The scheduling entity being changed.
 * @param state The new state for the entity.
 */
void Scheduler::set_entity_state(SchedulingEntity& entity, SchedulingEntityState::SchedulingEntityState state)
{
	assert(_algorithm);

	// If the state is not being changed -- do nothing.
	if (entity._state == state) return;

	// If the new state is runnable...
	if (state == SchedulingEntityState::RUNNABLE) {
		// Add the entity to the runqueue only if it is transitioning from STOPPED or SLEEPING
		if (entity._state == SchedulingEntityState::STOPPED || entity._state == SchedulingEntityState::SLEEPING) {
			_algorithm->add_to_runqueue(entity);
		}
	} else if (state == SchedulingEntityState::STOPPED || state == SchedulingEntityState::SLEEPING) {
		// Remove the entity from the runqueue only if it is transitioning from RUNNABLE or RUNNING
		if (entity._state == SchedulingEntityState::RUNNABLE || entity._state == SchedulingEntityState::RUNNING) {
			_algorithm->remove_from_runqueue(entity);
		}
	} else if (state == SchedulingEntityState::RUNNING) {
		// The entity can only transition into RUNNING if it is currently RUNNABLE
		assert(entity._state == SchedulingEntityState::RUNNABLE);
	}

	// Record the new state in the entity.
	entity._state = state;
	entity._state_changed.trigger();
}

void Scheduler::set_current_thread(Thread &thread) { _current_thread = &thread; }

extern char _SCHED_ALG_PTR_START, _SCHED_ALG_PTR_END;

SchedulingAlgorithm *Scheduler::acquire_scheduler_algorithm()
{
	if (strlen(sched_algorithm) == 0)
	{
		sched_log.messagef(LogLevel::ERROR, "Scheduling allocation algorithm not chosen on command-line");
		return NULL;
	}

	SchedulingAlgorithmFactory *schedulers = (SchedulingAlgorithmFactory *)&_SCHED_ALG_PTR_START;

	sched_log.messagef(LogLevel::DEBUG, "Searching for '%s' algorithm...", sched_algorithm);
	while (schedulers < (SchedulingAlgorithmFactory *)&_SCHED_ALG_PTR_END)
	{
		SchedulingAlgorithm *candidate = (*schedulers)();

		sched_log.messagef(LogLevel::DEBUG, "Found '%s' algorithm...", candidate->name());
		if (strncmp(candidate->name(), sched_algorithm, sizeof(sched_algorithm) - 1) == 0)
		{
			return candidate;
		}

		delete candidate;

		schedulers++;
	}

	return nullptr;
}
