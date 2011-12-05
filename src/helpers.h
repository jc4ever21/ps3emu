
static uint32_t dummy_ref;
template<typename ref_t,typename cont_t,uint32_t ref_t::t::*refcount_member>
struct id_handle {
	typedef typename ref_t::t t;
private:
	ref_t*p;
	cont_t*c;
public:
	uint32_t ref() {
		uint32_t r = atomic_inc(&p->r);
		if (refcount_member!=nullptr) atomic_inc(&((**p).*refcount_member));
		return r;
	}
	uint32_t deref() {
		uint32_t r = atomic_dec(&p->r);
		if (refcount_member!=nullptr) atomic_dec(&((**p).*refcount_member));
		if (!r) c->try_recycle(*p);
		return r;
	}
	id_handle() : p(0) {}
	id_handle(ref_t&p,cont_t*c) : p(&p), c(c) {
		ref();
		ASSUME(this->p);
	}
	id_handle(const id_handle&h) {
		p = h.p;
		c = h.c;
		if (p) ref();
	}
	id_handle(id_handle&&h) {
		p=0;
		*this = std::move(h);
	}
	static id_handle from_handle(id_handle<ref_t,cont_t,0>&&h) {
		if (refcount_member!=nullptr&&h) atomic_inc(&((*h).*refcount_member));
		return id_handle(std::move((id_handle&&)h));
	}
	~id_handle() {
		if (p) deref();
	}
	id_handle&operator=(const id_handle&h) {
		close();
		p = h.p;
		c = h.c;
		if (p) ref();
		return *this;
	}
	id_handle&operator=(id_handle&&h) {
		std::swap(p,h.p);
		std::swap(c,h.c);
		return *this;
	}
	void abandon() {
		p=0;
	}
	void close() {
		if (p) deref();
		abandon();
	}
	operator bool() const {
		return p ? true : false;
	}
	bool operator!() const {
		return p ? false : true;
	}
	t*operator->() const {return &**p;}
	t&operator*() const {return **p;}
	int id() {return p->id;}
	void revive() {c->revive(*p);}
	void kill() {c->kill(*p);}
	template<uint32_t t::*ref1>
	int try_kill() {
		return c->try_kill<ref1>(*p);
	}
	template<uint32_t t::*ref1,uint32_t t::*ref2>
	int try_kill() {
		return c->try_kill<ref1,ref2>(*p);
	}
	template<uint32_t t::*ref1,uint32_t t::*ref2,typename F>
	int try_kill(F&&f) {
		return c->try_kill<ref1,ref2,F>(*p,std::forward<F>(f));
	}
};
template<typename t,typename cont_t>
struct id_handle_new: id_handle<t,cont_t,0> {
private:
	bool b_keep;
public:
	id_handle_new(const id_handle&h) : id_handle(h), b_keep(false) {}
	~id_handle_new() {
		if (b_keep && *this) revive();
	}
	id_handle_new(id_handle_new&h) : id_handle(h) {
		b_keep = h.b_keep;
		h.b_keep = true;
	}
	void keep() {
		b_keep = true;
	}
};

