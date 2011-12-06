
namespace io {
namespace pad {
	bool inited = false;

	uint32_t max_pads = 0;

	enum {
		pad_ok = 0,
		err_fatal = (int32_t)(0x80121101),
		err_invalid_parameter = (int32_t)(0x80121102),
		err_already_initialized = (int32_t)(0x80121103),
		err_uninitialized = (int32_t)(0x80121104),
		err_resource_allocation_failed = (int32_t)(0x80121105),
		err_data_read_failed = (int32_t)(0x80121106),
		err_no_device = (int32_t)(0x80121107),
		err_unsupported_gamepad = (int32_t)(0x80121108),
		err_too_many_devices = (int32_t)(0x80121109),
		err_EBUSY = (int32_t)(0x8012110a),
	};

	struct pad_info {
		uint32_t max_pads;
		uint32_t cur_pads;
		uint32_t info;
		uint16_t vendor_id[127];
		uint16_t product_id[127];
		uint8_t status[127];
	};
	struct pad_data {
		uint32_t len;
		uint16_t data0, data1;
		uint16_t ctrl_select : 1, ctrl_l3: 1, ctrl_r3 : 1, ctrl_start : 1;
		uint16_t ctrl_up : 1, ctrl_right : 1, ctrl_down : 1, ctrl_left : 1;
		uint16_t reserved0 : 8;
		uint16_t ctrl_l2 : 1, ctrl_r2 : 1, ctrl_l1 : 1, ctrl_r1 : 1;
		uint16_t ctrl_triangle : 1, ctrl_circle : 1, ctrl_cross : 1, ctrl_square : 1;
		uint16_t reserved1 : 8;
		uint16_t right_stick_x, right_stick_y;
		uint16_t left_stick_x, left_stick_y;
		uint16_t press_right, press_left, press_up, press_down;
		uint16_t press_triangle, press_circle, press_cross, press_square;
		uint16_t press_l1, press_r1, press_l2, press_r2;
		uint16_t sensor_x, sensor_y, sensor_z, sensor_g;
		uint16_t reserved[40];
	};
	static_assert(sizeof(pad_data)==132,"sizeof(pad_data)!=132");
	struct pad_capabilities {
		uint32_t ps3_conformity : 1, press_mode : 1, sensor_mode : 1, high_precision_stick : 1, actuator : 1;
		uint32_t reserved : 27;
		uint32_t reserved1[31];
	};
	static_assert(sizeof(pad_capabilities)==128,"sizeof(pad_capabilities)!=128");
	
