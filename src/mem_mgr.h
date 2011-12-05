
static const int reserve_granularity = 1024*1024;

bool mm_reserve(void*addr) {
	if ((uint64_t)addr%reserve_granularity) xcept("attempt to reserve unaligned address %p",addr);
	void*a = VirtualAlloc(addr,reserve_granularity,MEM_RESERVE,PAGE_READWRITE);
	if (a&&a!=addr) xcept("VirtualAlloc(%p,MEM_RESERVE) returned unexpected value %p",addr,a);
	return a?true:false;
}

void mm_reserve_area(void*addr,size_t size) {
	if ((uint64_t)addr%reserve_granularity) xcept("attempt to reserve unaligned address %p",addr);
	for (uintptr_t a=(uintptr_t)addr;a<(uintptr_t)addr+size;a+=reserve_granularity) {
		if (!mm_reserve((void*)a)) xcept("failed to reserve %p; last error %d",(void*)a,GetLastError());
	}
}

void mm_release(void*addr) {
	if ((uint64_t)addr%reserve_granularity) xcept("attempt to release unaligned address %p",addr);
	if (!VirtualFree(addr,0,MEM_RELEASE)) xcept("VirtualFree(%p,0,MEM_RELEASE) failed; last error %d",addr,GetLastError());
}

void mm_release_area(void*addr,size_t size) {
	for (size_t a=(size_t)addr;a<(size_t)addr+size;a+=1024*1024) {
		mm_release((void*)a);
	}
}

void mm_commit(void*addr,size_t size) {
	//dbgf("commit %p + %#x\n",addr,size);
	if (!VirtualAlloc(addr,size,MEM_COMMIT,PAGE_READWRITE)) {
		xcept("VirtualAlloc(%p,%d,MEM_COMMIT) failed; last error %d",addr,size,GetLastError());
	}
}
void mm_decommit(void*addr,size_t size) {
	//dbgf("decommit %p + %#x\n",addr,size);
	if (!VirtualFree(addr,size,MEM_DECOMMIT)) xcept("VirtualFree(%p,%d,MEM_DECOMMIT) failed; last error %d",addr,size,GetLastError());
}

