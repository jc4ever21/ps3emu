
namespace sync {

	struct recursive {};
	struct not_recursive {};;
	struct fifo {};
	struct priority {};
	struct not_ipc {};
	struct ipc {};

	typedef int thread_id;

	namespace detail {

		template<typename base>
		struct node_funcs: base {
			template<typename protocol> struct xinfo;
			template<> struct xinfo<fifo> {
				typedef typename decltype(((base*)0)->a) container_type;
				typedef typename decltype(((base*)0)->a.parent) parent_type;
				typedef typename decltype(((base*)0)->a.left) left_type;
				typedef typename decltype(((base*)0)->a.right) right_type;
				typedef typename decltype(((base*)0)->a.color) color_type;
				static container_type&get_container(base*v) {
					return v->a;
				}
			};
			template<> struct xinfo<priority> {
				typedef typename decltype(((base*)0)->p) container_type;
				typedef typename decltype(((base*)0)->p.parent) parent_type;
				typedef typename decltype(((base*)0)->p.left) left_type;
				typedef typename decltype(((base*)0)->p.right) right_type;
				typedef typename decltype(((base*)0)->p.color) color_type;
				static container_type&get_container(base*v) {
					return v->p;
				}
			};
			template<typename protocol> typename xinfo<protocol>::parent_type&get_parent() {
				return xinfo<protocol>::get_container(this).parent;
			}
			template<typename protocol> typename xinfo<protocol>::left_type&get_left() {
				return xinfo<protocol>::get_container(this).left;
			}
			template<typename protocol> typename xinfo<protocol>::right_type&get_right() {
				return xinfo<protocol>::get_container(this).right;
			}
			template<typename protocol> typename xinfo<protocol>::color_type&get_color() {
				return xinfo<protocol>::get_container(this).color;
			}
			template<typename protocol>
			static typename xinfo<protocol>::color_type red() {
				return 1;
			}
			template<typename protocol>
			static typename xinfo<protocol>::color_type black() {
				return 0;
			}
		};

		struct thread_event;
		template<typename ipc> struct ti;
		struct ti_not_ipc_base {
			struct tid {
				ti<not_ipc>*ptr;
				tid&operator=(ti<not_ipc>*v) {
					ptr = v;
					return *this;
				}
				operator ti<not_ipc>*() {
					return ptr;
				}
			};
			typedef tid tid_prio;
			union {
				struct {
					tid parent,left,right;
					int color;
				} a;
				struct {
					tid_prio parent,left,right;
					int color;
					int prio;
				} p;
			};
			int id;
			int&get_prio() {
				return p.prio;
			}
			int get_id() {
				return id;
			}
			ipc_event&get_event();
		};
		template<> struct ti<not_ipc>: node_funcs<ti_not_ipc_base> {};

		struct thread_event: ti<not_ipc> {
			ipc_event e;
			char ipc_name[0x100];
			thread_event() {}
			ipc_event&get_event() {
				if (!e) e = ipc_event(ipc_name);
				return e;
			}
		};

		ipc_event&ti_not_ipc_base::get_event() {
			return ((thread_event*)this)->get_event();
		}

		thread_event thread_event_list[max_thread_id+1];
		thread_event&get_thread_event(thread_id n) {
			return thread_event_list[(int)n];
		}

		struct ti_ipc_base {
			struct tid {
				size_t shm_offset;
				tid&operator=(ti<ipc>*v) {
					shm_offset = shm_pointer_to_offset(v);
					return *this;
				}
				operator ti<ipc>*() {
					return (ti<ipc>*)shm_offset_to_pointer(shm_offset);
				}
			};
			typedef tid tid_prio;
			union {
				struct {
					tid parent,left,right;
					int color;
				} a;
				struct {
					tid_prio parent,left,right;
					int color;
					int prio;
				} p;
			};
			int id;
			int&get_prio() {
				return p.prio;
			}
			int get_id() {
				return id;
			}
			ipc_event&get_event() {
				return get_thread_event(id).get_event();
			}
		};
		template<> struct ti<ipc>: node_funcs<ti_ipc_base> {};
		struct ipc_ti_list_t {
			ti<ipc> list[max_thread_id+1];
			ipc_ti_list_t(){}
		};
		shm_object<ipc_ti_list_t> ipc_ti_list;
		ti<ipc>&get_ipc_ti(thread_id n) {
			return ipc_ti_list->list[(int)n];
		}
		void ti_initializer_func() {
			for (int i=0;i<=max_thread_id;i++) {
				auto&a = get_thread_event(i);
				auto&b = get_ipc_ti(i);
				a.id = b.id = i;
				static int n=0;
				strcpy(a.ipc_name,generate_ipc_name("thread-event",n++));
			}
		}
		shm_cb ti_initializer(&ti_initializer_func);

		template<typename t,typename ipc> struct thread_buffer_t;
		template<typename t> struct thread_buffer_t<t,not_ipc> {
			static t buf[max_thread_id+1];
			static t&get(thread_id n) {
				return buf[n];
			}
		};
		template<typename t> struct thread_buffer_t<t,ipc> {
			static shm_object<t[max_thread_id+1]> buf;
			static t&get(thread_id n) {
				return (*buf)[n];
			}
		};
		template<typename t> t thread_buffer_t<t,not_ipc>::buf[max_thread_id+1];
		template<typename t> shm_object<t[max_thread_id+1]> thread_buffer_t<t,ipc>::buf;
		