template<typename t,int id_offset=101>
struct id_list_t {
	typedef id_list_t self;
	typedef t t;
	typedef basic_slist_node avail_node;
	struct holder: avail_node {
		typedef t t;
		bool dead;
		int id;
		uint32_t r;
		uint64_t buf[(sizeof(t)-1)/sizeof(uint64_t)+1];
		holder() : dead(true), r(0) {}
		t&operator*() const {return (t&)buf;}
	};
	typedef id_handle<holder,self,nullptr> handle;
	typedef id_handle_new<holder,self> handle_new;
	template<uint32_t t::*refcount_member>
	struct handle_refcount {
		typedef id_handle<holder,self,refcount_member> type;
	};
	typedef boost::intrusive::circular_list_algorithms<avail_node> algo;
	avail_node avail_header;
	sync::busy_lock bl;
	int block_get, block_try_kill;
	holder list[0x1000];
	id_list_t() : block_get(0), block_try_kill(0) {
		algo::init_header(&avail_header);
		for (size_t i=0;i<0x1000;i++) {
			list[i].id = i+id_offset;
			algo::link_before(&avail_header,&list[i]);
		}
	}
	~id_list_t() {
		for (size_t i=0;i<0x1000;i++) {
			if (!list[i].next&&!list[i].r) outf(" !BUG! unreferenced object %d (id %d) leaked in %s\n",i,list[i].id,typeid(*this).name());
			//else if (!list[i].dead) outf("object %d (id %d, ref %d) leaked in %s\n",i,list[i].id,list[i].r,typeid(*this).name());
			//else if (list[i].r) outf("(dead) object %d (id %d, ref %d) leaked in %s\n",i,list[i].id,list[i].r,typeid(*this).name());
		}
	}
 	handle get_next(int id) {
		id -= id_offset-1;
		if (id<0) id=0;
		while (id<0x1000) {
			holder&h = list[id];
			handle r(h,this);
			while (atomic_read(&block_get))busy_yield();
			atomic_inc(&block_try_kill);
			if (atomic_read(&h.dead)) {atomic_dec(&block_try_kill);++id;continue;}
			atomic_dec(&block_try_kill);
			return r;
		}
		return handle();
 	}
	template<typename template_handle>
	template_handle get(int id) {
		mem_read_barrier();
		id -= id_offset;
 		if (id<0 || id>=0x1000) return template_handle();
		holder&h = list[id];
		handle r(h,this);
		// This loop is to ensure we do not execute the code below
		// at the same time as a try_kill is running.
		while (true) {
			while (atomic_read(&block_get))busy_yield();
			atomic_inc(&block_try_kill);
			if (atomic_read(&block_get)) {atomic_dec(&block_try_kill);continue;}
			break;
		}
		if (atomic_read(&h.dead)) {atomic_dec(&block_try_kill); return template_handle();}
		auto rv = template_handle::from_handle(std::move(r));
		atomic_dec(&block_try_kill);
		return rv;
	}
	handle get(int id) {
		return get<handle>(id);
	}
	template<uint32_t t::*ref>
	typename handle_refcount<ref>::type get(int id) {
		return get<handle_refcount<ref>::type>(id);
	}
	handle_new get_new() {
		bl.lock();
		if (algo::unique(&avail_header)) return bl.unlock(), handle_new(handle());
		holder&h = (holder&)*algo::node_traits::get_next(&avail_header);
		algo::unlink(&h);
		h.prev = 0;
		bl.unlock();
		new (&*h) t();
		return handle_new(handle(h,this));
	}
	std::pair<bool,handle> get_always(int id) {
		id -= id_offset;
 		if (id<0 || id>=0x1000) xcept("get_always bad id %d(-%d)",id+id_offset,id_offset);
		holder&h = list[id];
		handle r(h,this);
		while (atomic_read(&block_get)) busy_yield();
		if (!h.prev) return std::make_pair(false,r);
		bl.lock();
		if (!h.prev) return bl.unlock(), std::make_pair(false,r);
		algo::unlink(&h);
		h.prev = 0;
		bl.unlock();
		new (&*h) t();
		revive(h);
		return std::make_pair(true,r);
	}
	void try_recycle(holder&h) {
		if (h.prev) return;
		// Block get to prevent new handles being returned on this object (actually
		// this only prevents get from ref'ing our object AND return it, but if it
		// already ref'ed it (and the handle is still valid!), then h.r won't be 0).
		// The cas ensures the object is not recycled multiple times simultaneously
		atomic_inc(&block_get);
		if (h.r==0 && atomic_cas((void**)&h.prev,0,(void*)~0)==0) {
			h.dead = true;
			(*h).~t();
			bl.lock();
			algo::link_before(&avail_header,&h);
			bl.unlock();
		}
		atomic_dec(&block_get);
	}
	void revive(holder&h) {
		h.dead = false;
		atomic_inc(&h.r);
	}
	void kill(holder&h) {
		h.dead = true;
		atomic_dec(&h.r);
	}
	template<uint32_t t::* ref1>
	int try_kill(holder&h) {
		return try_kill<ref1,0>(h,[](){return false;});
	}
	template<uint32_t t::* ref1,uint32_t t::* ref2>
	int try_kill(holder&h) {
		return try_kill<ref1,ref2>(h,[](){return false;});
	}
	template<uint32_t t::* ref1,uint32_t t::* ref2,typename F>
	int try_kill(holder&h,F&&f) {
		int r;
		atomic_inc(&block_get);
		while (atomic_read(&block_try_kill))busy_yield();
		if ((*h).*ref1) r=1;
		else if (ref2!=0 && (*h).*ref2) r=2;
		else if (f()) r=3;
		else {
			h.dead = true;
			r = 0;
			atomic_dec(&h.r);
		}
		atomic_dec(&block_get);
		return r;
	}
};

template<typename t>
struct sharable_lock {
	t&l;
	sharable_lock(t&l) : l(l) {
		l.lock_sharable();
	}
	~sharable_lock() {
		l.unlock_sharable();
	}
};


template<typename t,typename cont_t>
struct ipc_handle {
private:
	cont_t*c;
	t*p;
	uint32_t*ref;
public:
	ipc_handle() : c(0), p(0), ref(0) {}
	ipc_handle(cont_t*c,t*p,uint32_t*ref) : c(c), p(p), ref(ref) {
		if (ref) atomic_inc(ref);
	}
	ipc_handle(const ipc_handle&h) {
		c = h.c;
		p = h.p;
		ref = h.ref;
		if (ref) atomic_inc(ref);
	}
	~ipc_handle() {
		close();
	}
	ipc_handle&operator=(const ipc_handle&h) {
		close();
		c = h.c;
		p = h.p;
		ref = h.ref;
		if (ref) atomic_inc(ref);
		return *this;
	}
	void close() {
		if (ref) {
			if (!atomic_dec(ref)) c->try_destroy(*this);
		}
		ref = 0;
		p = 0;
		c = 0;
	}
	operator bool() const {
		return p ? true : false;
	}
	bool operator!() const {
		return p?false:true;
	}
	t*operator->() const {return p;}
	t&operator*() const {return *p;}
};

