
#define GLEW_STATIC
#include <gl/glew.h>
#include <gl/wglew.h>
#include <gl/gl.h>

#pragma comment(lib,"glew64s.lib")
#pragma comment(lib,"opengl32.lib")

#include "cg_decompiler.h"

namespace gcm {

	typedef std::function<void(bool down,int vk)> key_cb;
	std::list<key_cb> key_cb_list;
	typedef std::list<key_cb>::iterator key_cb_handle;
	sync::busy_lock key_cb_lock;

	bool got_wm_close = false;

	LRESULT CALLBACK default_wndproc(HWND hwnd,UINT uMsg,WPARAM wParam, LPARAM lParam) {
		if ((uMsg==WM_KEYDOWN&&wParam==VK_ESCAPE) || uMsg==WM_CLOSE) {
			dbgf("got WM_CLOSE\n");
			got_wm_close = true;
			return 0;
		}
		if (uMsg==WM_KEYDOWN||uMsg==WM_KEYUP) {
			if (uMsg==WM_KEYDOWN&&lParam&0x40000000) return 0;
			sync::busy_locker l(key_cb_lock);
			for (auto i=key_cb_list.begin();i!=key_cb_list.end();++i) {
				(*i)(uMsg==WM_KEYDOWN,(int)wParam);
			}
			return 0;
		}
		return DefWindowProc(hwnd,uMsg,wParam,lParam);
	}

	struct window {
		HWND hwnd;
		window() {
			WNDCLASSEXA c;
			c.cbSize        = sizeof(c);
			c.style         = CS_OWNDC;
			c.lpfnWndProc   = default_wndproc;
			c.cbClsExtra    = 0;
			c.cbWndExtra    = 0;
			c.hInstance     = GetModuleHandleA(0);
			c.hIcon         = 0;
			c.hCursor       = 0;
			c.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
			c.lpszMenuName  = 0;
			c.lpszClassName = generate_ipc_name("windowclass",0);
			c.hIconSm       = 0;
			RegisterClassExA(&c);

			hwnd = CreateWindowExA(WS_EX_APPWINDOW|WS_EX_WINDOWEDGE,c.lpszClassName,"title",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,width(),height(),0,0,GetModuleHandleA(0),0);
			if (!hwnd) xcept("CreateWindow failed with error %d",GetLastError());
			ShowWindow(hwnd,SW_SHOWDEFAULT);
			UpdateWindow(hwnd);
		}
		sync::busy_lock dtor_cb_lock;
		std::vector<std::function<void()>> dtor_cb_list;
		template<typename F>
		void add_dtor_cb(F&&cb) {
			sync::busy_locker l(dtor_cb_lock);
			dtor_cb_list.push_back([cb](){cb();});
		}
		~window() {
			sync::busy_locker l(dtor_cb_lock);
			for (auto i=dtor_cb_list.begin();i!=dtor_cb_list.end();++i) {
				(*i)();
			}
			CloseHandle(hwnd);
		}
		HWND get_handle() {
			return hwnd;
		}
		int width() {
			return 854;
		}
		int height() {
			return 480;
		}
		static sync::busy_lock singleton_lock;
		static window*singleton;
		static window&get_singleton() {
			if (singleton) return *singleton;
			sync::busy_locker l(singleton_lock);
			if (singleton) return *singleton;
			win32_thread().start([]() {
				static window w;
				singleton = &w;
				MSG msg;
				while (GetMessageA(&msg,0,0,0)!=-1) {
					TranslateMessage(&msg);
					DispatchMessageA(&msg);
					if (got_wm_close) exit(0);
				}
				xcept("GetMessage failed; error %d\n",GetLastError());
			});
			while (!atomic_read(&singleton)) win32_thread::yield();
			return *singleton;
		}
	};
	sync::busy_lock window::singleton_lock;
	window*window::singleton=0;
	window&get_window() {
		return window::get_singleton();
	}

	key_cb_handle add_key_cb(key_cb cb) {
		get_window(); // We need a window to handle input
		sync::busy_locker l(key_cb_lock);
		return key_cb_list.insert(key_cb_list.end(),cb);
	}
	void remove_key_cb(key_cb_handle h) {
		sync::busy_locker l(key_cb_lock);
		key_cb_list.erase(h);
	}

	HDC dc;

