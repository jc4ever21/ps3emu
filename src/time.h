
struct timebase_frequency_fetcher {
	uint64_t value;
	timebase_frequency_fetcher() {
		uint64_t qf=0;
		QueryPerformanceFrequency((LARGE_INTEGER*)&qf);
		if (!qf) xcept("QueryPerformanceFrequency failed");
		auto calc = [&]() -> uint64_t {
			uint64_t begin_count=0;
			QueryPerformanceCounter((LARGE_INTEGER*)&begin_count);
			uint64_t begin_tsc = __rdtsc();

			uint64_t stop_count = begin_count+qf/10;

			uint64_t count=0, tsc;
			for (int i=0;i<0x10000 && count<stop_count;i++) {
				uint64_t pre_rdtsc = __rdtsc();
				QueryPerformanceCounter((LARGE_INTEGER*)&count);
				tsc = __rdtsc();
				tsc -= (tsc-pre_rdtsc)/2; // ...why not
			}
			tsc = tsc - begin_tsc;
			count = count - begin_count;

			int shift = 24;
			if (count<<shift>>shift!=count||tsc<<shift>>shift!=tsc) xcept("too big shift while calculating timebase frequency");
			count <<= shift;
			tsc <<= shift;
			uint64_t time = count/qf;
			if (!time) xcept("failed to measure timebase frequency");
			return tsc/time;
		};

		HANDLE ht = GetCurrentThread();
		int prio = GetThreadPriority(ht);
		SetThreadPriority(ht,THREAD_PRIORITY_TIME_CRITICAL);

		value = calc();

		SetThreadPriority(ht,prio);

		dbgf("timebase frequency calculated to %d\n",value);
	}
	uint64_t operator*() {
		return value;
	}
};

uint64_t timebase() {
	return __rdtsc();
}
uint64_t timebase_frequency = 0;

uint64_t sys_time_get_timebase_frequency() {
	return timebase_frequency;
}

// There is no syscall for this; liblv2 does it itself.
// This is just here for the convenience of timer code.
int64_t sys_time_get_system_time() {
	return timebase()/(timebase_frequency/1000000);
}