		template<typename protocol,typename ipc>
		struct ti_handle;
		template<> struct ti_handle<fifo,not_ipc> {
			typedef ti<not_ipc> ti;
			ti_handle(ti&ti,thread_id id) : ptr(&ti), id_(id) {}
			ti&get_ti(){return *ptr;}
			ipc_event&get_event() {
				return ((thread_event&)get_ti()).get_event();
			}
			ti*get_ptr() {return ptr;}
			thread_id get_id() {return id_;}
		private:
			ti*ptr;
			thread_id id_;
		};
		template<> struct ti_handle<priority,not_ipc> {
			typedef ti<not_ipc> ti;
			ti_handle(ti&ti,thread_id id,int prio) : ptr(&ti), id_(id) {
				get_ti().get_prio() = prio;
			}
			ti&get_ti() {return *ptr;}
			//int get_prio();
			ipc_event&get_event() {
				return ((thread_event&)get_ti()).get_event();
			}
			ti*get_ptr() {return ptr;}
			thread_id get_id() {return id_;}
		private:
			ti*ptr;
			thread_id id_;
		};
		template<> struct ti_handle<fifo,ipc> {
			typedef ti<ipc> ti;
			ti_handle(ti&ti,thread_id id) : ptr(&ti), id_(id) {}
			thread_id get_id() {return id_;}
			ti&get_ti(){return*ptr;}
			ti*get_ptr() {return ptr;}
			thread_event&get_te(){return get_thread_event(get_id());}
			ipc_event&get_event() {
				return get_te().get_event();
			}
		private:
			ti*ptr;
			thread_id id_;
		};
		template<> struct ti_handle<priority,ipc> {
			typedef ti<ipc> ti;
			ti_handle(ti&ti,thread_id id,int prio) : ptr(&ti), id_(id) {
				get_ti().get_prio() = prio;
			}
			ti&get_ti() {return *ptr;}
			//int get_prio();
			ti*get_ptr() {return ptr;}
			thread_id get_id() {return id_;}
			thread_event&get_te(){return get_thread_event(get_id());}
			ipc_event&get_event() {
				return get_te().get_event();
			}
		private:
			ti*ptr;
			thread_id id_;
		};

		template<typename protocol,typename ipc>
		ti_handle<protocol,ipc> get_this_ti();

		template<>ti_handle<fifo,not_ipc> get_this_ti<fifo,not_ipc>() {
			auto i = get_thread_id();
			return ti_handle<fifo,not_ipc>(get_thread_event(thread_id(i)),thread_id(i));
		}
		template<>ti_handle<priority,not_ipc> get_this_ti<priority,not_ipc>() {
			auto i = get_thread_id_and_priority();
			return ti_handle<priority,not_ipc>(get_thread_event(thread_id(i.first)),thread_id(i.first),i.second);
		}
		template<>ti_handle<fifo,ipc> get_this_ti<fifo,ipc>() {
			auto i = get_thread_id();
			return ti_handle<fifo,ipc>(get_ipc_ti(thread_id(i)),thread_id(i));
		}
		template<>ti_handle<priority,ipc> get_this_ti<priority,ipc>() {
			auto i = get_thread_id_and_priority();
			return ti_handle<priority,ipc>(get_ipc_ti(thread_id(i.first)),thread_id(i.first),i.second);
		}

		template<typename ti,typename ti_ptr,typename protocol>
		struct node_traits
		{
			typedef ti node;
			typedef ti_ptr node_ptr;
			typedef const ti_ptr const_node_ptr;
			typedef int color;

			static node_ptr get_parent(const_node_ptr n) {return n->get_parent<protocol>();}
			static void set_parent(node_ptr n,node_ptr next) {n->get_parent<protocol>()=next;}
			static node_ptr get_left(const_node_ptr n) {return n->get_left<protocol>();}
			static void set_left(node_ptr n,node_ptr next) {n->get_left<protocol>()=next;}
			static node_ptr get_right(const_node_ptr n) {return n->get_right<protocol>();}
			static void set_right(node_ptr n,node_ptr prev) {n->get_right<protocol>()=prev;}
			static color get_color(const_node_ptr n) {return n->get_color<protocol>();}
			static void set_color(node_ptr n,color c) {n->get_color<protocol>()=c;}
			static color black() {return node::black<protocol>();}
			static color red() {return node::red<protocol>();}

			static node_ptr get_next(const_node_ptr n) {return get_left(n);}
			static void set_next(node_ptr n,node_ptr next) {set_left(n,next);}
			static node_ptr get_previous(const_node_ptr n) {return get_right(n);}
			static void set_previous(node_ptr n,node_ptr prev) {set_right(n,prev);}
		};

		template<typename protocol,typename ipc>
		struct ti_traits {
			typedef ti<ipc> ti;
			typedef ti_handle<protocol,ipc> handle;
			typedef ti*ptr;
			//typedef ti_ptr<protocol,ipc> ptr;
			typedef node_traits<ti,ptr,protocol> node_traits;
		};






		template<typename t>
		struct opt_enum;

		struct opt_recursive {
			typedef opt_enum<recursive> e;
		};
		struct opt_protocol {
			typedef opt_enum<fifo> e;
		};
		struct opt_ipc {
			typedef opt_enum<not_ipc> e;
		};
		struct opt_none {
			typedef opt_none e;
			typedef void type;
			typedef void next;
			typedef opt_none opt;
		};
		struct opt_end {
			typedef opt_end type;
		};

		template<> struct opt_enum<recursive> {
			typedef recursive type;
			typedef opt_enum<not_recursive> next;
			typedef opt_recursive opt;
		};
		template<> struct opt_enum<not_recursive> {
			typedef not_recursive type;
			typedef void next;
			typedef opt_recursive opt;
		};
		template<> struct opt_enum<fifo> {
			typedef fifo type;
			typedef opt_enum<priority> next;
			typedef opt_protocol opt;
		};
		template<> struct opt_enum<priority> {
			typedef priority type;
			typedef void next;
			typedef opt_protocol opt;
		};
		template<> struct opt_enum<not_ipc> {
			typedef not_ipc type;
			typedef opt_enum<ipc> next;
			typedef opt_ipc opt;
		};
		template<> struct opt_enum<ipc> {
			typedef ipc type;
			typedef void next;
			typedef opt_ipc opt;
		};

		template<int n,typename prev,typename T,typename t1,typename t2,typename t3>
		struct test2;

