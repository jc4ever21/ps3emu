
struct spu_thread_attrib_t {
	uint32_t name, namelen, flags;
};

struct spu_thread_group_attrib_t {
	uint32_t namelen;
	uint32_t name;
	uint32_t type;
	uint32_t mem_container_id;
};

struct spu_image_t {
	uint32_t type;
	uint32_t entry_point;
	uint32_t seg_addr;
	uint32_t seg_n;
};

struct spu_image_segment_t {
	uint32_t type;
	uint32_t ls_addr;
	uint32_t size;
	uint32_t pad;
	uint32_t value;
	uint32_t pad2;
};

int sys_spu_initialize(uint32_t max_usable_spu,uint32_t max_raw_spu) {
	dbgf("spu initialize, max_usable_spu %d, max_raw_spu %d\n",max_usable_spu,max_raw_spu);
	return CELL_OK;
}

enum {
	SPU_RdSigNotify1 = 0x3, SPU_RdSigNotify2 = 0x4,
	SPU_WrOutMbox = 0x1c, SPU_WrOutIntrMbox = 0x1e, SPU_RdInMbox = 0x1d,
};

struct spu_thread_t;
struct spu_control;
__declspec(thread) spu_control*this_spu_control;
struct spu_control {
	int ref_count;
	sync::busy_lock thread_control_lock;
	spuc_handle spuc_h;
	uint32_t npc; // next program counter, lsb is (supposed to be) interrupt enabled/disabled
	uint64_t int0_mask, int0_stat, int2_mask, int2_stat;
	bool is_running;
	struct channel_data;
	channel_data*blocking_on_channel;
	bool is_suspended;
	int stop_count;
	win32_thread thread;
	CONTEXT thread_initial_context;
	bool thread_running, thread_ok_go;
	bool stopped_by_stop_and_signal;
	uint32_t stop_and_signal_type;
	int raw_spu_id;
	bool dead;
	win32_sync::event channel_wait_event;
	spu_thread_t*spu_thread;
	thread_t*set_this_thread;
	spu_control() : dead(false), ref_count(0), spuc_h(0), npc(0), is_running(false), blocking_on_channel(0), is_suspended(false), stop_count(0),
		thread_running(false), thread_ok_go(false), stopped_by_stop_and_signal(false), raw_spu_id(-1),
		int0_mask(~0), int0_stat(0), int2_mask(~0), int2_stat(0), chz(this), spu_thread(0), channel_wait_event(false,false), set_this_thread(0) {}
	~spu_control() {
		sync::busy_locker l(thread_control_lock);
		if (blocking_on_channel) xcept("unreachable: ~spu_control(), is_blocking_on_channel");
		if (thread_running) thread.terminate();
		if (spuc_h) spuc_close(spuc_h);
	}
	void set_args(uint64_t arg1,uint64_t arg2,uint64_t arg3,uint64_t arg4) {
		spuc_set_args(spuc_h,arg1,arg2,arg3,arg4);
	}
	void*spu_get_function(uint32_t offset) {
		sync::busy_locker l(thread_control_lock);
		return spuc_get_function(spuc_h,offset);
	}
	void init(void*ls) {
		spuc_h = spuc_open(ls,raw_spu_id!=-1);

		// We use Get/SetThreadContext to essentially trigger a setjmp/longjmp
		// from another thread, as an efficient way to implement SPU
		// start/stop behaviour, compared to spawning a new thread every
		// time the SPU is started.

		atomic_write(&thread_ok_go,false);
		thread.start([this]{
			this_spu_control = this;
			this_thread = set_this_thread;
			atomic_write(&thread_running,true);
			while(!atomic_read(&thread_ok_go));
			mem_read_barrier();
			void*f = spu_get_function(npc&-4);
			spuc_call(spuc_h,f);
			xcept("unreachable: spuc_call returned");
		});
		while(!atomic_read(&thread_running));
		thread.suspend();
		memset(&thread_initial_context,0,sizeof(thread_initial_context));
		thread_initial_context.ContextFlags = CONTEXT_ALL;
		if (!GetThreadContext(thread.native_handle(),&thread_initial_context)) xcept("GetThreadContext failed; error %d",GetLastError());
		atomic_write(&thread_ok_go,true);

	}
	void stop(uint32_t stop_and_signal_type_arg=-1,uint32_t npc_arg=-1) {
		sync::busy_locker l(thread_control_lock);
		if (dead) return;
		if (!is_running) return;
		if (blocking_on_channel) {
			xcept("fixme: spu stop while waiting on a blocked channel");
			// already have support for this, just need to write this block of code.
		}
		thread.suspend();
		atomic_dec(&ref_count);
		++stop_count;
		// Since it's impossible to determine the actual address we stopped the
		// SPU on, we set it to an invalid value we can check for if the SPU
		// is started again. We use stop_count to get a different value every
		// time. No good reason for this, but it feels right :).
		if (npc_arg==-1) npc = 0-(stop_count+1)*4;
		else npc = npc_arg;
		stopped_by_stop_and_signal = stop_and_signal_type_arg!=-1;
		stop_and_signal_type = stop_and_signal_type_arg;
		is_suspended = true;
		is_running = false;
		if (stopped_by_stop_and_signal && raw_spu_id!=-1) {
			if (int2_mask&2) {
				int2_stat |= 2;
				interrupt_trigger_spu(raw_spu_id,2);
			}
		}
	}
	void start() {
		sync::busy_locker l(thread_control_lock);
		if (dead) return;
		if (is_running) return;
		if (is_suspended) {
			// If npc has not been modified since we stopped, resume right
			// where we left off.
			if (npc==0-(stop_count+1)*4) {
				thread.resume();
				is_suspended = false;
				is_running = true;
				return;
			}
			is_suspended = false;
		}
		if (!SetThreadContext(thread.native_handle(),&thread_initial_context)) xcept("SetThreadContext failed; error %d",GetLastError());
		atomic_inc(&ref_count);
		thread_ok_go = true;
		thread.resume();
		is_running = true;
	}
	void restart(uint32_t npc) {
		sync::busy_locker l(thread_control_lock);
		if (dead) return;
		this->npc = npc;
		if (!is_running) return;
		thread.suspend();
		spuc_sync(spuc_h);
		if (!SetThreadContext(thread.native_handle(),&thread_initial_context)) xcept("SetThreadContext failed; error %d",GetLastError());
		thread_ok_go = true;
		thread.resume();
	}
	void kill() {
		sync::busy_locker l(thread_control_lock);
		dead = true;
		if (!is_running) return;
		if (blocking_on_channel) xcept("fixme: spu kill while waiting on a blocked channel");
		thread.suspend();
		atomic_dec(&ref_count);
		// The only thing that can happen from here on is object destruction,
		// so we don't care if the object is in an inconsistent state.
	}
	struct channel_data {
		spu_control*ctrl;
		bool spu_waiting;
		int maxsize, top, bot;
		static const int data_size = 0x10;
		uint32_t data[data_size];
		channel_data() : ctrl(0), maxsize(0), top(0), bot(0), spu_waiting(false) {}
		channel_data&resize(int size) {
			if (size<0||size>data_size) xcept("bad channel size");
			maxsize = size;
			return *this;
		}
		int push_size() {
			return maxsize - (top-bot)%(maxsize+1);
		}
		int pop_size() {
			return (top-bot)%(maxsize+1);
		}
		bool test_aborted() {
			if (!atomic_read(&ctrl->thread_ok_go)) {
				do busy_yield();
				while (!atomic_read(&ctrl->thread_ok_go));
				return true;
			} else return false;
		}
		void spu_wait() {
			spu_waiting = true;
			ctrl->blocking_on_channel = this;
			ctrl->thread_control_lock.unlock();
			do {
				ctrl->channel_wait_event.wait(0);
			} while (test_aborted());
			ctrl->thread_control_lock.lock();
		}
		// All functions assume the thread is holding ctrl->thread_control_lock
		void spu_push(uint32_t v) {
			if ((top-bot)%(maxsize+1)>=maxsize) spu_wait();
			data[top++%data_size] = v;
		}
		uint32_t spu_pop() {
			if (top==bot) spu_wait();
			return data[bot++%data_size];
		}
		void signal() {
			if (spu_waiting) {
				spu_waiting = false;
				ctrl->blocking_on_channel = 0;
				ctrl->channel_wait_event.set();
			}
		}
		bool ppu_push(uint32_t v) {
			if ((top-bot)%(maxsize+1)>=maxsize) return false;
			signal();
			data[top++%data_size] = v;
			return true;
		}
		std::pair<bool,uint32_t> ppu_pop() {
			if (top==bot) return std::make_pair(false,0);
			signal();
			return std::make_pair(true,data[bot++%data_size]);
		}
		void overwrite_top(uint32_t v) {
			data[top%data_size] = v;
			if (bot==top) {
				++top;
				signal();
			}
		}
		void or_top(uint32_t v) {
			if (bot==top) {
				data[top++%data_size] = v;
				signal();
			} else data[top%data_size] |= v;

		}
	};
	struct spu_channels {
		channel_data ch_WrOutMbox;
		channel_data ch_RdInMbox;
		channel_data ch_RdSigNotify1;
		channel_data ch_RdSigNotify2;
		channel_data ch_WrOutIntrMbox;
		bool snr_or[2];
		spu_channels(spu_control*ctrl) {
			snr_or[0] = snr_or[1] = false;
			ch_WrOutMbox.resize(0x10).ctrl = ctrl;
			ch_RdInMbox.resize(0x10).ctrl = ctrl;
			ch_RdSigNotify1.resize(1).ctrl = ctrl;
			ch_RdSigNotify2.resize(1).ctrl = ctrl;
			ch_WrOutIntrMbox.resize(1).ctrl = ctrl;
		}
		void write_snr(int n,uint32_t v) {
			if (snr_or[n]) (*this)[SPU_RdSigNotify1+n].or_top(v);
			else (*this)[SPU_RdSigNotify1+n].overwrite_top(v);
		}
		channel_data&operator[](int n) {
			switch (n) {
			case SPU_WrOutMbox: return ch_WrOutMbox;
			case SPU_RdInMbox: return ch_RdInMbox;
			case SPU_RdSigNotify1: return ch_RdSigNotify1;
			case SPU_RdSigNotify2: return ch_RdSigNotify2;
			case SPU_WrOutIntrMbox: return ch_WrOutIntrMbox;
			default:NODEFAULT;
			}
		}
	};
	spu_channels chz;
};

