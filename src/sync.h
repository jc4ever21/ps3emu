

struct mutex_attr_t {
	uint32_t protocol, recursive, ipc;
	uint64_t ipc_key;
	uint32_t flags;
	uint64_t name;
};

struct cond_attr_t {
	uint32_t ipc;
	uint32_t flags;
	uint64_t ipc_key;
	uint64_t name;
};

struct rwlock_attr_t {
	uint32_t protocol, ipc;
	uint64_t ipc_key;
	uint32_t flags;
	uint64_t name;
};

struct semaphore_attr_t {
	uint32_t protocol, ipc;
	uint64_t ipc_key;
	uint32_t flags;
	uint64_t name;
};

enum {
	sync_fifo = 1,
	sync_priority = 2,
};
enum {
	sync_recursive = 0x10,
	sync_not_recursive = 0x20,
};
enum {
	sync_ipc = 0x100,
	sync_not_ipc = 0x200,
};

enum {
	sync_ipc_get_new = 1,
	sync_ipc_get_existing = 2,
	sync_ipc_get_always = 3,
};

template<typename list_t>
std::pair<int,typename list_t::handle> sync_get_ipc2(list_t&i,uint64_t flags,uint64_t key,bool*created) {
	list_t::handle h;
	if (flags==sync_ipc_get_new) {
		dbgf("(ipc get new, key %#x)\n",key);
		h = i.get_new(key);
		if (!h) return std::make_pair(EEXIST,h);
		if (created) *created=true;
	} else if (flags==sync_ipc_get_existing) {
		dbgf("(ipc get, key %#x)\n",key);
		h = i.get_existing(key);
		if (!h) return std::make_pair(ESRCH,h);
		if (created) *created=false;
	} else if (flags==sync_ipc_get_always) {
		dbgf("(ipc get always, key %#x)\n",key);
		auto t = i.get(key);
		h = t.second;
		if (!h) return std::make_pair(EAGAIN,h);
		if (created) *created=t.first;
	} else xcept("invalid ipc flags %#x",flags);
	return std::make_pair(CELL_OK,h);
}

struct impl_mutex_t {
	sync::mutex_any local_mutex_any;
	sync::mutex_any*m;
	ipc_list_t2<sync::mutex_any>::handle ipc_h;
	int recursive, protocol;
	uint32_t lock_ref_count, cond_ref_count;
	uint64_t ipc_key;
	impl_mutex_t() : m(&local_mutex_any), lock_ref_count(0), cond_ref_count(0) , ipc_key(0) {}
	void set_ipc(const ipc_list_t2<sync::mutex_any>::handle&h) {
		ipc_h = h;
		m = &*h;
	}
	sync::mutex_any&mut() {
		return *m;
	}
	template<typename recursive,typename protocol,typename ipc>
	void init() {
		m->init<recursive,protocol,ipc>();
	}
	int lock(uint64_t timeout=0) {
		return m->lock(timeout);
	}
	int try_lock() {
		return m->try_lock();
	}
	int unlock() {
		return m->unlock();
	}
	bool is_locked() {
		return m->is_locked();
	}
};

id_list_t<impl_mutex_t,41> mutex_list;
shm_object<ipc_list_t2<sync::mutex_any>> ipc_mut;
typedef id_list_t<impl_mutex_t,41>::handle mutex_handle;
typedef id_list_t<impl_mutex_t,41>::handle_refcount<&impl_mutex_t::cond_ref_count>::type cond_mutex_handle;

