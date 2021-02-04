/* SPDX-License-Identifier: MIT */

#pragma once

#include <infos/util/time.h>
#include <infos/util/event.h>

namespace infos
{
	namespace kernel
	{
		class Scheduler;
		
		namespace SchedulingEntityState
		{
			enum SchedulingEntityState
			{
				STOPPED,
				SLEEPING,
				RUNNABLE,
				RUNNING,
			};
		}

		namespace SchedulingEntityType
		{
            enum SchedulingEntityType
            {
                NON_IDLE,
                IDLE
            };
		}
		
		class SchedulingEntity
		{
			friend class Scheduler;
		public:
			typedef util::Nanoseconds EntityRuntime;
			typedef util::KernelRuntimeClock::Timepoint EntityStartTime;
			
			SchedulingEntity() : _cpu_runtime(0), _exec_start_time(0), _state(SchedulingEntityState::STOPPED),
			        _type(SchedulingEntityType::NON_IDLE) {}
			virtual ~SchedulingEntity() { }
			
			virtual bool activate(SchedulingEntity *prev) = 0;
			
			EntityRuntime cpu_runtime() const { return _cpu_runtime; }
			
			void increment_cpu_runtime(EntityRuntime delta) { _cpu_runtime += delta; }
			void update_exec_start_time(EntityStartTime exec_start_time) { _exec_start_time = exec_start_time; }
			
			SchedulingEntityState::SchedulingEntityState state() const { return _state; }
			
			bool stopped() const { return _state == SchedulingEntityState::STOPPED; }

			inline SchedulingEntityType::SchedulingEntityType get_type() { return _type;}
			inline void set_type(SchedulingEntityType::SchedulingEntityType type) { _type = type; }

			util::Event& state_changed() { return _state_changed; }
			
		private:
			EntityRuntime _cpu_runtime;
			EntityStartTime _exec_start_time;
			
			SchedulingEntityState::SchedulingEntityState _state;
			SchedulingEntityType::SchedulingEntityType _type;
			util::Event _state_changed;
		};
	}
}