template<typename t>
struct ipc_list_t2 {
	typedef ipc_list_t2 self;
	typedef t t;
	typedef ipc_handle<t,self> handle;
	boost::interprocess::interprocess_upgradable_mutex mut;
	struct holder {
		uint32_t r;
		boost::interprocess::offset_ptr<t> p;
		holder() : r(0), p(0) {}
	};
	typename shm_map<uint64_t,holder>::type map;
	shm_allocator<t> alloc;
	handle get_new(uint64_t id) {
		mem_read_barrier();
		boost::unique_lock<boost::interprocess::interprocess_upgradable_mutex> l(mut);
		auto r = map.insert(std::make_pair(id,holder()));
		if (!r.second) return handle();
		holder&h = r.first->second;
		h.p = alloc.allocate(1);
		alloc.construct(h.p);
		mem_write_barrier();
		return handle(this,&*h.p,&h.r);
	}
	handle get_existing(uint64_t id) {
		mem_read_barrier();
		sharable_lock<boost::interprocess::interprocess_upgradable_mutex> l(mut);
		auto r = map.find(id);
		if (r==map.end()) return handle();
		holder&h = r->second;
		return handle(this,&*h.p,&h.r);
	}
	std::pair<bool,handle> get(uint64_t id) {
		mem_read_barrier();
		boost::unique_lock<boost::interprocess::interprocess_upgradable_mutex> l(mut);
		auto r = map.insert(std::make_pair(id,holder()));
		holder&h = r.first->second;
		bool created = r.second;
		if (created) {
			h.p = alloc.allocate(1);
			alloc.construct(h.p);
		}
		mem_write_barrier();
		return std::make_pair(created,handle(this,&*h.p,&h.r));
	}
	void try_destroy(handle&h) {
		//xcept("ipc_list_t try_destroy");
	}
};

template <typename id_list_t,typename ipc_list_t,typename ipc_list_t::handle id_list_t::t::* pointer_to_handle,int id_offset=80>
struct id_list_ipc_proxy {
	typedef id_list_ipc_proxy self;
	typedef typename id_list_t::t t;
	typedef typename id_list_t::handle handle;
	typedef typename id_list_t::handle_new handle_new;
	ipc_list_t*ipc_list;
	id_list_t*id_list;
	id_list_ipc_proxy(id_list_t*id_list,ipc_list_t*ipc_list) : id_list(id_list), ipc_list(ipc_list), next_id(id_offset) {}
	uint32_t next_id;
	handle_new get_new() {
		// todo: make a ipc_list::get_new() with no parameters
		for (int i=0;i<0x1000;i++) {
			int id = (atomic_inc(&next_id)-1)&0xfff;
			auto h = ipc_list->get_new(id);
			if (h) {
				auto h2 = id_list->get_always(id).second;
				*h2.*pointer_to_handle = std::move(h);
				return handle_new(h2);
			}
		}
		return handle_new(handle());
	}
	handle get(int id) {
		return id_list->get(id);
	}
// 	int destroy(handle&h) {
// 		return id_list->destroy(h);
// 	}
// 	int destroy(int id) {
// 		handle h = get(id);
// 		if (!h) return ESRCH;
// 		return destroy(h);
// 	}
};


template<typename t>
struct ref_ptr {
	t*p;
	ref_ptr() : p(0) {}
	ref_ptr(t*p) : p(p) {
		atomic_inc(&p->ref_count);
	}
	ref_ptr(const ref_ptr&n) {
		p = n.p;
		atomic_inc(&p->ref_count);
	}
	ref_ptr(ref_ptr&&n) {
		p = n.p;
		n.p = 0;
	}
	~ref_ptr() {
		if (p&&!atomic_dec(&p->ref_count)) delete p;
	}
	ref_ptr&operator=(t*p_arg) {
		if (p_arg) reset();
		p = p_arg;
		atomic_inc(&p->ref_count);
		return *this;
	}
	ref_ptr&operator=(const ref_ptr&n) {
		if (p) reset();
		p = n.p;
		atomic_inc(&p->ref_count);
		return *this;
	}
	ref_ptr&operator=(ref_ptr&&n) {
		swap(n);
		return *this;
	}
	void reset() {
		if (p) atomic_dec(&p->ref_count);
		p = 0;
	}
	void swap(ref_ptr&n)  {
		std::swap(p,n.p);
	}
	operator bool() const {
		return p ? true : false;
	}
	bool operator!() const {
		return p ? false : true;
	}
	t*operator->() const {return p;}
	t&operator*() const {return *p;}
};

template<typename t>
t&singleton() {
	static int lock;
	static t*p;
	if (p) return *p;
	if (atomic_xchg(&lock,1)) {
		do busy_yield();
		while (!atomic_read(&p));
		return *p;
	}
	static t v;
	p = &v;
	return v;
}


