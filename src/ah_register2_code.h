template<typename R,R(*ptr)()> struct func<R(),ptr> {
	typedef typename R R;
	static R f(args) {
		return ptr();
	}
};
template<typename R,typename A1,R(*ptr)(A1)> struct func<R(A1),ptr> {
	typedef typename R R;
	static const int a1_n = is_float<A1>::v ? 101 : 1;
	static R f(args) {
		return ptr(select(A1,a1_n,passargs));
	}
};
template<typename R,typename A1,typename A2,R(*ptr)(A1,A2)> struct func<R(A1,A2),ptr> {
	typedef typename R R;
	static const int a1_n = is_float<A1>::v ? 101 : 1;
	static const int a2_n = is_float<A2>::v == is_float<A1>::v ? a1_n+1 : is_float<A2>::v ? 101 : 1;
	static R f(args) {
		return ptr(select(A1,a1_n,passargs),select(A2,a2_n,passargs));
	}
};
template<typename R,typename A1,typename A2,typename A3,R(*ptr)(A1,A2,A3)> struct func<R(A1,A2,A3),ptr> {
	typedef typename R R;
	static const int a1_n = is_float<A1>::v ? 101 : 1;
	static const int a2_n = is_float<A2>::v == is_float<A1>::v ? a1_n+1 : is_float<A2>::v ? 101 : 1;
	static const int a3_n = is_float<A3>::v == is_float<A2>::v ? a2_n+1 : is_float<A3>::v == is_float<A1>::v ? a1_n+1 : is_float<A3>::v ? 101 : 1;
	static R f(args) {
		return ptr(select(A1,a1_n,passargs),select(A2,a2_n,passargs),select(A3,a3_n,passargs));
	}
};
template<typename R,typename A1,typename A2,typename A3,typename A4,R(*ptr)(A1,A2,A3,A4)> struct func<R(A1,A2,A3,A4),ptr> {
	typedef typename R R;
	static const int a1_n = is_float<A1>::v ? 101 : 1;
	static const int a2_n = is_float<A2>::v == is_float<A1>::v ? a1_n+1 : is_float<A2>::v ? 101 : 1;
	static const int a3_n = is_float<A3>::v == is_float<A2>::v ? a2_n+1 : is_float<A3>::v == is_float<A1>::v ? a1_n+1 : is_float<A3>::v ? 101 : 1;
	static const int a4_n = is_float<A4>::v == is_float<A3>::v ? a3_n+1 : is_float<A4>::v == is_float<A2>::v ? a2_n+1 : is_float<A4>::v == is_float<A1>::v ? a1_n+1 : is_float<A4>::v ? 101 : 1;
	static R f(args) {
		return ptr(select(A1,a1_n,passargs),select(A2,a2_n,passargs),select(A3,a3_n,passargs),select(A4,a4_n,passargs));
	}
};
template<typename R,typename A1,typename A2,typename A3,typename A4,typename A5,R(*ptr)(A1,A2,A3,A4,A5)> struct func<R(A1,A2,A3,A4,A5),ptr> {
	typedef typename R R;
	static const int a1_n = is_float<A1>::v ? 101 : 1;
	static const int a2_n = is_float<A2>::v == is_float<A1>::v ? a1_n+1 : is_float<A2>::v ? 101 : 1;
	static const int a3_n = is_float<A3>::v == is_float<A2>::v ? a2_n+1 : is_float<A3>::v == is_float<A1>::v ? a1_n+1 : is_float<A3>::v ? 101 : 1;
	static const int a4_n = is_float<A4>::v == is_float<A3>::v ? a3_n+1 : is_float<A4>::v == is_float<A2>::v ? a2_n+1 : is_float<A4>::v == is_float<A1>::v ? a1_n+1 : is_float<A4>::v ? 101 : 1;
	static const int a5_n = is_float<A5>::v == is_float<A4>::v ? a4_n+1 : is_float<A5>::v == is_float<A3>::v ? a3_n+1 : is_float<A5>::v == is_float<A2>::v ? a2_n+1 : is_float<A5>::v == is_float<A1>::v ? a1_n+1 : is_float<A5>::v ? 101 : 1;
	static R f(args) {
		return ptr(select(A1,a1_n,passargs),select(A2,a2_n,passargs),select(A3,a3_n,passargs),select(A4,a4_n,passargs),select(A5,a5_n,passargs));
	}
};
template<typename R,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,R(*ptr)(A1,A2,A3,A4,A5,A6)> struct func<R(A1,A2,A3,A4,A5,A6),ptr> {
	typedef typename R R;
	static const int a1_n = is_float<A1>::v ? 101 : 1;
	static const int a2_n = is_float<A2>::v == is_float<A1>::v ? a1_n+1 : is_float<A2>::v ? 101 : 1;
	static const int a3_n = is_float<A3>::v == is_float<A2>::v ? a2_n+1 : is_float<A3>::v == is_float<A1>::v ? a1_n+1 : is_float<A3>::v ? 101 : 1;
	static const int a4_n = is_float<A4>::v == is_float<A3>::v ? a3_n+1 : is_float<A4>::v == is_float<A2>::v ? a2_n+1 : is_float<A4>::v == is_float<A1>::v ? a1_n+1 : is_float<A4>::v ? 101 : 1;
	static const int a5_n = is_float<A5>::v == is_float<A4>::v ? a4_n+1 : is_float<A5>::v == is_float<A3>::v ? a3_n+1 : is_float<A5>::v == is_float<A2>::v ? a2_n+1 : is_float<A5>::v == is_float<A1>::v ? a1_n+1 : is_float<A5>::v ? 101 : 1;
	static const int a6_n = is_float<A6>::v == is_float<A5>::v ? a5_n+1 : is_float<A6>::v == is_float<A4>::v ? a4_n+1 : is_float<A6>::v == is_float<A3>::v ? a3_n+1 : is_float<A6>::v == is_float<A2>::v ? a2_n+1 : is_float<A6>::v == is_float<A1>::v ? a1_n+1 : is_float<A6>::v ? 101 : 1;
	static R f(args) {
		return ptr(select(A1,a1_n,passargs),select(A2,a2_n,passargs),select(A3,a3_n,passargs),select(A4,a4_n,passargs),select(A5,a5_n,passargs),select(A6,a6_n,passargs));
	}
};
template<typename R,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,R(*ptr)(A1,A2,A3,A4,A5,A6,A7)> struct func<R(A1,A2,A3,A4,A5,A6,A7),ptr> {
	typedef typename R R;
	static const int a1_n = is_float<A1>::v ? 101 : 1;
	static const int a2_n = is_float<A2>::v == is_float<A1>::v ? a1_n+1 : is_float<A2>::v ? 101 : 1;
	static const int a3_n = is_float<A3>::v == is_float<A2>::v ? a2_n+1 : is_float<A3>::v == is_float<A1>::v ? a1_n+1 : is_float<A3>::v ? 101 : 1;
	static const int a4_n = is_float<A4>::v == is_float<A3>::v ? a3_n+1 : is_float<A4>::v == is_float<A2>::v ? a2_n+1 : is_float<A4>::v == is_float<A1>::v ? a1_n+1 : is_float<A4>::v ? 101 : 1;
	static const int a5_n = is_float<A5>::v == is_float<A4>::v ? a4_n+1 : is_float<A5>::v == is_float<A3>::v ? a3_n+1 : is_float<A5>::v == is_float<A2>::v ? a2_n+1 : is_float<A5>::v == is_float<A1>::v ? a1_n+1 : is_float<A5>::v ? 101 : 1;
	static const int a6_n = is_float<A6>::v == is_float<A5>::v ? a5_n+1 : is_float<A6>::v == is_float<A4>::v ? a4_n+1 : is_float<A6>::v == is_float<A3>::v ? a3_n+1 : is_float<A6>::v == is_float<A2>::v ? a2_n+1 : is_float<A6>::v == is_float<A1>::v ? a1_n+1 : is_float<A6>::v ? 101 : 1;
	static const int a7_n = is_float<A7>::v == is_float<A6>::v ? a6_n+1 : is_float<A7>::v == is_float<A5>::v ? a5_n+1 : is_float<A7>::v == is_float<A4>::v ? a4_n+1 : is_float<A7>::v == is_float<A3>::v ? a3_n+1 : is_float<A7>::v == is_float<A2>::v ? a2_n+1 : is_float<A7>::v == is_float<A1>::v ? a1_n+1 : is_float<A7>::v ? 101 : 1;
	static R f(args) {
		return ptr(select(A1,a1_n,passargs),select(A2,a2_n,passargs),select(A3,a3_n,passargs),select(A4,a4_n,passargs),select(A5,a5_n,passargs),select(A6,a6_n,passargs),select(A7,a7_n,passargs));
	}
};
template<typename R,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,R(*ptr)(A1,A2,A3,A4,A5,A6,A7,A8)> struct func<R(A1,A2,A3,A4,A5,A6,A7,A8),ptr> {
	typedef typename R R;
	static const int a1_n = is_float<A1>::v ? 101 : 1;
	static const int a2_n = is_float<A2>::v == is_float<A1>::v ? a1_n+1 : is_float<A2>::v ? 101 : 1;
	static const int a3_n = is_float<A3>::v == is_float<A2>::v ? a2_n+1 : is_float<A3>::v == is_float<A1>::v ? a1_n+1 : is_float<A3>::v ? 101 : 1;
	static const int a4_n = is_float<A4>::v == is_float<A3>::v ? a3_n+1 : is_float<A4>::v == is_float<A2>::v ? a2_n+1 : is_float<A4>::v == is_float<A1>::v ? a1_n+1 : is_float<A4>::v ? 101 : 1;
	static const int a5_n = is_float<A5>::v == is_float<A4>::v ? a4_n+1 : is_float<A5>::v == is_float<A3>::v ? a3_n+1 : is_float<A5>::v == is_float<A2>::v ? a2_n+1 : is_float<A5>::v == is_float<A1>::v ? a1_n+1 : is_float<A5>::v ? 101 : 1;
	static const int a6_n = is_float<A6>::v == is_float<A5>::v ? a5_n+1 : is_float<A6>::v == is_float<A4>::v ? a4_n+1 : is_float<A6>::v == is_float<A3>::v ? a3_n+1 : is_float<A6>::v == is_float<A2>::v ? a2_n+1 : is_float<A6>::v == is_float<A1>::v ? a1_n+1 : is_float<A6>::v ? 101 : 1;
	static const int a7_n = is_float<A7>::v == is_float<A6>::v ? a6_n+1 : is_float<A7>::v == is_float<A5>::v ? a5_n+1 : is_float<A7>::v == is_float<A4>::v ? a4_n+1 : is_float<A7>::v == is_float<A3>::v ? a3_n+1 : is_float<A7>::v == is_float<A2>::v ? a2_n+1 : is_float<A7>::v == is_float<A1>::v ? a1_n+1 : is_float<A7>::v ? 101 : 1;
	static const int a8_n = is_float<A8>::v == is_float<A7>::v ? a7_n+1 : is_float<A8>::v == is_float<A6>::v ? a6_n+1 : is_float<A8>::v == is_float<A5>::v ? a5_n+1 : is_float<A8>::v == is_float<A4>::v ? a4_n+1 : is_float<A8>::v == is_float<A3>::v ? a3_n+1 : is_float<A8>::v == is_float<A2>::v ? a2_n+1 : is_float<A8>::v == is_float<A1>::v ? a1_n+1 : is_float<A8>::v ? 101 : 1;
	static R f(args) {
		return ptr(select(A1,a1_n,passargs),select(A2,a2_n,passargs),select(A3,a3_n,passargs),select(A4,a4_n,passargs),select(A5,a5_n,passargs),select(A6,a6_n,passargs),select(A7,a7_n,passargs),select(A8,a8_n,passargs));
	}
};
template<typename R,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,R(*ptr)(A1,A2,A3,A4,A5,A6,A7,A8,A9)> struct func<R(A1,A2,A3,A4,A5,A6,A7,A8,A9),ptr> {
	typedef typename R R;
	static const int a1_n = is_float<A1>::v ? 101 : 1;
	static const int a2_n = is_float<A2>::v == is_float<A1>::v ? a1_n+1 : is_float<A2>::v ? 101 : 1;
	static const int a3_n = is_float<A3>::v == is_float<A2>::v ? a2_n+1 : is_float<A3>::v == is_float<A1>::v ? a1_n+1 : is_float<A3>::v ? 101 : 1;
	static const int a4_n = is_float<A4>::v == is_float<A3>::v ? a3_n+1 : is_float<A4>::v == is_float<A2>::v ? a2_n+1 : is_float<A4>::v == is_float<A1>::v ? a1_n+1 : is_float<A4>::v ? 101 : 1;
	static const int a5_n = is_float<A5>::v == is_float<A4>::v ? a4_n+1 : is_float<A5>::v == is_float<A3>::v ? a3_n+1 : is_float<A5>::v == is_float<A2>::v ? a2_n+1 : is_float<A5>::v == is_float<A1>::v ? a1_n+1 : is_float<A5>::v ? 101 : 1;
	static const int a6_n = is_float<A6>::v == is_float<A5>::v ? a5_n+1 : is_float<A6>::v == is_float<A4>::v ? a4_n+1 : is_float<A6>::v == is_float<A3>::v ? a3_n+1 : is_float<A6>::v == is_float<A2>::v ? a2_n+1 : is_float<A6>::v == is_float<A1>::v ? a1_n+1 : is_float<A6>::v ? 101 : 1;
	static const int a7_n = is_float<A7>::v == is_float<A6>::v ? a6_n+1 : is_float<A7>::v == is_float<A5>::v ? a5_n+1 : is_float<A7>::v == is_float<A4>::v ? a4_n+1 : is_float<A7>::v == is_float<A3>::v ? a3_n+1 : is_float<A7>::v == is_float<A2>::v ? a2_n+1 : is_float<A7>::v == is_float<A1>::v ? a1_n+1 : is_float<A7>::v ? 101 : 1;
	static const int a8_n = is_float<A8>::v == is_float<A7>::v ? a7_n+1 : is_float<A8>::v == is_float<A6>::v ? a6_n+1 : is_float<A8>::v == is_float<A5>::v ? a5_n+1 : is_float<A8>::v == is_float<A4>::v ? a4_n+1 : is_float<A8>::v == is_float<A3>::v ? a3_n+1 : is_float<A8>::v == is_float<A2>::v ? a2_n+1 : is_float<A8>::v == is_float<A1>::v ? a1_n+1 : is_float<A8>::v ? 101 : 1;
	static const int a9_n = is_float<A9>::v == is_float<A8>::v ? a8_n+1 : is_float<A9>::v == is_float<A7>::v ? a7_n+1 : is_float<A9>::v == is_float<A6>::v ? a6_n+1 : is_float<A9>::v == is_float<A5>::v ? a5_n+1 : is_float<A9>::v == is_float<A4>::v ? a4_n+1 : is_float<A9>::v == is_float<A3>::v ? a3_n+1 : is_float<A9>::v == is_float<A2>::v ? a2_n+1 : is_float<A9>::v == is_float<A1>::v ? a1_n+1 : is_float<A9>::v ? 101 : 1;
	static R f(args) {
		return ptr(select(A1,a1_n,passargs),select(A2,a2_n,passargs),select(A3,a3_n,passargs),select(A4,a4_n,passargs),select(A5,a5_n,passargs),select(A6,a6_n,passargs),select(A7,a7_n,passargs),select(A8,a8_n,passargs),select(A9,a9_n,passargs));
	}
};
template<typename R,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,R(*ptr)(A1,A2,A3,A4,A5,A6,A7,A8,A9,A10)> struct func<R(A1,A2,A3,A4,A5,A6,A7,A8,A9,A10),ptr> {
	typedef typename R R;
	static const int a1_n = is_float<A1>::v ? 101 : 1;
	static const int a2_n = is_float<A2>::v == is_float<A1>::v ? a1_n+1 : is_float<A2>::v ? 101 : 1;
	static const int a3_n = is_float<A3>::v == is_float<A2>::v ? a2_n+1 : is_float<A3>::v == is_float<A1>::v ? a1_n+1 : is_float<A3>::v ? 101 : 1;
	static const int a4_n = is_float<A4>::v == is_float<A3>::v ? a3_n+1 : is_float<A4>::v == is_float<A2>::v ? a2_n+1 : is_float<A4>::v == is_float<A1>::v ? a1_n+1 : is_float<A4>::v ? 101 : 1;
	static const int a5_n = is_float<A5>::v == is_float<A4>::v ? a4_n+1 : is_float<A5>::v == is_float<A3>::v ? a3_n+1 : is_float<A5>::v == is_float<A2>::v ? a2_n+1 : is_float<A5>::v == is_float<A1>::v ? a1_n+1 : is_float<A5>::v ? 101 : 1;
	static const int a6_n = is_float<A6>::v == is_float<A5>::v ? a5_n+1 : is_float<A6>::v == is_float<A4>::v ? a4_n+1 : is_float<A6>::v == is_float<A3>::v ? a3_n+1 : is_float<A6>::v == is_float<A2>::v ? a2_n+1 : is_float<A6>::v == is_float<A1>::v ? a1_n+1 : is_float<A6>::v ? 101 : 1;
	static const int a7_n = is_float<A7>::v == is_float<A6>::v ? a6_n+1 : is_float<A7>::v == is_float<A5>::v ? a5_n+1 : is_float<A7>::v == is_float<A4>::v ? a4_n+1 : is_float<A7>::v == is_float<A3>::v ? a3_n+1 : is_float<A7>::v == is_float<A2>::v ? a2_n+1 : is_float<A7>::v == is_float<A1>::v ? a1_n+1 : is_float<A7>::v ? 101 : 1;
	static const int a8_n = is_float<A8>::v == is_float<A7>::v ? a7_n+1 : is_float<A8>::v == is_float<A6>::v ? a6_n+1 : is_float<A8>::v == is_float<A5>::v ? a5_n+1 : is_float<A8>::v == is_float<A4>::v ? a4_n+1 : is_float<A8>::v == is_float<A3>::v ? a3_n+1 : is_float<A8>::v == is_float<A2>::v ? a2_n+1 : is_float<A8>::v == is_float<A1>::v ? a1_n+1 : is_float<A8>::v ? 101 : 1;
	static const int a9_n = is_float<A9>::v == is_float<A8>::v ? a8_n+1 : is_float<A9>::v == is_float<A7>::v ? a7_n+1 : is_float<A9>::v == is_float<A6>::v ? a6_n+1 : is_float<A9>::v == is_float<A5>::v ? a5_n+1 : is_float<A9>::v == is_float<A4>::v ? a4_n+1 : is_float<A9>::v == is_float<A3>::v ? a3_n+1 : is_float<A9>::v == is_float<A2>::v ? a2_n+1 : is_float<A9>::v == is_float<A1>::v ? a1_n+1 : is_float<A9>::v ? 101 : 1;
	static const int a10_n = is_float<A10>::v == is_float<A9>::v ? a9_n+1 : is_float<A10>::v == is_float<A8>::v ? a8_n+1 : is_float<A10>::v == is_float<A7>::v ? a7_n+1 : is_float<A10>::v == is_float<A6>::v ? a6_n+1 : is_float<A10>::v == is_float<A5>::v ? a5_n+1 : is_float<A10>::v == is_float<A4>::v ? a4_n+1 : is_float<A10>::v == is_float<A3>::v ? a3_n+1 : is_float<A10>::v == is_float<A2>::v ? a2_n+1 : is_float<A10>::v == is_float<A1>::v ? a1_n+1 : is_float<A10>::v ? 101 : 1;
	static R f(args) {
		return ptr(select(A1,a1_n,passargs),select(A2,a2_n,passargs),select(A3,a3_n,passargs),select(A4,a4_n,passargs),select(A5,a5_n,passargs),select(A6,a6_n,passargs),select(A7,a7_n,passargs),select(A8,a8_n,passargs),select(A9,a9_n,passargs),select(A10,a10_n,passargs));
	}
};
template<typename R,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,R(*ptr)(A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11)> struct func<R(A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11),ptr> {
	typedef typename R R;
	static const int a1_n = is_float<A1>::v ? 101 : 1;
	static const int a2_n = is_float<A2>::v == is_float<A1>::v ? a1_n+1 : is_float<A2>::v ? 101 : 1;
	static const int a3_n = is_float<A3>::v == is_float<A2>::v ? a2_n+1 : is_float<A3>::v == is_float<A1>::v ? a1_n+1 : is_float<A3>::v ? 101 : 1;
	static const int a4_n = is_float<A4>::v == is_float<A3>::v ? a3_n+1 : is_float<A4>::v == is_float<A2>::v ? a2_n+1 : is_float<A4>::v == is_float<A1>::v ? a1_n+1 : is_float<A4>::v ? 101 : 1;
	static const int a5_n = is_float<A5>::v == is_float<A4>::v ? a4_n+1 : is_float<A5>::v == is_float<A3>::v ? a3_n+1 : is_float<A5>::v == is_float<A2>::v ? a2_n+1 : is_float<A5>::v == is_float<A1>::v ? a1_n+1 : is_float<A5>::v ? 101 : 1;
	static const int a6_n = is_float<A6>::v == is_float<A5>::v ? a5_n+1 : is_float<A6>::v == is_float<A4>::v ? a4_n+1 : is_float<A6>::v == is_float<A3>::v ? a3_n+1 : is_float<A6>::v == is_float<A2>::v ? a2_n+1 : is_float<A6>::v == is_float<A1>::v ? a1_n+1 : is_float<A6>::v ? 101 : 1;
	static const int a7_n = is_float<A7>::v == is_float<A6>::v ? a6_n+1 : is_float<A7>::v == is_float<A5>::v ? a5_n+1 : is_float<A7>::v == is_float<A4>::v ? a4_n+1 : is_float<A7>::v == is_float<A3>::v ? a3_n+1 : is_float<A7>::v == is_float<A2>::v ? a2_n+1 : is_float<A7>::v == is_float<A1>::v ? a1_n+1 : is_float<A7>::v ? 101 : 1;
	static const int a8_n = is_float<A8>::v == is_float<A7>::v ? a7_n+1 : is_float<A8>::v == is_float<A6>::v ? a6_n+1 : is_float<A8>::v == is_float<A5>::v ? a5_n+1 : is_float<A8>::v == is_float<A4>::v ? a4_n+1 : is_float<A8>::v == is_float<A3>::v ? a3_n+1 : is_float<A8>::v == is_float<A2>::v ? a2_n+1 : is_float<A8>::v == is_float<A1>::v ? a1_n+1 : is_float<A8>::v ? 101 : 1;
	static const int a9_n = is_float<A9>::v == is_float<A8>::v ? a8_n+1 : is_float<A9>::v == is_float<A7>::v ? a7_n+1 : is_float<A9>::v == is_float<A6>::v ? a6_n+1 : is_float<A9>::v == is_float<A5>::v ? a5_n+1 : is_float<A9>::v == is_float<A4>::v ? a4_n+1 : is_float<A9>::v == is_float<A3>::v ? a3_n+1 : is_float<A9>::v == is_float<A2>::v ? a2_n+1 : is_float<A9>::v == is_float<A1>::v ? a1_n+1 : is_float<A9>::v ? 101 : 1;
	static const int a10_n = is_float<A10>::v == is_float<A9>::v ? a9_n+1 : is_float<A10>::v == is_float<A8>::v ? a8_n+1 : is_float<A10>::v == is_float<A7>::v ? a7_n+1 : is_float<A10>::v == is_float<A6>::v ? a6_n+1 : is_float<A10>::v == is_float<A5>::v ? a5_n+1 : is_float<A10>::v == is_float<A4>::v ? a4_n+1 : is_float<A10>::v == is_float<A3>::v ? a3_n+1 : is_float<A10>::v == is_float<A2>::v ? a2_n+1 : is_float<A10>::v == is_float<A1>::v ? a1_n+1 : is_float<A10>::v ? 101 : 1;
	static const int a11_n = is_float<A11>::v == is_float<A10>::v ? a10_n+1 : is_float<A11>::v == is_float<A9>::v ? a9_n+1 : is_float<A11>::v == is_float<A8>::v ? a8_n+1 : is_float<A11>::v == is_float<A7>::v ? a7_n+1 : is_float<A11>::v == is_float<A6>::v ? a6_n+1 : is_float<A11>::v == is_float<A5>::v ? a5_n+1 : is_float<A11>::v == is_float<A4>::v ? a4_n+1 : is_float<A11>::v == is_float<A3>::v ? a3_n+1 : is_float<A11>::v == is_float<A2>::v ? a2_n+1 : is_float<A11>::v == is_float<A1>::v ? a1_n+1 : is_float<A11>::v ? 101 : 1;
	static R f(args) {
		return ptr(select(A1,a1_n,passargs),select(A2,a2_n,passargs),select(A3,a3_n,passargs),select(A4,a4_n,passargs),select(A5,a5_n,passargs),select(A6,a6_n,passargs),select(A7,a7_n,passargs),select(A8,a8_n,passargs),select(A9,a9_n,passargs),select(A10,a10_n,passargs),select(A11,a11_n,passargs));
	}
};
template<typename R,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,R(*ptr)(A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12)> struct func<R(A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12),ptr> {
	typedef typename R R;
	static const int a1_n = is_float<A1>::v ? 101 : 1;
	static const int a2_n = is_float<A2>::v == is_float<A1>::v ? a1_n+1 : is_float<A2>::v ? 101 : 1;
	static const int a3_n = is_float<A3>::v == is_float<A2>::v ? a2_n+1 : is_float<A3>::v == is_float<A1>::v ? a1_n+1 : is_float<A3>::v ? 101 : 1;
	static const int a4_n = is_float<A4>::v == is_float<A3>::v ? a3_n+1 : is_float<A4>::v == is_float<A2>::v ? a2_n+1 : is_float<A4>::v == is_float<A1>::v ? a1_n+1 : is_float<A4>::v ? 101 : 1;
	static const int a5_n = is_float<A5>::v == is_float<A4>::v ? a4_n+1 : is_float<A5>::v == is_float<A3>::v ? a3_n+1 : is_float<A5>::v == is_float<A2>::v ? a2_n+1 : is_float<A5>::v == is_float<A1>::v ? a1_n+1 : is_float<A5>::v ? 101 : 1;
	static const int a6_n = is_float<A6>::v == is_float<A5>::v ? a5_n+1 : is_float<A6>::v == is_float<A4>::v ? a4_n+1 : is_float<A6>::v == is_float<A3>::v ? a3_n+1 : is_float<A6>::v == is_float<A2>::v ? a2_n+1 : is_float<A6>::v == is_float<A1>::v ? a1_n+1 : is_float<A6>::v ? 101 : 1;
	static const int a7_n = is_float<A7>::v == is_float<A6>::v ? a6_n+1 : is_float<A7>::v == is_float<A5>::v ? a5_n+1 : is_float<A7>::v == is_float<A4>::v ? a4_n+1 : is_float<A7>::v == is_float<A3>::v ? a3_n+1 : is_float<A7>::v == is_float<A2>::v ? a2_n+1 : is_float<A7>::v == is_float<A1>::v ? a1_n+1 : is_float<A7>::v ? 101 : 1;
	static const int a8_n = is_float<A8>::v == is_float<A7>::v ? a7_n+1 : is_float<A8>::v == is_float<A6>::v ? a6_n+1 : is_float<A8>::v == is_float<A5>::v ? a5_n+1 : is_float<A8>::v == is_float<A4>::v ? a4_n+1 : is_float<A8>::v == is_float<A3>::v ? a3_n+1 : is_float<A8>::v == is_float<A2>::v ? a2_n+1 : is_float<A8>::v == is_float<A1>::v ? a1_n+1 : is_float<A8>::v ? 101 : 1;
	static const int a9_n = is_float<A9>::v == is_float<A8>::v ? a8_n+1 : is_float<A9>::v == is_float<A7>::v ? a7_n+1 : is_float<A9>::v == is_float<A6>::v ? a6_n+1 : is_float<A9>::v == is_float<A5>::v ? a5_n+1 : is_float<A9>::v == is_float<A4>::v ? a4_n+1 : is_float<A9>::v == is_float<A3>::v ? a3_n+1 : is_float<A9>::v == is_float<A2>::v ? a2_n+1 : is_float<A9>::v == is_float<A1>::v ? a1_n+1 : is_float<A9>::v ? 101 : 1;
	static const int a10_n = is_float<A10>::v == is_float<A9>::v ? a9_n+1 : is_float<A10>::v == is_float<A8>::v ? a8_n+1 : is_float<A10>::v == is_float<A7>::v ? a7_n+1 : is_float<A10>::v == is_float<A6>::v ? a6_n+1 : is_float<A10>::v == is_float<A5>::v ? a5_n+1 : is_float<A10>::v == is_float<A4>::v ? a4_n+1 : is_float<A10>::v == is_float<A3>::v ? a3_n+1 : is_float<A10>::v == is_float<A2>::v ? a2_n+1 : is_float<A10>::v == is_float<A1>::v ? a1_n+1 : is_float<A10>::v ? 101 : 1;
	static const int a11_n = is_float<A11>::v == is_float<A10>::v ? a10_n+1 : is_float<A11>::v == is_float<A9>::v ? a9_n+1 : is_float<A11>::v == is_float<A8>::v ? a8_n+1 : is_float<A11>::v == is_float<A7>::v ? a7_n+1 : is_float<A11>::v == is_float<A6>::v ? a6_n+1 : is_float<A11>::v == is_float<A5>::v ? a5_n+1 : is_float<A11>::v == is_float<A4>::v ? a4_n+1 : is_float<A11>::v == is_float<A3>::v ? a3_n+1 : is_float<A11>::v == is_float<A2>::v ? a2_n+1 : is_float<A11>::v == is_float<A1>::v ? a1_n+1 : is_float<A11>::v ? 101 : 1;
	static const int a12_n = is_float<A12>::v == is_float<A11>::v ? a11_n+1 : is_float<A12>::v == is_float<A10>::v ? a10_n+1 : is_float<A12>::v == is_float<A9>::v ? a9_n+1 : is_float<A12>::v == is_float<A8>::v ? a8_n+1 : is_float<A12>::v == is_float<A7>::v ? a7_n+1 : is_float<A12>::v == is_float<A6>::v ? a6_n+1 : is_float<A12>::v == is_float<A5>::v ? a5_n+1 : is_float<A12>::v == is_float<A4>::v ? a4_n+1 : is_float<A12>::v == is_float<A3>::v ? a3_n+1 : is_float<A12>::v == is_float<A2>::v ? a2_n+1 : is_float<A12>::v == is_float<A1>::v ? a1_n+1 : is_float<A12>::v ? 101 : 1;
	static R f(args) {
		return ptr(select(A1,a1_n,passargs),select(A2,a2_n,passargs),select(A3,a3_n,passargs),select(A4,a4_n,passargs),select(A5,a5_n,passargs),select(A6,a6_n,passargs),select(A7,a7_n,passargs),select(A8,a8_n,passargs),select(A9,a9_n,passargs),select(A10,a10_n,passargs),select(A11,a11_n,passargs),select(A12,a12_n,passargs));
	}
};