int sys_mutex_create(uint32_t*mutex_id,mutex_attr_t*attr) {
	const char*name = (const char*)&attr->name;
	int protocol = se(attr->protocol);
	int recursive = se(attr->recursive);
	dbgf("create mutex '%.8s', protocol %d, recursive %d\n",name,protocol,recursive);

	uint64_t ipc_key = se(attr->ipc_key);
	int ipc = se(attr->ipc);
	uint32_t flags = se(attr->flags);

	if (protocol!=sync_fifo&&protocol!=sync_priority) return EINVAL;
	if (recursive!=sync_recursive&&recursive!=sync_not_recursive) return EINVAL;

	int n = (protocol==sync_priority?1:0)|(recursive==sync_recursive?2:0);

	auto h = mutex_list.get_new();
	if (!h) return EAGAIN;
	int id = h.id();
	impl_mutex_t&m = *h;
	if (ipc==sync_not_ipc) {
		if (n==0) m.init<sync::not_recursive,sync::fifo,sync::not_ipc>();
		else if (n==1) m.init<sync::not_recursive,sync::priority,sync::not_ipc>();
		else if (n==2) m.init<sync::recursive,sync::fifo,sync::not_ipc>();
		else m.init<sync::recursive,sync::priority,sync::not_ipc>();
	} else if (ipc==sync_ipc) {
		m.ipc_key = ipc_key;
		bool created;
		auto t = sync_get_ipc2(*ipc_mut,flags,ipc_key,&created);
		if (t.first) return t.first;
		auto h = t.second;
		if (created) {
			sync::mutex_any&m = *h;
			if (n==0) m.init<sync::not_recursive,sync::fifo,sync::ipc>();
			else if (n==1) m.init<sync::not_recursive,sync::priority,sync::ipc>();
			else if (n==2) m.init<sync::recursive,sync::fifo,sync::ipc>();
			else m.init<sync::recursive,sync::priority,sync::ipc>();
		}
		m.set_ipc(h);
	} else return EINVAL;
	m.recursive = recursive;
	m.protocol = protocol;
	dbgf("created mutex with id %d\n",id);
	*mutex_id = se((uint32_t)id);
	h.keep();
	return CELL_OK;
}
int sys_mutex_destroy(uint32_t mutex_id) {
	dbgf("destroy mutex %d\n",mutex_id);
	auto h = mutex_list.get(mutex_id);
	if (!h) return ESRCH;
	impl_mutex_t&m = *h;
	if (m.ipc_key) dbgf(" (ipc key %#x)\n",m.ipc_key);
	switch (h.try_kill<&impl_mutex_t::lock_ref_count,&impl_mutex_t::cond_ref_count>([&m](){return m.is_locked();})) {
	case 0: return CELL_OK; // Guaranteed no one is holding the lock, and no associated condition variable exists.
	                        // Further attempts to mutex_list.get this mutex_id will return ESRCH, but existing
	                        // handles will remain valid until closed.
	case 1: return EBUSY;   // Someone is about to grab the lock.
	case 2: return EPERM;   // An associated condition variable exists.
	case 3: return EBUSY;   // Someone is holding the lock.
	default:NODEFAULT;
	}
}
int sys_mutex_lock(uint32_t mutex_id,uint64_t timeout) {
	dbgf("lock mutex %d (timeout %d)\n",mutex_id,timeout);
	auto h = mutex_list.get<&impl_mutex_t::lock_ref_count>(mutex_id);
	if (!h) return ESRCH;
	impl_mutex_t&m = *h;
	if (m.ipc_key) dbgf(" (ipc key %#x)\n",m.ipc_key);
	return m.lock(timeout);
}
int sys_mutex_trylock(uint32_t mutex_id) {
	dbgf("trylock mutex %d\n",mutex_id);
	auto h = mutex_list.get<&impl_mutex_t::lock_ref_count>(mutex_id);
	if (!h) return ESRCH;
	impl_mutex_t&m = *h;
	if (m.ipc_key) dbgf(" (ipc key %#x)\n",m.ipc_key);
	return m.try_lock();
}
int sys_mutex_unlock(uint32_t mutex_id) {
	dbgf("unlock mutex %d\n",mutex_id);
	auto h = mutex_list.get<&impl_mutex_t::lock_ref_count>(mutex_id);
	if (!h) return ESRCH;
	impl_mutex_t&m = *h;
	if (m.ipc_key) dbgf(" (ipc key %#x)\n",m.ipc_key);
	return m.unlock();
}