		template<int n,typename prev,typename T,typename t1,typename t2,typename t3>
		struct test2<n,prev,T,t1,t2,t3> {
			typedef T T;
			typedef t1 t1;
			typedef t2 t2;
			typedef t3 t3;
			typedef typename test2<n+1,test2,T,typename t1::next,t2,t3> next;
			static const int n = n;
		};
		template<int n,typename prev,typename T,typename t2,typename t3>
		struct test2<n,prev,T,void,t2,t3> {
			typedef T T;
			typedef opt_end t1;
			typedef t2 t2;
			typedef t3 t3;
			typedef typename test2<n,test2,T,typename prev::t1::opt::e,typename t2::next,t3> next;
			static const int n = -1;
		};
		template<int n,typename prev,typename T,typename t1,typename t3>
		struct test2<n,prev,T,t1,void,t3> {
			typedef T T;
			typedef t1 t1;
			typedef opt_end t2;
			typedef t3 t3;
			typedef typename test2<n,test2,T,t1,typename prev::t2::opt::e,typename t3::next> next;
			static const int n = -1;
		};
		template<int n,typename prev,typename T,typename t1,typename t2>
		struct test2<n,prev,T,t1,t2,void> {
			typedef T T;
			typedef t1 t1;
			typedef t2 t2;
			typedef opt_end t3;
			typedef void next;
			static const int n = -1;
		};
		template<typename T>
		struct xt_rebinder {
			template<typename t1,typename t2,typename t3> struct rebind {
				typedef typename T::rebind<t1,t2,t3>::other other;
			};
			template<typename t1,typename t2> struct rebind<t1,t2,void> {
				typedef typename T::rebind<t1,t2>::other other;
			};
			template<typename t1> struct rebind<t1,void,void> {
				typedef typename T::rebind<t1>::other other;
			};
			struct bad_rebind {
				typedef int other;
			};
			template<typename t2> struct rebind<opt_end,t2,void>:bad_rebind{};;
			template<typename t1> struct rebind<t1,opt_end,void>:bad_rebind{};;
			template<typename t2,typename t3> struct rebind<opt_end,t2,t3>:bad_rebind{};;
			template<typename t1,typename t3> struct rebind<t1,opt_end,t3>:bad_rebind{};;
			template<typename t1,typename t2> struct rebind<t1,t2,opt_end>:bad_rebind{};;
			template<> struct rebind<opt_end,opt_end,opt_end>:bad_rebind{};;
			template<> struct rebind<opt_end,opt_end,void>:bad_rebind{};;
			template<> struct rebind<opt_end,void,void>:bad_rebind{};;
		};
		template<typename xt,typename default_type>
		struct testx {
			typedef testx<typename xt::next,default_type> next;
			typedef typename xt_rebinder<typename xt::T>::rebind<typename xt::t1::type,typename xt::t2::type,typename xt::t3::type>::other t;
			typedef xt xt;
			static const int maxsize = sizeof(t)>next::maxsize?sizeof(t):next::maxsize;
			static const int n = xt::n;
			static const int maxn = n>next::maxn?n:next::maxn;
			template<typename opt1,typename opt2,typename opt3> struct getn {
				static const int n = next::getn<opt1,opt2,opt3>::n;
			};
			template<> struct getn<typename xt::t1::type,typename xt::t2::type,typename xt::t3::type> {
				static const int n = xt::n;
			};
			template<int n> struct get {
				typedef typename next::get<n>::type type;
			};
			template<> struct get<xt::n> {
				typedef typename t type;
			};
		};
		template<typename default_type>
		struct testx<void,default_type> {
			static const int maxsize = 0;
			static const int n = -1;
			static const int maxn = 0;
			typedef void xt;
			template<int n> struct get {
				typedef typename default_type type;
			};
		};
		template<typename xt>
		struct testy: testx<xt,typename xt_rebinder<typename xt::T>::rebind<typename xt::t1::type,typename xt::t2::type,typename xt::t3::type>::other> {
		};

