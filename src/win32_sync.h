

namespace win32_sync {

	void bad_return_value() {
		xcept("win32_sync: unexpected return value; last error is %d",(int)GetLastError());
	}
	template<typename t>
	void check_not_zero(t v) {
		if (v==0) bad_return_value();
	}

	// signal, wait and try_wait works as processor memory barriers.
	struct ipc_event {
		HANDLE h;
		ipc_event() : h(0) {}
		ipc_event(const char*name) {
			h = CreateEventA(0,FALSE,FALSE,name);
			check_not_zero(h);
		}
		ipc_event(ipc_event&&e) {
			std::swap(h,e.h);
		}
		ipc_event&operator=(ipc_event&&e) {
			std::swap(h,e.h);
			return *this;
		}
		~ipc_event() {
			if (h) CloseHandle(h);
		}
		void signal() {
			check_not_zero(SetEvent(h));
		}
		bool wait(uint64_t timeout) {
			DWORD r = WaitForSingleObject(h,timeout ? timeout/1000 : INFINITE);
			if (r==WAIT_OBJECT_0) return true;
			else if (r==WAIT_TIMEOUT) return false;
			else bad_return_value();
			return false;
		}
		bool try_wait() {
			DWORD r = WaitForSingleObject(h,0);
			if (r==WAIT_OBJECT_0) return true;
			else if (r==WAIT_TIMEOUT) return false;
			else bad_return_value();
			return false;
		}
		bool operator!() const {
			return h==0;
		}
		HANDLE native_handle() {
			return h;
		}
	};

	struct event {
		HANDLE h;
		event() : h(0) {}
		event(bool manual_reset,bool initial_state) {
			h = CreateEventA(0,manual_reset?TRUE:FALSE,initial_state?TRUE:FALSE,0);
			check_not_zero(h);
		}
		event(event&&e) {
			std::swap(h,e.h);
		}
		event&operator=(event&&e) {
			std::swap(h,e.h);
			return *this;
		}
		~event() {
			if (h) CloseHandle(h);
		}
		void reset() {
			check_not_zero(ResetEvent(h));
		}
		void set() {
			check_not_zero(SetEvent(h));
		}
		bool wait(uint64_t timeout) {
			DWORD r = WaitForSingleObject(h,timeout ? timeout/1000 : INFINITE);
			if (r==WAIT_OBJECT_0) return true;
			else if (r==WAIT_TIMEOUT) return false;
			else bad_return_value();
			return false;
		}
		bool try_wait() {
			DWORD r = WaitForSingleObject(h,0);
			if (r==WAIT_OBJECT_0) return true;
			else if (r==WAIT_TIMEOUT) return false;
			else bad_return_value();
			return false;
		}
		bool operator!() const {
			return h==0;
		}
		HANDLE native_handle() {
			return h;
		}
	};

}