struct cond_with_mutex_t {
	sync::condition_variable_any local_condition_variable_any;
	sync::condition_variable_any*c;
	cond_mutex_handle m_h;
	ipc_list_t2<sync::condition_variable_any>::handle ipc_h;
	uint32_t wait_ref_count;
	uint64_t ipc_key;
	cond_with_mutex_t() : c(&local_condition_variable_any), wait_ref_count(0), ipc_key(0) {}
	void set_ipc(const ipc_list_t2<sync::condition_variable_any>::handle&h) {
		ipc_h = h;
		c = &*h;
	}
	template<typename protocol,typename ipc>
	void init() {
		c->init<protocol,ipc>();
	}
	int signal() {
		return c->signal();
	}
	int signal_all() {
		return c->signal_all();
	}
	int signal_to(uint64_t thread_id) {
		return c->signal_to(thread_id);
	}
	int wait(uint64_t timeout) {
		return c->wait(m_h->mut(),timeout);
	}
};
typedef cond_with_mutex_t impl_cond_t;
id_list_t<impl_cond_t,21> cond_list;
shm_object<ipc_list_t2<sync::condition_variable_any>> ipc_cond;

int sys_cond_create(uint32_t *cond_id,uint32_t mutex_id,cond_attr_t *attr) {
	const char*name = (const char*)&attr->name;
	dbgf("create cond '%.8s', mutex %d\n",name,mutex_id);

	auto m_h = mutex_list.get<&impl_mutex_t::cond_ref_count>(mutex_id);
	if (!m_h) return ESRCH;
	impl_mutex_t&m = *m_h;

	uint64_t ipc_key = se(attr->ipc_key);
	int ipc = se(attr->ipc);
	uint32_t flags = se(attr->flags);

	int protocol = m.protocol;

	if (protocol!=sync_fifo&&protocol!=sync_priority) return EINVAL;

	int n = (protocol==sync_priority?1:0);

	auto h = cond_list.get_new();
	if (!h) return EAGAIN;
	int id = h.id();
	impl_cond_t&c = *h;
	c.m_h = m_h;
	if (ipc==sync_not_ipc) {
		if (n==0) c.init<sync::fifo,sync::not_ipc>();
		else if (n==1) c.init<sync::priority,sync::not_ipc>();
	} else if (ipc==sync_ipc) {
		c.ipc_key = ipc_key;
		bool created;
		auto t = sync_get_ipc2(*ipc_cond,flags,ipc_key,&created);
		if (t.first) return t.first;
		auto h = t.second;
		if (created) {
			sync::condition_variable_any&c = *h;
			if (n==0) c.init<sync::fifo,sync::ipc>();
			else if (n==1) c.init<sync::priority,sync::ipc>();
		}
		c.set_ipc(h);
	} else return EINVAL;

	dbgf("created condition variable with id %d\n",id);
	*cond_id = se((uint32_t)id);
	h.keep();
	return CELL_OK;
}
int sys_cond_destroy(uint32_t cond_id) {
	dbgf("destroy cond %d\n",cond_id);
	auto h = cond_list.get(cond_id);
	if (!h) return ESRCH;
	if (h->ipc_key) dbgf(" (ipc key %#x)\n",h->ipc_key);
	switch (h.try_kill<&impl_cond_t::wait_ref_count>()) {
	case 0: return CELL_OK;
	case 1: return EBUSY;
	default:NODEFAULT;
	}
}
int sys_cond_signal(uint32_t cond_id) {
	dbgf("signal cond %d\n",(int)cond_id);
	auto h = cond_list.get(cond_id);
	if (!h) return ESRCH;
	if (h->ipc_key) dbgf(" (ipc key %#x)\n",h->ipc_key);
	return h->signal();
}
int sys_cond_signal_all(uint32_t cond_id) {
	dbgf("signal all cond %d\n",cond_id);
	auto h = cond_list.get(cond_id);
	if (!h) return ESRCH;
	return h->signal_all();
}
int sys_cond_signal_to(uint32_t cond_id,uint64_t ppu_thread_id) {
	dbgf("signal to thread %d, cond %d\n",ppu_thread_id,cond_id);
	auto h = cond_list.get(cond_id);
	if (!h) return ESRCH;
	if (h->ipc_key) dbgf(" (ipc key %#x)\n",h->ipc_key);
	return h->signal_to(ppu_thread_id);
}

