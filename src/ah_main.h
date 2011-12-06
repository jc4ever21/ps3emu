

std::map<std::pair<std::string,uint32_t>,uint32_t> ah_map;

template<typename F,F*ptr> uint32_t ah_wrapfunc();
struct ah_register2 {
	const char*libname;
	ah_register2() {}
	ah_register2(const char*libname) : libname(libname) {}
	template<typename T> struct is_float {
		static const int v = 0;
	};
	template<> struct is_float<float> {
		static const int v = 1;
	};
	template<> struct is_float<double> {
		static const int v = 1;
	};
	template<typename F,F*ptr> struct func;
	template<int n_i,int n_f,typename F,F*ptr>  struct func2;
	template<typename T,int n,bool i> struct select;
	template<typename T,int n> struct select<T,n,true> {
		static T get(abi::context ctx) {
			return (T)abi::rgr(ctx,3+n-1);
		}
	};
	template<typename T,int n> struct select<T,n,false> {
		static T get(abi::context ctx) {
			return (T)abi::rfr(ctx,1+n-101);
		}
	};
#define args abi::context ctx
#define passargs ctx
#define select(t,n,ctx) select<t,n,(n<100)>::get(ctx)
#include "ah_register2_code.h"
#undef select
#undef passargs
#undef args
	template<typename F,F*ptr>
	ah_register2&add(const char*name) {
		ah_map[std::make_pair(libname,libname_id(name))] = ah_wrapfunc<F,ptr>();
		return *this;
	}
};

template<typename fx,typename R> struct ah_wrapfunc_impl2 {
	static uint64_t f(abi::context context) {
		abi::wgr(context,3,(uint64_t)fx::f(context));
		return rr(context,abi::r_lr);
	}
};
template<typename fx> struct ah_wrapfunc_impl2<fx,void> {
	static uint64_t f(abi::context context) {
		fx::f(context);
		return rr(context,abi::r_lr);
	}
};
template<typename F,F*ptr>
uint64_t ah_wrapfunc_impl(abi::context context) {
	typedef ah_register2::func<F,ptr> fx;
	return ah_wrapfunc_impl2<fx,fx::R>::f(context);
}
template<typename F,F*ptr>
void ah_wrapfunc_impl_noret(abi::context context) {
	typedef ah_register2::func<F,ptr> fx;
	ah_wrapfunc_impl2<fx,fx::R>::f(context);
}

struct ah_wrapfunc_list_data {
	uint8_t data[0x10];
};
std::list<ah_wrapfunc_list_data,mm_allocator<uint64_t>> ah_wrapfunc_list;
template<typename F,F*ptr>
uint32_t ah_wrapfunc() {
	auto f = &ah_wrapfunc_impl<F,ptr>;
	uint8_t buf[0x10];
	buf[0] = 0x48; // mov rax, f
	buf[1] = 0xb8;
	(uint64_t&)buf[2] = (uint64_t)f;
	buf[0xa] = 0xff; // jmp rax
	buf[0xb] = 0xe0;
	ah_wrapfunc_list.push_back((ah_wrapfunc_list_data&)buf[0]);
	auto r = &ah_wrapfunc_list.back();
	if ((uint32_t)r!=(uint64_t)r) xcept("%p does not fit in 32 bits",(void*)r);
	DWORD old_prot;
	VirtualProtect((void*)r,0x10,PAGE_EXECUTE_READWRITE,&old_prot);
	return (uint32_t)r;
}

#include "ah_macros.h"

namespace {
	// This is to avoid a lot of debug spam when stopping modules, and
	// to not run some libsysutil shutdown code.
	void sys_prx_exitspawn_with_level(uint64_t r) {
		dbgf("sys_prx_exitspawn_with_level - %d\n",r);
		exit(r);
	}
	ah_reg(sysPrxForUser,sys_prx_exitspawn_with_level);
}

#include "ah_video.h"
#include "ah_audio.h"
#include "ah_io.h"