		template<typename T, typename t1,typename t2,typename t3>
		struct testp {
			typedef testy<test2<0,void,T,typename t1::e,typename t2::e,typename t3::e>> x;
			uint64_t buf[(x::maxsize-1)/sizeof(uint64_t)+1];
			int mode;
			template<typename F>
			auto call(F&&f) -> decltype(f(*(x::get<n>::type*)0)) {
				static_assert(x::maxn<=7,"x::maxn>7");
#define c(n) case n: return f((x::get<n>::type&)buf)
				switch (mode) {c(0);c(1);c(2);c(3);c(4);c(5);c(6);c(7);
				default:NODEFAULT;}
#undef c
			}
			template<typename F,typename ta1>
			auto call(F&&f,ta1&&a1) -> decltype(f(*(x::get<n>::type*)0,std::forward<ta1>(a1))) {
				static_assert(x::maxn<=7,"x::maxn>7");
#define c(n) case n: return f((x::get<n>::type&)buf,std::forward<ta1>(a1))
				switch (mode) {c(0);c(1);c(2);c(3);c(4);c(5);c(6);c(7);
				default:NODEFAULT;}
#undef c
			}
			template<typename F,typename ta1,typename ta2>
			auto call(F&&f,ta1&&a1,ta2&&a2) -> decltype(f(*(x::get<n>::type*)0,std::forward<ta1>(a1),std::forward<ta2>(a2))) {
				static_assert(x::maxn<=7,"x::maxn>7");
#define c(n) case n: return f((x::get<n>::type&)buf,std::forward<ta1>(a1),std::forward<ta2>(a2))
				switch (mode) {c(0);c(1);c(2);c(3);c(4);c(5);c(6);c(7);
				default:NODEFAULT;}
#undef c
			}
			template<typename F,typename ta1,typename ta2,typename ta3>
			auto call(F&&f,ta1&&a1,ta2&&a2,ta3&&a3) -> decltype(f(*(x::get<n>::type*)0,std::forward<ta1>(a1),std::forward<ta2>(a2),std::forward<ta3>(a3))) {
				static_assert(x::maxn<=7,"x::maxn>7");
#define c(n) case n: return f((x::get<n>::type&)buf,std::forward<ta1>(a1),std::forward<ta2>(a2),std::forward<ta3>(a3))
				switch (mode) {c(0);c(1);c(2);c(3);c(4);c(5);c(6);c(7);
				default:NODEFAULT;}
#undef c
			}
		};
		template<typename T,typename t1,typename t2=opt_none,typename t3=opt_none>
		struct test: testp<T,t1,t2,t3> {
			template<typename opt1,typename opt2,typename opt3>
			typename T::rebind<opt1,opt2,opt3>::other&ref() {
				return (T::rebind<opt1,opt2,opt3>::other&)buf;
			}
			template<typename opt1,typename opt2,typename opt3>
			void init() {
				mode = x::getn<opt1,opt2,opt3>::n;
				new (&ref<opt1,opt2,opt3>()) T::rebind<opt1,opt2,opt3>::other();
			}
		};
		template<typename T,typename t1,typename t2>
		struct test<T,t1,t2,opt_none>: testp<T,t1,t2,opt_none> {
			template<typename opt1,typename opt2>
			typename T::rebind<opt1,opt2>::other&ref() {
				return (T::rebind<opt1,opt2>::other&)buf;
			}
			template<typename opt1,typename opt2>
			void init() {
				mode = x::getn<opt1,opt2,void>::n;
				new (&ref<opt1,opt2>()) T::rebind<opt1,opt2>::other();
			}
			template<typename opt1,typename opt2,typename ta1>
			void init(ta1&&a1) {
				mode = x::getn<opt1,opt2,void>::n;
				new (&ref<opt1,opt2>()) T::rebind<opt1,opt2>::other(std::forward<ta1>(a1));
			}
			template<typename opt1,typename opt2,typename ta1,typename ta2>
			void init(ta1&&a1,ta2&&a2) {
				mode = x::getn<opt1,opt2,void>::n;
				new (&ref<opt1,opt2>()) T::rebind<opt1,opt2>::other(std::forward<ta1>(a1),std::forward<ta2>(a2));
			}
		};
		template<typename T,typename t1>
		struct test<T,t1,opt_none,opt_none>: testp<T,t1,opt_none,opt_none> {
			template<typename opt1>
			typename T::rebind<opt1>::other&ref() {
				return (T::rebind<opt1>::other&)buf;
			}
			template<typename opt1>
			void init() {
				mode = x::getn<opt1,void,void>::n;
				new (&ref<opt1>()) T::rebind<opt1>::other();
			}
		};


		struct busy_lock {
			int n;
			busy_lock() : n(0) {}
			void lock() {
				while(atomic_xchg(&n,1))do busy_yield();while(atomic_read(&n));
			}
			void unlock() {
				mem_write_barrier();
				atomic_write(&n,0);
			}
		};

		template<typename recursive>
		struct recursive_mgr;
		template<> struct recursive_mgr<not_recursive> {
			int lock_rec() {
				return EDEADLK;
			}
			int on_lock() {
				return CELL_OK;
			}
			bool unlock() {
				return true;
			}
		};
		template<> struct recursive_mgr<recursive> {
			uint32_t r;
			int lock_rec() {
				if (r==0xffffffff) return EKRESOURCE;
				++r;
				return CELL_OK;
			}
			int on_lock() {
				r=1;
				return CELL_OK;
			}
			bool unlock() {
				return --r==0;
			}
		};
		template<typename ti_traits,typename protocol>
		struct protocol_mgr;
		template<typename ti_traits> struct protocol_mgr<ti_traits,fifo> {
			typedef typename ti_traits::ti ti;
			typedef typename ti_traits::ptr ti_ptr;
			typedef boost::intrusive::circular_list_algorithms<typename ti_traits::node_traits> algo;
			ti header;
			void insert(ti_ptr pn) {
				algo::link_before(&header,pn);
			}
			void erase(ti_ptr pn) {
				algo::unlink(pn);
			}
			ti_ptr begin() {
				return ti_traits::node_traits::get_next(&header);
			}
			ti_ptr end() {
				return &header;
			}
			ti_ptr unlink_no_rebalance() {
				auto i = begin();
				erase(i);
				return i;
			}
			ti_ptr next(ti_ptr n) {
				return ti_traits::node_traits::get_next(n);
			}
			protocol_mgr() {
				algo::init_header(&header);
			}
		};
		template<typename t>
		struct priority_node_cmp {
			bool operator()(const t&a,const t&b) const {
				return a->get_prio()<b->get_prio();
			}
		};
		template<typename ti_traits> struct protocol_mgr<ti_traits,priority> {
			typedef typename ti_traits::ti ti;
			typedef typename ti_traits::ptr ti_ptr;
			typedef boost::intrusive::rbtree_algorithms<typename ti_traits::node_traits> algo;
			ti header;
			void insert(ti_ptr pn) {
				algo::insert_equal_upper_bound(&header,pn,priority_node_cmp<ti_ptr>());
			}
			void erase(ti_ptr pn) {
				algo::erase(&header,pn);
			}
			ti_ptr begin() {
				return algo::begin_node(&header);
			}
			ti_ptr end() {
				return &header;
			}
			ti_ptr next(ti_ptr n) {
				return algo::next_node(n);
			}
			ti_ptr unlink_no_rebalance() {
				return algo::unlink_leftmost_without_rebalance(&header);
			}
			protocol_mgr() {
				algo::init_header(&header);
			}
		};