struct mem_mgr {
	struct region;
	typedef basic_rbtree_node<region> region_node;
	struct region: region_node {
		uint32_t begin,end;
	};
	struct region_cmp {
		bool operator()(const region*a,const region*b) const {
			return a->begin<b->begin;
		}
	};
	struct region_map {
		typedef boost::intrusive::rbtree_algorithms<region> algo;
		region header;
		uint32_t total_range_size;
		region_map() : total_range_size(0) {
			algo::init_header(&header);
		}
		~region_map() {
			while (!algo::unique(&header)) {
				region*n = algo::unlink_leftmost_without_rebalance(&header);
				delete n;
			}
		}
		region*begin() {
			return algo::begin_node(&header);
		}
		region*end() {
			return &header;
		}
		region*next(region*i) {
			return algo::next_node(i);
		}
		uint32_t size() {
			return total_range_size;
		}
		region*try_merge_range(uint32_t begin,uint32_t end) {
			auto insert_hint = &header;
			// This compare function is compatible only because it is guaranteed there are no adjacent regions
			for (auto i = algo::lower_bound(&header,begin,[](const region*a,uint32_t begin) {return a->end<begin;});i!=&header && i->begin<=end;i=algo::next_node(i)) {
				region&r = *i;
				if (begin<r.end&&end>r.begin) xcept("overlapping addresses [%#x,%#x) [%#x,%#x)",begin,end,r.begin,r.end);
				if (begin==i->end) {
					i = algo::next_node(i);
					if (i!=&header&&end>i->begin) xcept("overlapping addresses [%#x,%#x) [%#x,%#x)",begin,end,i->begin,i->end);
					total_range_size += end-r.end;
					r.end = end;
					return 0;
				} else if (i->begin==end) {
					total_range_size += r.begin-begin;
					r.begin = begin;
					return 0;
				}
				if (r.begin>begin && !insert_hint) insert_hint = i;
			}
			return insert_hint;
		}
		void add_range(uint32_t begin,uint32_t end) {
			auto insert_hint = try_merge_range(begin,end);
			if (!insert_hint) return;
			region&nr = *new region();
			nr.begin = begin;
			nr.end = end;
			algo::insert_equal(&header,insert_hint,&nr,region_cmp());
			total_range_size += end-begin;
		}
		void add_region(region&r) {
			auto insert_hint = try_merge_range(r.begin,r.end);
			if (!insert_hint) {
				delete &r;
				return;
			} else {
				algo::insert_equal(&header,insert_hint,&r,region_cmp());
				total_range_size += r.end-r.begin;
			}
		}
		bool try_extract(uint32_t begin,uint32_t end) {
			for (auto i = algo::lower_bound(&header,begin,[](const region*a,uint32_t begin) {return a->end<=begin;});i!=&header&&i->begin<end;i=algo::next_node(i)) {
				if (end>i->end) continue;
				region&r = *i;
				if (r.begin==begin&&r.end==end) {
					algo::erase(&header,&r);
					delete &r;
				} else if (r.begin==begin) {
					r.begin = end;
				} else if (r.end==end) {
					r.end = begin;
				} else {
					region&nr = *new region();
					nr.begin = end;
					nr.end = r.end;
					r.end = begin;
					algo::insert_equal(&header,algo::next_node(i),&nr,region_cmp());
				}
				total_range_size -= end-begin;
				return true;
			}
			return false;
		}
		void extract(uint32_t begin,uint32_t end) {
			if (!try_extract(begin,end)) xcept("failed to extract range [%#x,%#x)",begin,end);
		}
	};
	uint32_t max_avail_size;
	mem_mgr() : max_avail_size(0) {}
	~mem_mgr() {}
	region_map avail_address_map;
	boost::mutex avail_address_map_mutex;
	void add_address_range(uint32_t begin,uint32_t end) {
		boost::unique_lock<boost::mutex> l(avail_address_map_mutex);
		avail_address_map.add_range(begin,end);
		uint32_t s = avail_address_map.size();
		if (s>max_avail_size) max_avail_size=s;
	}
	void dump_avail_address_map() {
		boost::unique_lock<boost::mutex> l(avail_address_map_mutex);
		dbgf("Available address map ::\n");
		for (auto i=avail_address_map.begin();i!=avail_address_map.end();i=avail_address_map.next(i)) {
			region&r = *i;
			uint32_t size = r.end-r.begin;
			if (size&0xfffff) dbgf("  [%#x,%#x) - %dM + %dK\n",r.begin,r.end,size/0x100000,size/0x400&0x3ff);
			else dbgf("  [%#x,%#x) - %dM\n",r.begin,r.end,size/0x100000);
		}
		uint32_t total = avail_address_map.size();
		if (total&0xfffff) dbgf("Total size: %dM + %dK\n",total/0x100000,total/0x400&0x3ff);
		else dbgf("Total size: %dM\n",total/0x100000);
	}
	// alignment must be a power of 2
	uint32_t get_address(uint32_t alignment,uint32_t size,uint32_t min_addr=0,uint32_t max_addr=-1) {
		//dbgf("get_address alignment %u size %u\n",alignment,size);
		//dump_avail_address_map();
		uint32_t mask = ~(alignment-1);
		if ((alignment&mask)!=alignment) xcept("get_address alignment is not a power of 2");
		boost::unique_lock<boost::mutex> l(avail_address_map_mutex);
		for (auto i=avail_address_map.begin();i!=avail_address_map.end();i=avail_address_map.next(i)) {
			region&r = *i;
			uint32_t b = ((r.begin-1)&mask)+alignment;
			uint32_t e = b+size;
			if (e<b) break;
			if (b<min_addr || e>max_addr) continue;
			if (e<=r.end) {
				avail_address_map.extract(b,e);
				return b;
			}
		}
		return 0;
	}
	bool try_extract(uint32_t begin,uint32_t end) {
		return avail_address_map.try_extract(begin,end);
	}
	struct allocated_block {
		uint32_t begin, end;
		allocated_block(uint32_t begin,uint32_t end) : begin(begin), end(end) {}
		bool operator==(const allocated_block&o) const {
			return o.begin==begin;
		}
		operator size_t() const {
			return begin/0x10000;
		}
	};