	void init() {

		dc = GetDC(get_window().get_handle());
		if (!dc) xcept("GetDC failed; error %d",GetLastError());

		PIXELFORMATDESCRIPTOR pf;
		memset(&pf,0,sizeof(pf));
		pf.nSize = sizeof(pf);
		pf.nVersion = 1;
		pf.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pf.iPixelType = PFD_TYPE_RGBA;
		pf.cColorBits = 24;
		pf.cDepthBits = 16;
		pf.cStencilBits = 0;

		int format = ChoosePixelFormat(dc,&pf);
		if (!format) xcept("ChoosePixelFormat failed; error %d",GetLastError());
		if (!SetPixelFormat(dc,format,&pf)) xcept("SetPixelFormat failed; error %d",GetLastError());

		HGLRC rc = wglCreateContext(dc);
		if (!rc) xcept("wglCreateContext failed; error %d",GetLastError());
		wglMakeCurrent(dc,rc);

		GLenum err = glewInit();
		if (err!=GLEW_OK) xcept("glewInit failed; error %s",glewGetErrorString(err));

		wglSwapIntervalEXT(1);

// 		glPixelStorei(GL_UNPACK_SWAP_BYTES,1);
// 		glPixelStorei(GL_UNPACK_ALIGNMENT,1);
	}

	struct display_buffer_info {
		int offset,pitch,width,height;
		uint32_t addr, size;
		GLuint colorbuf, depthbuf;
	};
	display_buffer_info display_buffers[8];
	std::map<int,display_buffer_info*> display_buffer_offset_map;

	display_buffer_info*get_display_buffer_by_addr(uint32_t addr) {
		for (int i=0;i<8;i++) {
			auto&db = display_buffers[i];
			if (addr>=db.addr && addr<db.addr+db.size) return &db;
		}
		return 0;
	}

	// This function can be called from any thread; do not call GL functions in it!
	void set_display_buffer(int id,uint32_t addr,int pitch,int width,int height) {
		dbgf("set_display_buffer %#x\n",addr);
		if (id>=8) xcept("bad display buffer id %d\n",id);
		display_buffer_info&i = display_buffers[id];
		display_buffer_offset_map[addr] = &i;
		i.addr = addr;
		i.size = pitch*height;
		i.pitch = pitch;
		i.width = width;
		i.height = height;
	}

	void check_gl_error(const char*str) {
		GLenum err = glGetError();
		if (err) xcept("%s: opengl error %d\n",str,err);
	}

	RECT surface_clip_rect;
	bool is_rendering = false;

	int frame_count=0;
	DWORD last_frame_report = 0;

	GLuint fbo;
	display_buffer_info*cur_display_buffer = 0;
	void flip(int id) {
		if (cur_display_buffer) {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
			glBlitFramebuffer(
				0,0,cur_display_buffer->width,cur_display_buffer->height,
				0,0,cur_display_buffer->width,cur_display_buffer->height,
				GL_COLOR_BUFFER_BIT,GL_NEAREST);
			glBindFramebuffer(GL_FRAMEBUFFER,fbo);
			check_gl_error("flip blit");
		}
		if (!SwapBuffers(dc)) xcept("SwapBuffers failed; error %d\n",GetLastError());
		++frame_count;
		DWORD now = timeGetTime();
		if (now-last_frame_report>=1000) {
			outf("%d fps\n",frame_count);
			frame_count=0;
			last_frame_report = now;
		}
	}