		template<typename recursive,typename protocol,typename ipc>
		struct mutex {
			typedef typename detail::ti_traits<protocol,ipc> ti_traits;
			typedef typename ti_traits::ti ti;
			typedef typename ti_traits::handle ti_handle;
			typedef typename ti_traits::ptr ti_ptr;
			thread_id owner;
			busy_lock bl;
			recursive_mgr<recursive> rec;
			protocol_mgr<ti_traits,protocol> proto;
			mutex() : owner(-1) {}
			int lock(uint64_t timeout) {
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				thread_id tid = n.get_id();
				mem_read_barrier();
				if (owner==tid) return rec.lock_rec();
				ti_ptr pn = n.get_ptr();
				if ((owner==-1 && atomic_cas(&owner,-1,tid)==-1) || (bl.lock(), owner==-1?owner=tid,bl.unlock(),true:false)) return rec.on_lock();
				proto.insert(pn);
				bl.unlock();
				if (n.get_event().wait(timeout)) return rec.on_lock();
				else {
					bl.lock();
					if (n.get_event().try_wait()) return bl.unlock(), rec.on_lock();
					proto.erase(pn);
					bl.unlock();
					return ETIMEDOUT;
				}
			}
			int try_lock() {
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				thread_id tid = n.get_id();
				mem_read_barrier();
				if (owner!=-1) return owner==tid ? rec.lock_rec() : EBUSY;
				else return atomic_cas(&owner,-1,tid)==-1 ? rec.on_lock() : EBUSY;
			}
			int unlock() {
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				thread_id tid = n.get_id();
				if (owner!=tid) return EPERM;
				if (!rec.unlock()) return CELL_OK;
				bl.lock();
				ti_ptr b = proto.begin();
				if (b!=proto.end()) {
					proto.erase(b);
					owner = b->get_id();
					b->get_event().signal();
					bl.unlock();
					return CELL_OK;
				} else {
					owner = -1;
					bl.unlock();
					return CELL_OK;
				}
			}
			bool is_locked() {
				return owner!=-1;
			}
			template<typename recursive,typename protocol,typename ipc>
			struct rebind {
				typedef mutex<recursive,protocol,ipc> other;
			};
		};

		template<typename protocol,typename ipc>
		struct sleep_queue {
			typedef typename detail::ti_traits<protocol,ipc> ti_traits;
			typedef typename ti_traits::ti ti;
			typedef typename ti_traits::handle ti_handle;
			typedef typename ti_traits::ptr ti_ptr;
			busy_lock bl;
			recursive_mgr<recursive> rec;
			protocol_mgr<ti_traits,protocol> proto;
			int nowait_count;
			sleep_queue() : nowait_count(0) {}
			int wait(uint64_t timeout) {
				bl.lock();
				if (nowait_count) return --nowait_count, bl.unlock(), CELL_OK;
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				ti_ptr pn = n.get_ptr();
				proto.insert(pn);
				bl.unlock();
				if (n.get_event().wait(timeout)) return CELL_OK;
				else {
					bl.lock();
					if (n.get_event().try_wait()) return bl.unlock(), CELL_OK;
					proto.erase(pn);
					bl.unlock();
					return ETIMEDOUT;
				}
			}
			int try_wait() {
				bl.lock();
				return nowait_count ? --nowait_count, bl.unlock(), CELL_OK : (bl.unlock(), EBUSY);
			}
			int release_one() {
				bl.lock();
				ti_ptr b = proto.begin();
				if (b!=proto.end()) {
					proto.erase(b);
					b->get_event().signal();
					bl.unlock();
					return CELL_OK;
				} else {
					++nowait_count;
					bl.unlock();
					return CELL_OK;
				}
			}
			template<typename protocol,typename ipc>
			struct rebind {
				typedef sleep_queue<protocol,ipc> other;
			};
		};

		template<typename protocol,typename ipc>
		struct condition_variable {
			typedef typename detail::ti_traits<protocol,ipc> ti_traits;
			typedef typename ti_traits::ti ti;
			typedef typename ti_traits::handle ti_handle;
			typedef typename ti_traits::ptr ti_ptr;
			busy_lock bl;
			recursive_mgr<recursive> rec;
			protocol_mgr<ti_traits,protocol> proto;
			condition_variable() {}
			template<typename mut_t>
			int wait(mut_t&mut,uint64_t timeout) {
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				ti_ptr pn = n.get_ptr();
				bl.lock();
// 				auto u = mut.unlock_all();
// 				if (u.first) return bl.unlock(), u.first;
				auto r = mut.unlock();
				if (r) return bl.unlock(), r;
				proto.insert(pn);
				bl.unlock();
				//if (n.get_event().wait(timeout)) return mut.lock_all(u.second,0);
				if (n.get_event().wait(timeout)) return mut.lock(0);
				else {
					bl.lock();
					//if (n.get_event().try_wait()) return bl.unlock(), mut.lock_all(u.second,0);
					if (n.get_event().try_wait()) return bl.unlock(), mut.lock(0);
					proto.erase(pn);
					bl.unlock();
					//auto r = mut.lock_all(u.second,0);
					auto r = mut.lock(0);
					if (r) return r;
					return ETIMEDOUT;
				}
			}
			int signal() {
				bl.lock();
				ti_ptr b = proto.begin();
				if (b!=proto.end()) {
					proto.erase(b);
					b->get_event().signal();
					bl.unlock();
					return CELL_OK;
				} else {
					bl.unlock();
					return CELL_OK;
				}
			}
			int signal_to(thread_id tid) {
				bl.lock();
				for (ti_ptr i = proto.begin();i!=proto.end();i=proto.next(i)) {
					if (i->get_id()==tid) {
						proto.erase(i);
						i->get_event().signal();
						bl.unlock();
						return CELL_OK;
					}
				}
				bl.unlock();
				return EPERM;
			}
			int signal_all() {
				bl.lock();
				while (proto.begin()!=proto.end()) {
					ti_ptr i = proto.unlink_no_rebalance();
					i->get_event().signal();
				}
				bl.unlock();
				return CELL_OK;
			}
			template<typename protocol,typename ipc>
			struct rebind {
				typedef condition_variable<protocol,ipc> other;
			};
		};

