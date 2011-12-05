#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable:4345) // warning C4345: behavior change: an object of POD type constructed with an initializer of the form () will be default-initialized

#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <setjmp.h>

#ifdef _MSC_VER

#pragma comment(lib,"winmm.lib")

#define EXPORT __declspec(dllexport)
#define NODEFAULT __assume(0)
#define ASSUME(x) __assume(x)

#define _WIN32_WINNT 0x600
#define NOMINMAX
#include <windows.h>

#include <intrin.h>

static void busy_yield() {
	_mm_pause();
}

#define TLS __declspec(thread)

#else
#error
#endif

#include <exception>
#include <string>
#include "strf.h"
using namespace strf;

STRF_FUNC(void,xcept,const char*str=,
	outf("about to throw exception %s\n",str);
	fflush(stdout);
	throw (const char*)str;
);

extern int debug_flag;
STRF_FUNC(int,dbgf2,if (!debug_flag) return 0;const char*str=,fputs(str,stdout);return 0;);
#define dbgf(...) (debug_flag && dbgf2(__VA_ARGS__))

namespace lv {
	int get_thread_id();
}

extern "C" EXPORT
void F_bad_insn(uint64_t addr) {
	xcept("%02X bad insn at %p\n",lv::get_thread_id(),(void*)addr);
}
extern "C" EXPORT
void F_outside_code(uint64_t addr) {
	xcept("%02X outside code at %p\n",lv::get_thread_id(),(void*)addr);
}

extern "C" EXPORT
void F_dump_addr(uint64_t addr) {
	outf("%02X %016x :: \n",lv::get_thread_id(),addr);
	fflush(stdout);
}
extern "C" EXPORT
void F_stack_call(uint64_t addr) {
	outf("%02X      call %016x \n",lv::get_thread_id(),addr);
	fflush(stdout);
}
extern "C" EXPORT
void F_stack_ret(uint64_t addr) {
	outf("%02X      ret  %016x \n",lv::get_thread_id(),addr);
	fflush(stdout);
}

extern "C" EXPORT void F_err(uint64_t addr,const char*str) {
	xcept("%02X error at %016x :: %s",lv::get_thread_id(),addr,str);
}
extern "C" EXPORT void F_print(uint64_t addr,const char*str) {
	outf("%02X %016x :: %s\n",lv::get_thread_id(),addr,str);
	fflush(stdout);
}
extern "C" EXPORT void F_dump_regs(uint64_t r0,uint64_t r1,uint64_t r2,uint64_t r3,uint64_t r4,uint64_t r5,uint64_t r6,uint64_t r7,uint64_t r8,uint64_t r9,uint64_t r10,uint64_t r11,uint64_t r13) {
	outf("%02X REGS :: r0 %x r1 %x r2 %x r3 %x r4 %x r5 %x r6 %x r7 %x r8 %x r9 %x r10 %x r11 %x r13 %x\n",lv::get_thread_id(),r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r13);
	fflush(stdout);
}

#include <math.h>

extern "C" EXPORT float F_RoundToSPIntFloor(float v) {
	return floorf(v);
}
extern "C" EXPORT float F_RoundToSPIntNear(float v) {
	xcept("F_RoundToSPIntNear");
}
extern "C" EXPORT float F_RoundToSPIntCeil(float v) {
	xcept("F_RoundToSPIntCeil");
}
extern "C" EXPORT float F_RoundToSPIntTrunc(float v) {
	xcept("F_RoundToSPIntTrunc");
}

extern "C" EXPORT float F_sqrt32(float v) {
	return sqrt(v);
}
extern "C" EXPORT double F_sqrt64(double v) {
	return sqrt(v);
}

int insn_count = 0;
extern "C" void F_tmp() {
	outf("%d instructions executed\n",++insn_count);
}