	struct pad_driver {
		virtual ~pad_driver() {}
		virtual pad_capabilities get_capabilities() = 0;
		virtual bool has_press() = 0;
		virtual void set_press(bool enabled) = 0;
		virtual bool has_sensor() = 0;
		virtual void set_sensor(bool enabled) = 0;
		virtual pad_data get_data() = 0;
		virtual void clear() = 0;
	};
	struct pad_driver_simple: pad_driver {
		enum {
			ctrl_left, ctrl_right, ctrl_up, ctrl_down,
			ctrl_start, ctrl_select,
			ctrl_triangle, ctrl_circle, ctrl_cross, ctrl_square,
			ctrl_l1, ctrl_r1, ctrl_l2, ctrl_r2, ctrl_l3, ctrl_r3,
			ctrl_count
		};
		sync::busy_lock busy_lock;
		int ctrl_data[ctrl_count];
		pad_driver_simple() {
			memset(ctrl_data,0,sizeof(ctrl_data));
		}
		virtual ~pad_driver_simple() {}
		virtual pad_capabilities get_capabilities() {
			pad_capabilities r;
			memset(&r,0,sizeof(r));
			r.ps3_conformity = 1;
			return r;
		}
		virtual bool has_press() {return false;}
		virtual void set_press(bool) {}
		virtual bool has_sensor() {return false;}
		virtual void set_sensor(bool) {}
		virtual pad_data get_data() {
			sync::busy_locker l(busy_lock);
			pad_data r;
			memset(&r,0,sizeof(r));
			int changes=0;
			auto setbit = [&](int n,int v) {
				uint8_t&p = ((uint8_t*)&r.data0)[n/8];
				p = p&~(1<<n%8)|(v<<n%8);
			};
			auto test = [&](int n,int bitn) {
				if (ctrl_data[n]&2) {
					ctrl_data[n] &= ~2;
					ctrl_data[n] |= 1;
					changes++;
				} else if (ctrl_data[n]&4) {
					ctrl_data[n] &= ~(4|1);
					changes++;
				}
				setbit(bitn,ctrl_data[n]&1);
			};
			test(ctrl_select,32+0);
			test(ctrl_l3,32+1);
			test(ctrl_r3,32+2);
			test(ctrl_start,32+3);
			test(ctrl_up,32+4);
			test(ctrl_right,32+5);
			test(ctrl_down,32+6);
			test(ctrl_left,32+7);
			test(ctrl_l2,48+0);
			test(ctrl_r2,48+1);
			test(ctrl_l1,48+2);
			test(ctrl_r1,48+3);
			test(ctrl_triangle,48+4);
			test(ctrl_circle,48+5);
			test(ctrl_cross,48+6);
			test(ctrl_square,48+7);
			if (!changes) return r;
			r.len = 8;
			r.right_stick_x = 0x80;
			r.right_stick_y = 0x80;
			r.left_stick_x = 0x80;
			r.left_stick_y = 0x80;
			return r;
		}
		virtual void clear() {
			sync::busy_locker l(busy_lock);
			memset(ctrl_data,0,sizeof(ctrl_data));
		}
		void press(int ctrl) {
			sync::busy_locker l(busy_lock);
			ctrl_data[ctrl] |= 2;
			//outf("press %d (%#x)\n",ctrl,ctrl_data[ctrl]);
		}
		void release(int ctrl) {
			sync::busy_locker l(busy_lock);
			ctrl_data[ctrl] |= 4;
			//outf("release %d (%#x)\n",ctrl,ctrl_data[ctrl]);
		}
	};
	struct pad_driver_keyboard: pad_driver_simple {
		gcm::key_cb_handle key_cb_h;
		pad_driver_keyboard() {
			key_cb_h = gcm::add_key_cb([this](bool down,int vk) {
				int ctrl=-1;
				switch (vk) {
				case 'A': ctrl = ctrl_square; break;
				case 'Z': ctrl = ctrl_cross; break;
				case 'S': ctrl = ctrl_triangle; break;
				case 'X': ctrl = ctrl_circle; break;
				case 'D': ctrl = ctrl_l1; break;
				case 'C': ctrl = ctrl_r1; break;
				case 'F': ctrl = ctrl_l2; break;
				case 'V': ctrl = ctrl_r2; break;
				case 'G': ctrl = ctrl_l3; break;
				case 'B': ctrl = ctrl_r3; break;
				case VK_DOWN: ctrl = ctrl_down; break;
				case VK_UP: ctrl = ctrl_up; break;
				case VK_LEFT: ctrl = ctrl_left; break;
				case VK_RIGHT: ctrl = ctrl_right; break;
				case VK_RETURN: ctrl = ctrl_start; break;
				case VK_BACK: ctrl = ctrl_select; break;
				}
				if (ctrl==-1) return;
				if (down) press(ctrl);
				else release(ctrl);
			});
		}
		virtual ~pad_driver_keyboard() {
			gcm::remove_key_cb(key_cb_h);
		}
	};
	struct pad {
		bool connected;
		pad_driver*driver;
		bool press_enabled, sensor_enabled;
		pad() : connected(false), driver(0) {
			reset();
		}
		~pad() {
			if (driver) delete driver;
		}
		void reset() {
			if (driver) {
				delete driver;
				driver=0;
			}
			connected = false;
			press_enabled = false;
			sensor_enabled = false;
		}
	};
	pad pads[128];
	int connected_pads;
	sync::busy_lock busy_lock;

	int add_pad(pad_driver*driver) {
		sync::busy_locker l(busy_lock);
		int n = -1;
		for (int i=0;i<sizeof(pads)/sizeof(pads[0]);i++) {
			if (!pads[i].connected) {n=i;break;}
		}
		if (n==-1) xcept("add_pad failed; too many pads connected");
		pad&p = pads[n];
		p.connected = true;
		p.driver = driver;
		if (p.press_enabled) driver->set_press(true);
		if (p.sensor_enabled) driver->set_sensor(true);
		connected_pads++;
		return n;
	}