typedef ref_ptr<spu_control> spu_control_handle;

static struct spu_self_control {
	sync::busy_lock busy_lock;
	boost::mutex mut;
	boost::condition_variable cond;
	bool is_running;
	spu_control_handle stop_ctrl;
	uint32_t stop_and_signal_type;
	uint32_t next_pc;
	spu_self_control() : is_running(false) {}
	~spu_self_control() {
		is_running = false;
		cond.notify_all();
	}
	void run_thread() {
		boost::unique_lock<boost::mutex> l(mut);
		if (is_running) return;
		is_running = true;
		win32_thread([this]() {
			while (true) {
				boost::unique_lock<boost::mutex> l(mut);
				while (!stop_ctrl&&is_running) cond.wait(l);
				if (!is_running) break;
				if (stop_and_signal_type!=-1) stop_ctrl->stop(stop_and_signal_type,next_pc);
				else stop_ctrl->restart(next_pc);
				stop_ctrl.reset();
			}
		});
	}
	void do_control(spu_control*ctrl,uint32_t stop_and_signal_type_arg,uint32_t next_pc_arg) {
		// This function must be called from the SPU thread.
		// It must be suspended, but it cannot suspend itself, as that
		// would introduce a race-condition. So we use another thread
		// to do it.
		if (!is_running) run_thread();
		ctrl->thread_ok_go = false;
		while (true) {
			sync::busy_locker bl(ctrl->thread_control_lock);
			boost::unique_lock<boost::mutex> l(mut);
			if (stop_ctrl) {
				l.unlock();
				win32_thread::yield();
				continue;
			}
			next_pc = next_pc_arg;
			stop_and_signal_type = stop_and_signal_type_arg;
			stop_ctrl = spu_control_handle(ctrl);
			cond.notify_one();
			break;
		}
		// We should be suspended in this loop.
		while (!atomic_read(&ctrl->thread_ok_go)) busy_yield();
	}
	void stop(spu_control*ctrl,uint32_t stop_and_signal_type_arg,uint32_t next_pc_arg=-1) {
		do_control(ctrl,stop_and_signal_type_arg,next_pc_arg);
	}
	void restart(spu_control*ctrl,uint32_t offset) {
		do_control(ctrl,-1,offset);
	}
} spu_self_control;

const unsigned int ls_size = 1024*256;
struct spu_thread_t;
struct spu_thread_group_t {
	enum {state_not_initialized,state_initialized,state_running,state_suspended};
	int state;
	int initialized_count;
	sync::busy_lock control_lock;
	boost::unordered_map<int,spu_thread_t*> threads;
	int size;
	char*ls_data;
	int running_threads;
	win32_sync::event terminated_event;
	int is_joining, joining_ref;
	enum {exit_none,exit_group_exit,exit_threads_exit,exit_terminated};
	int exit_cause;
	uint32_t exit_value;
	int prio;
	event_queue_handle eq_run, eq_exception;
	int id;
	spu_thread_group_t() : ls_data(0), initialized_count(0), running_threads(0), terminated_event(true,false), is_joining(0), joining_ref(0), exit_cause(exit_none) {}
	~spu_thread_group_t() {
		if (ls_data) sys_memory_free((uint32_t)ls_data);
	}
	void collect_threads();
};

id_list_t<spu_thread_group_t> spu_thread_group_list;
typedef id_list_t<spu_thread_group_t>::handle spu_thread_group_handle;

struct spu_thread_t {
	char*ls;
	uint32_t entry;
	uint64_t arg1,arg2,arg3,arg4;
	spu_thread_group_handle group;
	int id;
	int num;
	sync::busy_lock event_ports_lock;
	event_queue_handle event_ports[0x40];
	sync::busy_lock bound_queues_lock;
	std::map<uint32_t,event_queue_handle> bound_queues;
	uint32_t exit_code;
	thread_handle dummy_thread_handle; // This is just to get event queues to work.
	spu_control_handle ctrl;
	bool blocking_on_eq_receive;
	spu_thread_t() : blocking_on_eq_receive(false) {}
	void write_snr(int n,uint32_t v) {
		sync::busy_locker l(ctrl->thread_control_lock);
		ctrl->chz.write_snr(n,v);
	}
};

id_list_t<spu_thread_t> spu_thread_list;
typedef id_list_t<spu_thread_t>::handle spu_thread_handle;

void spu_thread_group_t::collect_threads() {
	// control_lock is assumed to be held
	if (exit_cause!=exit_none) return;
	if (running_threads==0) {
		while (true) {
			bool all_stopped = true;
			for (auto i=threads.begin();i!=threads.end();++i) {
				sync::busy_locker l(i->second->ctrl->thread_control_lock);
				if (i->second->ctrl->is_running) {
					all_stopped = false;
					break;
				}
			}
			if (all_stopped) break;
			win32_thread::yield();
		}
		exit_cause = exit_threads_exit;
		state = state_initialized;
	}
}

