

struct page_attr_t {
	uint64_t prot_flags;
	uint64_t access_flags;
	uint32_t page_size;
};

struct memory_size_t {
	uint32_t total, available;
};

int sys_memory_allocate(uint32_t size,uint64_t flags,uint32_t *alloc_addr) {
	dbgf("sys_memory_allocate size %d, flags %#x\n",size,flags);
	uint32_t addr;
	if (flags==0x400) { // 1m page size
		if (size&0xfffff) return EALIGN;
		addr = user_mem_mgr.allocate(0x100000,size);
	} else if (flags==0x200) { // 64k page size
		if (size&0xffff) return EALIGN;
		addr = user_mem_mgr.allocate(0x10000,size);
	} else return EINVAL;
	if (!addr) return ENOMEM;
	dbgf("allocated memory at %#x\n",addr);
	*alloc_addr = se((uint32_t)addr);
	return CELL_OK;
}

int sys_memory_free(uint32_t start_addr) {
	dbgf("sys_memory_free %#x\n",start_addr);
	if (user_mem_mgr.free(start_addr)) return CELL_OK;
	else return EINVAL;
}

struct mmapper_ipc_mem_t {
	shm_string name;
};

struct mmapper_mem_t {
	HANDLE h;
	uint64_t size, granularity;
	mmapper_mem_t() : size(0), granularity(0), h(0) {}
	void map(uint32_t addr) {
		// It is unfortunate that Windows does not allow mapping memory to a reserved
		// region. Alas, we must release the memory and then map it. Technically,
		// another thread could reserve the memory between these two calls, in
		// which case we just break down and cry.
		mm_release_area((void*)addr,size);
		void*r = MapViewOfFileEx(h,FILE_MAP_ALL_ACCESS,0,0,size,(void*)addr);
		if (!r) {
			xcept("MapViewOfFileEx failed; last error %d",GetLastError());
		}
		if (r!=(void*)addr) xcept("MapViewOfFileEx(%p) returned unexpected address %p",(void*)addr,r);
	}
	void unmap(uint32_t addr) {
		xcept("unmap");
	}
};

id_list_t<mmapper_mem_t> mmapper_list;
typedef id_list_t<mmapper_mem_t>::handle mmapper_handle;
shm_object<ipc_list_t2<mmapper_ipc_mem_t>> ipc_mmapper;

uint64_t get_granularity(uint64_t flags) {
	if ((flags&0xf00)==0x400) return 1024*1024;
	else if ((flags&0xf00)==0x200) return 1024*64;
	else xcept("invalid granularity flags %x",flags&0xf00);
}

int sys_mmapper_allocate_shared_memory(uint64_t ipc_key,uint64_t size,uint64_t flags,uint32_t*mem_id) {
	dbgf("mmapper allocate shared memory, ipc_key %x, size %x, flags %x\n",ipc_key,size,flags);

	*mem_id = se((uint32_t)~0);

	uint64_t g = get_granularity(flags);
	if (size%g) return EALIGN;
	if ((uint32_t)size!=size) return ENOMEM;
	flags&=~0xf00;

	int mode = flags&0xf000;
	flags&=~0xf000;
	if (flags) xcept("sys_mmapper_allocate_shared_memory: unknown remaining flags %#x",flags);

	if (mode!=0&&mode!=0xc000) xcept("sys_mmapper_allocate_shared_memory unknown mode %#x\n",mode);

	auto h = mmapper_list.get_new();
	if (!h) return EAGAIN;
	int id = h.id();
	mmapper_mem_t&m = *h;

	m.size = size;
	m.granularity = g;

	const char*n;
	if (ipc_key==0xffff000000000000) {n=0;mode=0xc000;}
	else n = generate_ipc_name("sharedmem",ipc_key);

	if (mode==0xc000) {
		HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE,0,PAGE_READWRITE|SEC_COMMIT,0,(uint32_t)size,n);
		if (!h) xcept("CreateFileMappingA failed; last error %d",GetLastError());
		if (GetLastError()==ERROR_ALREADY_EXISTS) {
			CloseHandle(h);
			return EEXIST;
		}
		m.h = h;
	} else if (mode==0) {
		HANDLE h = OpenFileMappingA(FILE_MAP_ALL_ACCESS,FALSE,n);
		if (!h) dbgf("open %s failed with error %d\n",n,GetLastError());
		if (!h) return ESRCH;
		m.h = h;
	} else xcept("bad mode");
	

	*mem_id = se((uint32_t)id);
	dbgf("mem_id %d\n",id);
	h.keep();
	return CELL_OK;
}