		template<typename protocol,typename ipc>
		struct rwlock {
			typedef typename detail::ti_traits<protocol,ipc> ti_traits;
			typedef typename ti_traits::ti ti;
			typedef typename ti_traits::handle ti_handle;
			typedef typename ti_traits::ptr ti_ptr;
			busy_lock bl;
			protocol_mgr<ti_traits,protocol> rproto, wproto;
			thread_id owner; // Just to detect recursive write locks; only one thread cares about this at a time.
			int32_t count; // If write lock is not held: count or count-release_waiter_count is number of readers.
			               // If write lock is held: -count is recursive count for writer
			static const int32_t release_waiter_count = 0x80000000;
			// Note: Since count can be updated by multiple readers simultaneously, we can either use atomic operations
			//       or only update count within the busy lock. Most places that update count already hold the
			//       busy lock anyways. Nevertheless, I've chosen to use atomic operations. This allows for instance
			//       to grab the lock with just a cas.
			//       This applies only if count is >=0. If it is negative, only one thread writes to it at once.
			rwlock() : owner(-1), count(0) {}
			int rlock(uint64_t timeout) {
				while (true) {
					auto c = count;
					if (c<0) {
						if (bl.lock(), count>=0?atomic_inc(&count),bl.unlock(),true:false) return CELL_OK;
						else break;
					} else if (atomic_cas(&count,c,c+1)==c) return CELL_OK;
				}
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				ti_ptr pn = n.get_ptr();
				rproto.insert(pn);
				bl.unlock();
				if (n.get_event().wait(timeout)) return atomic_inc(&count), CELL_OK;
				else {
					bl.lock();
					if (n.get_event().try_wait()) return atomic_inc(&count), bl.unlock(), CELL_OK;
					rproto.erase(pn);
					bl.unlock();
					return ETIMEDOUT;
				}
			}
			int try_rlock() {
				mem_read_barrier();
				while (true) {
					auto c = count;
					if (c<0) return EBUSY;
					if (atomic_cas(&count,c,c+1)==c) return CELL_OK;
				}
			}
			int runlock() {
				mem_read_barrier();
				auto c = count;
				if ((c>=0)!=(c-1>=0)) return EPERM;
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				ti_ptr pn = n.get_ptr();
				if (atomic_dec(&count)==release_waiter_count) {
					bl.lock();
					ti_ptr b = wproto.begin();
					if (b!=wproto.end()) {
						wproto.erase(b);
						count = -1;
						owner = b->get_id();
						mem_write_barrier();
						b->get_event().signal();
					}
					bl.unlock();
				}
				return CELL_OK;
			}
			int wlock(uint64_t timeout) {
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				thread_id tid = n.get_id();
				mem_read_barrier();
				if (owner==tid) return --count, CELL_OK;
				auto c = count;
				if (!c && atomic_cas(&count,0,-1)==0) return owner=tid, CELL_OK;
				bl.lock();
				if (count>=0) { // count cannot switch sign outside the busy lock.
					do {
						// Unfortunately, this must be done in a loop, since we never know when readers will lock/unlock,
						// and we must hold the busy lock for the same reason. This would be faster without atomic operations :(
						auto c = count;
						if (!c && atomic_cas(&count,0,-1)==0) return bl.unlock(), owner=tid, CELL_OK;
					} while (atomic_cas(&count,c,c+release_waiter_count)!=c);
				}
				ti_ptr pn = n.get_ptr();
				wproto.insert(pn);
				bl.unlock();
				if (n.get_event().wait(timeout)) return CELL_OK;
				else {
					bl.lock();
					if (n.get_event().try_wait()) return bl.unlock(), CELL_OK;
					wproto.erase(pn);
					bl.unlock();
					return ETIMEDOUT;
				}
			}
			int try_wlock() {
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				thread_id tid = n.get_id();
				mem_read_barrier();
				if (owner==tid) return --count, CELL_OK;
				auto c = count;
				if (!c && atomic_cas(&count,0,-1)==0) return owner=tid, CELL_OK;
				return EBUSY;
			}
			int wunlock() {
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				thread_id tid = n.get_id();
				mem_read_barrier();
				if (owner!=tid) return EPERM;
				if (count!=-1) return ++count, CELL_OK;
				bl.lock();
				ti_ptr b = wproto.begin();
				if (b!=wproto.end()) {
					wproto.erase(b);
					count = -1;
					owner = b->get_id();
					mem_write_barrier();
					b->get_event().signal();
				} else {
					++count;
					owner = -1;
					while (rproto.begin()!=rproto.end()) {
						auto i = rproto.unlink_no_rebalance();
						i->get_event().signal();
					}
				}
				bl.unlock();
				return CELL_OK;
			}
			template<typename protocol,typename ipc>
			struct rebind {
				typedef rwlock<protocol,ipc> other;
			};
		};