int sys_cond_wait(uint32_t cond_id,uint64_t timeout) {
	dbgf("wait cond %d (timeout %d)\n",cond_id,timeout);
	auto h = cond_list.get<&impl_cond_t::wait_ref_count>(cond_id);
	if (!h) return ESRCH;
	if (h->ipc_key) dbgf(" (ipc key %#x)\n",h->ipc_key);
	return h->wait(timeout);
}

struct impl_lwmutex_queue {
	sync::sleep_queue_any q;
	int protocol;
	uint32_t wait_ref_count;
	impl_lwmutex_queue() : wait_ref_count(0) {}
	template<typename protocol>
	void init() {
		q.init<protocol,sync::not_ipc>();
	}
	int wait(uint64_t timeout) {
		return q.wait(timeout);
	}
	int try_wait() {
		return q.try_wait();
	}
	int release_one() {
		return q.release_one();
	}
};

id_list_t<impl_lwmutex_queue> lwmutex_queue_list;
typedef id_list_t<impl_lwmutex_queue> lwmutex_queue_handle;

int sys_lwmutex_create(uint32_t*queue_id,uint32_t protocol,void*lwmutex,uint64_t unk0,uint64_t name) {
	dbgf("lwmutex create queue %p, protocol %d, lwmutex %p, unk0 %#x, name %#x\n",queue_id,protocol,lwmutex,unk0,name);
	if (unk0!=0x80000001) xcept("lwmutex create: unk0 has unknown value %#x",unk0);

	if (protocol!=sync_fifo&&protocol!=sync_priority) return EINVAL;
	auto h = lwmutex_queue_list.get_new();
	if (!h) return EAGAIN;
	int id = h.id();
	impl_lwmutex_queue&q = *h;
	if (protocol==sync_fifo) q.init<sync::fifo>();
	else q.init<sync::priority>();
	q.protocol = protocol;
	dbgf("created queue with id %d\n",id);
	*queue_id = se((uint32_t)id);
	h.keep();
	return CELL_OK;
}

int sys_lwmutex_destroy(uint32_t queue_id) {
	dbgf("lwmutex destroy queue %d\n",queue_id);
	auto h = lwmutex_queue_list.get(queue_id);
	if (!h) return ESRCH;
	switch (h.try_kill<&impl_lwmutex_queue::wait_ref_count>()) {
	case 0: return CELL_OK;
	case 1: return EBUSY;
	default:NODEFAULT;
	}
}

int sys_lwmutex_lock(uint32_t queue_id,uint64_t timeout) {
	dbgf("lwmutex lock %d, timeout %d\n",queue_id,timeout);
	auto h = lwmutex_queue_list.get<&impl_lwmutex_queue::wait_ref_count>(queue_id);
	//if (!h) return ESRCH;
	//if (!h) return CELL_OK;
	if (!h) xcept("bad lwmutex!");
	return h->wait(timeout);
}
int sys_lwmutex_unlock(uint32_t queue_id) {
	dbgf("lwmutex unlock %d\n",queue_id);
	auto h = lwmutex_queue_list.get(queue_id);
	if (!h) return ESRCH;
	return h->release_one();
}
int sys_lwmutex_trylock(uint32_t queue_id) {
	dbgf("lwmutex try lock %d\n",queue_id);
	auto h = lwmutex_queue_list.get(queue_id);
	if (!h) return ESRCH;
	return h->try_wait();
}

struct impl_lwcond {
	sync::condition_variable_any c;
	sync::mutex_any m;
	uint32_t wait_ref_count;
	uint32_t release_count;
	impl_lwcond() : wait_ref_count(0), release_count(0) {}
	template<typename protocol>
	void init() {
		m.init<sync::not_recursive,protocol,sync::not_ipc>();
		c.init<protocol,sync::not_ipc>();
	}
	int signal() {
		return c.signal();
	}
	int signal_all() {
		return c.signal_all();
	}
	int signal_to(uint64_t thread_id) {
		return c.signal_to(thread_id);
	}
	int wait(uint64_t timeout) {
		return c.wait(m,timeout);
	}
};

id_list_t<impl_lwcond> lwcond_list;