#ifdef _MSC_VER
#include <intrin.h>
template<typename t>
static t se(t v);
template<>
static uint64_t se(uint64_t v) {
	return _byteswap_uint64(v);
}
template<>
static int64_t se(int64_t v) {
	return se((uint64_t)v);
}
template<>
static uint32_t se(uint32_t v) {
	static_assert(sizeof(decltype(_byteswap_ulong(v)))==4,"se32 bad size");
	return _byteswap_ulong(v);
}
template<>
static int32_t se(int32_t v) {
	return se((uint32_t)v);
}
template<>
static uint16_t se(uint16_t v) {
	static_assert(sizeof(decltype(_byteswap_ushort(v)))==2,"se16 bad size");
	return _byteswap_ushort(v);
}
#else
template<typename t>
static t se(t v);
template<>
static uint64_t se(uint64_t v) {
	return (v>>56) | ((v&0xff000000000000)>>40) | ((v&0xff0000000000)>>24) | ((v&0xff00000000)>>8) | ((v&0xff000000)<<8) | ((v&0xff0000)<<24) | ((v&0xff00)<<40) | ((v&0xff)<<56);
}
template<>
static int64_t se(int64_t v) {
	return se((uint64_t)v);
}
template<>
static uint32_t se(uint32_t v) {
	return v>>24 | (v&0xff0000)>>8 | (v&0xff00)<<8 | (v&0xff)<<24;
}
template<>
static int32_t se(int32_t v) {
	return se((uint32_t)v);
}
template<>
static uint16_t se(uint16_t v) {
	return v>>8 | (v&0xff)<<8;
}
template<typename t>
static t se24(t v) {
	return (v&0xff)<<16 | (v&0xff00) | (v&0xff0000)>>16;
}
#endif
template<>
static float se(float v) {
	uint32_t r = se((uint32_t&)v);
	return (float&)r;
}
// uint8 version exists only so se can be called on template values.
// It should not be called if the type is known to be uint8_t
template<>
static uint8_t se(uint8_t v) {
	return v;
}

#include <stdlib.h>
#include <stdint.h>

#include <vector>
#include <list>
#include <map>
#include <stack>
#include <utility>
#include <queue>
#include <string>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>


typedef void*spuc_handle;
spuc_handle spuc_open(void*ls,bool is_raw_spu);
void spuc_close(spuc_handle);
void spuc_reset(spuc_handle);
void*spuc_get_function(spuc_handle,uint32_t offset);
void spuc_call(spuc_handle,void*);
void spuc_sync(spuc_handle);
void spuc_set_args(spuc_handle,uint64_t,uint64_t,uint64_t,uint64_t);
#pragma comment(lib,"ps3spu.lib")

void*liblv2_start_entry=0; // this will be set to the start point of liblv2.prx

void(*entry)(uint64_t argc,uint64_t*argv,uint64_t envc,uint64_t*envv);
extern "C" void mainCRTStartup();
// entry point from elf images
extern "C" EXPORT void F_entry(void*f) {
	(void*&)entry=f;

	mainCRTStartup();
	// never returns
}
// DllMain must be set as the entry point, otherwise the CRT will initialize on dll load (we don't want that)
extern "C" BOOL WINAPI DllMain(HANDLE hDllHandle,DWORD dwReason,LPVOID lpreserved) {
	if (dwReason==DLL_PROCESS_ATTACH) DisableThreadLibraryCalls((HMODULE)hDllHandle);
	return TRUE;
}

void*dll_prx_dll;
void*dll_prx_image;
void*dll_prx_sections;
void*dll_prx_fixups;
void*dll_prx_mod_info;
void*dll_prx_fix_got;

// this is the entry point from prxes.
extern "C" EXPORT BOOL F_dll_main(HANDLE hDllHandle,DWORD dwReason,LPVOID lpreserved,void*image,void*sections,void*fixups,void*mod_info,void*fix_got) {
	if ((uint32_t)hDllHandle!=(uint64_t)hDllHandle) xcept("dll loaded at %p, which does not fit in 32 bits",(void*)hDllHandle);
	if (dwReason==DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls((HMODULE)hDllHandle);
		if (dll_prx_image) xcept("dll_prx_image not null");
		dll_prx_dll = (void*)hDllHandle;
		dll_prx_image = image;
		dll_prx_sections = sections;
		dll_prx_fixups = fixups;
		dll_prx_mod_info = mod_info;
		dll_prx_fix_got = fix_got;
	}
	return TRUE;
}


#include <boost/exception/all.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
//#include <boost/asio.hpp>
#include <boost/timer.hpp>
#include <boost/thread.hpp>
#include <boost/thread/xtime.hpp>
#include <boost/interprocess/detail/atomic.hpp>
#include <boost/thread/locks.hpp>
#include <boost/filesystem.hpp>
#include <boost/interprocess/offset_ptr.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <boost/interprocess/managed_windows_shared_memory.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/smart_ptr/shared_ptr.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/smart_ptr/deleter.hpp>
#include <boost/timer.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/rbtree.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/intrusive/unordered_set.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

