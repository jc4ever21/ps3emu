
#include <windows.h>
#include <process.h>

struct win32_thread {
	HANDLE h;
	static void sleep(uint32_t milliseconds) {
		Sleep(milliseconds);
	}
	static void exit(uint32_t exitcode) {
		_endthreadex(exitcode);
	}
	void join() {
		WaitForSingleObject(h,INFINITE);
	}
	static void yield() {
		SwitchToThread();
	}
	void close() {
		if (h) {CloseHandle(h);h=0;}
	}
	void suspend() {
		if (SuspendThread(h)==-1) xcept("SuspendThread failed; error %d",GetLastError());
	}
	void resume() {
		if (ResumeThread(h)==-1) xcept("ResumeThread failed; error %d",GetLastError());
	}
	void terminate() {
		if (!TerminateThread(h,-1)) xcept("TerminateThread failed; error %d",GetLastError());
	}
	HANDLE native_handle() {
		return h;
	}

	template<typename F>
	static unsigned __stdcall start_address(void*p) {
		F f = *(F*)p;
		delete (F*)p;
		f();
		return 0;
	}
	template<typename F>
	void start(F&&e) {
		close();
		F*ed = new F(std::forward<F>(e));
		auto r = _beginthreadex(0,0,&start_address<F>,(void*)ed,CREATE_SUSPENDED,0);
		if (!r||r==-1) {
			delete ed;
			xcept("_beginthreadex failed; error %d",errno);
		}
		h = (HANDLE)r;
		ResumeThread(h);
	}
	win32_thread() : h(0) {}
	win32_thread(win32_thread&&t) {
		h = t.h;
		t.h = 0;
	}
	win32_thread&operator=(win32_thread&&t) {
		h = t.h;
		t.h = 0;
		return *this;
	}
	~win32_thread() {
		close();
	}
	template<typename F>
	win32_thread(F&&f) : h(0) {
		start(std::forward<F>(f));
	}
	template<typename F,typename A1>
	win32_thread(F&&f,A1&&a1) : h(0) {
		start(boost::bind(std::forward<F>(f),std::forward<A1>(a1)));
	}
};
