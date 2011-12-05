
int sys_get_model_information(uint64_t cmd,void*buf) {
	dbgf("get model information cmd %#x\n",cmd);
	if (cmd==0x19004) {
		uint16_t*p = (uint16_t*)buf;
		p[0] = se((uint16_t)1);
		p[1] = se((uint16_t)0x85); // europe
		p[2] = se((uint16_t)1);
		p[3] = se((uint16_t)1);
		return CELL_OK;
	} else {
		//xcept("get_model_information unknown cmd %#x",cmd);
		dbgf(" !! get_model_information unknown cmd %#x\n",cmd);
	}
	return ENOSYS;
}

int sys_ss_get_cache_of_product_mode(uint8_t*product_mode) {

	// anything but 0xff is factory mode, i guess 0xff is service mode
	*product_mode = 0xff;

	return CELL_OK;
}

// what's this?
int sys_ss_get_cache_of_flash_ext_flag(uint8_t*a1) {

	*a1 = 0;

	return CELL_OK;
}

int sys_ss_access_control_engine() {
	dbgf("sys_ss_access_control_engine returning CELL_OK\n");
	return CELL_OK;
}

int sys_get_system_parameters(uint64_t*a,uint64_t*b,uint64_t*c,uint64_t*d) {
	*a = 0;
	*b = 0;
	*c = 0;
	*d = 0;

	//return CELL_OK;
	return ENOSYS;
}

int sys_storage_util_unmount(char*path) {
	dbgf("unmount %s\n",path);
	dbgf("(doing nothing)\n");
	return CELL_OK;
}

int sys_storage_util_mount(char*dev,char*fs,char*path,uint64_t a4,uint64_t read_only,uint64_t a6,uint64_t a7,uint64_t a8) {
	dbgf("mount %s %s %s %#x %#x %#x %#x\n",dev,fs,path,a4,read_only,a6,a7,a8);
	dbgf("(doing nothing)\n");
	return CELL_OK;
}


struct config_h {
	event_queue_handle eq_h;
};

id_list_t<config_h> config_list;

int sys_config_start(uint32_t eq_id,uint32_t*config_id) {
	dbgf("sys_config_start eq_id %d\n",eq_id);

	auto h = config_list.get_new();
	if (!h) return EAGAIN;
	int id = h.id();

	dbgf("config id is %d\n",id);
	
	*config_id = se((uint32_t)id);
	
	h.keep();
	return CELL_OK;
}

int sys_config_stop(uint32_t config_id) {
	dbgf("sys_config_stop %d\n",config_id);
	xcept("sys_config_stop");
	return CELL_OK;
}

// called from 0x52DAA8 in vsh.elf
int sys_storage_get_device_config(uint32_t*a1,uint32_t*a2) {
	outf("sys_storage_get_device_config()\n");
	*a1 = 0;
	*a2 = se((uint32_t)0);
	return CELL_OK;
}

// called from 0x52DB24 in vsh.elf
// a1 is *a1 and a3 is *a2 from sys_storage_get_device_config. a2 is 0.
int sys_storage_report_devices(uint32_t a1,uint32_t a2,uint32_t a3,uint64_t*a4) {
	// a3 seems to be the number of devices..?
	outf("sys_storage_report_devices(%u,%u,%u)\n",a1,a2,a3);
	for (uint32_t i=0;i<a3;i++) a4[i] = 0;
	return CELL_OK;
}