int sys_spu_thread_group_create(uint32_t*id,uint32_t size,int32_t prio,spu_thread_group_attrib_t*attr) {
	const char*name = (const char*)se(attr->name);
	int namelen = se(attr->namelen);
	int type = se(attr->type);
	int mem_container_id = se(attr->mem_container_id);
	dbgf("spu thread group create '%.*s', size %d, prio %d, type %d, mem_container_id %d\n",namelen,name,size,prio,type,mem_container_id);

	auto h = spu_thread_group_list.get_new();
	if (!h) return EAGAIN;
	spu_thread_group_t*g = &*h;
	g->size = size;
	uint32_t mem_size = (ls_size*size-1&~(1024*1024-1))+1024*1024;
	if (type==4) { // allocate memory from container
		uint32_t addr;
		int r = sys_memory_allocate_from_container(mem_size,mem_container_id,0x400,&addr);
		if (r!=CELL_OK) return ENOMEM;
		g->ls_data = (char*)se(addr);
	} else {
		uint32_t addr;
		int r = sys_memory_allocate(mem_size,0x400,&addr);
		if (r!=CELL_OK) return ENOMEM;
		g->ls_data = (char*)se(addr);
	}
	if ((uint64_t)g->ls_data%0x80) xcept("ls_data is not 128-byte aligned");
	g->state = spu_thread_group_t::state_not_initialized;
	g->prio = prio;
	g->id = h.id();
	*id = se((uint32_t)h.id());
	h.keep();
	return CELL_OK;
}

#include "elf.h"

enum {
	spu_segment_copy = 1,
	spu_segment_fill = 2,
	spu_segment_info = 4,
	spu_image_user_allocated = 0,
	spu_image_kernel_allocated = 1,
};

int sys_spu_image_open(spu_image_t*img,const char*path) {

	dbgf("spu image open %s\n",path);

	file_t f;
	f.set_fn(path);
	std::string s = f.host_fn;
	size_t p = s.rfind(".self");
	if (p!=std::string::npos) {
		s = s.substr(0,p) + ".elf";
	}

	spu_image_segment_t*seg;
	int seg_n = 0;

	auto addseg = [&](int type,uint32_t ls_addr,uint32_t size,uint32_t addr_or_value) {
		spu_image_segment_t s;
		s.type = se((uint32_t)type);
		s.ls_addr = se((uint32_t)ls_addr);
		s.size = se((uint32_t)size);
		s.value = se((uint32_t)addr_or_value);
		if (seg_n>=32) xcept("spu image open: too many segments");
		seg[seg_n++] = s;
	};

	char*file_data;
	size_t file_size;
	{
		const char*fn = s.c_str();
		FILE*f = fopen(fn,"rb");
		if (!f) xcept("failed to open %s\n",fn);
		fseek(f,0,SEEK_END);
		long fs = ftell(f);
		fseek(f,0,SEEK_SET);

		char*p = (char*)mm_alloc(fs + sizeof(spu_image_segment_t)*32);
		if (!p) {
			fclose(f);
			return ENOMEM;
		}

		seg = (spu_image_segment_t*)p;
		file_size = fs;
		file_data = p + sizeof(spu_image_segment_t)*32;

		if (fread(file_data,1,fs,f)!=fs) xcept("bad read");
		fclose(f);
	}

	elf32 elf;
	elf.data = file_data;
	elf.data_size = file_size;
	elf.parse([&](int type,uint32_t vaddr,uint32_t filesz,uint32_t memsz,char*data) {
		if (type==1) { // PT_LOAD
			if (filesz<16 || filesz%16) xcept("spu image open: filesz less than 16 or not a multiple of 16 bytes");
			if (filesz>memsz) xcept("spu image open: filesz>memsz");
			size_t zerosize = memsz-filesz;
			if (filesz) addseg(spu_segment_copy,vaddr,filesz,(uint32_t)data);
			if (zerosize) addseg(spu_segment_fill,vaddr+filesz,zerosize,0);
		} else if (type==4) { // PT_NOTE
			for (uint32_t i=0;i<filesz;) {
				if (filesz-i<4+4+4+8) break;
				uint32_t namesz = se((uint32_t&)data[i]);
				uint32_t descsz = se((uint32_t&)data[i+4]);
				uint32_t type   = se((uint32_t&)data[i+4+4]);
				char*name = &data[i+4+4+4];
				char*desc = &data[i+4+4+4+8];
				if (!memcmp(name,"SPUNAME",8)) {
					addseg(spu_segment_info,vaddr,descsz,(uint32_t)desc);
				}
				i += 4+4+4+namesz+descsz;
			}
		} else {
			xcept("spu image open: unknown segment type");
		}
	});

	img->entry_point = se((uint32_t)elf.entry);
	img->seg_n = se((int32_t)seg_n);
	img->seg_addr = se((uint32_t)seg);
	img->type = se((uint32_t)spu_image_kernel_allocated);

	return CELL_OK;
}

int sys_spu_image_close(spu_image_t*img) {
	dbgf("spu image close\n");
	uint32_t type = se(img->type);
	if (type!=spu_image_kernel_allocated) xcept("attempt to close spu image not allocated by kernel");
	mm_free((void*)se(img->seg_addr));
	return CELL_OK;
}

int sys_spu_thread_initialize(uint32_t*thread_id,uint32_t group,unsigned int spu_num,spu_image_t*img,spu_thread_attrib_t*attr,uint64_t*arg) {
	const char*name = (const char*)se(attr->name);
	int namelen = se(attr->namelen);
	dbgf("spu thread initialize '%.*s', group %d, spu_num %d\n",namelen,name,group,spu_num);

	auto h = spu_thread_list.get_new();
	if (!h) return EAGAIN;
	spu_thread_t*t = &*h;
	
	auto h_g = spu_thread_group_list.get(group);
	if (!h_g) return ESRCH;
	spu_thread_group_t*g = &*h_g;

	if ((int)spu_num<0||(int)spu_num>255) return EINVAL;
	sync::busy_locker l(g->control_lock);
	if (g->initialized_count>=g->size) return EBUSY;
	auto i = g->threads.insert(std::make_pair(spu_num,t));
	if (!i.second) return EBUSY;

	t->id = h.id();
	t->group = h_g;
	t->num = spu_num;
	t->dummy_thread_handle = get_new_dummy_thread(process_params.primary_prio);

	dbgf("g->ls_data is %p\n",g->ls_data);

	t->ls = &g->ls_data[ls_size*g->initialized_count];
	memset(t->ls,0,ls_size);

	dbgf("t->ls is %p\n",t->ls);

	if ((uint64_t)t->ls&ls_size-1) xcept("ls not aligned to ls_size (%d bytes)!",ls_size);

	auto ctrl_h = spu_control_handle(new spu_control());
	t->ctrl = ctrl_h;
	t->ctrl->spu_thread = t;
	t->ctrl->set_this_thread = &*t->dummy_thread_handle;
	t->ctrl->init(t->ls);

	int seg_n = se(img->seg_n);
	uint32_t type = se(img->type);
	uint32_t entry_point = se(img->entry_point);
	dbgf("type is %d, entry_point is %#x, nsegs is %d\n",type,entry_point,seg_n);

	spu_image_segment_t*seg = (spu_image_segment_t*)se(img->seg_addr);
	for (int i=0;i<seg_n;i++) {
		int type = se(seg->type);
		uint32_t ls_start = se(seg->ls_addr);
		uint32_t size = se(seg->size);
		dbgf("seg %d: type %d, ls_start %#x, size %#x\n",i,type,ls_start,size);
		seg++;
	}

	std::string image_name;
	size_t image_size = 0;

	seg = (spu_image_segment_t*)se(img->seg_addr);
	for (int i=0;i<seg_n;i++) {
		int type = se(seg->type);
		uint32_t ls_start = se(seg->ls_addr);
		uint32_t size = se(seg->size);
		void*src = (void*)se(seg->value);
		uint32_t val = se(seg->value);

		if (type==spu_segment_copy) {
			if (image_size<ls_start+size) image_size=ls_start+size;
			if (ls_size<ls_start+size) xcept("spu program does not fit in local storage (%u bytes)",ls_size);
			memcpy(&t->ls[ls_start],src,size);
		} else if (type==spu_segment_fill) {
			if (image_size<ls_start+size) image_size=ls_start+size;
			if (ls_size<ls_start+size) xcept("spu program does not fit in local storage (%u bytes)",ls_size);
			if (size%4) xcept("size not multiple of 4 for fill");
			for (uint32_t i=0;i<size;i+=4) {
				(uint32_t&)t->ls[ls_start+i] = val;
			}
		} else if (type==spu_segment_info) {
			image_name.assign((const char*)src,strnlen((const char*)src,size));
		} else xcept("bad segment type %d",type);
		
		seg++;
	}

	dbgf("image name is %s\n",image_name.c_str());

	t->entry = entry_point;

	t->arg1 = se(arg[0]);
	t->arg2 = se(arg[1]);
	t->arg3 = se(arg[2]);
	t->arg4 = se(arg[3]);

	g->initialized_count++;
	if (g->initialized_count==g->size) g->state = spu_thread_group_t::state_initialized;

	*thread_id = se((uint32_t)h.id());
	h.keep();
	return CELL_OK;
}