struct basic_slist_node {
	basic_slist_node*prev,*next;
	typedef basic_slist_node node;
	typedef node*node_ptr;
	typedef const node*const_node_ptr;
	static node_ptr get_previous(const_node_ptr n) {return n->prev;}
	static void set_previous(node_ptr n,node_ptr prev) {n->prev=prev;}
	static node_ptr get_next(const_node_ptr n) {return n->next;}
	static void set_next(node_ptr n,node_ptr next) {n->next=next;}
};

template<typename node>
struct basic_rbtree_node {
	node*parent,*left,*right;
	typedef node node;
	typedef node*node_ptr;
	typedef const node*const_node_ptr;
	static node_ptr get_parent(const_node_ptr n) {return n->parent;}
	static void set_parent(node_ptr n,node_ptr parent) {n->parent=parent;}
	static node_ptr get_left(const_node_ptr n) {return n->left;}
	static void set_left(node_ptr n,node_ptr left) {n->left=left;}
	static node_ptr get_right(const_node_ptr n) {return n->right;}
	static void set_right(node_ptr n,node_ptr right) {n->right=right;}
	typedef int color;
	color color_;
	static color get_color(const_node_ptr n) {return n->color_;}
	static void set_color(node_ptr n,color c) {n->color_=c;}
	static color black() {return 0;}
	static color red() {return 1;}
};


struct t_settimeres {
	DWORD period;
	t_settimeres() {
		period=1;
		TIMECAPS tc;
		if (timeGetDevCaps(&tc,sizeof(tc))==MMSYSERR_NOERROR) period=tc.wPeriodMin;
		timeBeginPeriod(period);
	}
	~t_settimeres() {
		timeEndPeriod(period);
	}
} settimeres;

std::vector<std::function<void()>> proc_init_cb_list;

struct proc_init_cb {
	template<typename F>
	proc_init_cb(F&&f) {
		proc_init_cb_list.push_back(std::forward<F>(f));
	}
};

namespace lv {
	int main(int argc,char**argv);
}
int main(int argc,char**argv) {
	return lv::main(argc,argv);
}

#include "config_ini.h"

int debug_flag = config.get_int("main","debug_flag");
//int debug_flag = 0;

namespace lv {

	// The highest possible thread id. Thread code must make sure it never
	// generates a thread with higher id than this.
	const int max_thread_id = 0xfff;
	std::pair<int,int> get_thread_id_and_priority();

#include "mem_mgr.h"

#include "errno.h"
#include "atomic.h"
#include "time.h"
#include "shm.h"

#include "win32_thread.h"
#include "win32_sync.h"
	namespace sync {
		typedef win32_sync::ipc_event ipc_event;
	}
#include "sync_ns.h"
#include "mmio.h"
#include "gcm.h"

#include "helpers.h"

#include "sync.h"
#include "mem.h"
#include "file.h"
#include "event.h"
#include "process.h"
#include "prx.h"
#include "thread.h"
#include "interrupt.h"
#include "timer.h"
#include "misc.h"
#include "spu.h"
#include "rsx.h"

#include "sysutil_daemon.h"


	int main(int argc,char**argv) {
		try {

			auto reserve_range = [&](uint64_t begin,uint64_t end) {
				void*r = VirtualAlloc((void*)begin,end-begin,MEM_RESERVE,PAGE_NOACCESS);
				if (!r||r!=(void*)begin) xcept("failed to reserve address range [%p,%p) (error %d)",(void*)begin,(void*)end,GetLastError());
			};
			reserve_range(0xe0000000,0xf0000000); // raw spu mmio
			reserve_range(0xf0000000,0x100000000); // spu thread mmio

			const char*shm_name = "default";
			int pid = 0;

			int i;
			for (i=1;i<argc;i++) {
				if (!strcmp(argv[i],"--")) {i++;break;}
				if (i==1) shm_name = argv[i];
				if (i==2) pid = atoi(argv[i]);
			}

			mmio::init();

			shm_setup(shm_name);
			proc_setup(pid);

			if (atomic_xchg(&shm_root->sysutil_daemon_running,1)==0) sysutil_daemon_init();

			int p_argc = argc-i;
			int p_envc = 0;
			// These must fit in 32 bits, so we allocate them
			uint64_t*p_argv = (uint64_t*)mm_alloc(sizeof(uint64_t)*(p_argc+1+p_envc+1));
			uint64_t*p_envv = p_argv+p_argc+1;

			if (!p_argv || !p_envv) xcept("failed to allocate argv/envv memory");

			for (;i<argc;i++) {
				*p_argv++ = se((uint64_t)argv[i]);
			}
			p_argv -= p_argc;
			
			p_argv[p_argc] = 0;
			p_envv[p_envc] = 0;
			entry(p_argc,p_argv,p_envc,p_envv);
			// never returns
			xcept("unreachable (entry returned)");
			return 0;
		} catch (const char*str) {
			outf("[exception] %s\n",str);
			return 1;
		} catch (const std::exception&e) {
			outf("[std::exception] %s\n",e.what());
			return 1;
		}
	}

