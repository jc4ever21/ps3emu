

void*sysutil_shm;
void dump_sysutil_shm() {
	if (!sysutil_shm) return;
	for (unsigned char*c = (unsigned char*)sysutil_shm;c<(unsigned char*)sysutil_shm + 0x10000;c+=8) {
		if (!(uint64_t&)*c) continue;
		outf("%05x  %02x %02x %02x %02x %02x %02x %02x %02x %c %c %c %c %c %c %c %c\n",c-(unsigned char*)sysutil_shm,
			c[0],c[1],c[2],c[3],c[4],c[5],c[6],c[7],c[0],c[1],c[2],c[3],c[4],c[5],c[6],c[7]);
	}
	
}

struct sysutil_shutdown_dumper {
	~sysutil_shutdown_dumper() {
		dump_sysutil_shm();
	}
} sysutil_shutdown_dumper;

void sysutil_daemon_init() {

	dbgf(" -- sysutil daemon init --\n");
	
	auto checkret = [&](int r) {
		if (r==CELL_OK) return;
		xcept("syscall returned error value %#x\n",r);
	};

	uint32_t mem_id;
	uint32_t mem_addr;
	uint32_t mut_id;
	uint32_t nem_id, nfl_id, stf_id, ctf_id;

	checkret(sys_mmapper_allocate_shared_memory(0x8006010000000010,0x10000,0xc200,&mem_id));
	mem_id = se(mem_id);

	checkret(sys_mmapper_allocate_address(1024*1024*256,0x200,0,&mem_addr));
	mem_addr = se(mem_addr);
	checkret(sys_mmapper_map_shared_memory(mem_addr,mem_id,0x40000));
	sysutil_shm = (void*)mem_addr;

	char*c = (char*)mem_addr;
	(uint32_t&)c[4] = se((uint32_t)0x1000);
	(uint32_t&)c[8] = se((uint32_t)0x100);

	mutex_attr_t mutex_attr;
	memset(&mutex_attr,0,sizeof(mutex_attr));
	mutex_attr.protocol = se((uint32_t)sync_priority);
	mutex_attr.recursive = se((uint32_t)sync_not_recursive);
	mutex_attr.ipc = se((uint32_t)sync_ipc);
	mutex_attr.flags = se((int32_t)sync_ipc_get_new);
	mutex_attr.ipc_key = se((uint64_t)0x8006010000000020);
	memcpy(&mutex_attr.name,"_s__shm",8);
	checkret(sys_mutex_create(&mut_id,&mutex_attr));
	mut_id = se(mut_id);

	cond_attr_t cond_attr;
	memset(&cond_attr,0,sizeof(cond_attr));
	cond_attr.ipc = se((uint32_t)sync_ipc);
	cond_attr.flags = se((int32_t)sync_ipc_get_new);
	cond_attr.ipc_key = se((uint64_t)0x8006010000000030);
	memcpy(&cond_attr.name,"_s__nem",8);
	checkret(sys_cond_create(&nem_id,mut_id,&cond_attr));
	nem_id = se(nem_id);
	cond_attr.ipc_key = se((uint64_t)0x8006010000000040);
	memcpy(&cond_attr.name,"_s__nfl",8);
	checkret(sys_cond_create(&nfl_id,mut_id,&cond_attr));
	nfl_id = se(nfl_id);
	cond_attr.ipc_key = se((uint64_t)0x8006010000000050);
	memcpy(&cond_attr.name,"_s__stf",8);
	checkret(sys_cond_create(&stf_id,mut_id,&cond_attr));
	stf_id = se(stf_id);
	cond_attr.ipc_key = se((uint64_t)0x8006010000000060);
	memcpy(&cond_attr.name,"_s__ctf",8);
	checkret(sys_cond_create(&ctf_id,mut_id,&cond_attr));
	ctf_id = se(ctf_id);

	uint32_t mut2_id;
	uint32_t nem2_id, nfl2_id, stf2_id, ctf2_id;

	mutex_attr.ipc_key = se((uint64_t)0x8006010000000021);
	memcpy(&mutex_attr.name,"_s__shm",8);
	checkret(sys_mutex_create(&mut2_id,&mutex_attr));
	mut2_id = se(mut2_id);
	cond_attr.ipc_key = se((uint64_t)0x8006010000000031);
	memcpy(&cond_attr.name,"_s__nem",8);
	checkret(sys_cond_create(&nem2_id,mut2_id,&cond_attr));
	nem2_id = se(nem2_id);
	cond_attr.ipc_key = se((uint64_t)0x8006010000000041);
	memcpy(&cond_attr.name,"_s__nfl",8);
	checkret(sys_cond_create(&nfl2_id,mut2_id,&cond_attr));
	nfl2_id = se(nfl2_id);
	cond_attr.ipc_key = se((uint64_t)0x8006010000000051);
	memcpy(&cond_attr.name,"_s__stf",8);
	checkret(sys_cond_create(&stf2_id,mut2_id,&cond_attr));
	stf2_id = se(stf2_id);
	cond_attr.ipc_key = se((uint64_t)0x8006010000000061);
	memcpy(&cond_attr.name,"_s__ctf",8);
	checkret(sys_cond_create(&ctf2_id,mut2_id,&cond_attr));
	ctf2_id = se(ctf2_id);

	dbgf(" -- sysutil daemon initialized --\n");
}


