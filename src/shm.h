

boost::interprocess::managed_windows_shared_memory shm;
size_t shm_size = 1024*1024;

std::string shm_name_prefix;
void*shm_base=0;

__declspec(thread) static char ipc_name_buf[0x100];
const char*generate_ipc_name(const char*prefix,uint64_t id=0) {
	if (shm_name_prefix.empty()) xcept("generate_ipc_name called before shm_setup");
	// Names are generated with the most volatile fields first,
	// to make lookup (for the OS) faster.
	char*c = ipc_name_buf;
	while (id) {
		int n = id&0xf;
		id>>=4;
		*c++="0123456789abcdef"[n];
	}
	char*e = c+0x40;
	while (*prefix&&c!=e) *c++ = *prefix++;
	const char*p = shm_name_prefix.c_str();
	e = c+0x40;
	while (*p&&c!=e) *c++ = *p++;
	*c=0;
	return ipc_name_buf;
}

template<typename t>
struct shm_allocator: boost::interprocess::allocator<t,boost::interprocess::managed_windows_shared_memory::segment_manager> {
	shm_allocator() : boost::interprocess::allocator<t,boost::interprocess::managed_windows_shared_memory::segment_manager>(shm.get_segment_manager()) {}
	template<typename t>
	struct rebind {
		typedef shm_allocator<t> other;
	};
	template<class t2>
	shm_allocator(const shm_allocator<t2> &other) 
		: boost::interprocess::allocator<t,boost::interprocess::managed_windows_shared_memory::segment_manager>(other.get_segment_manager()){}
};
template<typename t>
struct shm_deleter: boost::interprocess::deleter<t,boost::interprocess::managed_windows_shared_memory::segment_manager> {
	shm_deleter() : boost::interprocess::deleter<t,boost::interprocess::managed_windows_shared_memory::segment_manager>(shm.get_segment_manager()) {}
	template<typename t>
	struct rebind {
		typedef shm_deleter<t> other;
	};
};

typedef shm_allocator<void> shm_void_allocator;
typedef shm_void_allocator::rebind<char>::other shm_char_allocator;

typedef boost::interprocess::basic_string<char,std::char_traits<char>,shm_char_allocator> shm_string;
template<typename t>
struct shm_vector {
	typedef boost::interprocess::vector<t,shm_allocator<t>> type;
};
template<typename key_t,typename val_t>
struct shm_map {
	typedef boost::interprocess::map<key_t,val_t,std::less<key_t>,shm_allocator<std::pair<const key_t,val_t>>> type;
};
template<typename t>
struct shm_shared_ptr {
	typedef boost::interprocess::shared_ptr<t,shm_allocator<t>,shm_deleter<t>> type;
};

size_t shm_pointer_to_offset(void*p) {
	if (!p) return -1;
	return (size_t)((char*)p - (char*)shm_base);
}
void*shm_offset_to_pointer(size_t offset) {
	if (offset==-1) return 0;
	void*r = ((char*)shm_base + offset);
	ASSUME(r);
	return r;
}

template<typename t>
struct shm_pointer {
	size_t offset;
	shm_pointer() : offset(-1) {}
	shm_pointer(const shm_pointer&);
	t*ptr() const {
		return shm_offset_to_pointer(offset);
	}
	t&operator*() const {
		return *ptr();
	}
	operator t*() const {
		return ptr();
	}
	shm_pointer&operator=(t*p) {
		offset = shm_pointer_to_offset(p);
		return *this;
	}
};
template<>
struct shm_pointer<void> {
	typedef void t;
	size_t offset;
	shm_pointer() : offset(-1) {}
	void*ptr() const {
		return shm_offset_to_pointer(offset);
	}
	operator void*() const {
		return ptr();
	}
	shm_pointer&operator=(t*p) {
		offset = shm_pointer_to_offset(p);
		return *this;
	}
};

std::vector<std::function<void*(void*)>> shm_object_list;
template<typename t>
struct shm_object {
	typedef t t;
	t*v;
	t*operator->() const {
		return v;
	}
	t&operator*() const {
		return *v;
	}
	shm_object() {
		v=0;
		shm_object_list.push_back([this](void*p) -> void* {
			return v=p?(t*)p:(t*)new (shm.allocate(sizeof(t))) t();
		});
	}
};

struct shm_cb {
	template<typename F>
	shm_cb(F f) {
		shm_object_list.push_back([this,f](void*p) -> void* {
			f();
			return p;
		});
	}
};

struct shm_root_t {
	uint64_t reserved1, reserved2;
	boost::interprocess::interprocess_mutex setup_mutex;
	shm_vector<shm_pointer<void>>::type shm_objects;
	timebase_frequency_fetcher tb_freq_calculator;
	int sysutil_daemon_running;
	shm_root_t() : reserved1(0), reserved2(0) {}
	void init() {
		boost::unique_lock<boost::interprocess::interprocess_mutex> l(setup_mutex);
		shm_objects.resize(shm_object_list.size());
		for (size_t i=0;i<shm_object_list.size();i++) {
			shm_objects[i] = shm_object_list[i](shm_objects[i]);
		}
		timebase_frequency = *tb_freq_calculator;
	}
};
shm_root_t*shm_root;

void shm_setup(const char*shm_name) {

	shm_name_prefix = format("ps3-shm-%s",shm_name);
	if (shm_name_prefix.size()>0x20) shm_name_prefix.resize(0x20);

	shm_base = 0;
	shm.swap(boost::interprocess::managed_windows_shared_memory(boost::interprocess::open_or_create,generate_ipc_name("root"),shm_size,shm_base));
	shm_base = shm.get_address();

	shm_root = shm.find_or_construct<shm_root_t>("root")();
	shm_root->init();

	dbgf("shm  address: %p  size: %#x  used: %#x\n",shm_base,shm_size,shm_size-shm.get_free_memory());
}