		template<typename protocol,typename ipc>
		struct semaphore {
			typedef typename detail::ti_traits<protocol,ipc> ti_traits;
			typedef typename ti_traits::ti ti;
			typedef typename ti_traits::handle ti_handle;
			typedef typename ti_traits::ptr ti_ptr;
			busy_lock bl;
			protocol_mgr<ti_traits,protocol> proto;
			uint32_t count, max;
			uint32_t waiters;
			semaphore(uint32_t initial,uint32_t max) : count(initial), max(max), waiters(0) {}
			int wait(uint64_t timeout) {
				while (true) {
					auto c = count;
					if (!c) {
						if (bl.lock(), count?--count,bl.unlock(),true:false) return CELL_OK;
						else break;
					} else if (atomic_cas(&count,c,c-1)==c) return CELL_OK;
				}
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				ti_ptr pn = n.get_ptr();
				proto.insert(pn);
				++waiters;
				bl.unlock();
				if (n.get_event().wait(timeout)) return CELL_OK;
				else {
					bl.lock();
					if (n.get_event().try_wait()) return bl.unlock(), CELL_OK;
					proto.erase(pn);
					bl.unlock();
					return ETIMEDOUT;
				}
			}
			int try_wait() {
				mem_read_barrier();
				while (true) {
					auto c = count;
					if (!c) return EBUSY;
					if (atomic_cas(&count,c,c-1)==c) return CELL_OK;
				}
			}
			int post(uint32_t n) {
				bl.lock();
				while (true) {
					auto w = waiters;
					auto c = count;
					auto r = w>n?n:w;
					auto nc = c+(n-r);
					if (nc<c) return EINVAL;
					if (nc>max) return bl.unlock(), EBUSY;
					if (atomic_cas(&count,c,nc)!=c) continue;
					waiters -= r;
					if (r>=w) {
						while (proto.begin()!=proto.end()) proto.unlink_no_rebalance()->get_event().signal();
					} else {
						for (auto i=proto.begin();r;--r) {
							auto n = i;
							i=proto.next(i);
							proto.erase(n);
							n->get_event().signal();
						}
					}
					bl.unlock();
					return CELL_OK;
				}
			}
			uint32_t get_value() {
				return count;
			}
			template<typename protocol,typename ipc>
			struct rebind {
				typedef semaphore<protocol,ipc> other;
			};
		};
		template<typename protocol,typename ipc,typename item_type,uint32_t buffer_size>
		struct message_queue {
			typedef typename detail::ti_traits<protocol,ipc> ti_traits;
			typedef typename ti_traits::ti ti;
			typedef typename ti_traits::handle ti_handle;
			typedef typename ti_traits::ptr ti_ptr;
			typedef thread_buffer_t<item_type,ipc> thread_buffer;
			busy_lock bl;
			protocol_mgr<ti_traits,protocol> proto;
			item_type buf[buffer_size];
			uint32_t bot, top, size;
			bool cancelled;
			// Note: size must be below or equal to buffer_size, but there are
			//       no runtime checks for it.
			message_queue(uint32_t size) : bot(0), top(0), size(size), cancelled(false) {}
			int receive(item_type&dst,uint64_t timeout) {
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				ti_ptr pn = n.get_ptr();
				bl.lock();
				if (cancelled) return bl.unlock(), ECANCELED;
				if (top!=bot) return dst = buf[bot++%buffer_size], bl.unlock(), CELL_OK;
				proto.insert(pn);
				bl.unlock();
				thread_id tid = n.get_id();
				auto&mybuf = thread_buffer::get(tid);
				auto&e = n.get_event();
				if (e.wait(timeout)) return dst=mybuf, cancelled?ECANCELED:CELL_OK;
				else {
					bl.lock();
					if (e.try_wait()) return bl.unlock(), dst=mybuf, CELL_OK;
					proto.erase(pn);
					bl.unlock();
					return ETIMEDOUT;
				}
			}
			int try_receive(item_type*dst,int count,int*retcount) {
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				int i;
				bl.lock();
				for (i=0;top!=bot&&i<count;++i) dst[i] = buf[bot++%buffer_size];
				bl.unlock();
				*retcount = i;
				return CELL_OK;
			}
			int send(const item_type&src) {
				ti_handle n = detail::get_this_ti<protocol,ipc>();
				bl.lock();
				auto b = proto.begin();
				if (b!=proto.end()) return proto.erase(b), bl.unlock(), thread_buffer::get(b->get_id())=src, b->get_event().signal(), CELL_OK;
				else if ((top-bot)%buffer_size>=size) return bl.unlock(), EBUSY;
				else return buf[top++%buffer_size] = src, bl.unlock(), CELL_OK;
			}
			// cancel() makes all currently blocking and future receive() calls
			// return ECANCELED
			void cancel() {
				cancelled = true;
				bl.lock();
				while (proto.begin()!=proto.end()) proto.unlink_no_rebalance()->get_event().signal();
				bl.unlock();
			}
			void drain() {
				bl.lock();
				while (top!=bot) ++bot;
				bl.unlock();
			}
			template<typename protocol,typename ipc,typename item_type,uint32_t buffer_size>
			struct rebind {
				typedef message_queue<protocol,ipc,item_type,buffer_size> other;
			};
		};

		struct call_lock {template<typename t>int operator()(t&v,uint64_t timeout){return v.lock(timeout);}};
		struct call_try_lock {template<typename t>int operator()(t&v){return v.try_lock();}};
		struct call_unlock {template<typename t>int operator()(t&v){return v.unlock();}};
		struct call_wait {
			template<typename t>int operator()(t&v,uint64_t timeout){return v.wait(timeout);}
			template<typename t,typename mut_t>int operator()(t&v,mut_t&mut,uint64_t timeout){return v.wait(mut,timeout);}
		};
		struct call_release_one {template<typename t>int operator()(t&v){return v.release_one();}};
		struct call_signal {template<typename t>int operator()(t&v){return v.signal();}};
		struct call_signal_to {template<typename t>int operator()(t&v,thread_id tid){return v.signal_to(tid);}};
		struct call_signal_all {template<typename t>int operator()(t&v){return v.signal_all();}};
		struct call_rlock {template<typename t>int operator()(t&v,uint64_t timeout){return v.rlock(timeout);}};
		struct call_try_rlock {template<typename t>int operator()(t&v){return v.try_rlock();}};
		struct call_runlock {template<typename t>int operator()(t&v){return v.runlock();}};
		struct call_wlock {template<typename t>int operator()(t&v,uint64_t timeout){return v.wlock(timeout);}};
		struct call_try_wlock {template<typename t>int operator()(t&v){return v.try_wlock();}};
		struct call_wunlock {template<typename t>int operator()(t&v){return v.wunlock();}};
		struct call_try_wait {template<typename t>int operator()(t&v){return v.try_wait();}};
		struct call_post {template<typename t>int operator()(t&v,uint32_t n){return v.post(n);}};
		struct call_get_value {template<typename t>uint32_t operator()(t&v){return v.get_value();}};
		struct call_receive {template<typename t,typename item_type>int operator()(t&v,item_type&dst,uint64_t timeout){return v.receive(dst,timeout);}};
		struct call_try_receive {template<typename t,typename item_type>int operator()(t&v,item_type*dst,int count,int*retcount){return v.try_receive(dst,count,retcount);}};
		struct call_send {template<typename t,typename item_type>int operator()(t&v,const item_type&src){return v.send(src);}};
		struct call_cancel {template<typename t>void operator()(t&v){return v.cancel();}};
		struct call_drain {template<typename t>void operator()(t&v){return v.drain();}};
		struct call_is_locked {template<typename t>bool operator()(t&v){return v.is_locked();}};
	}