	extern "C" EXPORT uint64_t F_alloc_main_stack() {
		return alloc_main_stack();
	}

	void setup_syscalls();
	
	extern "C" EXPORT void*F_proc_init(void*p_thread_init,process_param_t*params,prx_info_t*prx_info,void*unresolved_import_cb,void*libcall_wrapper,uint64_t*malloc_pagesize,uint64_t*main_thread_id) {
		(void*&)I_thread_entry = p_thread_init;
		(void*&)I_unresolved_import = unresolved_import_cb;
		(void*&)I_libcall_wrapper = libcall_wrapper;

		for (auto i=proc_init_cb_list.begin();i!=proc_init_cb_list.end();++i) {
			(*i)();
		}

		set_process_params(params);
		set_prx_info(prx_info);

		init_main_thread();

		*malloc_pagesize = process_params.malloc_pagesize;
		*main_thread_id = get_thread_id();

		setup_syscalls();

		// if liblv2.prx was loaded, return the entry point
		// otherwise, return 0, and the .elf entry will be called directly
		return liblv2_start_entry;
	}

	extern "C" EXPORT void F_unresolved_import(uint64_t v) {
		call_to_unresolved_import(v);
	}
	extern "C" EXPORT void F_libcall_print(uint64_t v) {
		libcall_print(v);
	}
	
	extern "C" EXPORT void F_spu_stop(uint32_t stop_and_signal_type,uint32_t next_pc) {
		spu_stop(stop_and_signal_type,next_pc);
	}
	extern "C" EXPORT void F_spu_restart(uint32_t offset) {
		spu_restart(offset);
	}
	extern "C" EXPORT void*F_spu_get_function(uint32_t offset) {
		return spu_get_function(offset);
	}
	extern "C" EXPORT uint32_t F_spu_rdch(uint32_t ca) {
		return spu_rdch(ca);
	}
	extern "C" EXPORT uint32_t F_spu_rdch_count(uint32_t ca) {
		return spu_rdch_count(ca);
	}
	extern "C" EXPORT void F_spu_wrch(uint32_t ca,uint32_t v) {
		spu_wrch(ca,v);
	}
	extern "C" EXPORT uint32_t F_spu_wrch_count(uint32_t ca) {
		return spu_wrch_count(ca);
	}
	extern "C" EXPORT uint64_t F_spu_x_get_ls_addr(uint32_t spu_num) {
		return spu_x_get_ls_addr(spu_num);
	}
	extern "C" EXPORT void F_spu_x_write_snr(uint32_t spu_num,uint32_t snr,uint32_t v) {
		spu_x_write_snr(spu_num,snr,v);
	}
	extern "C" EXPORT void F_spu_wr_out_intr_mbox(uint32_t v) {
		spu_wr_out_intr_mbox(v);
	}