int sys_lwcond_create(uint32_t*lwcond_id,uint32_t lwmutex_id,void*lwcond,uint64_t name,uint64_t zero) {
	dbgf("lwcond create, lwmutex_id %d, lwcond %p, name %#x, zero %#x\n",lwmutex_id,lwcond,name,zero);
	if (zero!=0) xcept("lwcond create: zero has unknown value %#x",zero);
	auto h = lwcond_list.get_new();
	if (!h) return EAGAIN;
	auto qh = lwmutex_queue_list.get(lwmutex_id);
	if (!qh) return ESRCH;
	impl_lwmutex_queue&q = *qh;
	impl_lwcond&c = *h;
	if (q.protocol==sync_fifo) c.init<sync::fifo>();
	else c.init<sync::priority>();
	*lwcond_id = se((uint32_t)h.id());
	h.keep();
	return CELL_OK;
}
int sys_lwcond_destroy(uint32_t lwcond_id) {
	dbgf("lwcond destroy %d\n",lwcond_id);
	auto h = lwcond_list.get(lwcond_id);
	if (!h) return ESRCH;
	switch (h.try_kill<&impl_lwcond::wait_ref_count>()) {
	case 0: return CELL_OK;
	case 1: return EBUSY;
	default:NODEFAULT;
	}
}
int sys_lwcond_wait(uint32_t lwcond_id,uint32_t lwmutex_id,uint64_t timeout) {
	dbgf("lwcond wait, lwcond_id %d, lwmutex_id %d, timeout %d\n",lwcond_id,lwmutex_id,timeout);
	auto h = lwcond_list.get<&impl_lwcond::wait_ref_count>(lwcond_id);
	if (!h) return ESRCH;
	auto qh = lwmutex_queue_list.get(lwmutex_id);
	if (!qh) return ESRCH;
	impl_lwcond&c = *h;
	impl_lwmutex_queue&q = *qh;
	c.m.lock(0);
	q.release_one(); // lwmutex owner is set to -3, so we need to release one
	                 // in order to let anyone lock it again.
	int r = c.wait(timeout);
	if (r) return c.m.unlock(), r;
	if (c.release_count) {
		atomic_dec(&c.release_count);
		q.wait(0);
		c.m.unlock();
		return CELL_OK;
	} else return c.m.unlock(), EBUSY;
}
int sys_lwcond_signal(uint32_t lwcond_id,uint32_t lwmutex_id,uint32_t thread_id,uint64_t mode) {
	dbgf("lwcond signal, lwcond_id %d, lwmutex_id %d, thread_id %d, mode %d\n",lwcond_id,lwmutex_id,thread_id,mode);
	auto h = lwcond_list.get<&impl_lwcond::wait_ref_count>(lwcond_id);
	if (!h) return ESRCH;
	impl_lwcond&c = *h;
	if (mode==1) atomic_inc(&c.release_count);
	if (thread_id==-1) return c.signal();
	else return c.signal_to(thread_id);
}
int sys_lwcond_signal_all(uint32_t lwcond_id,uint32_t lwmutex_id,uint64_t mode) {
	dbgf("lwcond signal all, lwcond_id %d, lwmutex_id %d, mode %d\n",lwcond_id,lwmutex_id,mode);
	auto h = lwcond_list.get<&impl_lwcond::wait_ref_count>(lwcond_id);
	if (!h) return ESRCH;
	impl_lwcond&c = *h;
	// We should increase c.release_count by the number of threads actually
	// released, and also return that number from this syscall. Returning
	// 0/CELL_OK works, but it's not the best solution.
	int r = c.c.signal_all();
	if (r) return r;
	return CELL_OK;
}

struct impl_rwlock_t {
	sync::rwlock_any local_rwlock_any;
	sync::rwlock_any*rw;
	ipc_list_t2<sync::rwlock_any>::handle ipc_h;
	int protocol;
	uint32_t lock_ref_count;
	impl_rwlock_t() : rw(&local_rwlock_any), lock_ref_count(0) {}
	void set_ipc(const ipc_list_t2<sync::rwlock_any>::handle&h) {
		ipc_h = h;
		rw = &*h;
	}
	template<typename protocol,typename ipc>
	void init() {
		rw->init<protocol,ipc>();
	}
	int rlock(uint64_t timeout) {
		return rw->rlock(timeout);
	}
	int try_rlock() {
		return rw->try_rlock();
	}
	int runlock() {
		return rw->runlock();
	}
	int wlock(uint64_t timeout) {
		return rw->wlock(timeout);
	}
	int try_wlock() {
		return rw->try_wlock();
	}
	int wunlock() {
		return rw->wunlock();
	}
};

