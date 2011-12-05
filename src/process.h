
struct process_param_t {
	uint32_t size;
	uint32_t magic; // 0x13bcc5f6
	uint32_t version;
	uint32_t sdk_version;
	int32_t primary_prio;
	uint32_t primary_stacksize;
	uint32_t malloc_pagesize;
	uint32_t ppc_seg;
};

process_param_t process_params;

struct process_t {
	int pid;
	shm_vector<shm_string>::type argv;
	uint32_t sdk_version;
};

struct process_mgr_t {
	process_t list[0x100];
	int get_new() {
		for (size_t i=1;i<0x100;i++) {
			process_t&p = list[i];
			if (atomic_cas(&p.pid,0,i)==0) {
				return i;
			}
		}
		return -1;
	}
	process_t*get(int id) {
		mem_read_barrier();
		if (id<1 || id>0x100) return 0;
		process_t&r = list[id];
		if (r.pid!=id) return 0;
		return &r;
	}
};

shm_object<process_mgr_t> proc_mgr;
int my_pid;
process_t*my_proc;

void proc_setup(int pid) {
	if (pid==0) {
		pid=proc_mgr->get_new();
		if (pid==-1) xcept("proc_mgr->get_new() failed");
	}
	process_t*p = proc_mgr->get(pid);
	if (!p) {
		xcept("no such process: %d",pid);
	}
	my_pid = pid;
	my_proc = p;
	dbgf("proc_setup(): my_pid is %d\n",my_pid);
	if (p->argv.size()) xcept("arguments? :o");
}

void set_process_params(process_param_t*p) {
	dbgf("process params is at %p\n",p);
	if (!p) {
		dbgf("using default process params\n");
		static process_param_t default_process_param;
		default_process_param.size = sizeof(default_process_param);
		default_process_param.magic = se((uint32_t)0x13bcc5f6);
		default_process_param.version = se((uint32_t)0x330000);
		default_process_param.sdk_version = se((uint32_t)0x355000);
		default_process_param.primary_prio = se((int32_t)1001);
		default_process_param.primary_stacksize = se((uint32_t)0x10000);
		default_process_param.malloc_pagesize = se((uint32_t)0x10000);
		default_process_param.ppc_seg = se((uint32_t)0x0);
		p = &default_process_param;
	}
	uint32_t size = se(p->size);
	if (size<sizeof(process_param_t)) xcept("process parameters size too small");
	uint32_t magic = se(p->magic);
	if (magic!=0x13bcc5f6) xcept("process parameters magic mismatch; got %x, expected %x",magic,0x13bcc5f6);
	uint32_t version = se(p->version);
	uint32_t sdk_version = se(p->sdk_version);
	int32_t primary_prio = se(p->primary_prio);
	uint32_t primary_stacksize = se(p->primary_stacksize);
	uint32_t malloc_pagesize = se(p->malloc_pagesize);
	uint32_t ppc_seg = se(p->ppc_seg);
	process_params.size = size;
	process_params.magic = magic;
	process_params.version = version;
	process_params.sdk_version = sdk_version;
	process_params.primary_prio = primary_prio;
	process_params.primary_stacksize = primary_stacksize;
	process_params.malloc_pagesize = malloc_pagesize;
	process_params.ppc_seg = ppc_seg;
	dbgf("process parameters::\n");
	dbgf("size: %d (vs %d)\n",size,sizeof(process_param_t));
	dbgf("magic: %#x\n",magic);
	dbgf("version: %#x\n",version);
	dbgf("sdk_version: %#x\n",sdk_version);
	dbgf("primary_prio: %d\n",primary_prio);
	dbgf("primary_stacksize: %#x\n",primary_stacksize);
	dbgf("malloc_pagesize: %#x\n",malloc_pagesize);
	dbgf("ppc_seg: %#x\n",ppc_seg);

	my_proc->sdk_version = sdk_version;
}

uint32_t sys_process_getpid() {
	return my_pid;
}

void sys_process_exit(uint64_t exit_code) {
	dbgf("exiting with exit code %d\n",exit_code);
	exit((int)exit_code);
}

int sys_process_get_paramsfo() {
	dbgf("_sys_process_get_paramsfo returning ENOENT\n");
	return ENOENT;
}

int sys_process_is_spu_lock_line_reservation_address() {
	return CELL_OK;
}

int sys_process_get_sdk_version(uint32_t pid,uint32_t*v) {
	process_t*p = proc_mgr->get(pid);
	if (!p) return ESRCH;
	*v = se((uint32_t)p->sdk_version);
	return CELL_OK;
}

int sys_process_create(uint32_t*pid,uint64_t prio,uint64_t flags,uint64_t*info,uint64_t a5,uint64_t a6,uint64_t a7) {

	dbgf("process create, pid %p, prio %d, flags %#x, info %p, a5 %#x, a6 %#x, a7 %#x\n",pid,prio,flags,info,a5,a6,a7);
	
	uint64_t p_argv = se(info[0]);

	std::vector<const char*> argv;

	const char*c = (const char*)p_argv;
	while (*c) {
		argv.push_back(c);
		dbgf("-- %s\n",c);
		while (*c) c++;
		c++;
	}
	if (argv.empty()) return EINVAL;

	file_t f;
	f.set_fn(argv[0]);

	std::string s = f.host_fn;
	size_t p = s.rfind(".");
	if (p!=std::string::npos) {
		s = s.substr(0,p);
	}
	s = s + ".exe";

// 	id_handle<process_t> h = process_list.get_new();
// 	int id = h.id;
// 
// 	dbgf("spawn process with id %d\n",id);

	xcept("spawn %s",s.c_str());

	dbgf("returning ENOENT\n");
	return ENOENT;
}