int sys_spu_thread_get_exit_status(uint32_t id,int32_t*status) {
	dbgf("spu thread get exit status %d\n",id);
	auto h = spu_thread_list.get(id);
	if (!h) return ESRCH;
	spu_thread_t*t = &*h;
	sync::busy_locker l(t->ctrl->thread_control_lock);
	if (!t->ctrl->stopped_by_stop_and_signal || t->ctrl->stop_and_signal_type!=0x102) return ESTAT;
	*status = se((int32_t)t->exit_code);
	return CELL_OK;
}

int sys_spu_thread_connect_event(uint32_t thread_id,uint32_t eq_id,uint32_t et,uint8_t spup) {
	dbgf("spu thread connect event thread %d, eq %d, et %d, spup %d\n",thread_id,eq_id,et,spup);
	if (et!=1) xcept("et!=1");
	if (spup>63) return EINVAL;
	auto th = spu_thread_list.get(thread_id);
	if (!th) return ESRCH;
	auto eqh = event_queue_list.get(eq_id);
	if (!eqh) return ESRCH;

	spu_thread_t*t = &*th;
	impl_event_queue_t*eq = &*eqh;
	sync::busy_locker l(t->event_ports_lock);
	auto&port = t->event_ports[spup];
	if (port) return EISCONN;
	port = eqh;

	return CELL_OK;
}
int sys_spu_thread_disconnect_event(uint32_t id,uint32_t et,uint8_t spup) {
	dbgf("spu thread disconnect event thread %d, et %d, spup %d\n",id,et,spup);
	if (et!=1) xcept("et!=1");
	auto h = spu_thread_list.get(id);
	if (!h) return ESRCH;
	spu_thread_t*t = &*h;
	sync::busy_locker l(t->event_ports_lock);
	auto&port = t->event_ports[spup];
	if (!port) return ENOTCONN;
	port.close();
	return CELL_OK;
}

int sys_spu_thread_bind_queue(uint32_t thread_id,uint32_t eq_id,uint32_t spuq) {
	dbgf("spu thread bind queue thread %d, eq %d, spuq %d\n",thread_id,eq_id,spuq);
	auto th = spu_thread_list.get(thread_id);
	if (!th) return ESRCH;
	auto eqh = event_queue_list.get(eq_id);
	if (!eqh) return ESRCH;

	spu_thread_t*t = &*th;
	impl_event_queue_t*eq = &*eqh;

	sync::busy_locker l(t->bound_queues_lock);
	if (t->bound_queues.size()>=32) return EAGAIN;
	auto r = t->bound_queues.insert(std::make_pair(spuq,eqh));
	if (!r.second) return EBUSY;

	return CELL_OK;
}
int sys_spu_thread_unbind_queue(uint32_t id,uint32_t spuq) {
	dbgf("spu thread unbind queue %d, spuq %d\n",id,spuq);
	auto h = spu_thread_list.get(id);
	if (!h) return ESRCH;
	spu_thread_t*t = &*h;
	sync::busy_locker l(t->bound_queues_lock);
	auto i = t->bound_queues.erase(spuq);
	if (!i) return EINVAL;
	return CELL_OK;
}


int sys_spu_thread_write_snr(uint32_t id,int32_t n,uint32_t val) {
	dbgf("spu thread write snr thread %d, n %d, val %#x\n",id,n,val);
	auto h = spu_thread_list.get(id);
	if (!h) return ESRCH;
	spu_thread_t*t = &*h;
	if (n!=0&&n!=1) return EINVAL;
	t->write_snr(n,val);
	return CELL_OK;
}

int sys_spu_thread_write_ls(uint32_t id,uint32_t address,uint64_t value,uint32_t size) {
	dbgf("spu thread write ls thread %d, address %#x, value %#x, size %d\n",id,address,value,size);
	auto h = spu_thread_list.get(id);
	if (!h) return ESRCH;
	spu_thread_t*t = &*h;
	if (t->group->state!=spu_thread_group_t::state_running) return ESTAT;
	if (address>=ls_size) return EINVAL;
	if (size>1 && address&(size-1)) return EINVAL;
	switch (size) {
	case 1:
		(uint8_t&)t->ls[address] = (uint8_t)value;
		break;
	case 2:
		(uint16_t&)t->ls[address] = se((uint16_t)value);
		break;
	case 4:
		(uint32_t&)t->ls[address] = se((uint32_t)value);
		break;
	case 8:
		(uint64_t&)t->ls[address] = se((uint64_t)value);
		break;
	default:
		return EINVAL;
	}
	return CELL_OK;
}
int sys_spu_thread_read_ls(uint32_t id,uint32_t address,uint64_t*value,uint32_t size) {
	dbgf("spu thread read ls thread %d, address %#x, size %d\n",id,address,size);
	auto h = spu_thread_list.get(id);
	if (!h) return ESRCH;
	spu_thread_t*t = &*h;
	if (t->group->state!=spu_thread_group_t::state_running) return ESTAT;
	if (address>=ls_size) return EINVAL;
	if (size>1 && address&(size-1)) return EINVAL;
	switch (size) {
	case 1:
		*value = se((uint64_t)(uint8_t&)t->ls[address]);
		break;
	case 2:
		*value = se((uint64_t)se((uint16_t&)t->ls[address]));
		break;
	case 4:
		*value = se((uint64_t)se((uint32_t&)t->ls[address]));
		break;
	case 8:
		*value = se((uint64_t)se((uint64_t&)t->ls[address]));
		break;
	default:
		return EINVAL;
	}
	return CELL_OK;
}

int sys_spu_thread_write_spu_mb(uint32_t id,uint32_t value) {
	dbgf("spu thread write mb thread %d, value %#x\n",id,value);
	auto h = spu_thread_list.get(id);
	if (!h) return ESRCH;
	spu_thread_t*t = &*h;
	sync::busy_locker l(t->ctrl->thread_control_lock);
	if (!t->ctrl->chz[SPU_RdInMbox].ppu_push(value)) {
		// No idea what this is supposed to do when the mailbox is full.
		// Since we don't support blocking on the PPU side for this,
		// we'll return EBUSY for now.
		xcept("sys_spu_thread_write_spu_mb failed (return EBUSY?)");
		return EBUSY;
	}
	return CELL_OK;
}

