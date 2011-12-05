
// template<typename t>
// t atomic_cas(t*ptr,t cmp,t val);
// 
// template<typename t>
// t atomic_inc(t*ptr);
// template<typename t>
// t atomic_dec(t*ptr);

// void mem_read_barrier();
// void mem_write_barrier();

#ifdef __GNUC__

template<typename t>
t atomic_cas(t*ptr,t cmp,t val) {
	return __sync_val_compare_and_swap(ptr,cmp,val);
}

template<typename t>
t atomic_inc(t*ptr) {
	return __sync_fetch_and_add(ptr,1)+1;
}
template<typename t>
t atomic_dec(t*ptr) {
	return __sync_fetch_and_sub(ptr,1)-1;
}

void mem_read_barrier() {
	__sync_synchronize();
}
void mem_write_barrier() {
	__sync_synchronize();
}

#else


void*atomic_cas(void**ptr,void*cmp,void*val) {
	return InterlockedCompareExchangePointer(ptr,val,cmp);
}

int atomic_cas(int*ptr,int cmp,int val) {
	static_assert(sizeof(int)==sizeof(long),"sizeof(int)!=sizeof(long)");
	return _InterlockedCompareExchange((long*)ptr,val,cmp);
}
uint32_t atomic_cas(uint32_t*ptr,uint32_t cmp,uint32_t val) {
	static_assert(sizeof(uint32_t)==sizeof(long),"sizeof(uint32_t)!=sizeof(long)");
	return _InterlockedCompareExchange((long*)ptr,val,cmp);
}
uint64_t atomic_cas(uint64_t*ptr,uint64_t cmp,uint64_t val) {
	static_assert(sizeof(uint64_t)==sizeof(LONG64),"sizeof(uint64_t)!=sizeof(LONG64)");
	return _InterlockedCompareExchange64((LONG64*)ptr,val,cmp);
}

void*atomic_xchg(void**ptr,void*val) {
	return _InterlockedExchangePointer(ptr,val);
}

int atomic_xchg(int*ptr,int val) {
	static_assert(sizeof(int)==sizeof(long),"sizeof(int)!=sizeof(long)");
	return _InterlockedExchange((long*)ptr,val);
}

//template<>
uint32_t atomic_inc(uint32_t*ptr) {
	static_assert(sizeof(uint32_t)==sizeof(long),"sizeof(uint32_t)!=sizeof(long)");
	return _InterlockedIncrement((long*)ptr);
}
int32_t atomic_inc(int32_t*ptr) {
	static_assert(sizeof(int32_t)==sizeof(long),"sizeof(int32_t)!=sizeof(long)");
	return _InterlockedIncrement((long*)ptr);
}
//template<>
uint32_t atomic_dec(uint32_t*ptr) {
	static_assert(sizeof(uint32_t)==sizeof(long),"sizeof(uint32_t)!=sizeof(long)");
	return _InterlockedDecrement((long*)ptr);
}
int32_t atomic_dec(int32_t*ptr) {
	static_assert(sizeof(int32_t)==sizeof(long),"sizeof(int32_t)!=sizeof(long)");
	return _InterlockedDecrement((long*)ptr);
}

void mem_read_barrier() {
	_ReadBarrier();
}
void mem_write_barrier() {
	_WriteBarrier();
}

#endif

template<typename t>
t atomic_read(t*ptr) {
	return *(volatile t*)ptr;
}
template<typename t>
void atomic_write(t*ptr,t val) {
	*(volatile t*)ptr = val;
}