id_list_t<impl_rwlock_t,41> rwlock_list;
shm_object<ipc_list_t2<sync::rwlock_any>> ipc_rwlock;

int sys_rwlock_create(uint32_t*rwlock_id,rwlock_attr_t*attr) {
	const char*name = (const char*)&attr->name;
	int protocol = se(attr->protocol);
	dbgf("rwlock create '%.8s', protocol %d\n",name,protocol);
	uint64_t ipc_key = se(attr->ipc_key);
	int ipc = se(attr->ipc);
	uint32_t flags = se(attr->flags);

	if (protocol!=sync_fifo&&protocol!=sync_priority) return EINVAL;

	int n = (protocol==sync_priority?1:0);

	auto h = rwlock_list.get_new();
	if (!h) return EAGAIN;
	int id = h.id();
	impl_rwlock_t&l = *h;
	if (ipc==sync_not_ipc) {
		if (n==0) l.init<sync::fifo,sync::not_ipc>();
		else if (n==1) l.init<sync::priority,sync::not_ipc>();
	} else if (ipc==sync_ipc) {
		bool created;
		auto t = sync_get_ipc2(*ipc_rwlock,flags,ipc_key,&created);
		if (t.first) return t.first;
		auto h = t.second;
		if (created) {
			sync::rwlock_any&l = *h;
			if (n==0) l.init<sync::fifo,sync::ipc>();
			else if (n==1) l.init<sync::priority,sync::ipc>();
		}
		l.set_ipc(h);
	} else return EINVAL;
	l.protocol = protocol;

	*rwlock_id = se((uint32_t)id);
	h.keep();
	return CELL_OK;
}
int sys_rwlock_destroy(uint32_t rwlock_id) {
	dbgf("destroy rwlock %d\n",rwlock_id);
	auto h = rwlock_list.get(rwlock_id);
	if (!h) return ESRCH;
	switch (h.try_kill<&impl_rwlock_t::lock_ref_count>()) {
	case 0: return CELL_OK;
	case 1: return EBUSY;
	default:NODEFAULT;
	}
}
int sys_rwlock_rlock(uint32_t rwlock_id,uint64_t timeout) {
	dbgf("rwlock rlock %d timeout %d\n",rwlock_id,timeout);
	auto h = rwlock_list.get<&impl_rwlock_t::lock_ref_count>(rwlock_id);
	if (!h) return ESRCH;
	return h->rlock(timeout);
}
int sys_rwlock_tryrlock(uint32_t rwlock_id) {
	dbgf("rwlock tryrlock %d\n",rwlock_id);
	auto h = rwlock_list.get<&impl_rwlock_t::lock_ref_count>(rwlock_id);
	if (!h) return ESRCH;
	return h->try_rlock();
}
int sys_rwlock_runlock(uint32_t rwlock_id) {
	dbgf("rwlock runlock %d\n",rwlock_id);
	auto h = rwlock_list.get(rwlock_id);
	if (!h) return ESRCH;
	return h->runlock();
}
int sys_rwlock_wlock(uint32_t rwlock_id,uint64_t timeout) {
	dbgf("rwlock wlock %d timeout %d\n",rwlock_id,timeout);
	auto h = rwlock_list.get<&impl_rwlock_t::lock_ref_count>(rwlock_id);
	if (!h) return ESRCH;
	return h->wlock(timeout);
}
int sys_rwlock_trywlock(uint32_t rwlock_id) {
	dbgf("rwlock trywlock %d\n",rwlock_id);
	auto h = rwlock_list.get<&impl_rwlock_t::lock_ref_count>(rwlock_id);
	if (!h) return ESRCH;
	return h->try_wlock();
}
int sys_rwlock_wunlock(uint32_t rwlock_id) {
	dbgf("rwlock wunlock %d\n",rwlock_id);
	auto h = rwlock_list.get(rwlock_id);
	if (!h) return ESRCH;
	return h->wunlock();
}

