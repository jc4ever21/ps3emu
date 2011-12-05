

struct thread_stack_info_t {
	uint32_t addr, size;
};

struct thread_attr_t {
	uint32_t entry, tls_addr;
};

// Thread IDs are unique across processes, but a process can only operate
// on threads that belong to itself. They are inter-process unique only to
// ease the implementation of interprocess synchronization.

struct ipc_thread_t {};
shm_object<ipc_list_t2<ipc_thread_t>> ipc_thread_list;

struct thread_t {
	int id;
	char name[28];
	bool joinable;
	uint64_t stack_size, stack_top, stack_bot, entry_addr, tls_addr;
	uint64_t arg, arg2;
	uint64_t exit_code;
	int32_t prio;
	bool is_running;
	win32_thread thread;
	int is_joining;
	ipc_list_t2<ipc_thread_t>::handle ipc_h;
	bool is_interrupt_thread;
	void*interrupt_thread;
	thread_t() : joinable(false), stack_size(0), stack_top(0), stack_bot(0), entry_addr(0),tls_addr(0), arg(0),
		is_running(false), is_joining(0), is_interrupt_thread(false), interrupt_thread(0) {
		memset(name,0,sizeof(name));
	}
};
__declspec(thread) thread_t*this_thread;
id_list_t<thread_t,0x60> real_thread_list;
id_list_ipc_proxy<id_list_t<thread_t,0x60>,ipc_list_t2<ipc_thread_t>,&thread_t::ipc_h,0x60> thread_list(&real_thread_list,0);
typedef id_list_ipc_proxy<id_list_t<thread_t,0x60>,ipc_list_t2<ipc_thread_t>,&thread_t::ipc_h,0x60>::handle thread_handle;
typedef id_list_ipc_proxy<id_list_t<thread_t,0x60>,ipc_list_t2<ipc_thread_t>,&thread_t::ipc_h,0x60>::handle_new thread_handle_new;
void ipc_thread_list_updater_func() {
 	thread_list = id_list_ipc_proxy<id_list_t<thread_t,0x60>,ipc_list_t2<ipc_thread_t>,&thread_t::ipc_h,0x60>(&real_thread_list,&*ipc_thread_list);
}
shm_cb ipc_thread_list_updater(&ipc_thread_list_updater_func);
// error C2065: 'thread_list' : undeclared identifier
// shm_cb ipc_thread_list_updater([]() {
// 	thread_list = id_list_ipc_proxy<id_list_t<thread_t>,ipc_list_t<ipc_thread_t>,&thread_t::ipc_h>(real_thread_list,*ipc_thread_list);
// });


int get_thread_id() {
	if (!this_thread) return -1;
	return this_thread->id;
}
std::pair<int,int> get_thread_id_and_priority() {
	thread_t*t = this_thread;
	return std::make_pair(t->id,t->prio);
}

void on_new_thread() {
	AddVectoredExceptionHandler(1,&mmio::filter);
}

thread_handle_new get_new_dummy_thread(int prio) {
	auto h = thread_list.get_new();
	if (!h) xcept("get_new_dummy_thread() failed");
	int id = h.id();
	thread_t*t = &*h;
	t->id = id;
	t->prio = prio;
	this_thread = t;
	h.keep();
	return h;
}

void init_main_thread() {
	auto h = get_new_dummy_thread(process_params.primary_prio);
	this_thread = &*h;
	h.keep();
	on_new_thread();
}

uint64_t alloc_main_stack() {
	size_t stacksize = process_params.primary_stacksize;
	void*p_stack = mm_alloc(stacksize+0x80);
	if (!p_stack) {
		xcept("failed to allocate stack of %x bytes",stacksize);
	}

	uint64_t sp = (uint64_t)p_stack;
	sp += stacksize - 16;
	sp &= -16;
	*(uint64_t*)sp = 0;
	sp -= 0x60;
	*(uint64_t*)sp = sp+0x60;

	thread_t*t = this_thread;
	t->stack_size = (uint64_t)stacksize;
	t->stack_top = (uint64_t)sp;
	t->stack_bot = (uint64_t)p_stack;

	return sp;
}

uint64_t (*I_thread_entry)(uint64_t stack_addr,uint64_t entry_addr,uint64_t tls_addr,uint64_t arg,uint64_t arg2);

void thread_entry(thread_t*t) {
	this_thread = t;
	on_new_thread();
	t->exit_code = 0;
	t->exit_code = I_thread_entry(t->stack_top,t->entry_addr,t->tls_addr,t->arg,t->arg2);
	dbgf("thread %d returned %#x\n",t->id,t->exit_code);
	atomic_write(&t->is_running,false);
	if (!atomic_read(&t->joinable)) {
		auto h = thread_list.get(t->id);
		h.kill();
	}
	win32_thread::exit(0);
}

void thread_sleep(uint32_t milliseconds) {
	win32_thread::sleep(milliseconds);
}