int sys_spu_thread_get_spu_cfg(uint32_t id,uint64_t*val) {
	dbgf("spu thread get spu cfg %d\n",id);
	auto h = spu_thread_list.get(id);
	if (!h) return ESRCH;
	spu_thread_t&t = *h;
	*val = se((uint64_t)((t.ctrl->chz.snr_or[0]?1:0)|(t.ctrl->chz.snr_or[1]?2:0)));
	return CELL_OK;
}
int sys_spu_thread_get_spu_cfg(uint32_t id,uint64_t val) {
	dbgf("spu thread get spu cfg %d, val %#x\n",id,val);
	auto h = spu_thread_list.get(id);
	if (!h) return ESRCH;
	spu_thread_t&t = *h;
	t.ctrl->chz.snr_or[0] = val&1?true:false;
	t.ctrl->chz.snr_or[1] = val&2?true:false;
	return CELL_OK;
}

int sys_spu_thread_group_start(uint32_t id) {
	dbgf("spu thread group start %d\n",id);

	auto h = spu_thread_group_list.get(id);
	if (!h) return ESRCH;
	spu_thread_group_t*g = &*h;
	sync::busy_locker l(g->control_lock);
	g->collect_threads();
	if (g->state!=spu_thread_group_t::state_initialized) return ESTAT;
	
	g->state = spu_thread_group_t::state_running;
	g->exit_cause = spu_thread_group_t::exit_none;
	g->terminated_event.reset();
	for (auto i=g->threads.begin();i!=g->threads.end();++i) {
		g->running_threads++;
		spu_thread_t*t = i->second;
		t->ctrl->npc = t->entry;
		t->ctrl->set_args(t->arg1,t->arg2,t->arg3,t->arg4);
		t->ctrl->start();
	}
	if (g->eq_run) {
		event_t d;
		d.source = se((uint64_t)0xFFFFFFFF53505500);
		d.data1 = se((uint64_t)g->id);
		d.data2 = 0;
		d.data3 = 0;
		g->eq_run->send(d);
	}

	return CELL_OK;
}

int sys_spu_thread_group_join(uint32_t id,int32_t*cause,int32_t*status) {
	dbgf("spu thread group join %d\n",id);
	auto h = spu_thread_group_list.get(id);
	if (!h) return ESRCH;
	spu_thread_group_t*g = &*h;

	if (atomic_xchg(&g->is_joining,1)) return EBUSY;

	g->terminated_event.wait(0);
	sync::busy_lock l(g->control_lock);
	g->collect_threads();

	switch (g->exit_cause) {
	case spu_thread_group_t::exit_threads_exit:
		*cause = se((int32_t)2);
		break;
	case spu_thread_group_t::exit_group_exit:
		*cause = se((uint32_t)1);
		break;
	case spu_thread_group_t::exit_terminated:
		*cause = se((uint32_t)4);
	default:
		xcept("bad g->exit_cause (%d)",g->exit_cause);
	}
	*status = se((int32_t)g->exit_value);

	return CELL_OK;
}

int sys_spu_thread_group_destroy(uint32_t id) {
	dbgf("spu thread group destroy %d\n",id);
	auto h = spu_thread_group_list.get(id);
	if (!h) return ESRCH;
	spu_thread_group_t*g = &*h;
	sync::busy_locker l(g->control_lock);
	g->collect_threads();
	if (g->state!=spu_thread_group_t::state_initialized&&g->state!=spu_thread_group_t::state_not_initialized) return EBUSY;

	for (auto i=g->threads.begin();i!=g->threads.end();++i) {
		spu_thread_t*t = i->second;
		spu_thread_list.get(t->id).kill();
	}

	h.kill();

	return CELL_OK;
}

int sys_spu_thread_group_terminate(uint32_t id,uint32_t exit_val) {
	dbgf("spu thread group terminate %d, exit_val %d\n",id,exit_val);
	auto h = spu_thread_group_list.get(id);
	if (!h) return ESRCH;
	spu_thread_group_t&g = *h;
	sync::busy_locker l(g.control_lock);
	if (g.state==spu_thread_group_t::state_not_initialized||g.state==spu_thread_group_t::state_initialized) return ESTAT;
	for (auto i=g.threads.begin();i!=g.threads.end();++i) {
		spu_thread_t&t = *i->second;
		sync::busy_locker l(t.ctrl->thread_control_lock);
		{
			sync::busy_locker l(t.ctrl->thread_control_lock);
			if (t.blocking_on_eq_receive) xcept("fixme: spu thread group terminate, blocking_on_eq_receive");
			// also do something so it can't start blocking on eq receive again before we stop it
		}
		t.ctrl->stop();
	}
	g.running_threads = 0;
	g.state = spu_thread_group_t::state_initialized;
	g.exit_cause = spu_thread_group_t::exit_terminated;
	g.exit_value = exit_val;
	g.terminated_event.set();
	return CELL_OK;
}
int sys_spu_thread_group_suspend(uint32_t id) {
	dbgf("spu thread group suspend %d\n",id);
	auto h = spu_thread_group_list.get(id);
	if (!h) return ESRCH;
	spu_thread_group_t&g = *h;
	sync::busy_locker l(g.control_lock);
	if (g.state==spu_thread_group_t::state_not_initialized||g.state==spu_thread_group_t::state_initialized) return ESTAT;
	for (auto i=g.threads.begin();i!=g.threads.end();++i) {
		spu_thread_t&t = *i->second;
		{
			sync::busy_locker l(t.ctrl->thread_control_lock);
			if (t.blocking_on_eq_receive) xcept("fixme: spu thread group suspend, blocking_on_eq_receive");
			// also do something so it can't start blocking on eq receive again before we stop it
		}
		t.ctrl->stop();
	}
	g.state = spu_thread_group_t::state_suspended;
	return CELL_OK;
}
int sys_spu_thread_group_resume(uint32_t id) {
	dbgf("spu thread group resume %d\n",id);
	auto h = spu_thread_group_list.get(id);
	if (!h) return ESRCH;
	spu_thread_group_t&g = *h;
	sync::busy_locker l(g.control_lock);
	if (g.state==spu_thread_group_t::state_suspended) return ESTAT;
	for (auto i=g.threads.begin();i!=g.threads.end();++i) {
		spu_thread_t&t = *i->second;
		sync::busy_locker l(t.ctrl->thread_control_lock);
		t.ctrl->start();
	}
	g.state = spu_thread_group_t::state_running;
	return CELL_OK;
}
int sys_spu_thread_group_yield(uint32_t id) {
	dbgf("spu thread group yield %d\n",id);
	// No support for this.
	return CELL_OK;
}