struct impl_semaphore_t {
	sync::semaphore_any local_sem_any;
	sync::semaphore_any*sem;
	ipc_list_t2<sync::semaphore_any>::handle ipc_h;
	int protocol;
	uint32_t wait_ref_count;
	impl_semaphore_t() : sem(&local_sem_any), wait_ref_count(0) {}
	void set_ipc(const ipc_list_t2<sync::semaphore_any>::handle&h) {
		ipc_h = h;
		sem = &*h;
	}
	template<typename protocol,typename ipc>
	void init(uint32_t initial,uint32_t max) {
		sem->init<protocol,ipc>(initial,max);
	}
	int wait(uint64_t timeout=0) {
		return sem->wait(timeout);
	}
	int try_wait() {
		return sem->try_wait();
	}
	int post(uint32_t n) {
		return sem->post(n);
	}
	uint32_t get_value() {
		return sem->get_value();
	}
};

id_list_t<impl_semaphore_t,41> semaphore_list;
shm_object<ipc_list_t2<sync::semaphore_any>> ipc_semaphore;

int sys_semaphore_create(uint32_t*sem,semaphore_attr_t*attr,uint32_t initial_val,uint32_t max_val) {
	int protocol = se(attr->protocol);
	dbgf("create semaphore, protocol %d, initial_val %d, max_val %d\n",protocol,initial_val,max_val);
	uint64_t ipc_key = se(attr->ipc_key);
	int ipc = se(attr->ipc);
	uint32_t flags = se(attr->flags);

	if (protocol!=sync_fifo&&protocol!=sync_priority) return EINVAL;

	int n = (protocol==sync_priority?1:0);

	auto h = semaphore_list.get_new();
	if (!h) return EAGAIN;
	int id = h.id();
	impl_semaphore_t&s = *h;
	if (ipc==sync_not_ipc) {
		if (n==0) s.init<sync::fifo,sync::not_ipc>(initial_val,max_val);
		else if (n==1) s.init<sync::priority,sync::not_ipc>(initial_val,max_val);
	} else if (ipc==sync_ipc) {
		bool created;
		auto t = sync_get_ipc2(*ipc_semaphore,flags,ipc_key,&created);
		if (t.first) return t.first;
		auto h = t.second;
		if (created) {
			sync::semaphore_any&s = *h;
			if (n==0) s.init<sync::fifo,sync::ipc>(initial_val,max_val);
			else if (n==1) s.init<sync::priority,sync::ipc>(initial_val,max_val);
		}
		s.set_ipc(h);
	} else return EINVAL;
	s.protocol = protocol;

	dbgf("created semaphore with id %d\n",id);
	*sem = se((uint32_t)id);
	h.keep();
	return CELL_OK;

}
int sys_semaphore_destroy(uint32_t sem) {
	dbgf("destroy semaphore %d\n",sem);
	auto h = semaphore_list.get(sem);
	if (!h) return ESRCH;
	switch (h.try_kill<&impl_semaphore_t::wait_ref_count>()) {
	case 0: return CELL_OK;
	case 1: return EBUSY;
	default:NODEFAULT;
	}
}
int sys_semaphore_wait(uint32_t sem,uint64_t timeout) {
	dbgf("semaphore wait %d (timeout %d)\n",sem,timeout);
	auto h = semaphore_list.get<&impl_semaphore_t::wait_ref_count>(sem);
	if (!h) return ESRCH;
	return h->wait(timeout);
}
int sys_semaphore_trywait(uint32_t sem) {
	dbgf("semaphore trywait %d\n",sem);
	auto h = semaphore_list.get(sem);
	if (!h) return ESRCH;
	return h->try_wait();
}
int sys_semaphore_post(uint32_t sem,uint32_t val) {
	dbgf("semaphore post %d val %d\n",sem,val);
	auto h = semaphore_list.get(sem);
	if (!h) return ESRCH;
	return h->post(val);
}
int sys_semaphore_get_value(uint32_t sem,uint32_t *val) {
	dbgf("semaphore get_value %d val %p\n",sem,val);
	auto h = semaphore_list.get(sem);
	if (!h) return ESRCH;
	*val = se((uint32_t)h->get_value());
	return CELL_OK;
}