	typedef detail::busy_lock busy_lock;
	struct busy_locker {
		sync::busy_lock&l;
		busy_locker(sync::busy_lock&l) : l(l) {
			l.lock();
		}
		~busy_locker() {
			l.unlock();
		}
	};

	template<typename recursive,typename protocol,typename ipc>
	struct mutex: detail::mutex<recursive,protocol,ipc> {};
	typedef mutex<recursive,priority,not_ipc> default_mutex;
	struct mutex_any {
		detail::test<default_mutex,detail::opt_recursive,detail::opt_protocol,detail::opt_ipc> x;
		template<typename recursive,typename protocol,typename ipc>
		void init() {
			x.init<recursive,protocol,ipc>();
		}
		int lock(uint64_t timeout) {
			return x.call(detail::call_lock(),timeout);
		}
		int try_lock() {
			return x.call(detail::call_try_lock());
		}
		int unlock() {
			return x.call(detail::call_unlock());
		}
		bool is_locked() {
			return x.call(detail::call_is_locked());
		}
	};

	template<typename protocol,typename ipc>
	struct sleep_queue: detail::sleep_queue<protocol,ipc> {};
	typedef sleep_queue<priority,not_ipc> default_sleep_queue;
	struct sleep_queue_any {
		detail::test<default_sleep_queue,detail::opt_protocol,detail::opt_ipc> x;
		template<typename protocol,typename ipc>
		void init() {
			x.init<protocol,ipc>();
		}
		int wait(uint64_t timeout) {
			return x.call(detail::call_wait(),timeout);
		}
		int try_wait() {
			return x.call(detail::call_try_wait());
		}
		int release_one() {
			return x.call(detail::call_release_one());
		}
	};

	template<typename protocol,typename ipc>
	struct condition_variable: detail::condition_variable<protocol,ipc> {};
	typedef condition_variable<priority,not_ipc> default_condition_variable;
	struct condition_variable_any {
		detail::test<default_condition_variable,detail::opt_protocol,detail::opt_ipc> x;
		template<typename protocol,typename ipc>
		void init() {
			x.init<protocol,ipc>();
		}
		template<typename mut_t>
		int wait(mut_t&mut,uint64_t timeout) {
			return x.call(detail::call_wait(),mut,timeout);
		}
		int signal() {
			return x.call(detail::call_signal());
		}
		int signal_to(thread_id tid) {
			return x.call(detail::call_signal_to(),tid);
		}
		int signal_all() {
			return x.call(detail::call_signal_all());
		}
	};

	template<typename protocol,typename ipc>
	struct rwlock: detail::rwlock<protocol,ipc> {};
	typedef rwlock<priority,not_ipc> default_rwlock;
	struct rwlock_any {
		detail::test<default_rwlock,detail::opt_protocol,detail::opt_ipc> x;
		template<typename protocol,typename ipc>
		void init() {
			x.init<protocol,ipc>();
		}
		int rlock(uint64_t timeout) {
			return x.call(detail::call_rlock(),timeout);
		}
		int try_rlock() {
			return x.call(detail::call_try_rlock());
		}
		int runlock() {
			return x.call(detail::call_runlock());
		}
		int wlock(uint64_t timeout) {
			return x.call(detail::call_wlock(),timeout);
		}
		int try_wlock() {
			return x.call(detail::call_try_wlock());
		}
		int wunlock() {
			return x.call(detail::call_wunlock());
		}
	};

	template<typename protocol,typename ipc>
	struct semaphore: detail::semaphore<protocol,ipc> {
		semaphore(uint32_t initial,uint32_t max) : detail::semaphore<protocol,ipc>(initial,max) {}
	};
	typedef semaphore<priority,not_ipc> default_semaphore;
	struct semaphore_any {
		detail::test<default_semaphore,detail::opt_protocol,detail::opt_ipc> x;
		template<typename protocol,typename ipc>
		void init(uint32_t initial,uint32_t max) {
			x.init<protocol,ipc>(initial,max);
		}
		int wait(uint64_t timeout) {
			return x.call(detail::call_wait(),timeout);
		}
		int try_wait() {
			return x.call(detail::call_try_wait());
		}
		int post(uint32_t n) {
			return x.call(detail::call_post(),n);
		}
		uint32_t get_value() {
			return x.call(detail::call_get_value());
		}
	};

	template<typename protocol,typename ipc,typename item_type,uint32_t buffer_size>
	struct message_queue: detail::message_queue<protocol,ipc,item_type,buffer_size> {
		message_queue(uint32_t size) : detail::message_queue<protocol,ipc,item_type,buffer_size>(size) {}
	};
	typedef message_queue<priority,not_ipc,char,0x100> default_message_queue;
	template<typename item_type,uint32_t buffer_size>
	struct message_queue_any {
		struct rebinder {
			template<typename protocol,typename ipc>
			struct rebind {
				typedef message_queue<protocol,ipc,item_type,buffer_size> other;
			};
		};
		detail::test<rebinder,detail::opt_protocol,detail::opt_ipc> x;
		template<typename protocol,typename ipc>
		void init(uint32_t size) {
			x.init<protocol,ipc>(size);
		}
		int receive(item_type&dst,uint64_t timeout) {
			return x.call(detail::call_receive(),dst,timeout);
		}
		int try_receive(item_type*dst,int count,int*retcount) {
			return x.call(detail::call_try_receive(),dst,count,retcount);
		}
		int send(const item_type&src) {
			return x.call(detail::call_send(),src);
		}
		void cancel() {
			return x.call(detail::call_cancel());
		}
		void drain() {
			return x.call(detail::call_drain());
		}
	};

}