int sys_spu_thread_group_set_priority(uint32_t id,int32_t prio) {
	dbgf("spu thread group set priority %d, prio %d\n",id,prio);
	auto h = spu_thread_group_list.get(id);
	if (!h) return ESRCH;
	h->prio = prio;
	return CELL_OK;
}
int sys_spu_thread_group_get_priority(uint32_t id,int32_t*prio) {
	dbgf("spu thread group get priority %d\n",id);
	auto h = spu_thread_group_list.get(id);
	if (!h) return ESRCH;
	*prio = se((int32_t)h->prio);
	return CELL_OK;
}
int sys_spu_thread_group_connect_event(uint32_t id,uint32_t eq_id,uint32_t type) {
	dbgf("spu thread group connect event %d, eq %d, type %d\n",id,eq_id,type);
	auto h = spu_thread_group_list.get(id);
	if (!h) return ESRCH;
	auto eqh = event_queue_list.get(eq_id);
	if (!eqh) return ESRCH;
	spu_thread_group_t&g = *h;
	event_queue_handle*p = 0;
	if (type==1) p = &g.eq_run;
	else if (type==2) p = &g.eq_exception;
	else return EINVAL;
	sync::busy_locker l(g.control_lock);
	if (*p) return EBUSY;
	*p = eqh;
	return CELL_OK;
}
int sys_spu_thread_group_disconnect_event(uint32_t id,uint32_t type) {
	dbgf("spu thread group disconnect event %d, type %d\n",id,type);
	auto h = spu_thread_group_list.get(id);
	if (!h) return ESRCH;
	spu_thread_group_t&g = *h;
	event_queue_handle*p = 0;
	if (type==1) p = &g.eq_run;
	else if (type==2) p = &g.eq_exception;
	else return EINVAL;
	sync::busy_locker l(g.control_lock);
	if (!*p) return ENOTCONN;
	p->close();
	return CELL_OK;
}
int sys_spu_thread_group_connect_event_all_threads(uint32_t id,uint32_t eq_id,uint64_t mask,uint8_t*port_num) {
	dbgf("spu thread group connect event all threads %d, eq %d, mask %#x\n",id,eq_id,mask);
	auto h = spu_thread_group_list.get(id);
	if (!h) return ESRCH;
	auto eqh = event_queue_list.get(eq_id);
	if (!eqh) return ESRCH;
	spu_thread_group_t&g = *h;
	if (!mask) return EINVAL;
	sync::busy_locker l(g.control_lock);
	for (auto i=g.threads.begin();i!=g.threads.end();++i) {
		i->second->event_ports_lock.lock();
	}
	int n = -1;
	for (int pn=0;pn<64;pn++) {
		if (~mask&((uint64_t)1<<pn)) continue;
		bool okay = true;
		for (auto i=g.threads.begin();i!=g.threads.end();++i) {
			auto&port = i->second->event_ports[pn];
			if (port) {
				okay=false;
				break;
			}
		}
		if (okay) {
			n = pn;
			break;
		}
	}
	if (n!=-1) {
		for (auto i=g.threads.begin();i!=g.threads.end();++i) {
			auto&port = i->second->event_ports[n];
			port = eqh;
		}
	}
	for (auto i=g.threads.begin();i!=g.threads.end();++i) {
		i->second->event_ports_lock.unlock();
	}
	if (n==-1) return EISCONN;
	*port_num = n;
	return CELL_OK;
}
int sys_spu_thread_group_disconnect_event_all_threads(uint32_t id,uint8_t port_num) {
	dbgf("spu thread group disconnect event all threads %d, port %d\n",id,port_num);
	auto h = spu_thread_group_list.get(id);
	if (!h) return ESRCH;
	spu_thread_group_t&g = *h;
	if (port_num>63) return EINVAL;
	sync::busy_locker l(g.control_lock);
	for (auto i=g.threads.begin();i!=g.threads.end();++i) {
		sync::busy_locker l(i->second->event_ports_lock);
		auto&port = i->second->event_ports[port_num];
		if (port) port.close();
	}
	return CELL_OK;
}

struct raw_spu_t {
	int in_use;
	char*ls;
	spu_control_handle ctrl;
	uint32_t mfc_lsa, mfc_eah, mfc_eal, mfc_size, mfc_tagid;
	uint32_t proxy_query_type, proxy_query_mask;
	uint32_t run_cntl;
	raw_spu_t() : ls(0),
		mfc_lsa(0), mfc_eah(0), mfc_eal(0), mfc_size(0), mfc_tagid(0),
		proxy_query_type(0), proxy_query_mask(0),
		run_cntl(0) {}
	~raw_spu_t() {
		if (ls) mm_free(ls);
	}
};

id_list_t<raw_spu_t,0> raw_spu_list;

int sys_raw_spu_create(uint32_t*id,void*opt) {
	dbgf("raw spu create\n");
	auto h = raw_spu_list.get_new();
	if (!h) return EAGAIN;
	if ((unsigned int)h.id()>5) return EAGAIN;
	raw_spu_t&s = *h;
	auto ctrl_h = spu_control_handle(new spu_control());
	s.ctrl = ctrl_h;
	s.ctrl->raw_spu_id = h.id();
	s.ls = (char*)mm_alloc(ls_size);
	if (!s.ls) return ENOMEM;
	s.ctrl->init(s.ls);
	*id = se((uint32_t)h.id());
	h.keep();
	dbgf("created raw spu with id %d\n",h.id());
	return CELL_OK;
}
int sys_raw_spu_destroy(uint32_t id) {
	dbgf("raw spu destroy %d\n",id);
	auto h = raw_spu_list.get(id);
	if (!h) return ESRCH;
	h.kill();
	raw_spu_t&s = *h;
	s.ctrl->kill();
	interrupt_destroy_for_spu(id);
	return CELL_OK;
}
int sys_raw_spu_set_int_mask(uint32_t id,uint32_t class_id,uint64_t mask) {
	dbgf("raw spu set int mask %d, class %d, mask %#x\n",id,class_id,mask);
	auto h = raw_spu_list.get(id);
	if (!h) return ESRCH;
	if (class_id==0) h->ctrl->int0_mask = mask;
	else if (class_id==2) h->ctrl->int2_mask = mask;
	else return EINVAL;
	return CELL_OK;
}
int sys_raw_spu_get_int_mask(uint32_t id,uint32_t class_id,uint64_t*mask) {
	dbgf("raw spu get int mask %d, class %d\n",id,class_id);
	auto h = raw_spu_list.get(id);
	if (!h) return ESRCH;
	if (class_id==0) *mask = se((uint64_t)h->ctrl->int0_mask);
	else if (class_id==2) *mask = se((uint64_t)h->ctrl->int2_mask);
	else return EINVAL;
	return CELL_OK;
}
int sys_raw_spu_set_int_stat(uint32_t id,uint32_t class_id,uint64_t stat) {
	dbgf("raw spu set int stat %d, class %d, mask %#x\n",id,class_id,stat);
	auto h = raw_spu_list.get(id);
	if (!h) return ESRCH;
	if (class_id==0) h->ctrl->int0_stat &= ~stat;
	else if (class_id==2) h->ctrl->int2_stat &= ~stat;
	else return EINVAL;
	return CELL_OK;
}
int sys_raw_spu_get_int_stat(uint32_t id,uint32_t class_id,uint64_t*stat) {
	dbgf("raw spu get int stat %d, class %d\n",id,class_id);
	auto h = raw_spu_list.get(id);
	if (!h) return ESRCH;
	if (class_id==0) *stat = se((uint64_t)h->ctrl->int0_stat);
	else if (class_id==2) *stat = se((uint64_t)h->ctrl->int2_stat);
	else return EINVAL;
	return CELL_OK;
}
int sys_raw_spu_read_puint_mb(uint32_t id,uint32_t*value) {
	dbgf("raw spu read int mailbox %d\n",id);
	auto h = raw_spu_list.get(id);
	if (!h) return ESRCH;
	auto r = h->ctrl->chz[SPU_WrOutIntrMbox].ppu_pop();
	// If there is no value, this function should probably return an error
	// or undefined value.
	if (!r.first) xcept("sys_raw_spu_read_puint_mb - SPU_WrOutIntrMbox is empty");
	*value = se((uint32_t)r.second);
	return CELL_OK;
}

int sys_raw_spu_create_interrupt_tag(uint32_t id,uint32_t class_id,uint32_t hwthread,uint32_t*tag_id) {
	dbgf("rwa spu create interrupt tag, id %d, class %d\n",id,class_id);
	auto h = raw_spu_list.get(id);
	if (!h) return ESRCH;
	auto r = interrupt_new_spu_tag(id,class_id);
	if (r.first) return r.first;
	*tag_id = se((uint32_t)r.second.id());
	return CELL_OK;
}