	pad*get_pad(uint32_t port) {
		if (port>=max_pads) return 0;
		return (pads[port].connected) ? &pads[port] : 0;
	}

	int32_t cellPadInit(uint32_t max) {
		dbgf("cellPadInit, max_pads %d\n",max);
		{
			sync::busy_locker l(busy_lock);
			if (inited) return err_already_initialized;
			if (max>sizeof(pads)/sizeof(pads[0])) return err_invalid_parameter;
			max_pads = max;
			inited = true;
			connected_pads = 0;
		}
		add_pad(new pad_driver_keyboard());
		return pad_ok;
	}

	int32_t cellPadEnd() {
		dbgf("cellPadEnd\n");
		sync::busy_locker l(busy_lock);
		if (!inited) return err_uninitialized;
		inited = false;
		for (uint32_t i=0;i<max_pads;i++) {
			pads[i].reset();
		}
		return pad_ok;
	}
	int32_t cellPadClearBuf(uint32_t port) {
		sync::busy_locker l(busy_lock);
		pad*p = get_pad(port);
		if (!p) return err_no_device;
		p->driver->clear();
		return pad_ok;
	}
	int32_t cellPadGetCapabilityInfo(uint32_t port,pad_capabilities*cap) {
		sync::busy_locker l(busy_lock);
		pad*p = get_pad(port);
		if (!p) return err_no_device;
		pad_capabilities c = p->driver->get_capabilities();
		uint32_t*src = (uint32_t*)&c;
		uint32_t*dst = (uint32_t*)cap;
		for (uint32_t i=0;i<32;i++) *dst++ = se(*src++);
		return pad_ok;
	}
	int32_t cellPadGetInfo(pad_info*info) {
		sync::busy_locker l(busy_lock);
		if (!inited) return err_uninitialized;
		memset(info,0,sizeof(*info));
		info->max_pads = se((uint32_t)max_pads);
		info->cur_pads = se((uint32_t)connected_pads);
		info->info = 0;
		for (uint32_t i=0;i<max_pads;i++) {
			info->status[i] = pads[i].connected ? 1 : 0;
			info->vendor_id[i] = 0;
			info->product_id[i] = 0;
		}
		return pad_ok;
	}
	int32_t cellPadGetRawData(uint32_t port,pad_data*data) {
		xcept("cellPadGetRawData");
	}
	int32_t cellPadGetData(uint32_t port,pad_data*data) {
		sync::busy_locker l(busy_lock);
		pad*p = get_pad(port);
		if (!p) return err_no_device;
		pad_data d = p->driver->get_data();
		data->len = se(d.len);
		uint16_t*src = &d.data0;
		uint16_t*dst = &data->data0;
		for (int i=0;i<64;i++) *dst++ = se(*src++);
		return pad_ok;
	}
	int32_t cellPadSetPressMode(uint32_t port,uint32_t mode) {
		sync::busy_locker l(busy_lock);
		if (port>=max_pads) return err_no_device;
		pads[port].press_enabled = mode ? true : false;
		if (pads[port].connected) pads[port].driver->set_press(mode?true:false);
		return pad_ok;
	}
	int32_t cellPadInfoPressMode(uint32_t port) {
		sync::busy_locker l(busy_lock);
		pad*p = get_pad(port);
		if (!p) return err_no_device;
		return p->driver->has_press() ? 1 : 0;
	}
	int32_t cellPadSetSensorMode(uint32_t port,uint32_t mode) {
		sync::busy_locker l(busy_lock);
		if (port>=max_pads) return err_no_device;
		pads[port].sensor_enabled = mode ? true : false;
		if (pads[port].connected) pads[port].driver->set_sensor(mode?true:false);
		return pad_ok;
	}
	int32_t cellPadInfoSensorMode(uint32_t port) {
		sync::busy_locker l(busy_lock);
		pad*p = get_pad(port);
		if (!p) return err_no_device;
		return p->driver->has_sensor() ? 1 : 0;
	}
	int32_t cellPadLddRegisterController() {
		xcept("cellPadLddRegisterController");
	}
	int32_t cellPadLddUnregisterController(int32_t handle) {
		xcept("cellPadLddUnregisterController");
	}
	int32_t cellPadLddDataInsert(int32_t handle,pad_data*data) {
		xcept("cellPadLddDataInsert");
	}
	int32_t cellPadLddGetPortNo(int32_t handle) {
		xcept("cellPadLddGetPortNo");
	}
	int32_t cellPadGetDataExtra(uint32_t port,uint32_t*device_type,pad_info*data) {
		xcept("cellPadGetDataExtra");
	}
	int32_t cellPadSetActDirect(uint32_t port,void*param) {
		xcept("cellPadSetActDirect");
	}

