
struct interrupt_thread_t;
struct interrupt_tag_t {
	int spu_id, class_id;
	sync::busy_lock busy_lock;
	std::vector<interrupt_thread_t*> interrupt_threads;
	bool dead;
	interrupt_tag_t() : spu_id(-1), dead(false) {}
};
id_list_t<interrupt_tag_t> interrupt_tag_list;
typedef id_list_t<interrupt_tag_t>::handle interrupt_tag_handle;

namespace spu_interrupt_tag_map {
	boost::shared_mutex mut;
	interrupt_tag_handle map[5*4];
	interrupt_tag_handle get(int spu_id,int class_id) {
		boost::shared_lock<boost::shared_mutex> l(mut);
		return map[spu_id*4+class_id];
	}
}

sync::busy_lock new_spu_interrupt_tag_lock;
std::pair<int,interrupt_tag_handle> interrupt_new_spu_tag(int spu_id,int class_id) {
	if (class_id!=0&&class_id!=2) return std::make_pair(EINVAL,interrupt_tag_handle());
	auto h = interrupt_tag_list.get_new();
	interrupt_tag_t&t = *h;
	t.spu_id = spu_id;
	t.class_id = class_id;
	boost::unique_lock<boost::shared_mutex> l(spu_interrupt_tag_map::mut);
	interrupt_tag_handle&tag_h = spu_interrupt_tag_map::map[spu_id*4+class_id];
	if (tag_h) return std::make_pair(EAGAIN,interrupt_tag_handle());
	tag_h = h;
	h.keep();
	return std::make_pair(CELL_OK,h);
}

int sys_interrupt_tag_destroy(uint32_t tag) {
	auto h = interrupt_tag_list.get(tag);
	if (!tag) return ESRCH;
	interrupt_tag_t&t = *h;
	sync::busy_locker l(t.busy_lock);
	if (!t.interrupt_threads.empty()) return EBUSY;
	if (t.spu_id!=-1) {
		boost::unique_lock<boost::shared_mutex> l(spu_interrupt_tag_map::mut);
		spu_interrupt_tag_map::map[t.spu_id*4+t.class_id].close();
	}
	h->dead = true;
	h.kill();
	return CELL_OK;
}

struct interrupt_thread_t;
__declspec(thread) interrupt_thread_t*this_interrupt_thread;
struct interrupt_thread_t {
	int id;
	interrupt_tag_handle tag_h;
	thread_handle thread_h;
	uint64_t arg1, arg2;
	uint32_t interrupt_count;
	bool is_running, stop_flag;
	win32_sync::ipc_event e;
	jmp_buf eoi_jmp;
	interrupt_thread_t() : interrupt_count(0), e(0), is_running(false), stop_flag(false) {}
	void start() {
		is_running = true;
		win32_thread([this]() {
			this_interrupt_thread = this;
			thread_t*t = &*thread_h;
			this_thread = t;
			while (!atomic_read(&stop_flag)) {
				if (!atomic_read(&interrupt_count)) this->e.wait(0);
				if (!interrupt_count) continue;
				atomic_dec(&interrupt_count);
				if (!setjmp(eoi_jmp)) I_thread_entry(t->stack_top,t->entry_addr,t->tls_addr,arg1,arg2);
			}
			is_running = false;
		});
	}
	void stop() {
		atomic_write(&stop_flag,true);
		e.signal();
		while (atomic_read(&is_running)) win32_thread::yield();
	}
	void eoi() {
		// The next line is to get longjmp to just restore the registers,
		// instead of calling RtlUnwindEx. Not only do we have nothing to
		// unwind, but RtlUnwindEx generates a STATUS_BAD_STACK exception for
		// some reason (maybe because it can't find unwind info for the 
		// caller). The longjmp works anyways (when not running under a
		// debugger), but we don't need the overhead.
		(uint64_t&)eoi_jmp = 0;
		longjmp(eoi_jmp,0);
	}
};
id_list_t<interrupt_thread_t> interrupt_thread_list;

void interrupt_trigger_spu(int spu_id,int class_id) {
	interrupt_tag_handle h = spu_interrupt_tag_map::get(spu_id,class_id);
	if (!h) return;
	sync::busy_locker l(h->busy_lock);
	auto&v = h->interrupt_threads;
	for (auto i=v.begin();i!=v.end();++i) {
		interrupt_thread_t*t = *i;
		int n = atomic_inc(&t->interrupt_count);
		t->e.signal();
	}
}

void interrupt_destroy_for_spu(int spu_id) {
	for (int i=0;i<4;i++) {
		boost::unique_lock<boost::shared_mutex> l(spu_interrupt_tag_map::mut);
		auto&tag_h = spu_interrupt_tag_map::map[spu_id*4+i];
		if (!tag_h) continue;
		auto h = tag_h;
		tag_h.close();
		l.unlock();
		sync::busy_locker l2(h->busy_lock);
		h->dead = true;
		h.kill();
		auto&v = h->interrupt_threads;
		for (auto i=v.begin();i!=v.end();++i) {
			interrupt_thread_t*t = *i;
			t->stop();
			interrupt_thread_list.get(t->id).kill();
		}
		v.clear();
	}
}

int sys_interrupt_thread_establish(uint32_t*id,uint32_t tag,uint64_t thread_id,uint64_t arg1,uint64_t arg2) {
	auto tag_h = interrupt_tag_list.get(tag);
	if (!tag_h) return ESRCH;
	auto thread_h = thread_list.get(thread_id);
	if (!thread_h) return ESRCH;
	if (!thread_h->is_interrupt_thread) return EINVAL;
	if (thread_h->interrupt_thread) return EAGAIN;
	
	auto h = interrupt_thread_list.get_new();
	if (!h) return EAGAIN;
	interrupt_thread_t&i = *h;
	i.id = h.id();
	i.tag_h = tag_h;
	i.thread_h = thread_h;
	i.arg1 = arg1;
	i.arg2 = arg2;

	{
		sync::busy_locker l(tag_h->busy_lock);
		if (tag_h->dead) return ESRCH;
		tag_h->interrupt_threads.push_back(&i);
	}

	i.start();

	*id = se((uint32_t)h.id());
	h.keep();
	return CELL_OK;
}
int sys_interrupt_thread_disestablish(uint32_t id) {
	auto h = interrupt_thread_list.get(id);
	{
		sync::busy_locker l(h->tag_h->busy_lock);
		auto&v = h->tag_h->interrupt_threads;
		for (auto i=v.begin();i!=v.end();++i) {
			if (*i==&*h) {
				v.erase(i);
				break;
			}
		}
	}
	h->stop();
	h.kill();
	return CELL_OK;
}
int sys_interrupt_thread_eoi() {
	this_interrupt_thread->eoi();
	xcept("unreachable: eoi returned");
	return CELL_OK;
}