void raw_spu_mfc_cmd(raw_spu_t&s,int cmd) {
	uint64_t ea = (uint64_t)s.mfc_eah<<32 | s.mfc_eal;
	uint32_t lsa = (s.mfc_lsa)%ls_size;
	uint32_t size = s.mfc_size;
	uint32_t tag_id = s.mfc_tagid;
	uint8_t*ls = (uint8_t*)&s.ls[lsa];
	uint8_t*ms = (uint8_t*)ea;
	dbgf("proxy MFC cmd %d, lsa %#x, ea %#x, size %d, tag id %d\n",cmd,lsa,ea,size,tag_id);
	switch (cmd) {
	case 0x20: // put
		memcpy(ms,ls,size);
		break;
	case 0x40: // get
		memcpy(ls,ms,size);
		break;
	default:
		xcept("unknown proxy MFC cmd %d",cmd);
	}
}

template<typename t>
void raw_spu_ps_write(raw_spu_t&s,uint32_t offset,t val) {
	switch (offset) {
	case 0x3004: // MFC_LSA
		s.mfc_lsa = val;
		break;
	case 0x3008: // MFC_EAH
		s.mfc_eah = val;
		break;
	case 0x300c: // MFC_EAL
		s.mfc_eal = val;
		break;
	case 0x3010: // MFC_Size/MFC_Tag
		s.mfc_size = (uint32_t)val>>16;
		s.mfc_tagid = val&0xffff;
		break;
	case 0x3014: // MFCClassID/MFC_Cmd
		// We don't care about the class id
		raw_spu_mfc_cmd(s,val&0xffff);
		break;
	case 0x3204: // Prxy_QueryType
		s.proxy_query_type = val;
		break;
	case 0x321c: // Prxy_QueryMask
		s.proxy_query_type = val;
		break;
	case 0x400c: // SPU_In_Mbox
		// The behaviour when the mailbox is full is undefined.
		{
			sync::busy_locker l(s.ctrl->thread_control_lock);
			s.ctrl->chz[SPU_RdInMbox].ppu_push(val);
			break;
		}
	case 0x401c: // SPU_RunCntl
		s.run_cntl = val;
		if ((val&3)==0) s.ctrl->stop();
		else if ((val&3)==1) s.ctrl->start();
		else if ((val&3)==2) xcept("SPU isolation exit request");
		else if ((val&3)==3) xcept("SPU isolation load request");
		break;
	case 0x4034: // SPU_NPC
		{
			sync::busy_locker l(s.ctrl->thread_control_lock);
			s.ctrl->npc = val;
		}
		break;
	default:
		xcept("mmio write to unknown raw spu register at problem state offset %#x",offset);
	}
}
template<typename t>
t raw_spu_ps_read(raw_spu_t&s,uint32_t offset) {
	switch (offset) {
	case 0x3014: // MFC_CMDStatus (least significant 2 bits)
		return 0;
	case 0x3104: // MFC_QStatus
		// queue does not contain commands, queue free space is 1
		return (uint32_t)(1<<31 | 1);
	case 0x3204: // Prxy_QueryType
		return s.proxy_query_type;
	case 0x321c: // Prxy_QueryMask
		return s.proxy_query_mask;
	case 0x322f:
	case 0x322e:
	case 0x322d:
	case 0x322c: // Prxy_TagStatus
		return ~0; // Since MFC is implemented synchronously, all operations are complete when we get here.
	case 0x4004: // SPU_Out_Mbox
		// The return value when the mailbox is empty is undefined.
		{
			sync::busy_locker l(s.ctrl->thread_control_lock);
			return s.ctrl->chz[SPU_WrOutMbox].ppu_pop().second;
		}
	case 0x4014: // SPU_Mbox_Stat
		// SPU documentation states it should be like this:
		// return s.ctrl->ch[SPU_WrOutMbox].pop_size() | s.ctrl->ch[SPU_RdInMbox].push_size()<<8;
		// But PPU code expects this:
		{
			sync::busy_locker l(s.ctrl->thread_control_lock);
			return (s.ctrl->chz[SPU_WrOutMbox].pop_size()?1:0) | (s.ctrl->chz[SPU_RdInMbox].push_size()?1:0)<<8 |
				(s.ctrl->chz[SPU_WrOutIntrMbox].pop_size()?1:0)<<16;
		}
		// Maybe mailboxes are implemented with a queue depth of 1?
	case 0x401c: // SPU_RunCntl
		return s.run_cntl;
	case 0x4024: // SPU_Status
		{
			sync::busy_locker l(s.ctrl->thread_control_lock);
			bool is_halted = false;
			bool is_single_step_stopped = false;
			bool invalid_instruction = false;
			bool invalid_channel_instruction = false;
			bool isolated = false;
			return (s.ctrl->is_running?1:0) | (s.ctrl->stopped_by_stop_and_signal?2:0) | (is_halted?4:0) | 
				(s.ctrl->blocking_on_channel?8:0) | (is_single_step_stopped?0x10:0) | (invalid_instruction?0x20:0) |
				(invalid_channel_instruction?0x40:0) | (isolated?0x80:0) |
				(s.ctrl->stop_and_signal_type<<16);
		}
	case 0x4034: // SPU_NPC
		return s.ctrl->npc;
	default:
		xcept("mmio read from unknown raw spu register at problem state offset %#x",offset);
	}
}

template<typename t>
void raw_spu_mmio_write(uint32_t offset,t val) {
	uint32_t spu_num = offset/0x100000;
	offset &= 0xfffff;
	auto h = raw_spu_list.get(spu_num);
	if (!h) xcept("mmio write to non-existing raw spu %d!",spu_num);
	raw_spu_t&s = *h;
	if (offset<0x40000) {
		//xcept("write to ls at offset %d\n",offset);
		(t&)s.ls[offset] = val;
		return;
	} else if (offset>0x60000) xcept("mmio access to unmapped (raw spu) offset %#x",offset);
	offset -= 0x40000;
	raw_spu_ps_write<t>(s,offset,se(val));
}
template<typename t>
t raw_spu_mmio_read(uint32_t offset) {
	uint32_t spu_num = offset/0x100000;
	offset &= 0xfffff;
	auto h = raw_spu_list.get(spu_num);
	if (!h) xcept("mmio read to non-existing raw spu %d!",spu_num);
	raw_spu_t&s = *h;
	if (offset<0x40000) {
		//xcept("read from ls at offset %d\n",offset);
		return (t&)s.ls[offset];
	} else if (offset>0x60000) xcept("mmio access to unmapped (raw spu) offset %#x",offset);
	offset -= 0x40000;
	uint32_t rv = se(raw_spu_ps_read<uint32_t>(s,offset&-4));
	rv >>= (offset&3)*8;
	return rv;
}