struct allocated_address {
	uint32_t begin, end, page_size;
	std::map<uint32_t,mmapper_handle> mapped;
	mem_mgr mgr;
};

allocated_address*allocated_addresses[0x10];
boost::mutex mmapper_mutex;

int sys_mmapper_allocate_address(uint64_t size,uint64_t flags,uint64_t alignment,uint32_t *alloc_addr) {
	dbgf("mmapper allocate address, size %x, flags %x, alignment %x, alloc_addr %p\n",size,flags,alignment,alloc_addr);
	// flags specifies page size and access rights (ppu, spu thread, raw spu)

	if (alignment==0) alignment = 1024*1024*256;
	if (alignment<1024*1024*256) return EALIGN;
	if ((alignment&~(alignment-1))!=alignment) return EALIGN;
	if (size%1024*1024*256) return EALIGN;
	if ((uint32_t)alignment!=alignment) return EALIGN;
	if ((uint32_t)size!=size) return EALIGN;

	uint32_t page_size;
	if ((flags&0xf00)==0x400) { // 1m page size
		page_size = 1024*1024;
	} else if ((flags&0xf00)==0x200) { // 64k page size
		page_size = 1024*64;
	} else return EINVAL;

	uint32_t addr = user_mem_mgr.get_address((uint32_t)alignment,(uint32_t)size);
	if (!addr) return ENOMEM;

	allocated_address&a=*new allocated_address();
	a.begin = addr;
	a.end = addr + (uint32_t)size;
	a.page_size = page_size;
	a.mgr.add_address_range(a.begin,a.end);
	for (uint32_t i=a.begin;i<a.end;i+=1024*1024*256) {
		allocated_addresses[i/(1024*1024*256)] = &a;
	}

	dbgf("returning address %#x\n",addr);
	*alloc_addr = se((uint32_t)addr);
	
	return CELL_OK;
}


int sys_mmapper_map_shared_memory(uint32_t addr,uint32_t mem_id,uint64_t flags) {
	dbgf("mmapper map memory, start_addr %#x, mem_id %d, flags %#x\n",addr,mem_id,flags);
	auto h = mmapper_list.get(mem_id);
	if (!h) return ESRCH;
	mmapper_mem_t&m = *h;
	boost::unique_lock<boost::mutex> l(mmapper_mutex);
	allocated_address*a = allocated_addresses[addr/(1024*1024*256)];
	if (!a) return EINVAL;
	if ((uint64_t)addr+m.size>=a->end) return EINVAL;
	if (addr&(a->page_size-1)) return EALIGN;
	if (m.granularity&(a->page_size-1)) return EALIGN;
	if (!a->mgr.try_extract(addr,addr+m.size)) return EBUSY;
	a->mapped[addr] = h;
	m.map(addr);
	dbgf("memory mapped to %#x\n",addr);
	return CELL_OK;
}

int sys_mmapper_unmap_shared_memory(uint32_t addr,uint32_t*mem_id) {
	*mem_id = ~0;
	boost::unique_lock<boost::mutex> l(mmapper_mutex);
	allocated_address*a = allocated_addresses[addr/(1024*1024*256)];
	if (!a) return EINVAL;
	auto i = a->mapped.find(addr);
	if (i==a->mapped.end()) return EINVAL;
	auto h = std::move(i->second);
	a->mapped.erase(i);
	h->unmap(addr);
	return CELL_OK;
}
	
int sys_mmapper_search_and_map(uint32_t start_addr,uint32_t mem_id,uint64_t flags,uint32_t*alloc_addr) {
	dbgf("mmapper search and map, start_addr %#x, mem_id %d, flags %x, alloc_addr %p\n",start_addr,mem_id,flags,alloc_addr);
	
	boost::unique_lock<boost::mutex> l(mmapper_mutex);
	allocated_address*a = allocated_addresses[start_addr/(1024*1024*256)];
	if (!a) return EINVAL;
	if (start_addr!=a->begin) return EINVAL;

	auto h = mmapper_list.get(mem_id);
	if (!h) return ESRCH;
	mmapper_mem_t&m = *h;

	if (m.granularity&(a->page_size-1)) return EALIGN;

	uint32_t addr = a->mgr.get_address(m.granularity,m.size);
	if (!addr) return ENOMEM;
	a->mapped[addr] = h;
	m.map(addr);
	dbgf("memory mapped to %#x\n",addr);
	*alloc_addr = se((uint32_t)addr);
	return CELL_OK;
}

