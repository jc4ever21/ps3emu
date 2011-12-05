
struct timer_info_t {
	uint64_t next_time;
	uint64_t period;
	uint32_t running;
};

int sys_timer_usleep(uint64_t sleep_time) {
	dbgf("usleep %d\n",sleep_time);
	//if (sleep_time==30) sleep_time = 1000*1000;
	//if (sleep_time==300) sleep_time = 1000*1000;
	//if (sleep_time==10 || sleep_time==100) sleep_time = 1000*1000;
	thread_sleep((sleep_time-1+(1000-(sleep_time-1)%1000))/1000);
	//thread_usleep(sleep_time*1000);
	return CELL_OK;
}

int sys_timer_sleep(uint32_t sleep_time) {
	dbgf("sleep %d\n",sleep_time);
	thread_sleep(sleep_time*1000);
	return CELL_OK;
}

struct timer_t: public boost::intrusive::set_base_hook<> {
	event_queue_handle eqh;
	int64_t next_time;
	uint64_t period;
	bool active;
	uint64_t name, data1, data2;
	timer_t() : active(false), next_time(0), period(0) {}
	bool operator<(const timer_t&t) const {
		return next_time<t.next_time;
	}
};

struct timer_mgr {
	win32_thread server_thread;
	boost::intrusive::multiset<timer_t> timers;
	boost::mutex mut;
	boost::condition_variable cond;
	bool kill_server;
	void server() {
		boost::unique_lock<boost::mutex> lock(mut);
		while (!kill_server) {
			if (timers.empty()) {
				cond.wait(lock);
				continue;
			}
			timer_t&n = *timers.begin();
			int64_t now = sys_time_get_system_time();
			if (now>=n.next_time) {
				int64_t exp_time = n.next_time;
				if (n.period) {
					timers.erase(timers.iterator_to(n));
					n.next_time += n.period;
					timers.insert(n);
				} else {
					timers.erase(timers.iterator_to(n));
					n.active = false;
				}
				if (n.eqh) {
					event_t d;
					d.source = se(n.name);
					d.data1 = se(n.data1);
					d.data2 = se(n.data2);
					d.data3 = se(exp_time);
					n.eqh->send(d);
				}
			} else {
				auto wait = (n.next_time - now);
				cond.timed_wait(lock,boost::posix_time::microseconds(wait));
			}
		}
	}
	timer_mgr() : kill_server(false) {
		server_thread.start([this]() {
			auto h = get_new_dummy_thread(0);
			this_thread = &*h;
			server();
		});
	}
	~timer_mgr() {
// 		boost::lock_guard<boost::mutex> lock(mut);
// 		atomic_write(&kill_server,true);
// 		cond.notify_one();
// 		server_thread.join();
	}
	static timer_mgr&get_singleton() {
		return singleton<timer_mgr>();
	}
	void start_timer(timer_t&t) {
		boost::unique_lock<boost::mutex> lock(mut);
		if (t.active) return;
		t.active = true;
		timers.insert(t);
		cond.notify_one();
	}
	void stop_timer(timer_t&t) {
		boost::unique_lock<boost::mutex> lock(mut);
		if (!t.active) return;
		t.active = false;
		timers.erase(timers.iterator_to(t));
	}
};

id_list_t<timer_t> timer_list;

int sys_timer_create(uint32_t*timer_id) {
	dbgf("timer create\n");
	auto h = timer_list.get_new();
	if (!h) return EAGAIN;
	dbgf("created timer with id %d\n",h.id());
	*timer_id = se((uint32_t)h.id());
	h.keep();
	return CELL_OK;
}

int sys_timer_destroy(uint32_t timer_id) {
	dbgf("timer destroy %d\n",timer_id);
	auto h = timer_list.get(timer_id);
	if (!h) return ESRCH;
	timer_t*t = &*h;
	if (t->eqh) return EISCONN;
	if (t->active) timer_mgr::get_singleton().stop_timer(*t);
	h.kill();
	return CELL_OK;
}

int sys_timer_get_information(uint32_t timer_id,timer_info_t*info) {
	dbgf("timer get information %d\n",timer_id);
	auto h = timer_list.get(timer_id);
	if (!h) return ESRCH;
	timer_t*t = &*h;
	info->next_time = se((int64_t)t->next_time);
	info->period = se((uint64_t)t->period);
	info->running = t->active ? 1 : 0;
	return CELL_OK;
}


int sys_timer_start(uint32_t timer_id,int64_t first_time,uint64_t period) {
	dbgf("timer start %d, first_time %d, period %d\n",timer_id,first_time,period);
	auto h = timer_list.get(timer_id);
	if (!h) return ESRCH;
	timer_t*t = &*h;
	if (t->active) return EBUSY;
	int64_t next_time = first_time;
	if (period) {
		if (period<100) return EINVAL;
		int64_t now = sys_time_get_system_time();
		if (first_time<now) {
			auto offset = first_time%period;
			next_time = (now-1-offset)+(period-((now-1-offset)%period)) + offset;
		}
	}
	t->next_time = next_time;
	t->period = period;
	timer_mgr::get_singleton().start_timer(*t);
	return CELL_OK;
}

int sys_timer_stop(uint32_t timer_id) {
	dbgf("timer stop %d\n",timer_id);
	auto h = timer_list.get(timer_id);
	if (!h) return ESRCH;
	timer_t*t = &*h;
	timer_mgr::get_singleton().stop_timer(*t);
	return CELL_OK;
}

int sys_timer_connect_event_queue(uint32_t timer_id,uint32_t queue_id,uint64_t name,uint64_t data1,uint64_t data2) {
	dbgf("timer connect event queue, timer %d, queue %d, name %#x, data1 %#x, data2 %#x\n",timer_id,queue_id,name,data1,data2);
	auto h = timer_list.get(timer_id);
	if (!h) return ESRCH;
	timer_t*t = &*h;
	boost::unique_lock<boost::mutex> (timer_mgr::get_singleton().mut);
	auto eqh = event_queue_list.get(queue_id);
	if (!eqh) return ESRCH;
	if (t->eqh) return EISCONN;
	t->eqh = eqh;
	if (name==0) name = (uint64_t)sys_process_getpid()<<32 | timer_id;
	t->name = name;
	t->data1 = data1;
	t->data2 = data2;
	return CELL_OK;
}

int sys_timer_disconnect_event_queue(uint32_t timer_id) {
	dbgf("timer disconnect event queue, timer %d\n",timer_id);
	auto h = timer_list.get(timer_id);
	if (!h) return ESRCH;
	timer_t*t = &*h;
	boost::unique_lock<boost::mutex> (timer_mgr::get_singleton().mut);
	if (!t->eqh) return ENOTCONN;
	t->eqh.close();
	return CELL_OK;
}



