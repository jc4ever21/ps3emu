

namespace rsx {

	int flip_status = 0;
	int get_flip_status() {
		return flip_status;
	}
	void reset_flip_status() {
		flip_status = 1;
	}
	void set_flip_status() {
		flip_status = 0;
	}
	rsx_context_t*ctx;
	std::map<uint32_t,int> flip_queue;
	void set_flip(uint32_t at,int id) {
		sync::busy_locker l(ctx->busy_lock);
		flip_queue[at] = id;
	}
	bool stop=false;
	win32_thread cmd_thread;
	void cmd_thread_entry();
	void stop_cmd_thread() {
		stop=true;
		cmd_thread.join();
	}
	void run_cmd_thread(rsx_context_t&c) {
		ctx = &c;
		cmd_thread.start(&cmd_thread_entry);
		gcm::get_window().add_dtor_cb(&stop_cmd_thread);
	}

	bool first=true;
	void do_cmd(rsx_context_t&c,uint32_t cmd,uint32_t*data,uint32_t count);
	std::stack<uint32_t> call_stack;
	void cmd_thread_entry() {
		rsx_context_t&c = *ctx;

		gcm::init();

		int cnt = 0;
		while (!atomic_read(&stop)) {
			//win32_thread::yield();
			win32_thread::sleep(1);

			uint32_t*ct = (uint32_t*)&c.control[0x40];
			uint32_t put;
			uint32_t get;

			if (cnt) first = false;
			while (!atomic_read(&stop)) {
				{
					c.busy_lock.lock();
					put = se(ct[0]);
					get = se(ct[1]);
					{
						uint32_t at = ctx->get_addr(get);
						auto i = flip_queue.find(at);
						if (i!=flip_queue.end()) {
							int id = i->second;
							flip_queue.erase(i);
							c.busy_lock.unlock();
							gcm::flip(id);
							flip_status = 0;
						} else c.busy_lock.unlock();
					}
					if (put==get) break;
				}
				cnt++;
				dbgf("put %#x, get %#x\n",put,get);
				dbgf("got command, %d bytes\n",put-get);

				char*p = (char*)c.get_addr(get);
				uint32_t cmd = se((uint32_t&)p[0]);

				enum {
					flag_non_increment = 0x40000000,
					flag_jump = 0x20000000,
					flag_call = 2,
					flag_return = 0x20000
				};

				dbgf("rsx cmd %#x\n",cmd);
				if (cmd&flag_jump) {
					cmd&=~(flag_non_increment|flag_jump);
					//if (cmd&flag_call || cmd&flag_return) xcept("rsx jump with call or return?");
					dbgf("rsx jump to %#x\n",cmd);
					ct[1] = se(cmd);
					continue;
				}
				if (cmd&flag_call) {
					cmd&=~(flag_call);
					if (cmd&flag_non_increment || cmd&flag_jump || cmd&flag_return) xcept("rsx call with ni, jump or return?");
					dbgf("rsx call %#x\n",cmd);
					call_stack.push(get+4);
					ct[1] = se(cmd);
					continue;
				}
				if (cmd&flag_return) {
					cmd&=~(flag_return);
					if (cmd&flag_non_increment || cmd&flag_jump || cmd&flag_call) xcept("rsx return with ni, jump or call?");
					dbgf("rsx ret\n");
					uint32_t v = call_stack.top();
					call_stack.pop();
					ct[1] = se(v);
					continue;
				}
				if (cmd&flag_non_increment) dbgf("non increment?\n");
				uint32_t count = cmd>>18&0x7ff;
				cmd &= 0x3FFFF;
				uint32_t*in_data = &(uint32_t&)p[4];
				uint32_t data[0x800];
				dbgf("count %d\n",count);
				for (int i=0;i<(int)count;i++) {
					data[i] = se(in_data[i]);
					dbgf(" %d: %#x\n",i,data[i]);
				}
				ct[1] = se(get + count*4 + 4);
				do_cmd(c,cmd,data,count);
			}
		}

	}