	int sys_tty_write(int tty,const char*str,uint32_t size,uint32_t*written) {
		outf("%02X TTY%02d: %.*s\n",get_thread_id(),tty,size,str);
		//outf("%.*s",size,str);
		*written = se((uint32_t)size);
		return CELL_OK;
	}
	int sys_tty_read(int tty,char*buf,uint32_t size,uint32_t*read) {
		uint32_t i;
		for (i=0;i<size;i++) {
			int c = getchar();
			*buf++ = (char)c;
		}
		*read = se((uint32_t)i);
		return CELL_OK;
	}
	int sys_462() {
		// don't know what this is, but when it returns 0
		// it loads /app_home/libsysmodule.sprx, and otherwise
		// it loads /dev_flash/sys/external/libsysmodule.sprx
		dbgf("syscall 462 returning ENOSYS\n");
		return ENOSYS;
	}
	int sys_817() {
		// called from cellFsOpen? investigate!
		return ENOSYS;
	}
	int sys_398(char*str) {
		outf("398: %s\n",str);
		return CELL_OK;
	}
	int sys_998(uint64_t a1,uint64_t a2) {
		xcept("998");
		// Return value doesn't seem to be checked
		outf("988: r3 %#x r4 %#x (returning ENOSYS)\n",a1,a2);
		return ENOSYS;
	}
	int sys_863() {
		xcept("update manager service");
		// update manager service
		return ENOSYS;
	}
	std::function<void(uint64_t*)> syscall_list[1024];
	void setup_syscalls() {
		for (int i=0;i<1024;i++) {
			syscall_list[i] = [i](uint64_t*context) {
				xcept("unknown syscall %d",i);
			};
		}
#define sc(n,f) syscall_list[n] = &ah_wrapfunc_impl_noret<decltype(f),&f>
		sc(1,sys_process_getpid);
		sc(14,sys_process_is_spu_lock_line_reservation_address);
		sc(21,sys_process_create);
		sc(22,sys_process_exit);
		sc(25,sys_process_get_sdk_version);
		sc(30,sys_process_get_paramsfo);
		sc(41,sys_ppu_thread_exit);
		sc(43,sys_ppu_thread_yield);
		sc(44,sys_ppu_thread_join);
		sc(45,sys_ppu_thread_detach);
		sc(46,sys_ppu_thread_get_join_state);
		sc(47,sys_ppu_thread_set_priority);
		sc(48,sys_ppu_thread_get_priority);
		sc(49,sys_ppu_thread_get_stack_information);
		sc(52,sys_ppu_thread_create);
		sc(53,sys_ppu_thread_start);
		sc(70,sys_timer_create);
		sc(71,sys_timer_destroy);
		sc(72,sys_timer_get_information);
		sc(73,sys_timer_start);
		sc(74,sys_timer_stop);
		sc(75,sys_timer_connect_event_queue);
		sc(76,sys_timer_disconnect_event_queue);
		sc(81,sys_interrupt_tag_destroy);
		sc(84,sys_interrupt_thread_establish);
		sc(88,sys_interrupt_thread_eoi);
		sc(89,sys_interrupt_thread_disestablish);
		sc(90,sys_semaphore_create);
		sc(91,sys_semaphore_destroy);
		sc(92,sys_semaphore_wait);
		sc(93,sys_semaphore_trywait);
		sc(94,sys_semaphore_post);
		sc(95,sys_lwmutex_create);
		sc(96,sys_lwmutex_destroy);
		sc(97,sys_lwmutex_lock);
		sc(98,sys_lwmutex_unlock);
		sc(99,sys_lwmutex_trylock);
		sc(100,sys_mutex_create);
		sc(101,sys_mutex_destroy);
		sc(102,sys_mutex_lock);
		sc(103,sys_mutex_trylock);
		sc(104,sys_mutex_unlock);
		sc(105,sys_cond_create);
		sc(106,sys_cond_destroy);
		sc(107,sys_cond_wait);
		sc(108,sys_cond_signal);
		sc(109,sys_cond_signal_all);
		sc(110,sys_cond_signal_to);
		sc(111,sys_lwcond_create);
		sc(112,sys_lwcond_destroy);
		sc(113,sys_lwcond_wait);
		sc(114,sys_semaphore_get_value);
		sc(115,sys_lwcond_signal);
		sc(116,sys_lwcond_signal_all);
		sc(120,sys_rwlock_create);
		sc(121,sys_rwlock_destroy);
		sc(122,sys_rwlock_rlock);
		sc(123,sys_rwlock_tryrlock);
		sc(124,sys_rwlock_runlock);
		sc(125,sys_rwlock_wlock);
		sc(126,sys_rwlock_trywlock);
		sc(127,sys_rwlock_wunlock);
		sc(128,sys_event_queue_create);
		sc(129,sys_event_queue_destroy);
		sc(130,sys_event_queue_receive);
		sc(131,sys_event_queue_tryreceive);
		sc(133,sys_event_queue_drain);
		sc(134,sys_event_port_create);
		sc(135,sys_event_port_destroy);
		sc(136,sys_event_port_connect_local);
		sc(137,sys_event_port_disconnect);
		sc(138,sys_event_port_send);
		sc(140,sys_event_port_connect_ipc);
		sc(141,sys_timer_usleep);
		sc(142,sys_timer_sleep);
		sc(147,sys_time_get_timebase_frequency);
		sc(150,sys_raw_spu_create_interrupt_tag);
		sc(151,sys_raw_spu_set_int_mask);
		sc(152,sys_raw_spu_get_int_mask);
		sc(153,sys_raw_spu_set_int_stat);
		sc(154,sys_raw_spu_get_int_stat);
		sc(156,sys_spu_image_open);
		sc(158,sys_spu_image_close);
		sc(160,sys_raw_spu_create);
		sc(161,sys_raw_spu_destroy);
		sc(163,sys_raw_spu_read_puint_mb);
		sc(165,sys_spu_thread_get_exit_status);
		sc(169,sys_spu_initialize);
		sc(170,sys_spu_thread_group_create);
		sc(171,sys_spu_thread_group_destroy);
		sc(172,sys_spu_thread_initialize);
		sc(173,sys_spu_thread_group_start);
		sc(178,sys_spu_thread_group_join);
		sc(181,sys_spu_thread_write_ls);
		sc(182,sys_spu_thread_read_ls);
		sc(184,sys_spu_thread_write_snr);
		sc(185,sys_spu_thread_group_connect_event);
		sc(186,sys_spu_thread_group_disconnect_event);
		sc(190,sys_spu_thread_write_spu_mb);
		sc(191,sys_spu_thread_connect_event);
		sc(192,sys_spu_thread_disconnect_event);
		sc(193,sys_spu_thread_bind_queue);
		sc(194,sys_spu_thread_unbind_queue);
		sc(251,sys_spu_thread_group_connect_event_all_threads);
		sc(252,sys_spu_thread_group_disconnect_event_all_threads);
		sc(330,sys_mmapper_allocate_address);
		sc(332,sys_mmapper_allocate_shared_memory);
		sc(334,sys_mmapper_map_shared_memory);
		sc(335,sys_mmapper_unmap_shared_memory);
		sc(337,sys_mmapper_search_and_map);
		sc(341,sys_memory_container_create);
		sc(342,sys_memory_container_destroy);
		sc(343,sys_memory_container_get_size);
		sc(348,sys_memory_allocate);
		sc(349,sys_memory_free);
		sc(350,sys_memory_allocate_from_container);
		sc(351,sys_memory_get_page_attribute);
		sc(352,sys_memory_get_user_memory_size);
		sc(380,sys_get_system_parameters);
		sc(398,sys_398);
		sc(402,sys_tty_read);
		sc(403,sys_tty_write);
		sc(462,sys_462);
		sc(465,sys_prx_load_module_list);
		sc(480,sys_prx_load_module);
		sc(481,sys_prx_start_module);
		sc(482,sys_prx_stop_module);
		sc(483,sys_prx_unload_module);
		sc(484,sys_prx_register_module);
		sc(486,sys_prx_register_library);
		sc(494,sys_prx_get_module_list);
		sc(496,sys_prx_get_module_id_by_name);
		sc(497,sys_prx_load_module_on_memcontainer);
		sc(516,sys_config_start);
		sc(517,sys_config_stop);
		sc(610,sys_storage_get_device_config);
		sc(611,sys_storage_report_devices);
		sc(668,sys_rsx_memory_allocate);
		sc(670,sys_rsx_context_allocate);
		sc(672,sys_rsx_context_iomap);
		sc(674,sys_rsx_context_attribute);
		sc(675,sys_rsx_device_map);
		sc(677,sys_rsx_attribute);
		sc(801,sys_fs_open);
		sc(802,sys_fs_read);
		sc(803,sys_fs_write);
		sc(804,sys_fs_close);
		sc(805,sys_fs_open_dir);
		sc(806,sys_fs_read_dir);
		sc(807,sys_fs_close_dir);
		sc(808,sys_fs_stat);
		sc(809,sys_fs_fstat);
		sc(810,sys_fs_link);
		sc(811,sys_fs_mkdir);
		sc(812,sys_fs_rename);
		sc(813,sys_fs_rmdir);
		sc(814,sys_fs_unlink);
		sc(815,sys_fs_utime);
		sc(817,sys_817);
		sc(818,sys_fs_lseek64);
		sc(820,sys_fs_fsync);
		sc(831,sys_fs_truncate);
		sc(832,sys_fs_ftruncate);
		sc(834,sys_fs_chmod);
		sc(837,sys_storage_util_mount);
		sc(838,sys_storage_util_unmount);
		sc(863,sys_863);
		sc(867,sys_get_model_information);
		sc(871,sys_ss_access_control_engine);
		sc(873,sys_ss_get_cache_of_product_mode);
		sc(874,sys_ss_get_cache_of_flash_ext_flag);
		sc(988,sys_998);
#undef sc
	}

	extern "C" EXPORT
	void F_syscall(uint64_t n,uint64_t*context) {
		dbgf("%02X syscall %d\n",get_thread_id(),n);
		syscall_list[n](context);
	}

}

