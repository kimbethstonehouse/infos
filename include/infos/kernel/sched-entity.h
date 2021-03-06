/* SPDX-License-Identifier: MIT */

#pragma once

#include <infos/util/time.h>
#include <infos/util/event.h>

namespace infos
{
	namespace kernel
	{
		class Scheduler;

        enum class SchedulingEntityState
        {
            CREATED,
            STOPPED,
            SLEEPING,
            RUNNABLE,
            RUNNING,
            IDLE
        };

		class SchedulingEntity
		{
			friend class Scheduler;
		public:
			typedef util::Nanoseconds EntityRuntime;
			typedef util::KernelRuntimeClock::Timepoint EntityStartTime;

			SchedulingEntity() : _cpu_runtime(0), _exec_start_time(0), _state(SchedulingEntityState::CREATED), _current_scheduler(nullptr) {}
			virtual ~SchedulingEntity() { }

			virtual bool activate(SchedulingEntity *prev) = 0;

			EntityRuntime cpu_runtime() const { return _cpu_runtime; }

			void increment_cpu_runtime(EntityRuntime delta) { _cpu_runtime += delta; }
			void update_exec_start_time(EntityStartTime exec_start_time) { _exec_start_time = exec_start_time; }

			SchedulingEntityState state() const { return _state; }

			bool stopped() const { return _state == SchedulingEntityState::STOPPED; }
            util::Event &state_changed() { return _state_changed; }

            Scheduler *current_scheduler() const { return _current_scheduler; }
            void current_scheduler(Scheduler *sched) { _current_scheduler = sched; }

		private:
			EntityRuntime _cpu_runtime;
			EntityStartTime _exec_start_time;

			SchedulingEntityState _state;
			util::Event _state_changed;
			Scheduler *_current_scheduler;
		};
	}
}