	void*get_location(int location,uint32_t offset) {
		if (location==0) return &ctx->mem->mem[offset];
		else return (void*)ctx->get_addr(offset);
	}
	void*get_context_addr(uint32_t context,uint32_t offset) {
		switch (context) {
		case 0xfeed0000: return &ctx->mem->mem[offset];
		case 0xfeed0001: return (void*)ctx->get_addr(offset);
		default:
			xcept("unknown context %#x\n",context);
		}
	}

	void do_cmd(rsx_context_t&c,uint32_t cmd,uint32_t*data,uint32_t count) {
		if (first && false) {
			switch (cmd) {
			case 0x50: // set reference
				(uint32_t&)c.control[0x48] = data[0];
				break;

 			}
			return;
		}
#define case16(first,stride) \
		case first+15*stride:++index; \
		case first+14*stride:++index; \
		case first+13*stride:++index; \
		case first+12*stride:++index; \
		case first+11*stride:++index; \
		case first+10*stride:++index; \
		case first+9*stride:++index; \
		case first+8*stride:++index; \
		case first+7*stride:++index; \
		case first+6*stride:++index; \
		case first+5*stride:++index; \
		case first+4*stride:++index; \
		case first+3*stride:++index; \
		case first+2*stride:++index; \
		case first+1*stride:++index; \
		case first
		int index=0;
		switch (cmd) {
		case 0x100: // no op
			break;
		case 0: // set object
			dbgf("set object %#x\n",data[0]);
			switch (data[0]) {
			case 0x31337000:
				dbgf("set 3d object\n");
				break;
			default:
				xcept("unknown object %#x\n",data[0]);
			}
			break;
		case 0x60: // set context dma semaphore
			dbgf("set context dma semaphore: %#x\n",data[0]);
			static char*semaphore_p;
			if (data[0]==0x56616661) {
				semaphore_p = &c.report[0x0ff10000-0x0fe00000];
			} else if (data[0]==0x66616661) {
				semaphore_p = &c.report[0];
			} else xcept("unknown dma object %#x",data[0]);
			break;
		case 0x50: // set reference
			(uint32_t&)c.control[0x48] = se(data[0]);
			break;
		case 0x64: // semaphore offset
		case 0x1d6c: // nv4097 semaphore offset
			static uint32_t semaphore_offset;
			semaphore_offset = data[0];
			dbgf("semaphore offset: %#x\n",semaphore_offset);
			break;
		case 0x68: // semaphore acquire
			dbgf("semaphore acquire: %#x\n",data[0]);
			{
				uint32_t*s = (uint32_t*)&semaphore_p[semaphore_offset];
				while (se(*s)!=data[0]) win32_thread::yield();
			}
			break;
		case 0x6c: // semaphore release

			dbgf("semaphore release: %#x\n",data[0]);
			{
				uint32_t*s = (uint32_t*)&semaphore_p[semaphore_offset];
				*s = se(data[0]);
			}
			break;
		case 0x1d74: // texture read semaphore release
			{
				gcm::set_textures();
				uint32_t*s = (uint32_t*)&semaphore_p[semaphore_offset];
				*s = se(data[0]);
			}
			break;
		case 0x1d70: // back end write semaphore release
			// byte 0 and 2 are swapped for some reason
			{
				uint32_t v = data[0]&0xff00ff00 | (data[0]>>16&0xff) | (data[0]<<16&0xff0000);
				uint32_t*s = (uint32_t*)&semaphore_p[semaphore_offset];
				*s = se(v);
			}
			break;
		case 0x194: // set context dma color a
		case 0x18c: // set context dma color b
		case 0x1b4: // set context dma color c (+d)
		case 0x198: // set context dma z
			if (data[0]!=0xfeed0000) xcept("context dma !=0xfeed0000");
			break;
		case 0x208: // set surface format
			static int surface_color_format;
			static int surface_depth_format;
			static int surface_type;
			static int surface_antialias;
			static int surface_width;
			static int surface_height;
			static uint32_t surface_pitch_a;
			static uint32_t surface_offset_a;
			static uint32_t surface_offset_z;
			static uint32_t surface_offset_b;
			static uint32_t surface_pitch_b;
			surface_color_format = data[0]&0x1f;
			surface_depth_format = data[0]>>5&7;
			surface_type = data[0]>>8&0xf;
			surface_antialias = data[0]>>12&0xf;
			surface_width = data[0]>>16&0xff;
			surface_height = data[0]>>24&0xff;
			surface_pitch_a = data[1];
			surface_offset_a = data[2];
			surface_offset_z = data[3];
			surface_offset_b = data[4];
			surface_pitch_b = data[5];
			dbgf("set surface format, color format %d, depth format %d, type %d, antialias %d, width %d, height %d, pitch a %u, offset a %u, offset z %u, offset b %u, pitch b %u\n",
				surface_color_format,surface_depth_format,surface_type,surface_antialias,surface_width,surface_height,
				surface_pitch_a,surface_offset_a,surface_offset_z,surface_offset_b,surface_pitch_b);
			gcm::set_surface(surface_offset_a);
			break;
		case 0x22c: // set surface pitch z
			break;
		case 0x280: // set surface pitch c
			break;
		case 0x220: // set surface color target
			break;
		case 0x2b8: // set window offset
			static int window_offset_x;
			static int window_offset_y;
			window_offset_x = data[0]&0xffff;
			window_offset_y = data[0]>>16&0xffff;
			dbgf("set window offset, %d x %d\n",window_offset_x,window_offset_y);
			break;
		case 0x1d88: // set shader window
			break;
		case 0x200: // set surface clip horizontal (vertical)
			static int surface_clip_x, surface_clip_y, surface_clip_w, surface_clip_h;
			surface_clip_x = data[0]&0xffff;
			surface_clip_w = data[0]>>16&0xffff;
			surface_clip_y = data[1]&0xffff;
			surface_clip_h = data[1]>>16&0xffff;
			dbgf("set surface clip horizontal %dx%d, %dx%d\n",surface_clip_x,surface_clip_y,surface_clip_w,surface_clip_h);
			gcm::set_surface_clip(surface_clip_x,surface_clip_y,surface_clip_w,surface_clip_h);
			break;
		case 0xa70: // set depth mask
			gcm::set_depth_mask(data[0]);
			break;
		case 0xa74: // set depth test enable
			gcm::set_depth_test_enable(data[0]?true:false);
			break;
		case 0xa00: // set viewport horizontal (vertical)
			static int viewport_x,viewport_w,viewport_y,viewport_h;
			viewport_x = data[0]&0xffff;
			viewport_w = data[0]>>16&0xffff;
			viewport_y = data[1]&0xffff;
			viewport_h = data[1]>>16&0xffff;
			dbgf("set viewport horizontal/vertical %dx%d, %dx%d\n",viewport_x,viewport_y,viewport_w,viewport_h);
			break;
		case 0x394: // set viewport clip min/max
			static float viewport_clip_min, viewport_clip_max;
			viewport_clip_min = (float&)data[0];
			viewport_clip_min = (float&)data[1];
			dbgf("set viewport clip, min %f, max %f\n",viewport_clip_min,viewport_clip_max);
			break;
		case 0xa20: // set viewport offset/scale
			static float viewport_offset0,viewport_offset1,viewport_offset2,viewport_offset3;
			static float viewport_scale0,viewport_scale1,viewport_scale2,viewport_scale3;
			viewport_offset0 = (float&)data[0];
			viewport_offset1 = (float&)data[1];
			viewport_offset2 = (float&)data[2];
			viewport_offset3 = (float&)data[3];
			viewport_scale0 = (float&)data[4];
			viewport_scale1 = (float&)data[5];
			viewport_scale2 = (float&)data[6];
			viewport_scale3 = (float&)data[7];
			gcm::set_viewport(viewport_x,viewport_y,viewport_w,viewport_h,viewport_clip_min,viewport_clip_max,viewport_offset0,
				viewport_offset1,viewport_offset2,viewport_offset3,viewport_scale0,viewport_scale1,viewport_scale2,viewport_scale3);
			break;
		case 0x1d90: // set color clear value;
			gcm::set_clear_color(data[0]);
			break;
		case 0x1d94: // clear surface
			gcm::clear_surface(data[0]);
			break;
		case 0x1d98: // set clear rect
			break;
		case 0x1da4: // set clip id test enable
			break;
		case 0x324: // set color mask
			gcm::set_color_mask(data[0]);
			break;
		case 0x370: // set color mask mrt
			if (data[0]) xcept("bad color mask mrt %#x\n",data[0]);
			break;
		case 0xa6c: // set depth func
			gcm::set_depth_func(data[0]);
			break;
		case 0x1e9c: // transform program load
			gcm::transform_program_load(data[0]);
			break;
		case 0xb80: // transform program
			gcm::transform_program_data(data[0],data[1],data[2],data[3]);
			break;
		case 0xb80+0x10: // transform program index 1
			xcept("transform program index 1?");
			break;
		case 0x1ff0: // set vertex attrib input mask
			break;
		case 0x1ff4: // set vertex attrib output mask
			break;
		case 0x1ef8: // set transform timeout
			break;
		case 0x1efc: // transform constant load
			gcm::transform_constant_load(data[0],(float*)&data[1],count-1);
			break;
		case16(0x1740,4): // set vertex data array format
			{
				int frequency = data[0]>>16;
				int stride = data[0]>>8&0xff;
				int size = data[0]>>4&0xf;
				int type = data[0]&0xf;
				dbgf("set vertex data array format, index %d, frequency %d, stride %d, size %d, type %d\n",index,frequency,stride,size,type);
				gcm::set_vertex_data_array_format(index,frequency,stride,size,type);
			}
			break;
		case16(0x1680,4): // set vertex data array offset
			gcm::set_vertex_data_array(index,get_location(data[0]>>31,data[0]&0x7FFFFFFF));
			break;
		case 0x1710: // invalid vertex cache file
		case 0x1714: // invalidate vertex file
			break;
		case 0x1808: // set begin end
			gcm::set_begin_end(data[0]);
			break;
		case 0x1814: // draw arrays;
			for (uint32_t i=0;i<count;i++) gcm::draw_arrays(data[i]&0xFFFFFF,(data[i]>>24)+1);
			break;
		case 0x8e4: // set shader program
			gcm::set_shader_program(get_location(~data[0]&1,data[0]&~1));
			break;
		case 0x1d60: // set shader control
			break;
		case 0x6188: // surface2d set context dma image destination
			static uint32_t context_dma_image_destination;
			context_dma_image_destination = data[0];
			dbgf("surface2d set image destination %#x\n",context_dma_image_destination);
			break;
		case 0x630c: // surface2d set offset destination
			static uint32_t s2d_offset_dest;
			s2d_offset_dest = data[0];
			dbgf("surface2d set offset destination %#x\n",s2d_offset_dest);
			break;
		case 0x6300: // surface2d set color format/pitch
			static uint32_t s2d_color_format;
			static int s2d_src_pitch;
			static int s2d_dst_pitch;
			s2d_color_format = data[0];
			s2d_src_pitch = data[1]&0xffff;
			s2d_dst_pitch = data[1]>>16;
			dbgf("surface2d cpu, set color format/pitch, format %#x, src pitch %#x, dst pitch %#x\n",s2d_color_format,s2d_src_pitch,s2d_dst_pitch);
			break;
		case 0xa304: // image from cpu point
			static int img_cpu_point_x, img_cpu_point_y;
			static int img_cpu_outsize_x, img_cpu_outsize_y;
			static int img_cpu_insize_x, img_cpu_insize_y;
			img_cpu_point_x = data[0]&0xffff;
			img_cpu_point_y = data[0]>>16;
			img_cpu_outsize_x = data[1]&0xffff;
			img_cpu_outsize_y = data[1]>>16;
			img_cpu_insize_x = data[2]&0xffff;
			img_cpu_insize_y = data[2]>>16;
			dbgf("image from cpu, point %dx%d, outsize %dx%d, insize %dx%d\n",img_cpu_point_x,img_cpu_point_y,img_cpu_outsize_x,img_cpu_outsize_y,img_cpu_insize_x,img_cpu_insize_y);
			break;
		case 0xa400: // image from cpu color
			{
				//if (img_cpu_point_x||img_cpu_point_y) xcept("img_cpu_point is set!");
				if (img_cpu_outsize_x!=img_cpu_insize_x) xcept("outsize/insize mismatch");
				if (img_cpu_outsize_y!=img_cpu_insize_y) xcept("outsize/insize mismatch!");
				uint32_t*ptr = (uint32_t*)get_context_addr(context_dma_image_destination,s2d_offset_dest);
				if (s2d_color_format!=0xb) xcept("unknown s2d_color_format");
				if (img_cpu_outsize_y!=1) xcept("img_cpu_outsize_y!=1");
				dbgf("ptr is %p\n",ptr);
				ptr += img_cpu_point_x + img_cpu_point_y*img_cpu_insize_x;
				for (uint32_t i=0;i<count;i++) {
					ptr[i] = se(data[i]);
				}
			}
			break;
		case 0x1828: // set front polygon mode
			gcm::set_front_polygon_mode(data[0]);
			break;
		case 0x182c: // set back polygon mode
			gcm::set_back_polygon_mode(data[0]);
			break;
		case 0x310: // set blend enable
			gcm::set_blend_enable(data[0]);
			break;
		case 0x320: // set blend equation
			gcm::set_blend_equation(data[0]&0xffff,data[0]>>16);
			break;
		case 0x368: // set shade mode
			gcm::set_shade_mode(data[0]);
			break;
		case 0x1830: // set cull face
			gcm::set_cull_face(data[0]);
			break;
		case 0x183c: // set cull face enable
			gcm::set_cull_face_enable(data[0]);
			break;
		case 0x314: // set blend func src/dst factor
			gcm::set_blend_func(data[0]&0xffff,data[0]>>16,data[1]&0xffff,data[1]>>16);
			break;
		case 0x181c: // set index array offset/format
			static uint32_t index_array_offset;
			static int index_array_location;
			static int index_array_type;
			index_array_offset = data[0];
			index_array_location = data[1]&1;
			index_array_type = data[1]>>4;
			break;
		case 0x1824: // draw index array
			for (uint32_t i=0;i<count;i++) gcm::draw_arrays_indexed(data[i]&0xFFFFFF,(data[i]>>24)+1,index_array_type,get_location(index_array_location,index_array_offset));
			break;
		case16(0xb40,4): // set tex coord control
			// bit 0 is 2d
			if (data[0]&~1) xcept("unknown tex coord control %#x",data[0]);
			break;
		case16(0x1a00,32): // set texture offset/format
			static uint32_t texture_offset[16], texture_format[16];
			texture_offset[index] = data[0];
			texture_format[index] = data[1];
			{
				uint32_t offset = data[0];
				int location = (data[1]&3)-1;
				int cubemap = data[1]>>2&1;
				int dimension = data[1]>>4&0xf;
				int format = data[1]>>8&0xff;
				int mipmap = data[1]>>16&0xffff;
				gcm::set_texture(index,get_location(location,offset),cubemap,dimension,format,mipmap);
			}
			break;
		case16(0x1a18,32): // set texture image rect
			static uint32_t texture_image_rect[16];
			texture_image_rect[index] = data[0];
			{
				int height = data[0]&0xffff;
				int width = data[0]>>16;
				gcm::set_texture_image_rect(index,width,height);
			}
			break;
		case16(0x1840,4): // set texture control 3
			static uint32_t texture_control_3[16];
			texture_control_3[index] = data[0];
			{
				int pitch = data[0]&0xFFFFF;
				int depth = data[0]>>20;
				gcm::set_texture_pitch_depth(index,pitch,depth);
			}
			break;
		case16(0x1a10,32): // set texture control 1
			static uint32_t texture_control_1[16];
			texture_control_1[index] = data[0];
			{
				uint32_t remap = data[0];
				gcm::set_texture_remap(index,remap);
			}
			break;
		case16(0x1a1c,32): // set texture border color
			static uint32_t texture_border_color[16];
			texture_border_color[index] = data[0];
			{
				uint32_t border_color = data[0];
				gcm::set_texture_border_color(index,border_color);
			}
			break;
		case16(0x1a0c,32): // set texture control 0
			static uint32_t texture_control_0[16];
			texture_control_0[index] = data[0];
			{
				int maxaniso = data[0]>>4&7;
				int maxlod = data[0]>>7&0xFFF;
				int minlod = data[0]>>19&0xFFF;
				int enable = data[0]>>31;
				gcm::set_texture_control(index,enable,minlod,maxlod,maxaniso);
			}
			break;
		case16(0x1a08,32): // set texture address
			static uint32_t texture_address[16];
			texture_address[index] = data[0];
			{
				int wraps = data[0]&0xf;
				int aniso_bias = data[0]>>4&0xf;
				int wrapt = data[0]>>8&0xf;
				int unsigned_remap = data[0]>>12&0xf;
				int wrapr = data[0]>>16&0xf;
				int gamma = data[0]>>20&0xff;
				int zfunc = data[0]>>28&0xf;
				gcm::set_more_texture_stuff(index,wraps,aniso_bias,wrapt,unsigned_remap,wrapr,gamma,zfunc);
			}
			break;
		case16(0x1a14,32): // set texture filter
			static uint32_t texture_filter[16];
			texture_filter[index] = data[0];
			{
				int bias = data[0]&0x1FFF;
				int conv = data[0]>>13&7;
				int min = data[0]>>16&0xff;
				int mag = data[0]>>24&0xff;
				gcm::set_texture_filter(index,bias,conv,min,mag);
			}
			break;
		case 0x3bc: // set line smooth enable
			gcm::set_line_smooth_enable(data[0]);
			break;
		case 0x8c0: // set scissor
			gcm::set_scissor(data[0]&0xffff,data[1]&0xffff,data[0]>>16,data[1]>>16);
			break;
		case 0x1d8c: // set zstencil clear value
			gcm::set_zstencil_clear_value(data[0]);
			break;
		case 0x1fd8: // invalidate l2 (texture cache)
			break;
		case 0x374: // set logic op enable
			gcm::set_logic_op_enable(data[0]);
			break;
		case 0x378: // set logic op
			gcm::set_logic_op(data[0]);
			break;
		case 0x180: // set context dma notifies
			dbgf("set context dma notifies?\n");
			break;
// 		case 0x280: // set surface pitch c
// 			dbgf("set surface pitch c?\n");
// 			break;
		case 0x1d80: // set surface compression
			dbgf("set surface compression?\n");
			break;
// 		case 0x2b8: // set window offset
// 			dbgf("set window offset?\n");
// 			break;
		case 0x304: // set alpha test enable
			gcm::set_alpha_test_enable(data[0]);
			break;
		case 0x308: // set alpha func/ref
			gcm::set_alpha_func(data[0],data[1]);
			break;
		case 0x328: // set stencil test enable
			gcm::set_stencil_test_enable(data[0]);
			break;
		case 0x32c: // set stencil mask
			gcm::set_stencil_mask(data[0]);
			break;
		case 0x348: // set two sided stencil test enable
			gcm::set_two_sided_stencil_test_enable(data[0]);
			break;
		case 0x380: // set depth bounds test enable
			gcm::set_depth_bounds_test_enable(data[0]);
			break;
		case 0x384: // set depth bounds min/max
			gcm::set_depth_bounds((float&)data[0],(float&)data[1]);
			break;
		case 0x36c: // set blend enable mrt
			if (data[0]) xcept("blend enable mrt? >.<");
			break;
		case 0xa60: // set polygon offset point enable
			if (data[0]) xcept("set polygon offset point enable");
			break;
		case 0xa64: // set polygon offset line enable
			if (data[0]) xcept("set polygon offset line enable");
			break;
		case 0xa68: // set polygon offset fill enable
			if (data[0]) xcept("set polygon offset fill enable");
			break;
		case 0x1dac: // set restart index enable
			if (data[0]) xcept("set restart index enable");
			break;
		case 0x1fec: // set shader packer (fragment program gamma enable)
			if (data[0]) xcept("set shader packer");
			break;
		case 0x142c: // set two side light enable
			if (data[0]) xcept("two side light enable");
			break;
		case 0x1ee4: // set point params enable
			if (data[0]) xcept("set point params enable");
			break;
		case 0x1ee8: // set point sprite control
			if (data[0]&1) xcept("set point sprite control");
			break;
		case 0x1fc0: // set frequency divider operation
			if (data[0]) xcept("set frequency divider operation");
			break;
		case 0x300: // set dither enable
			gcm::set_dither_enable(data[0]);
			break;
		default:
			if (first||true) outf("unknown rsx cmd %#x\n",cmd);
			else xcept("unknown rsx cmd %#x\n",cmd);
		}
		gcm::check_gl_error("post cmd");
	}
}
