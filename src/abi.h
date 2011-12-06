
namespace abi {
	typedef void*context;
	enum {
		r_cr = 0, r_lr = 1, r_ctr = 2,
		r_gpr = 3,
		r_xer = 35,
		r_fpr = 36,
		r_fpscr= 68,
		r_vr = 69,
		r_vscr = 101,
		r_vrsave = 102,
		r_count = 103,
	};
	uint64_t rr(context ctx,int n) {
		return ((uint64_t*)ctx)[n*2];
	}
	void wr(context ctx,int n,uint64_t v) {
		((uint64_t*)ctx)[n*2] = v;
	}
	uint64_t rgr(context ctx,int n) {
		return rr(ctx,r_gpr+n);
	}
	void wgr(context ctx,int n,uint64_t v) {
		wr(ctx,r_gpr+n,v);
	}
	double rfr(context ctx,int n) {
		uint64_t v = rr(ctx,r_fpr+n);
		static_assert(sizeof(double)==sizeof(uint64_t),"double is not 64 bits");
		return (double&)v;
	}
	void wfr(context ctx,int n,double v) {
		wgr(ctx,r_fpr+n,(uint64_t&)v);
	}
	void call(context ctx,void*func) {
		((void(*)(context))func)(ctx);
	}

}