	void set_surface(uint32_t addr) {
		display_buffer_info*i = display_buffer_offset_map[addr];
		if (!i) xcept("no display buffer for address %#x!\n",addr);
		cur_display_buffer = i;
		dbgf("set surface %#x\n",addr);
		if (!i->colorbuf) {
			glGenRenderbuffers(1,&i->colorbuf);
			glBindRenderbuffer(GL_RENDERBUFFER,i->colorbuf);
			glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA,i->width,i->height);
			glGenRenderbuffers(1,&i->depthbuf);
			glBindRenderbuffer(GL_RENDERBUFFER,i->depthbuf);
			glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,i->width,i->height);
			check_gl_error("generate renderbuffer");
		}
		if (!fbo) {
			glGenFramebuffers(1,&fbo);
			check_gl_error("generate framebuffer");
		}
		glBindFramebuffer(GL_FRAMEBUFFER,fbo);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,i->colorbuf);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,i->depthbuf);
		check_gl_error("set_surface");
	}

	void set_surface_clip(int x,int y,int w,int h) {
		surface_clip_rect.left = x;
		surface_clip_rect.top = y;
		surface_clip_rect.right = x+w;
		surface_clip_rect.bottom = y+h;
	}

	void set_depth_test_enable(bool enable) {
		if (enable) glEnable(GL_DEPTH_TEST);
		else glDisable(GL_DEPTH_TEST);
	}
	void set_depth_mask(uint32_t enabled) {
		glDepthMask(enabled);
	}

	void set_viewport(int x,int y,int w,int h,float min,float max,float offset0,float offset1,float offset2,float offset3,float scale0,float scale1,float scale2,float scale3) {
		glViewport(x,y,w,h);
		glDepthRangef(max,min);


	}

	void set_clear_color(uint32_t color) {
		glClearColor((float)(color>>16&0xff)/256.0f,(float)(color>>8&0xff)/256.0f,(float)(color&0xff)/256.0f,(float)(color>>24&0xff)/256.0f);
	}

	void clear_surface(int mask) {
		if (!mask) return;
		if ((mask&(0x10|0x20|0x30|0x40))!=(0x10|0x20|0x30|0x40)) xcept("partial color clear? <.<");
		GLbitfield f=0;
		if (mask&1) f|=GL_DEPTH_BUFFER_BIT;
		if (mask&2) f|=GL_STENCIL_BUFFER_BIT;
		if (mask&0x10) f|=GL_COLOR_BUFFER_BIT;
		glClear(f);
	}

	void set_color_mask(uint32_t mask) {
		glColorMask(mask&0x10000?GL_TRUE:GL_FALSE,mask&0x100?GL_TRUE:GL_FALSE,mask&1?GL_TRUE:GL_FALSE,mask&0x1000000?GL_TRUE:GL_FALSE);
	}

	void set_depth_func(int n) {
		glDepthFunc(n);
	}


	struct vertex_data_array: boost::intrusive::list_base_hook<> {
		int index;
		int frequency, stride, size, type;
		void*data;
	};
	vertex_data_array vertex_data_arrays[16];
	boost::intrusive::list<vertex_data_array> enabled_vertex_data_arrays;
	struct transform_constant: boost::intrusive::list_base_hook<> {
		int n;
		float v[4];
	};
	transform_constant transform_constants[468];
	boost::intrusive::list<transform_constant> loaded_transform_constants;

	struct transform_program {
		int size;
		uint32_t data[512*4];
		bool operator==(const transform_program&v) const {
			return size==v.size && !memcmp(data,v.data,size*4);
		}
	};
	size_t hash_value(const transform_program&v) {
		return v.size/4;
	}
	std::map<int,transform_program> transform_programs;
	transform_program*cur_vp;
	void transform_program_load(int slot) {
		transform_program&p = transform_programs[slot];
		p.size = 0;
		cur_vp = &p;
		loaded_transform_constants.clear();
	}
	void transform_program_data(uint32_t d0,uint32_t d1,uint32_t d2,uint32_t d3) {
		transform_program&p = *cur_vp;
		uint32_t*d = &p.data[p.size];
		d[0] = d0; d[1] = d1; d[2] = d2; d[3] = d3;
		p.size += 4;
	}

	boost::unordered_map<transform_program,GLuint> transform_program_gl_map;

	GLuint get_shader(int type,const std::string&s) {
		dbgf("compiling %s shader :: \n%s\n",type==GL_VERTEX_SHADER?"vertex":"fragment",s.c_str());

		GLuint shader = glCreateShader(type);
		if (!shader) xcept("glCreateShader(GL_VERTEX_SHADER) failed");
		const GLchar*str = s.c_str();
		GLint length = s.size();
		glShaderSource(shader,1,&str,&length);
		glCompileShader(shader);
		GLint r;
		glGetShaderiv(shader,GL_COMPILE_STATUS,&r);

		char buf[0x1000];
		GLsizei len;
		glGetShaderInfoLog(shader,0x1000,&len,buf);
		buf[0xfff] = 0;
		if (r!=GL_TRUE) xcept("Failed to compile shader ;; \n%s\n ;; info log :: \n%s",s.c_str(),buf);
		dbgf("compiled successfully. info log: %s\n",buf);
		return shader;
	}

	GLuint get_vertex_shader() {
		transform_program&p = *cur_vp;
		auto i = transform_program_gl_map.insert(std::make_pair(p,0));
		if (i.second) {
			i.first->second = get_shader(GL_VERTEX_SHADER,vertex_program::decompile(p.data));
		}
		return i.first->second;
	}

	struct fragment_program_data {
		uint32_t*data;
		int size;
		GLuint shader;
		boost::unordered_set<int> immediates;
		bool operator==(const fragment_program_data&v) const {
			return size==v.size && !memcmp(data,v.data,size*4);
		}
		operator size_t() const {
			return size/4;
		}
	};