	std::unordered_set<allocated_block> allocated_block_set;
	boost::mutex allocate_mutex;
	// alignment must be a multiple of 65536
	uint32_t allocate(uint32_t alignment,uint32_t size) {
		size = (size-1&~0xffff)+0x10000;
		if (alignment&0xffff) xcept("alignment&0xffff");
		uint32_t addr = get_address(alignment,size);
		if (!addr) return 0;
		// Apparently, Windows doesn't like commits across multiple reserves 
		// (returns 487, invalid address). Can't find anything about this in
		// the documentation.
		uint32_t a=(addr-1&-reserve_granularity)+reserve_granularity;
		if (a>addr+size) a=addr+size;
		if (a-addr) mm_commit((void*)addr,a-addr);
		for (;a+reserve_granularity<addr+size;a+=reserve_granularity) {
			mm_commit((void*)a,reserve_granularity);
		}
		if (addr+size-a) mm_commit((void*)a,addr+size-a);
		boost::unique_lock<boost::mutex> l(allocate_mutex);
		allocated_block_set.insert(allocated_block(addr,addr+size));
		return addr;
	}
	bool free(uint32_t addr) {
		uint32_t size;
		{
			boost::unique_lock<boost::mutex> l(allocate_mutex);
			auto i = allocated_block_set.find(allocated_block(addr,0));
			if (i==allocated_block_set.end()) return false;
			allocated_block_set.erase(i);
			size=i->end-i->begin;
		}
		// Same goes when decommitting.
		uint32_t a=(addr-1&-reserve_granularity)+reserve_granularity;
		if (a>addr+size) a=addr+size;
		if (a-addr) mm_decommit((void*)addr,a-addr);
		for (;a+reserve_granularity<addr+size;a+=reserve_granularity) {
			mm_decommit((void*)a,reserve_granularity);
		}
		if (addr+size-a) mm_decommit((void*)a,addr+size-a);
		add_address_range(addr,addr+size);
		return true;
	}
	static const uint32_t max_reserve = -1;
	void find_addresses(uint32_t begin,uint32_t end) {
		// TODO: Make it skip already reserved addresses
		begin = (begin-1&-reserve_granularity) + reserve_granularity;
		if (!begin) begin = reserve_granularity;
		for (uint32_t addr=begin;addr<end;) {
			if (avail_address_map.size()>=max_reserve) break;
			if (mm_reserve((void*)addr)) {
				add_address_range(addr,addr+reserve_granularity);
			}
			addr += reserve_granularity;
		}
	}
	uint32_t avail_size() {
		return avail_address_map.size();
	}
	uint32_t total_size() {
		return max_avail_size;
	}
};

mem_mgr user_mem_mgr;

void*mm_alloc(size_t size) {
	return (void*)user_mem_mgr.allocate(0x10000,size);
}
void mm_free(void*addr) {
	if (!user_mem_mgr.free((uint32_t)addr)) xcept("user_mem_mgr.free(%p) failed",addr);
}


void mm_find_addresses() {
	static bool si_valid=false;
	static SYSTEM_INFO si;
	if (!si_valid) {
		GetSystemInfo(&si);
		si_valid = true;
	}

	uint32_t g = (uint32_t)si.dwAllocationGranularity;
	uint32_t p = (uint32_t)si.dwPageSize;

	if (reserve_granularity%g) xcept("Reserve granularity (%d) is not a multiple of the allocation granularity (%d)",reserve_granularity,g);
	user_mem_mgr.find_addresses(0,0x80000000);
	user_mem_mgr.dump_avail_address_map();
}

void mm_setup() {

	SYSTEM_INFO si;
	GetSystemInfo(&si);

	uint32_t g = (uint32_t)si.dwAllocationGranularity;
	uint32_t p = (uint32_t)si.dwPageSize;

	dbgf("Page Size: %d\n",si.dwPageSize);
	dbgf("Min address: %p\n",si.lpMinimumApplicationAddress);
	dbgf("Max address: %p\n",si.lpMaximumApplicationAddress);
	dbgf("Allocation granularity: %d\n",si.dwAllocationGranularity);

	if (0x100000%p) xcept("Page size (%d) is not a multiple of 1M",si.dwPageSize);

	mm_find_addresses();
}

struct mm_setuper {
	mm_setuper() {
		mm_setup();
	}
} mm_setuper;

template<typename T>
struct mm_allocator {
	typedef T value_type;
	typedef T*pointer;
	typedef T&reference;
	typedef const T*const_pointer;
	typedef const T&const_reference;
	typedef size_t size_type;
	typedef ptrdiff_t difference_type;
	mm_allocator() {}
	template<typename U> mm_allocator(const mm_allocator<U>&) {}
	pointer address(reference x) const {return &x;}
	const_pointer address(const_reference x) const {return &x;}
	pointer allocate(size_type n,void*hint=0) {
		pointer r = (pointer)mm_alloc(sizeof(T)*n);
		if (!r) xcept("mm_allocator::allocate failed");
		return r;
	}
	void deallocate(pointer p,size_type n) {
		mm_free(p);
	}
	size_t max_size() const {
		return (size_t)-1/sizeof(T);
	}
	void construct(pointer p,const_reference val) {
		new (p) T(val);
	}
	void destroy(pointer p) {
		p->~T();
	}
	template<typename T>
	struct rebind {
		typedef mm_allocator<T> other;
	};
};