int sys_memory_get_user_memory_size(memory_size_t*ms) {
	dbgf("memory get user memory size\n");
	ms->available = se((uint32_t)(user_mem_mgr.avail_size()));
	ms->total = se((uint32_t)(user_mem_mgr.total_size()));
	return CELL_OK;
}
int sys_memory_get_page_attribute(uint32_t addr,page_attr_t*attr) {
	dbgf("memory get page attribute, addr %#x\n",addr);
	MEMORY_BASIC_INFORMATION mi;
	if (VirtualQuery((void*)addr,&mi,sizeof(mi))<sizeof(mi)) return EINVAL;
	if (mi.State!=MEM_COMMIT) return EINVAL;
	attr->access_flags = se((uint64_t)0xf); // ppu/spu/everything has access
	switch (mi.Protect&0xFF) {
	case PAGE_READWRITE:
	case PAGE_EXECUTE_READWRITE:
	case PAGE_WRITECOPY:
	case PAGE_EXECUTE_WRITECOPY:
		attr->prot_flags = se((uint64_t)0x40000); // read write
		break;
	case PAGE_READONLY:
	case PAGE_EXECUTE_READ:
		attr->prot_flags = se((uint64_t)0x80000); // read only
		break;
	default:
		attr->prot_flags = 0;
	}
	attr->page_size = se((uint32_t)1024*64);
	return CELL_OK;
}


struct memory_container_t {
	mem_mgr mgr;
};

id_list_t<memory_container_t> memory_container_list;

int sys_memory_container_create(uint32_t*container,uint32_t memsize) {
	dbgf("memory container create, container %p, memsize %d\n",container,memsize);
	memsize = memsize&~(1024*1024-1); // round down to multiple of 1M
	if (!memsize) return ENOMEM;
	auto h = memory_container_list.get_new();
	if (!h) return EAGAIN;
	int id = h.id();
	*container = se((uint32_t)id);
	memory_container_t&c = *h;
	uint32_t addr = user_mem_mgr.get_address(1024*1024,(uint32_t)memsize);
	if (!addr) return ENOMEM;
	c.mgr.add_address_range(addr,addr+memsize);
	dbgf("container created with id %d\n",id);
	h.keep();
	return CELL_OK;
}
int sys_memory_container_destroy(uint32_t container) {
	dbgf("memory container destroy %d\n",container);
	auto h = memory_container_list.get(container);
	if (!h) return ESRCH;
	memory_container_t&c = *h;
	if (c.mgr.avail_size()!=c.mgr.total_size()) return EBUSY;
	uint32_t size = c.mgr.total_size();
	uint32_t addr = c.mgr.get_address(1024*1024,size);
	if (!addr) xcept("memory container destroy: get_address failed");
	user_mem_mgr.add_address_range(addr,addr+size);
	h.kill();
	return CELL_OK;
}
int sys_memory_container_get_size(memory_size_t*ms,uint32_t container) {
	dbgf("memory container get size, container %d\n",container);
	auto h = memory_container_list.get(container);
	if (!h) return ESRCH;
	memory_container_t&c=*h;
	ms->available = se((uint32_t)(c.mgr.avail_size()));
	ms->total = se((uint32_t)(c.mgr.total_size()));
	return CELL_OK;
}
int sys_memory_allocate_from_container(uint32_t memsize,uint32_t container,uint64_t flags,uint32_t*addr) {
	dbgf("memory container allocate, container %d, size %d, flags %#x\n",container,memsize,flags);
	auto h = memory_container_list.get(container);
	if (!h) return ESRCH;
	memory_container_t&c = *h;
	uint32_t a;
	if (flags==0x400) { // 1m page size
		if (memsize&0xfffff) return EALIGN;
		a = c.mgr.allocate(0x100000,memsize);
	} else if (flags==0x200) { // 64k page size
		if (memsize&0xffff) return EALIGN;
		a = c.mgr.allocate(0x10000,memsize);
	} else return EINVAL;
	if (!a) return ENOMEM;
	dbgf("allocated memory at %#x\n",a);
	*addr = se((uint32_t)a);
	return CELL_OK;
}