// 	size_t hash_value(const fragment_program_data&v) {
// 		return v.size/4;
// 	}
	fragment_program_data*cur_fp;

	std::unordered_set<fragment_program_data> fragment_programs;
	GLuint get_fragment_shader() {
		fragment_program_data&p = *cur_fp;
		if (!p.shader) {
			auto r = shader_program::decompile(p.data);
			p.shader = get_shader(GL_FRAGMENT_SHADER,r.first);
			p.immediates = r.second;
		}
		return p.shader;
	}

	struct program {
		GLuint id;
		boost::unordered_map<int,GLint> vertex_uniform_map, fragment_uniform_map, tex_uniform_map;
		void set_vertex_uniform(int n,float x,float y,float z,float w) {
			GLint&i = vertex_uniform_map[n];
			if (i==0) {
				i = glGetUniformLocation(id,format("vc%u",n));
				check_gl_error("glGetUniformLocation");
			}
			glUniform4f(i,x,y,z,w);
		}
		void set_fragment_uniform(int n,float x,float y,float z,float w) {
			GLint&i = fragment_uniform_map[n];
			if (i==0) {
				i = glGetUniformLocation(id,format("imm%d",n));
				check_gl_error("glGetUniformLocation");
			}
			glUniform4f(i,x,y,z,w);
		}
		void set_tex_unit(int n) {
			GLint&i = tex_uniform_map[n];
			if (i==0) {
				i = glGetUniformLocation(id,format("tex%u",n));
				check_gl_error("glGetUniformLocation");
			}
			glUniform1i(i,n);
		}
	};

	boost::unordered_map<std::pair<GLuint,GLuint>,program> program_gl_map;
	program&get_program() {
		GLuint vs = get_vertex_shader();
		GLuint fs = get_fragment_shader();
		auto i = program_gl_map.insert(std::make_pair(std::make_pair(vs,fs),program()));
		if (i.second) {
			GLuint prog = glCreateProgram();
			if (!prog) xcept("glCreateProgram() failed");
			glAttachShader(prog,vs);
			glAttachShader(prog,fs);

			const char*table[] = {"in_pos","in_weight","in_normal","in_col0","in_col1","in_fogc","in_6","in_7",
				"in_tc0","in_tc1","in_tc2","in_tc3","in_tc4","in_tc5","in_tc6","in_tc7"};
			for (int i2=0;i2<16;i2++) {
				glBindAttribLocation(prog,i2,table[i2]);
				check_gl_error("glBindAttribLocation");
			}

			glBindFragDataLocation(prog,0,"r0");
			check_gl_error("glBindFragDataLocation");

			glLinkProgram(prog);
			GLint r;
			glGetProgramiv(prog,GL_LINK_STATUS,&r);
			char buf[0x1000];
			GLsizei len;
			glGetProgramInfoLog(prog,0x1000,&len,buf);
			buf[0xfff] = 0;
			if (r!=GL_TRUE) xcept("glLinkProgram failed; info log: %s",buf);
			dbgf("program linked. info log: %s\n",buf);

			i.first->second.id = prog;
		}
		return i.first->second;
	}



	void set_vertex_data_array_format(int index,int frequency,int stride,int size,int type) {
		if (index<0||index>15) xcept("set_vertex_data_array_format: bad index %d",index);
		vertex_data_array&d = vertex_data_arrays[index];
		if (size<0||size>4) xcept("bad vertex data size %d",size);
		if (type<1||type>7) xcept("unknown vertex data type %d",type);
		if (d.size && !size) enabled_vertex_data_arrays.erase(enabled_vertex_data_arrays.iterator_to(d));
		else if (size && !d.size) enabled_vertex_data_arrays.push_back(d);
		d.index = index;
		d.frequency = frequency;
		d.stride = stride;
		d.size = size;
		d.type = type;
	}
	void set_vertex_data_array(int index,void*ptr) {
		vertex_data_arrays[index].data = ptr;
	}

	void transform_constant_load(int at,float*data,int count) {
		if (count%4) xcept("transform constant load count (%d) not a multiple of 4",count);
		while (count) {

			if ((unsigned int)at>=sizeof(transform_constants)/sizeof(transform_constants[0])) xcept("too high constant index %d",at);

			transform_constant&c = transform_constants[at];
			c.n = at;
			c.v[0] = data[0];
			c.v[1] = data[1];
			c.v[2] = data[2];
			c.v[3] = data[3];
			loaded_transform_constants.push_back(c);

			at++;
			count-=4;
			data += 4;
		}
	}
	program*cur_prog=0;
	void set_program() {
		program&prog = get_program();
		//if (cur_prog==&prog) return;
		cur_prog = &prog;
		glUseProgram(prog.id);
		for (auto i=loaded_transform_constants.begin();i!=loaded_transform_constants.end();++i) {
			prog.set_vertex_uniform(i->n,i->v[0],i->v[1],i->v[2],i->v[3]);
		}
		for (auto i=cur_fp->immediates.begin();i!=cur_fp->immediates.end();++i) {
			uint32_t*ptr = cur_fp->data;
			uint16_t*p = (uint16_t*)&ptr[*i*4];
			uint32_t x = se(p[0]) | se(p[1])<<16;
			uint32_t y = se(p[2]) | se(p[3])<<16;
			uint32_t z = se(p[4]) | se(p[5])<<16;
			uint32_t w = se(p[6]) | se(p[7])<<16;
			prog.set_fragment_uniform(*i,(float&)x,(float&)y,(float&)z,(float&)w);
		}
	}
	GLuint buffer_id=0;
	void set_vertex_buffer(void*data,size_t size) {
		if (!buffer_id) glGenBuffers(1,&buffer_id);
		glBindBuffer(GL_ARRAY_BUFFER,buffer_id);
		glBufferData(GL_ARRAY_BUFFER,size,data,GL_DYNAMIC_DRAW);
	}

	std::vector<uint8_t> vertex_data;
	void load_vertex_data(int start,int count) {
		int stride = enabled_vertex_data_arrays.size()*16;
		if (vertex_data.size()<(start+count)*stride) vertex_data.resize((start+count)*stride);
		char*ptr = (char*)&vertex_data[start*stride];
		for (int i=start;i<start+count;i++) {
			for (auto i2=enabled_vertex_data_arrays.begin();i2!=enabled_vertex_data_arrays.end();++i2) {
				int idx = i2->index;
				int freq = i2->frequency;
				int stride = i2->stride;
				int size = i2->size;
				int type = i2->type;
				if (freq) xcept("frequency is non-zero!");
				char*d = (char*)i2->data;
				d += stride*i;
				char*prev_ptr = ptr;
				uint16_t*&dst_s1 = (uint16_t*&)ptr;
				float*&dst_f = (float*&)ptr;
				uint8_t*&dst_ub = (uint8_t*&)ptr;
				uint16_t*&src_s1 = (uint16_t*&)d;
				float*&src_f = (float*&)d;
				uint8_t*&src_ub = (uint8_t*&)d;
				for (int n=0;n<size;n++) {
					switch (type) {
					case 1: case 5: case 3: *dst_s1++ = se(*src_s1++); break;
					case 2: *dst_f++ = se(*src_f++);/* dbgf("%d %d %d : %f\n",idx,i,n,*(dst_f-1));*/ break;
					case 4: case 7: *dst_ub++ = *src_ub++; break;
					case 6: xcept("vertex data type cmp");
					default: NODEFAULT;
					}
				}
				if (ptr-prev_ptr>16) xcept("ptr advanced more than 16 bytes!");
				ptr = prev_ptr + 16;
			}
		}
	}
	void enable_vertex_data() {
		char*d = (char*)&vertex_data[0];
		set_vertex_buffer(&vertex_data[0],vertex_data.size());
		int ptr_stride = enabled_vertex_data_arrays.size()*16;
		int offset=0;
		for (auto i2=enabled_vertex_data_arrays.begin();i2!=enabled_vertex_data_arrays.end();++i2) {
			int idx = i2->index;
			int size = i2->size;
			int type = i2->type;
			GLenum gl_type;
			bool normalized = false;
			switch (type) {
			case 1: gl_type = GL_SHORT; normalized = true; break;
			case 2: gl_type = GL_FLOAT; break;
			case 3: gl_type = GL_HALF_FLOAT; break;
			case 4: gl_type = GL_UNSIGNED_BYTE; normalized = true; break;
			case 5: gl_type = GL_SHORT; break;
			case 6: xcept("cmp");
			case 7: gl_type = GL_UNSIGNED_BYTE; break;
			default: NODEFAULT;
			}
			glVertexAttribPointer(idx,size,gl_type,normalized,ptr_stride,(void*)offset);
			glEnableVertexAttribArray(idx);
			offset += 16;
		}
	}
	void disable_vertex_data() {
		for (auto i2=enabled_vertex_data_arrays.begin();i2!=enabled_vertex_data_arrays.end();++i2) {
			int idx = i2->index;
			glDisableVertexAttribArray(idx);
		}
	}
	int draw_indexed_first, draw_indexed_count, draw_indexed_type;
	uint32_t draw_indexed_low_index, draw_indexed_high_index;
	int draw_arrays_first, draw_arrays_count;
	std::vector<uint8_t> index_data;
	int draw_mode;
	void set_textures();
	void set_begin_end(int mode) {
		if (mode) {
			set_program();
			set_textures();
			draw_indexed_count = 0;
			draw_arrays_count = 0;
		} else {
			if (draw_indexed_count) {
				load_vertex_data(draw_indexed_low_index,draw_indexed_high_index-draw_indexed_low_index+1);
				enable_vertex_data();
				if (draw_indexed_type==GL_UNSIGNED_INT) glDrawElements(draw_mode,draw_indexed_count,draw_indexed_type,&index_data[draw_indexed_first*4]);
				else glDrawElements(draw_mode,draw_indexed_count,draw_indexed_type,&index_data[draw_indexed_first*2]);
				disable_vertex_data();
			}
			if (draw_arrays_count) {
				enable_vertex_data();
				glDrawArrays(draw_mode,draw_arrays_first,draw_arrays_count);
				disable_vertex_data();
			}
		}
		draw_mode = mode-1;
	}
	void draw_arrays(int first,int count) {
		load_vertex_data(first,count);
		if (draw_arrays_count==0) draw_arrays_first = first;
		if (first!=draw_arrays_first+draw_arrays_count) xcept("first is %d, expected %d",first,draw_arrays_first+draw_arrays_count);
		draw_arrays_count += count;
	}

	void set_shader_program(void*data) {
		fragment_program_data fp;
		fp.data = (uint32_t*)data;
		fp.size = shader_program::get_size((uint32_t*)data);
		fp.shader = 0;
		auto i = fragment_programs.insert(fp);
		cur_fp = (fragment_program_data*)&*i.first;
	}

	void set_front_polygon_mode(int mode) {
		glPolygonMode(GL_FRONT,mode);
	}
	void set_back_polygon_mode(int mode) {
		glPolygonMode(GL_BACK,mode);
	}

	void set_blend_enable(int enable) {
		if (enable) glEnable(GL_BLEND);
		else glDisable(GL_BLEND);
	}
	void set_blend_equation(int color,int alpha) {
		if (color>=0xf005 || alpha>=0xf005) xcept("set blend equation; signed? what does that mean? >.<");
		glBlendEquationSeparate(color,alpha);
	}

	void set_shade_mode(int mode) {
		glShadeModel(mode);
	}
	
	void set_cull_face(int mode) {
		glCullFace(mode);
	}
	void set_cull_face_enable(int enable) {
		if (enable) glEnable(GL_CULL_FACE);
		else glDisable(GL_CULL_FACE);
	}

	void set_blend_func(int src_color,int src_alpha,int dst_color,int dst_alpha) {
		glBlendFuncSeparate(src_color,dst_color,src_alpha,dst_alpha);
	}

	void draw_arrays_indexed(int first,int count,int type,void*indices) {
		if (type==0) type = GL_UNSIGNED_INT;
		else if (type==1) type = GL_UNSIGNED_SHORT;
		else xcept("draw_arrays_indexed bad type %d",type);
		if (draw_indexed_count==0) {
			draw_indexed_first = first;
			draw_indexed_type = type;
			draw_indexed_low_index = ~0;
			draw_indexed_high_index = 0;
		}
		for (int i=first;i<first+count;i++) {
			if (index_data.size()<i*4+4) index_data.resize(i*4+4);
			uint16_t&src16 = (uint16_t&)((char*)indices)[i*2];
			uint32_t&src32 = (uint32_t&)((char*)indices)[i*4];
			uint16_t&dst16 = (uint16_t&)index_data[i*2];
			uint32_t&dst32 = (uint32_t&)index_data[i*4];
			uint32_t idx;
			if (type==GL_UNSIGNED_INT) idx = dst32 = se(src32);
			else idx = dst16 = se(src16);
			if (idx<draw_indexed_low_index) draw_indexed_low_index=idx;
			if (idx>draw_indexed_high_index) draw_indexed_high_index=idx;
		}
		//load_vertex_data(low_index,high_index-low_index+1);
		if (first!=draw_indexed_first+draw_indexed_count) xcept("first is %d, expected %d",first,draw_indexed_first+draw_indexed_count);
		if (type!=draw_indexed_type) xcept("type is %d, expected %d",type,draw_indexed_type);
		draw_indexed_count += count;
	}

	struct tex_info {
		bool enabled, read;
		void*data;
		bool cubemap;
		int dimensions, format, mipmap;
		int width, height;
		int pitch, depth;
		uint32_t remap;
		uint32_t border_color;
		int minlod, maxlod, maxaniso;
		int wraps, wrapt, wrapr;
		int aniso_bias, unsigned_remap, gamma, zfunc;
		int filter_bias, filter_conv, filter_min, filter_mag;
		GLuint tex_id;
		tex_info() {
			memset(this,0,sizeof(*this));
		}
		tex_info(const tex_info&i) {
			memcpy(this,&i,sizeof(i));
		}
	};
	boost::unordered_map<void*,tex_info*> tex_map;
	tex_info tex[16];

	void set_textures() {
		for (int i=0;i<16;i++) {
			glActiveTexture(GL_TEXTURE0+i);
			cur_prog->set_tex_unit(i);
			if (!tex[i].enabled) {
				glBindTexture(GL_TEXTURE_2D,0);
				continue;
			}
			tex_info*&pt = tex_map[tex[i].data];
			if (!pt) {
				pt = new tex_info(tex[i]);
				glGenTextures(1,&pt->tex_id);
			}
			tex_info&t = *pt;
			if (t.dimensions!=2) xcept("%dD texture",t.dimensions);
			int target = GL_TEXTURE_2D;
			glBindTexture(target,t.tex_id);
			if (t.read) continue;

			outf("tex width is %d, height is %d\n",t.width,t.height);
			outf("format is %#x\n",t.format);
			outf("data is %p\n",t.data);
			uint8_t*p = (uint8_t*)t.data;
			outf("mipmap is %d\n",t.mipmap);

			int format = t.format;
			bool ln = format&0x20?true:false;
			bool un = format&0x40?true:false;
			format &= ~0x60;

			if (format==0x85) {

				int internal_format = GL_RGBA;
				int format = GL_BGRA;

				glTexImage2D(target,0,internal_format,t.width,t.height,0,format,GL_UNSIGNED_INT_8_8_8_8,t.data);

				//glTexParameteri(target,GL_TEXTURE_MIN_FILTER,t.filter_min);

// 				glTexParameteri(target,GL_TEXTURE_SWIZZLE_R,GL_GREEN);
// 				glTexParameteri(target,GL_TEXTURE_SWIZZLE_G,GL_BLUE);
// 				glTexParameteri(target,GL_TEXTURE_SWIZZLE_B,GL_ALPHA);
// 				glTexParameteri(target,GL_TEXTURE_SWIZZLE_A,GL_RED);
			} else if (format==0x81) {

				if (t.pitch!=t.width) xcept("t.pitch!=t.width");

				glTexImage2D(target,0,GL_RGBA,t.width,t.height,0,GL_RED,GL_UNSIGNED_BYTE,t.data);
				check_gl_error("glTexImage2D");

				glTexParameteri(target,GL_TEXTURE_SWIZZLE_R,GL_RED);
				glTexParameteri(target,GL_TEXTURE_SWIZZLE_G,GL_RED);
				glTexParameteri(target,GL_TEXTURE_SWIZZLE_B,GL_RED);
				glTexParameteri(target,GL_TEXTURE_SWIZZLE_A,GL_RED);

				check_gl_error("glTexParameteri");
			}

			glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(target, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
			glTexParameteri(target, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

			check_gl_error("glTexParameteri 2");

			t.read = true;
		}
		check_gl_error("post tex");
	}


	void set_texture(int index,void*data,int cubemap,int dimensions,int format,int mipmap) {
		tex_info&t = tex[index];
		t.data = data;
		t.cubemap = cubemap?true:false;
		t.dimensions = dimensions;
		t.format = format;
		t.mipmap = mipmap;
	}
	void set_texture_image_rect(int index,int width,int height) {
		tex_info&t = tex[index];
		t.width = width;
		t.height = height;
	}
	void set_texture_pitch_depth(int index,int pitch,int depth) {
		tex_info&t = tex[index];
		t.pitch = pitch;
		t.depth = depth;
	}
	void set_texture_remap(int index,uint32_t remap) {
		tex_info&t = tex[index];
		t.remap = remap;
	}
	void set_texture_border_color(int index,uint32_t border_color) {
		tex_info&t = tex[index];
		t.border_color = border_color;
	}
	void set_texture_control(int index,int enable,int minlod,int maxlod,int maxaniso) {
		tex_info&t = tex[index];
		t.enabled = enable?true:false;
		t.minlod = minlod;
		t.maxlod = maxlod;
		t.maxaniso = maxaniso;
	}
	void set_more_texture_stuff(int index,int wraps,int aniso_bias,int wrapt,int unsigned_remap,int wrapr,int gamma,int zfunc) {
		tex_info&t = tex[index];
		t.wraps = wraps;
		t.aniso_bias = aniso_bias;
		t.wrapt = wrapt;
		t.unsigned_remap = unsigned_remap;
		t.wrapr = wrapr;
		t.gamma = gamma;
		t.zfunc = zfunc;
	}
	void set_texture_filter(int index,int bias,int conv,int min,int mag) {
		tex_info&t = tex[index];
		t.filter_bias = bias;
		t.filter_conv = conv;
		t.filter_min = min;
		t.filter_mag = mag;
	}

	void set_line_smooth_enable(int enable) {
		if (enable) glEnable(GL_LINE_SMOOTH);
		else glDisable(GL_LINE_SMOOTH);
	}

	void set_scissor(int x,int y,int w,int h) {
		//outf("set scissor %dx%d %dx%d\n",x,y,w,h);
	}

	void set_zstencil_clear_value(uint32_t v) {
		glClearStencil(v);
	}

	void set_logic_op_enable(uint32_t enable) {
		if (enable) glEnable(GL_LOGIC_OP);
		else glDisable(GL_LOGIC_OP);
	}
	void set_logic_op(uint32_t op) {
		glLogicOp(op);
	}

	void set_alpha_test_enable(uint32_t enable) {
		if (enable) glEnable(GL_ALPHA_TEST);
		else glDisable(GL_ALPHA_TEST);
	}
	void set_alpha_func(uint32_t func,uint32_t ref) {
		glAlphaFunc(func,(float)ref/255.0f);
	}

	void set_depth_bounds_test_enable(uint32_t enable) {
		if (enable) glDisable(GL_DEPTH_CLAMP);
		else glEnable(GL_DEPTH_CLAMP);
	}
	void set_depth_bounds(float min,float max) {
		glDepthRangef(max,min);
	}

	void set_stencil_test_enable(uint32_t enable) {
		if (enable) glEnable(GL_STENCIL_TEST);
		else glDisable(GL_STENCIL_TEST);
	}
	void set_stencil_mask(uint32_t mask) {
		glStencilMask(mask);
	}

	void set_two_sided_stencil_test_enable(uint32_t enable) {
		if (enable) xcept("two sided stencil test");
// 		if (enable) glEnable(GL_STENCIL_TEST_TWO_SIDE_EXT);
// 		else glDisable(GL_STENCIL_TEST_TWO_SIDE_EXT);
	}

	void set_dither_enable(uint32_t enable) {
		if (enable) glEnable(GL_DITHER);
		else glDisable(GL_DITHER);
	}

	void copy2d(void*src,void*dst,uint32_t src_pitch,uint32_t dst_pitch,uint32_t line_length,uint32_t line_count,uint32_t src_format,uint32_t dst_format) {
		dbgf("copy2d src %p, dst %p, src_pitch %u, dst_pitch %u, line_length %u, line_count %u, src_format %d, dst_format %d\n",src,dst,src_pitch,dst_pitch,line_length,line_count,src_format,dst_format);
		if ((src_format|dst_format)==1) {
			if (get_display_buffer_by_addr((uint32_t)src)) xcept("fixme: copy2d src display buffer");
			auto db = get_display_buffer_by_addr((uint32_t)dst);
			if (db) {
				if ((uint32_t)dst!=db->addr || src_pitch!=dst_pitch || (line_length*line_count)!=db->width*db->height*4) xcept("fixme: don't use glDrawPixels!");

				// FIXME: Do not use glDrawPixels for this! It is deprecated, and for good reason.
				//        It would be better to load up a texture and draw a quad.
 				glRasterPos2f(-1.0f,1.0f);
				glPixelZoom(1.0f,-1.0f);
				glDrawPixels(db->width,db->height,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8,src);
				check_gl_error("glDrawPixels");

			} else {
				for (uint32_t i=0;i<line_count;i++) {
					memcpy(dst,src,line_length);
					(uint8_t*&)src += src_pitch;
					(uint8_t*&)dst += dst_pitch;
				}
			}
		} else xcept("unknown copy2d format");
	}

}

