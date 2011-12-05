

int sys_rsx_device_map(uint32_t*a1,uint32_t*a2,uint32_t a3) {
	dbgf("rsx device map, %p, %p, %#x\n",a1,a2,a3);
	a1[1] = se((uint32_t)20);
	return CELL_OK;
}

struct rsx_memory_t {
	char*mem;
	uint32_t mem_size;
};

id_list_t<rsx_memory_t> rsx_memory_list;
typedef id_list_t<rsx_memory_t>::handle rsx_memory_handle;

int sys_rsx_memory_allocate(uint32_t*mem_id,uint64_t*mem_addr,uint32_t mem_size,uint32_t a4,uint32_t a5,uint32_t a6,uint32_t a7) {
	dbgf("rsx memory allocate, mem_size %#x, %#x, %#x, %#x, %#x\n",mem_size,a4,a5,a6,a7);

	auto h = rsx_memory_list.get_new();
	if (!h) return EAGAIN;
	rsx_memory_t&m = *h;

	m.mem_size = mem_size;
	m.mem = (char*)mm_alloc(mem_size);

	dbgf("rsx memory allocated at %p\n",m.mem);

	*mem_id = se((uint32_t)h.id());
	*mem_addr = se((uint64_t)m.mem);

	h.keep();
	return CELL_OK;
}

struct rsx_context_t {
	rsx_memory_handle mem;
	sync::busy_lock busy_lock;
	char*control,*info,*report;
	void*addr;
	uint32_t size;
	uint32_t put,get;
	win32_thread cmd_thread;
	rsx_context_t() : addr(0), size(0), put(0), get(0) {}
};
id_list_t<rsx_context_t> rsx_context_list;

#include "rsx_cmd.h"

// TODO: Thread '_gcm_intr_thread' does receive on event queue 0. It probably reads an event queue id from somewhere
//       that one of these syscalls are expected to fill in.
//       We probably won't ever fire off a gcm interrupt anyways, so maybe we don't care.
int context_count=0;
int sys_rsx_context_allocate(uint32_t*context_id,uint64_t*control_addr,uint64_t*info_addr,uint64_t*report_addr,uint32_t a5,uint64_t a6,uint32_t*a7) {
	dbgf("rsx context allocate, %#x, %#x\n",a5,a6);
	if (atomic_inc(&context_count)!=1) xcept("multiple contexts?");
	auto mh = rsx_memory_list.get(a5);
	if (!mh) return ESRCH;
	rsx_memory_t&m = *mh;
	auto h = rsx_context_list.get_new();
	if (!h) return EAGAIN;
	rsx_context_t&c = *h;

	c.mem = mh;

	c.control = (char*)mm_alloc(1024*1024);
	c.info = (char*)mm_alloc(1024*1024);
	c.report = (char*)mm_alloc(1024*1024*10);

	dbgf("control is %p, info is %p, report is %p\n",c.control,c.info,c.report);

	(uint32_t&)c.info[0] = se((uint32_t)0x211);
	(uint32_t&)c.info[4] = se((uint32_t)0xc);
	(uint32_t&)c.info[8] = se((uint32_t)1024*1024*256);
	(uint32_t&)c.info[0xc] = se((uint32_t)1);
	(uint32_t&)c.info[0x10] = se((uint32_t)500000000);
	(uint32_t&)c.info[0x14] = se((uint32_t)650000000);

	(uint32_t&)c.control[0x40] = 0;  // put
	(uint32_t&)c.control[0x44] = 0;  // get
	(uint32_t&)c.control[0x48] = -1; // ref

	(uint32_t&)c.report[0x0ff10000-0x0fe00000 + 0x30] = se((uint32_t)1);

	*context_id = se((uint32_t)h.id());
	*control_addr = se((uint64_t)c.control);
	*info_addr = se((uint64_t)c.info);
	*report_addr = se((uint64_t)c.report);

	rsx::run_cmd_thread(c);

	h.keep();
	return CELL_OK;
}

int sys_rsx_context_iomap(uint32_t context_id,uint32_t a2,uint32_t addr,uint32_t size) {
	dbgf("rsx context iomap, id %d, %#x, addr %#x, size %#x\n",context_id,a2,addr,size);
	auto h = rsx_context_list.get(context_id);
	if (!h) return ESRCH;
	rsx_context_t&c = *h;
	c.busy_lock.lock();
	c.addr = (void*)addr;
	c.size = size;
	c.busy_lock.unlock();
	return CELL_OK;
}

int sys_rsx_context_attribute(uint32_t context_id,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5,uint64_t a6) {
	dbgf("rsx context attribute, %#x, %#x, %#x, %#x, %#x, %#x\n",context_id,a2,a3,a4,a5,a6);
	auto h = rsx_context_list.get(context_id);
	if (!h) return ESRCH;
	rsx_context_t&c = *h;
	switch (a2) {
	case 1:
		c.busy_lock.lock();
		(uint32_t&)c.control[0x40] = se((uint32_t)a3);
		(uint32_t&)c.control[0x44] = se((uint32_t)a4);
		(uint32_t&)c.control[0x48] = se((uint32_t)a5);
		c.busy_lock.unlock();
		break;
	case 0x101: // set flip mode? (0x1, 0x2, 0, 0) // 0x2 is vsync
		dbgf("set flip mode %d?\n",a3);
		break;
	case 0x10a: // unknown (0, 0, 0x80000000, 0)
		break;
	case 0x104: // set display buffer
		{
			int buffer_id = (int)a3;
			uint32_t offset = a5&0xffffffff;
			uint32_t pitch = a5>>32;
			uint32_t width = a4>>32;
			uint32_t height = a4&0xffffffff;
			dbgf("set display buffer %d, offset %#x, pitch %u, width %u, height %u\n",buffer_id,offset,pitch,width,height);
			gcm::set_display_buffer(buffer_id,offset,pitch,width,height);
		}
		break;
	case 0x300: // set tile info
		dbgf("set tile info?\n");
		break;
	case 0x301: // bind z cull
		dbgf("bind z cull?\n");
		break;
	default:
		xcept("unknown a2 for context attribute: %#x\n",a2);
	}
	return CELL_OK;
}

int sys_rsx_attribute(uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5) {
	dbgf("rsx attribute, %#x, %#x, %#x, %#x, %#x\n",a1,a2,a3,a4,a5);
	switch (a1) {
	case 0x2: // dunno, set flip mode calls this (0x1, 0, 0, 0)
		break;
	case 0x202:
		// dunno, set flip mode calls this (0x1, 0x1, 0, 0)
		break;
	default:
		xcept("rsx attribute unknown a1: %#x\n",a1);
	}
	return CELL_OK;
}