void spu_thread_stop(uint32_t stop_and_signal_type) {
	//dbgf("spu stop %#x\n",stop_and_signal_type);
	spu_thread_t*t = this_spu_control->spu_thread;

	// We use the PPU methods for interacting with mailboxes here,
	// as blocking would never return.
	auto fail = [&](int r) {
		t->ctrl->chz[SPU_RdInMbox].ppu_push((uint32_t)r);
	};
	auto get_event = [&]() -> event_queue_handle {
		spu_thread_t*t_bug_workaround = t;
		auto fail = [&](int r) -> event_queue_handle {
			t_bug_workaround->ctrl->chz[SPU_RdInMbox].ppu_push((uint32_t)r);
			return event_queue_handle();
		};
		const spu_thread_t*const_t = t;
		auto spuq = t->ctrl->chz[SPU_WrOutMbox].ppu_pop().second;
		event_queue_handle eqh;
		{
			sync::busy_locker l(t->bound_queues_lock);
			auto i = const_t->bound_queues.find(spuq);
			if (i==const_t->bound_queues.end()) return fail(EINVAL);
			eqh = i->second;
		}
		return eqh;
	};
	auto receive_event = [&]() {

		// Documentation for sys_spu_thread_receive_event states that while waiting for an event, the thread group
		// is in WAITING state.
		// Should we suspend the thread group and put it in WAITING state, and then resume it to RUNNING when
		// an event is received? Unless there is code that depends on this behavior, I see no reason to do this.
		sync::busy_locker l(t->ctrl->thread_control_lock);
		if (t->ctrl->chz[SPU_RdInMbox].pop_size()) return fail(EBUSY);
		auto eqh = get_event();
		if (!eqh) return;
		impl_event_queue_t*eq = &*eqh;
		event_t e;
		t->blocking_on_eq_receive = true;
		t->ctrl->thread_control_lock.unlock();
		int r = eq->receive(e,0);
		t->ctrl->thread_control_lock.lock();
		t->blocking_on_eq_receive = false;
		if (r) return fail(r);
		t->ctrl->chz[SPU_RdInMbox].ppu_push((uint32_t)CELL_OK);
		t->ctrl->chz[SPU_RdInMbox].ppu_push((uint32_t)se(e.data1));
		t->ctrl->chz[SPU_RdInMbox].ppu_push((uint32_t)se(e.data2));
		t->ctrl->chz[SPU_RdInMbox].ppu_push((uint32_t)se(e.data3));
	};
	auto tryreceive_event = [&]() {
		sync::busy_locker l(t->ctrl->thread_control_lock);
		if (t->ctrl->chz[SPU_RdInMbox].pop_size()) return fail(EBUSY);
		auto eqh = get_event();
		if (!eqh) return;
		impl_event_queue_t*eq = &*eqh;
		event_t e;
		int n;
		int r = eq->try_receive(&e,1,&n);
		if (r) return fail(r);
		if (!n) return fail(EBUSY);
		t->ctrl->chz[SPU_RdInMbox].ppu_push((uint32_t)CELL_OK);
		t->ctrl->chz[SPU_RdInMbox].ppu_push((uint32_t)se(e.data1));
		t->ctrl->chz[SPU_RdInMbox].ppu_push((uint32_t)se(e.data2));
		t->ctrl->chz[SPU_RdInMbox].ppu_push((uint32_t)se(e.data3));
	};

	switch (stop_and_signal_type) {
	case 0x102:
		{
			sync::busy_locker l(t->ctrl->thread_control_lock);
			t->exit_code = t->ctrl->chz[SPU_WrOutMbox].ppu_pop().second;
		}
		{
			sync::busy_locker l(t->group->control_lock);
			if (--t->group->running_threads==0) {
				t->group->terminated_event.set();
			}
		}
		spu_self_control.stop(&*t->ctrl,0x102);
		break;
	case 0x110:
		receive_event();
		break;
	case 0x111:
		tryreceive_event();
		break;
	default:
		xcept("unknown stop and signal type %#x",stop_and_signal_type);
	}
}

//
// NOTE: SPU can be suspended at any time it is not holding
// spu_control::thread_control_lock. As such, we must never acquire locks,
// or call *any* library functions (like dbgf) without holding the mentioned
// lock.
//

void spu_stop(uint32_t stop_and_signal_type,uint32_t next_pc) {
	//dbgf(" spu stop %#x\n",stop_and_signal_type);
	spu_control*ctrl = this_spu_control;
	if (ctrl->spu_thread) return spu_thread_stop(stop_and_signal_type);
	spu_self_control.stop(ctrl,stop_and_signal_type,next_pc);
}

void spu_restart(uint32_t offset) {
	//dbgf(" spu restart %#x\n",offset);
	spu_control*ctrl = this_spu_control;
	spu_self_control.restart(ctrl,offset);
}

void*spu_get_function(uint32_t offset) {
	spu_control*ctrl = this_spu_control;
	return ctrl->spu_get_function(offset);
}

uint32_t spu_rdch(int ca) {
	//dbgf("spu rdch %#x\n",ca);
	spu_control*ctrl = this_spu_control;
	sync::busy_locker l(ctrl->thread_control_lock);
	auto r = ctrl->chz[ca].spu_pop();
//	outf("rdch %#x returning %d\n",ca,r);
	return r;
}
uint32_t spu_rdch_count(int ca) {
	//printf("spu rdch count 0x%x\n",(int)ca);
	spu_control*ctrl = this_spu_control;
	sync::busy_locker l(ctrl->thread_control_lock);
	auto r = ctrl->chz[ca].pop_size();
//	fprintf(stdout,"rdch_count %x returning %d\n",ca,(int)r);
	return r;
}
void spu_wrch(int ca,uint32_t v) {
	//printf("spu wrch 0x%x 0x%x\n",(int)ca,(int)v);
	spu_control*ctrl = this_spu_control;
	sync::busy_locker l(ctrl->thread_control_lock);
	ctrl->chz[ca].spu_push(v);
}
uint32_t spu_wrch_count(int ca) {
	//printf("spu wrch count 0x%x\n",(int)ca);
	spu_control*ctrl = this_spu_control;
	sync::busy_locker l(ctrl->thread_control_lock);
	return ctrl->chz[ca].push_size();
}

uint64_t spu_x_get_ls_addr(uint32_t spu_num) {
	spu_thread_t*t = this_spu_control->spu_thread;
	return (uint64_t)t->group->threads[spu_num]->ls;
}
void spu_x_write_snr(uint32_t spu_num,uint32_t snr,uint32_t v) {
	spu_thread_t*t = this_spu_control->spu_thread;
	t->group->threads[spu_num]->write_snr(snr,v);
}
void spu_thread_wr_out_intr_mbox(uint32_t v) {
	spu_thread_t*t = this_spu_control->spu_thread;
	sync::busy_locker l(t->ctrl->thread_control_lock);

	uint32_t data1 = t->ctrl->chz[SPU_WrOutMbox].ppu_pop().second;
	uint32_t data0 = v&0x00FFFFFF;
	uint32_t port_num = v>>24;

	if (port_num>=0x40*2) xcept("bad port_num %#x\n",port_num);
	bool no_retcode = false;
	if (port_num>=0x40) {
		port_num -= 0x40;
		no_retcode=true;
	}
	auto&port = t->event_ports[port_num];
	if (!port) xcept("write outbound interrupt mailbox; port %d not connected",port_num);
	impl_event_queue_t*eq = &*port;

	event_t d;
	d.source = se((uint64_t)0xFFFFFFFF53505501ULL);
	d.data1 = se((uint64_t)t->id);
	d.data2 = se((uint64_t)((uint64_t)port_num<<32 | data0));
	d.data3 = se((uint64_t)data1);
	int r = eq->send(d);
	if (!no_retcode) t->ctrl->chz[SPU_RdInMbox].ppu_push(r);
}
void spu_wr_out_intr_mbox(uint32_t v) {
	spu_control*ctrl = this_spu_control;
	if (ctrl->spu_thread) return spu_thread_wr_out_intr_mbox(v);
	{
		sync::busy_locker l(ctrl->thread_control_lock);
		ctrl->chz[SPU_WrOutIntrMbox].spu_push(v);
	}
	if (ctrl->int2_mask&1) {
		ctrl->int2_stat |= 1;
		{
			sync::busy_locker l(ctrl->thread_control_lock);
			interrupt_trigger_spu(ctrl->raw_spu_id,2);
		}
		while (atomic_read(&ctrl->int2_stat)&1) win32_thread::yield();
	}
	auto test = [&]() -> bool {
		sync::busy_locker l(ctrl->thread_control_lock);
		return ctrl->chz[SPU_WrOutIntrMbox].pop_size()==0;
	};
	while (!test()) win32_thread::yield();
}