	ah_reg(sys_io,
		cellPadInit,
		cellPadEnd,
		cellPadGetCapabilityInfo,
		cellPadGetInfo,
		cellPadGetRawData,
		cellPadGetData,
		cellPadSetPressMode,
		cellPadInfoPressMode,
		cellPadSetSensorMode,
		cellPadInfoSensorMode,
		cellPadLddRegisterController,
		cellPadLddUnregisterController,
		cellPadLddDataInsert,
		cellPadLddGetPortNo,
		cellPadLddGetPortNo,
		cellPadGetDataExtra,
		cellPadSetActDirect
		);
}

namespace kb {
	enum {
		kb_ok = 0,
	};
	struct kb_info {
		uint32_t max;
		uint32_t cur;
		uint32_t info;
		uint8_t status[127];
	};
	struct kb_data {

	};
	struct kb_config {

	};
	uint32_t max_kb;
	int32_t cellKbInit(uint32_t max) {
		max_kb = max;
		return kb_ok;
	}
	int32_t cellKbEnd() {
		return kb_ok;
	}
	int32_t cellKbClearBuf(uint32_t port) {
		xcept("cellKbClearbuf");
	}
	int32_t cellKbGetInfo(kb_info*info) {
		memset(info,0,sizeof(*info));
		info->max = se((uint32_t)max_kb);
		return kb_ok;
	}
	int32_t cellKbRead(uint32_t port,kb_data*data) {
		xcept("cellKbRead");
	}
	int32_t cellKbSetCodeType(uint32_t port,uint32_t type) {
		return kb_ok;
	}
	int32_t cellKbSetLEDStatus(uint32_t port,uint8_t led) {
		xcept("cellKbSetLEDStatus");
	}
	int32_t cellKbSetReadMode(uint32_t port,uint32_t mode) {
		xcept("cellKbSetReadMode");
	}
	int32_t cellKbGetConfiguration(uint32_t port,kb_config*config) {
		xcept("cellKbGetConfiguration");
	}

	ah_reg(sys_io,
		cellKbInit,
		cellKbEnd,
		cellKbClearBuf,
		cellKbGetInfo,
		cellKbRead,
		cellKbSetCodeType,
		cellKbSetLEDStatus,
		cellKbSetReadMode,
		cellKbGetConfiguration
		);
}

namespace mouse {
	enum {
		mouse_ok = 0,
	};
	struct mouse_info {
		uint32_t max;
		uint32_t cur;
		uint32_t info;
		uint16_t vendor_id[127];
		uint16_t product_id[127];
		uint8_t status[127];
	};
	struct mouse_raw_data {

	};
	struct mouse_data {

	};
	struct mouse_data_list {

	};
	uint32_t max_mice;
	int32_t cellMouseInit(uint32_t max) {
		max_mice = max;
		return mouse_ok;
	}
	int32_t cellMouseEnd() {
		return mouse_ok;
	}
	int32_t cellMouseClearBuf(uint32_t port) {
		xcept("cellMouseClearBuf");
	}
	int32_t cellMouseGetInfo(mouse_info*info) {
		memset(info,0,sizeof(*info));
		info->max = se((uint32_t)max_mice);
		return mouse_ok;
	}
	int32_t cellMouseGetRawData(uint32_t port,mouse_raw_data*data) {
		xcept("cellMouseGetRawData");
	}
	int32_t cellMouseGetData(uint32_t port,mouse_data*data) {
		xcept("cellMouseGetData");
	}
	int32_t cellMouseGetDataList(uint32_t port,mouse_data_list*data) {
		xcept("cellMouseGetDataList");
	}

	ah_reg(sys_io,
		cellMouseInit,
		cellMouseEnd,
		cellMouseClearBuf,
		cellMouseGetInfo,
		cellMouseGetRawData,
		cellMouseGetData,
		cellMouseGetDataList
		);
}

}