// threads are created in a suspended state, since liblv2 needs the thread_id before it can properly set up the tls data
// liblv2's sys_ppu_thread_create will call sys_ppu_thread_start

int sys_ppu_thread_create(uint64_t*thread_id,thread_attr_t*attr,uint64_t arg,uint64_t arg2,int prio,uint32_t stacksize,uint64_t flags,const char*name) {
	uint32_t entry = se(attr->entry);
	uint32_t tls_addr = se(attr->tls_addr);
	dbgf("ppu thread create (entry %#x, tls_mem_addr %#x), arg %#x, arg2 %#x, prio %d, stacksize %#x, flags %#x, name '%s'\n",entry,tls_addr,arg,arg2,prio,stacksize,flags,name);

	auto h = thread_list.get_new();
	if (!h || h.id()>=max_thread_id) {
		dbgf("thread_list.get_new() failed (or id(%d)>max_thread_id(%d)\n",h.id(),max_thread_id);
		return EAGAIN;
	}
	int id = h.id();

	void*p_stack = mm_alloc(stacksize+0x80);
	if (!p_stack) {
		dbgf("failed to allocate stack of %x bytes\n",stacksize);
		return ENOMEM;
	}

	uint64_t sp = (uint64_t)p_stack;
	sp += stacksize - 16;
	sp &= -16;
	*(uint64_t*)sp = 0;
	sp -= 0x60;
	*(uint64_t*)sp = sp+0x60;

	*thread_id = se((uint64_t)id);
	dbgf("created thread %d\n",id);

	thread_t*t = &*h;
	t->id = id;
	t->joinable = (flags&1)?true:false;

	t->is_interrupt_thread = (flags&2)?true:false;

	t->stack_size = (uint64_t)stacksize;
	t->stack_top = (uint64_t)sp;
	t->stack_bot = (uint64_t)p_stack;
	t->entry_addr = (uint64_t)entry;
	t->tls_addr = (uint64_t)tls_addr;
	t->arg = arg;
	t->arg2 = arg;
	t->prio = prio;
	
	h.keep();
	return CELL_OK;
}

int sys_ppu_thread_start(uint64_t thread_id) {
	dbgf("start thread %d\n",thread_id);
	auto h = thread_list.get((int)thread_id);
	if (!h) return ESRCH;
	thread_t*t = &*h;
	
	t->is_running = true;
	t->thread = win32_thread(&thread_entry,t);
	return CELL_OK;
}

int sys_ppu_thread_exit(uint64_t exit_code) {
	dbgf("exit thread with value %d (thread %d)\n",exit_code,get_thread_id());
	thread_t*t = this_thread;
	
	t->exit_code = exit_code;
	atomic_write(&t->is_running,false);
	if (!atomic_read(&t->joinable)) {
		auto h = thread_list.get(t->id);
		h.kill();
	}
	win32_thread::exit((uint32_t)exit_code);
	return CELL_OK;
}

int sys_ppu_thread_join(uint64_t thread_id,uint64_t*exit_code) {
	dbgf("join thread %d\n",thread_id);
	auto h = thread_list.get((int)thread_id);
	if (!h) return ESRCH;
	thread_t*t = &*h;
	if (!t->joinable) return EINVAL;
	if (t==this_thread) return EDEADLK;
	if (atomic_cas(&t->is_joining,0,1)!=0) return EINVAL;
	t->thread.join();
	atomic_write(&t->is_joining,0);
	*exit_code = se((uint64_t)t->exit_code);
	h.kill();
	return CELL_OK;
}

int sys_ppu_thread_get_stack_information(thread_stack_info_t*sp) {
	thread_t*t = this_thread;
	sp->addr = se((uint32_t)t->stack_bot);
	sp->size = se((uint32_t)t->stack_size);
	return CELL_OK;
}

int sys_ppu_thread_yield() {
	this_thread->thread.yield();
	return CELL_OK;
}

int sys_ppu_thread_detach(uint64_t thread_id) {
	auto h = thread_list.get(thread_id);
	if (!h) return ESRCH;
	thread_t*t = &*h;
	if (t->joinable) return EINVAL;
	if (t->is_joining) return EBUSY;
	atomic_write(&t->joinable,false);
	if (!atomic_read(&t->is_running)) h.kill();
	return CELL_OK;
}

int sys_ppu_thread_set_priority(uint64_t thread_id,int32_t prio) {
	auto h = thread_list.get(thread_id);
	if (!h) return ESRCH;
	h->prio = prio;
	return CELL_OK;
}
int sys_ppu_thread_get_priority(uint64_t thread_id,int32_t*prio) {
	auto h = thread_list.get(thread_id);
	if (!h) return ESRCH;
	*prio = se((int32_t)h->prio);
	return CELL_OK;
}
int sys_ppu_thread_get_join_state(int32_t*joinable) {
	*joinable = se((int32_t)(this_thread->joinable?1:0));
	return CELL_OK;
}




