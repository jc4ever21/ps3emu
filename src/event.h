
struct event_queue_attr_t {
	uint32_t protocol, type;
	char name[8];
};

struct event_t {
	uint64_t source, data1, data2, data3;
};

struct impl_event_queue_t {
	sync::message_queue_any<event_t,128> local_message_queue_any;
	sync::message_queue_any<event_t,128>*q;
	ipc_list_t2<sync::message_queue_any<event_t,128>>::handle ipc_h;
	int protocol;
	uint32_t wait_ref_count;
	impl_event_queue_t() : q(&local_message_queue_any), wait_ref_count(0) {}
	void set_ipc(const ipc_list_t2<sync::message_queue_any<event_t,128>>::handle&h) {
		ipc_h = h;
		q = &*h;
	}
	template<typename protocol,typename ipc>
	void init(uint32_t size) {
		q->init<protocol,ipc>(size);
	}
	int receive(event_t&e,uint64_t timeout) {
		return q->receive(e,timeout);
	}
	int try_receive(event_t*e,int count,int*retcount) {
		return q->try_receive(e,count,retcount);
	}
	void drain() {
		return q->drain();
	}
	int send(event_t&e) {
		return q->send(e);
	}
	void cancel() {
		return q->cancel();
	}
};

id_list_t<impl_event_queue_t,41> event_queue_list;
shm_object<ipc_list_t2<sync::message_queue_any<event_t,128>>> ipc_event_queue;
typedef id_list_t<impl_event_queue_t,41>::handle event_queue_handle;
typedef ipc_list_t2<sync::message_queue_any<event_t,128>>::handle ipc_event_queue_handle;

int sys_event_queue_create(uint32_t*equeue_id, event_queue_attr_t*attr, uint64_t event_queue_key, int32_t size) {
	uint32_t protocol = se(attr->protocol);
	int type = se((uint32_t)attr->type);
	char*name = attr->name;
	dbgf("create event queue, protocol %x, type %x, name '%s', key %x, size %d\n",protocol,type,name,event_queue_key,size);
	// Note: we do not differ between PPU and SPU queues, nor do we check the type in syscalls
	if (type!=1&&type!=2) { // 1 is ppu queue, 2 is spu queue
		xcept("sys_event_queue_create: type %d not supported",type);
		return EINVAL;
	}
	if (size<1 || size>127) return EINVAL;

	if (protocol!=sync_fifo&&protocol!=sync_priority) return EINVAL;

	int n = (protocol==sync_priority?1:0);

	auto h = event_queue_list.get_new();
	if (!h) return EAGAIN;
	int id = h.id();
	impl_event_queue_t&q = *h;
	if (event_queue_key==0) {
		if (n==0) q.init<sync::fifo,sync::not_ipc>(size);
		else if (n==1) q.init<sync::priority,sync::not_ipc>(size);
	} else {
		auto h = ipc_event_queue->get_new(event_queue_key);
		if (!h) return EEXIST;
		if (n==0) h->init<sync::fifo,sync::ipc>(size);
		else if (n==1) h->init<sync::priority,sync::ipc>(size);
		q.set_ipc(h);
	}
	*equeue_id = se((uint32_t)id);
	dbgf("created event queue with id %d\n",id);
	h.keep();
	return CELL_OK;
}
int sys_event_queue_destroy(uint32_t equeue_id,int32_t mode) {
	dbgf("destroy event queue %d, mode %d\n",equeue_id,mode);
	auto h = event_queue_list.get(equeue_id);
	if (!h) return ESRCH;
	if (mode==1) { // force destroy
		h->cancel();
		h.kill();
	}  else if (mode==0) {
		switch (h.try_kill<&impl_event_queue_t::wait_ref_count>()) {
		case 0: break;
		case 1: return EBUSY;
		default:NODEFAULT;
		}
	}
	// TODO: forcibly disconnect all event ports
	return CELL_OK;
}
int sys_event_queue_receive(uint32_t equeue_id,event_t*event,uint64_t timeout) {
	dbgf("receive event queue %d, event %p, timeout %d\n",equeue_id,event,timeout);
	auto h = event_queue_list.get<&impl_event_queue_t::wait_ref_count>(equeue_id);
	if (!h) return ESRCH;
	return h->receive(*event,timeout);
}
int sys_event_queue_tryreceive(uint32_t equeue_id,event_t * event_array,int32_t size,int32_t*number) {
	dbgf("tryreceive event queue %d, event_array %p, size %d, number %p\n",equeue_id,event_array,size,number);
	auto h = event_queue_list.get(equeue_id);
	if (!h) return ESRCH;
	int retcount;
	int r = h->try_receive(event_array,size,&retcount);
	*number = se((int32_t)retcount);
	return r;
}
int sys_event_queue_drain(uint32_t equeue_id) {
	dbgf("drain event queue %d\n",equeue_id);
	auto h = event_queue_list.get(equeue_id);
	if (!h) return ESRCH;
	h->drain();
	return CELL_OK;
}

