

namespace ah_video {

	enum {
		video_ok = 0,
		err_not_implemented = (int32_t)0x8002b220,
		err_illegal_configuration = (int32_t)0x8002b221,
		err_illegal_parameter = (int32_t)0x8002b222,
		err_parameter_out_of_range = (int32_t)0x8002b223,
		err_device_not_found = (int32_t)0x8002b224,
		err_unsupported_video_out = (int32_t)0x8002b225,
		err_unsupported_display_mode = (int32_t)0x8002b226,
		err_condition_busy = (int32_t)0x8002b227
	};

	enum {
		resolution_1080 = 1,
		resolution_720 = 2,
		resolution_480 = 4,
		resolution_576 = 5,
		resolution_1600x1080 = 10,
		resolution_1440x1080 = 11,
		resolution_1280x1080 = 12,
		resolution_960x1080 = 13
	};

	struct out_display_mode {
		uint8_t resolution, scan_mode, conversion, aspect_ratio;
		uint8_t reserved[2];
		uint16_t refresh_rates;
	};

	struct out_state {
		uint8_t state, color_space;
		uint8_t reserved[6];
		out_display_mode display_mode;
	};

	struct out_config_t {
		uint8_t resution, format, aspect_ratio;
		uint8_t reserved[9];
		uint32_t pitch;
	};

	int32_t cellVideoOutGetNumberOfDevice(uint32_t video_out) {
		dbgf("cellVideoGetNumberOfDevice, video_out %d\n",video_out);
		if (video_out!=0) return err_unsupported_video_out;
		return 1;
	}

	int32_t cellVideoOutGetState(uint32_t video_out,uint32_t device_index,out_state*state) {
		dbgf("cellVideoOutGetstate, video_out %d, device_index %d\n",video_out,device_index);
		if (video_out!=0 || device_index!=0) return err_device_not_found;

		memset(state,0,sizeof(*state));
		state->state = (uint8_t)0; // enabled
		state->color_space = (uint8_t)1; // rgb
		state->display_mode.resolution = (uint8_t)resolution_480;
		state->display_mode.scan_mode = (uint8_t)1; // progressive
		state->display_mode.conversion = (uint8_t)0; // no conversion
		state->display_mode.aspect_ratio = (uint8_t)2; // 16:9
		state->display_mode.refresh_rates = se((uint16_t)4); // 60hz

		return video_ok;
	}

	int32_t cellVideoOutConfigure(uint32_t videoOut,out_config_t*config,void*option,uint32_t waitForEvent) {
		dbgf("cellVideoOutConfigure (doing nothing)\n");
		return video_ok;
	}

	void cellVideoOutGetDeviceInfo() {
		xcept("cellVideoOutGetDeviceInfo");
	}
	void cellVideoOutGetConfiguration() {
		xcept("cellVideoOutGetConfiguration");
	}
	int32_t cellVideoOutGetResolution(uint32_t resolution,uint16_t*dst) {
		uint16_t w,h;
		w = 720;
		h = 480;
		dst[0] = se(w);
		dst[1] = se(h);
		return video_ok;
	}
	void cellVideoOutGetResolutionAvailability() {
		xcept("cellVideoOutGetResolutionAvailability");
	}
	void cellVideoOutDebugSetMonitorType() {
		xcept("cellVideoOutDebugSetMonitorType");
	}

	ah_reg(cellSysutil,
		cellVideoOutGetNumberOfDevice,
		cellVideoOutGetState,
		cellVideoOutGetDeviceInfo,
		cellVideoOutConfigure,
		cellVideoOutGetResolution,
		cellVideoOutGetResolutionAvailability,
		cellVideoOutDebugSetMonitorType,
		cellVideoOutGetConfiguration
		);
}

namespace rsx {
	int get_flip_status();
	void reset_flip_status();
	void set_flip_status();
	void set_flip(uint32_t at,int id);
}
namespace gcm {

	uint32_t cellGcmGetFlipStatus() {
		return rsx::get_flip_status();
	}
	void cellGcmResetFlipStatus() {
		return rsx::reset_flip_status();
	}
	void cellGcmSetFlipStatus() {
		return rsx::set_flip_status();
	}
	struct context {
		uint32_t begin;
		uint32_t end;
		uint32_t current;
		void*cb;
	};
	int32_t cellGcmSetFlip(context*ctx,uint8_t id) {
		dbgf("flip to %d!\n",id);
		rsx::set_flip(se(ctx->current),id);
		return 0;
	}

	ah_reg(cellGcmSys,
		cellGcmGetFlipStatus,
		cellGcmResetFlipStatus,
		cellGcmSetFlipStatus,
		cellGcmSetFlip
		);

};