struct impl_event_port_t {
	sync::busy_lock bl;
	uint32_t type;
	uint64_t name;
	event_queue_handle eqh;
	ipc_event_queue_handle ipc_eqh;
	impl_event_port_t() {}
};

id_list_t<impl_event_port_t> event_port_list;

int sys_event_port_create(uint32_t*eport_id,uint32_t port_type,uint64_t name) {
	dbgf("event port create eport_id %p, port_type %d, name %x\n",eport_id,port_type,name);
	if (port_type!=1 && port_type!=3) {
		xcept("unsupported port type %d",port_type);
		return EINVAL;
	}
	if (port_type==3) dbgf("ipc port\n");
	auto h = event_port_list.get_new();
	if (!h) return EAGAIN;
	int id = h.id();
	impl_event_port_t&p = *h;
	p.type = port_type;
	if (name==0) p.name = id;
	else p.name = name;
	*eport_id = se((uint32_t)id);
	dbgf("created event port with id %d\n",id);
	h.keep();
	return CELL_OK;
}
int sys_event_port_destroy(uint32_t eport_id) {
	dbgf("destroy event port %d\n",eport_id);
	auto h = event_port_list.get(eport_id);
	impl_event_port_t&p = *h;
	sync::busy_locker l(p.bl);
	if (p.eqh||p.ipc_eqh) return EISCONN;
	h.kill();
	return CELL_OK;
}
int sys_event_port_connect_local(uint32_t event_port_id,uint32_t event_queue_id) {
	dbgf("connect local event port %d to queue %d\n",event_port_id,event_queue_id);
	auto p_h = event_port_list.get(event_port_id);
	if (!p_h) return ESRCH;
	impl_event_port_t&p = *p_h;
	auto q_h = event_queue_list.get(event_queue_id);
	if (!q_h) return EINVAL;
	sync::busy_locker l(p.bl);
	if (p.eqh||p.ipc_eqh) return EISCONN;
	p.eqh = q_h;
	return CELL_OK;
}
int sys_event_port_connect_ipc(uint32_t event_port_id,uint64_t q_key) {
	dbgf("connect ipc event port %d to queue with key %#x\n",event_port_id,q_key);
	auto p_h = event_port_list.get(event_port_id);
	if (!p_h) return ESRCH;
	impl_event_port_t&p = *p_h;
	auto q_h = ipc_event_queue->get_existing(q_key);
	if (!q_h) return EINVAL;
	sync::busy_locker l(p.bl);
	if (p.eqh||p.ipc_eqh) return EISCONN;
	p.ipc_eqh = q_h;
	return CELL_OK;
}

int sys_event_port_disconnect(uint32_t event_port_id) {
	dbgf("disconnect event port %d\n",event_port_id);
	auto h= event_port_list.get(event_port_id);
	if (!h) return ESRCH;
	impl_event_port_t&p = *h;
	sync::busy_locker l(p.bl);
	if (p.eqh) {
		p.eqh.close();
		return CELL_OK;
	} else if (p.ipc_eqh) {
		p.ipc_eqh.close();
		return CELL_OK;
	} else return ENOTCONN;
}
int sys_event_port_send(uint32_t event_port_id,uint64_t data1,uint64_t data2,uint64_t data3) {
	dbgf("send event port %d, data1 %#x, data2 %#x, data3 %#x\n",event_port_id,data1,data2,data3);
	auto h = event_port_list.get(event_port_id);
	if (!h) return ESRCH;
	impl_event_port_t&p = *h;
	event_t d;
	d.source = se((uint64_t)p.name);
	d.data1 = se((uint64_t)data1);
	d.data2 = se((uint64_t)data2);
	d.data3 = se((uint64_t)data3);
	sync::busy_locker l(p.bl);
	if (p.eqh) {
		return p.eqh->send(d);
	} else if (p.ipc_eqh) {
		return p.ipc_eqh->send(d);
	} else return ENOTCONN;
}



