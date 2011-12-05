
#include "libname_id.h"
#include "ah_main.h"


struct prx_register_module_info_t {
	uint64_t size, flags;
	uint32_t toc_addr, toc_size;
	uint32_t exports_addr, exports_size;
	uint32_t imports_addr, imports_size;
	uint64_t reserved;
};

struct prx_start_stop_module_info_t {
	uint64_t size, flags, addr, retval, reserved;
};

struct prx_get_module_list_info_t {
	uint64_t size;
	uint32_t pid;
	uint32_t max, count, id_list;
};

struct prx_info_t {
	uint32_t size;
	uint32_t magic; // 0x1b434cec
	uint32_t struct_version;
	uint32_t reserved1;
	uint32_t exports_begin, exports_end, imports_begin, imports_end;
	uint16_t version;
	uint8_t reserved2[6];
};

prx_info_t prx_info;

struct module_info_t {
	uint16_t attribs;
	uint16_t version;
	char name[28];
	uint32_t gp;
	uint32_t exports_begin, exports_end;
	uint32_t imports_begin, imports_end;
};

struct module_t {
	int id;
	void*image;
	std::string filename;
	void*dll,*fixups,*sections,*fix_got;
	module_info_t*mod_info;
	uint32_t start_entry, stop_entry;
	module_t() : start_entry(0), stop_entry(0) {}
};

id_list_t<module_t,1> module_list;
std::map<std::string,module_t*> module_map;
std::map<std::string,module_t*> module_name_map;
boost::mutex module_name_map_mutex;
void prx_init(module_t*);
void prx_allocate_image_and_relocate_dll(module_t*);
void*load_dll(const char*);
boost::recursive_mutex load_module_mutex;
module_t*load_module(const char*prx) {
	boost::unique_lock<boost::recursive_mutex> lock(load_module_mutex);

	dbgf("load module %s\n",prx);

	module_t*&map_m = module_map[prx];
	if (map_m) {
		dbgf("module %s already loaded (id %d)\n",prx,map_m->id);
		return map_m;
	}

	file_t f;
	f.set_fn(prx);
	std::string s = f.host_fn;
	if (s.rfind(".prx")==s.size()-4) s = s.substr(0,s.size()-4) + ".dll";
	if (s.rfind(".sprx")==s.size()-5) s = s.substr(0,s.size()-5) + ".dll";
	const char*dll = s.c_str();

	dbgf("loading %s...\n",dll);

	dll_prx_dll = 0;
	dll_prx_image = 0;
	dll_prx_sections = 0;
	dll_prx_fixups = 0;
	dll_prx_mod_info = 0;
	dll_prx_fix_got = 0;

	HANDLE dll_h = LoadLibraryA(dll);
	if (!dll_h) xcept("failed to load %s; error %d",dll,GetLastError());
	dbgf("dll loaded at %p\n",(void*)dll_h);

	if (!dll_prx_dll || !dll_prx_image || !dll_prx_sections || !dll_prx_fixups || !dll_prx_mod_info || !dll_prx_fix_got) xcept("dll %s did not initialize properly",dll);

	auto h = module_list.get_new();
	if (!h) xcept("module_list.get_new() failed");
	int id = h.id();
	module_t*m = &*h;
	map_m = m;
	m->id = id;
	m->filename = prx;
	m->dll = dll_prx_dll;
	m->image = dll_prx_image;
	m->sections = dll_prx_sections;
	m->fixups = dll_prx_fixups;
	m->mod_info = (module_info_t*)dll_prx_mod_info;
	m->fix_got = dll_prx_fix_got;
	dbgf("initializing module %d (%s)\n",id,prx);
	//prx_allocate_image_and_relocate_dll(m);
	prx_init(m);
	h.keep();
	return m;
}



template<typename F>
void prx_relocate_dll(void*dll,F&&f) {

}

void prx_allocate_image_and_relocate_dll(module_t*m) {

	void*dll = m->dll;
	uint64_t*sections = (uint64_t*)m->sections;

	uint32_t image_size = 0;
	for (;sections[0]|sections[1];sections+=2) {
		image_size += (uint32_t)sections[1];
	}
	dbgf("image size is %#x\n",image_size);
	void*image = mm_alloc(image_size);
	m->image = image;
	
	prx_relocate_dll(dll,[&]() {
	});

	xcept(".");
}

// Note that the pointer used as key when indexing into this must have process lifetime!
// This is fine since the pointer usually comes from an import or export table.
struct t_str_less{bool operator()(const char*a,const char*b){return strcmp(a,b)<0;}};
std::map<const char*,std::map<uint32_t,uint32_t>,t_str_less> lib_map;
std::map<const char*,std::map<uint32_t,uint32_t>,t_str_less> lib_var_map;

std::map<const char*,std::map<uint32_t,std::vector<uint32_t*>>,t_str_less> unresolved_import_map;
std::map<const char*,std::map<uint32_t,std::vector<uint32_t*>>,t_str_less> unresolved_import_var_map;

void find_and_load_library(const char*libname) {
	std::string s(libname);
	const char*prx = 0;
	bool is_liblv2 = false;
	if (s=="sysPrxForUser") {
		prx="/dev_flash/sys/external/liblv2.prx";
		is_liblv2 = true;
	}
	if (prx) {
		dbgf("prx for '%s' is '%s'\n",libname,prx);

		module_t*m = load_module(prx);
		if (is_liblv2) liblv2_start_entry = (void*)m->start_entry;

		if (lib_map.find(libname)==lib_map.end()) xcept("prx %s did not register library %s",prx,libname);
		return;
	}
}


void register_lib_func(const char*libname,uint32_t id,uint32_t addr) {
	{
		auto i = ah_map.find(std::make_pair(libname,id));
		if (i!=ah_map.end()) {
			((uint32_t*)addr)[0] = se((uint32_t)i->second);
			((uint32_t*)addr)[1] = 0;
		}
	}
	auto i = lib_map[libname].insert(std::make_pair(id,addr));
	if (!i.second) xcept("library function %#x from '%s' already registered",id,libname);
}

uint32_t get_lib_func(const char*libname,uint32_t id) {
	auto li = lib_map.find(libname);
	if (li==lib_map.end()) {
		find_and_load_library(libname);
		li = lib_map.find(libname);
		if (li==lib_map.end()) return 0;
	}
	auto&l = li->second;
	auto i = l.find(id);
	if (i==l.end()) xcept("function %#x could not be located in library '%s'",id,libname);
	return i->second;
}

void register_lib_var(const char*libname,uint32_t id,uint32_t addr) {
	auto i = lib_var_map[libname].insert(std::make_pair(id,addr));
	if (!i.second) xcept("library variable %#x from '%s' already registered",id,libname);
}

uint32_t get_lib_var(const char*libname,uint32_t id) {
	auto li = lib_var_map.find(libname);
	if (li==lib_var_map.end()) {
		find_and_load_library(libname);
		li = lib_var_map.find(libname);
		if (li==lib_var_map.end()) return 0;
	}
	auto&l = li->second;
	auto i = l.find(id);
	if (i==l.end()) xcept("variable %#x could not be located in library '%s'",id,libname);
	return i->second;
}

void*I_unresolved_import,*I_libcall_wrapper;
struct unresolved_import_t {
	uint32_t f,rtoc;
	uint32_t addr;
	const char*n;
	uint32_t id;
};
std::list<unresolved_import_t> unresolved_import_stub_list;

void set_import(uint32_t*at,uint32_t addr,const char*libname,uint32_t id) {
	if (true) {
		unresolved_import_stub_list.push_back(unresolved_import_t());
		unresolved_import_t&i = unresolved_import_stub_list.back();
		if ((uint32_t)&i!=(uint64_t)&i) xcept("unresolved_import_stub_list - %p does not fit in 32 bits",&i);
		i.f = se((uint32_t)I_libcall_wrapper);
		i.rtoc = se((uint32_t)&i);
		i.n = libname;
		i.id = id;
		i.addr = se(addr);
		*at = se((uint32_t)&i);
	} else {
		*at = se(addr);
	}
}

void set_var_import(uint32_t*at,uint32_t addr) {
	at = (uint32_t*)se(*at);
	if (se(at[0])!=1) xcept("at[0]!=1");
	*(uint32_t*)se(at[1]) = se(addr);
}

void resolve_unresolved_imports_from_lib(const char*libname) {
	auto i = unresolved_import_map.find(libname);
	auto vi = unresolved_import_var_map.find(libname);
	if (i==unresolved_import_map.end() && vi==unresolved_import_var_map.end()) {
		dbgf("no unresolved imports from library '%s'\n",libname);
		return;
	}
	if (i!=unresolved_import_map.end()) {
		auto&lib = lib_map[libname];
		auto&ui = i->second;
		int count = 0;
		for (auto i=ui.begin();i!=ui.end();++i) {
			uint32_t id = i->first;
			auto&list = i->second;
			auto i2 = lib.find(id);
			if (i2==lib.end()) xcept("function %x could not be located in library '%s'",id,libname);
			uint32_t p = i2->second;
			for (auto i=list.begin();i!=list.end();++i) {
				set_import(*i,p,libname,id);
				count++;
			}
		}
		dbgf("resolved %d function imports from library '%s'\n",count,libname);
	}
	if (vi!=unresolved_import_var_map.end()) {
		auto&lib = lib_var_map[libname];
		auto&ui = vi->second;
		int count = 0;
		for (auto i=ui.begin();i!=ui.end();++i) {
			uint32_t id = i->first;
			auto&list = i->second;
			auto i2 = lib.find(id);
			if (i2==lib.end()) xcept("variable 0x%x could not be located in library '%s'",id,libname);
			uint32_t p = i2->second;
			for (auto i=list.begin();i!=list.end();++i) {
				set_var_import(*i,p);
				count++;
			}
		}
		dbgf("resolved %d variable imports from library '%s'\n",count,libname);
	}

}

void call_to_unresolved_import(uint64_t v) {
	unresolved_import_t&i=*(unresolved_import_t*)v;
	xcept("call to unresolved import %x from %s",i.id,i.n);
}

void libcall_print(uint64_t v) {
	unresolved_import_t&i=*(unresolved_import_t*)v;
	dbgf("call import %x (%s) from %s\n",i.id,libid_name(i.n,i.id),i.n);
}

void do_imports(uint64_t b,uint64_t e) {
	dbgf("import from %#x to %#x\n",b,e);
	while (b<e) {
		uint16_t c = se(((uint16_t*)b)[3]);

		uint16_t var_c = se(((uint16_t*)b)[4]);

		char*n = (char*)se(((uint32_t*)b)[4]);
		uint64_t ids = se(((uint32_t*)b)[5]);
		uint64_t pointers = se(((uint32_t*)b)[6]);

		dbgf("%d+%d from %s\n",c,var_c,n);

		uint32_t*p_i = (uint32_t*)ids;
		uint32_t*p_p = (uint32_t*)pointers;
		for (int i=0;i<c;i++) {
			uint32_t id = se(*p_i++);
			uint32_t*p = p_p++;
			//dbgf("func id %x, p %p\n",id,p);
			uint32_t addr = get_lib_func(n,id);
			//dbgf("addr is %x\n",addr);
			if (addr) {
				set_import(p,addr,n,id);
			} else {
				unresolved_import_map[n][id].push_back(p);

				unresolved_import_stub_list.push_back(unresolved_import_t());
				unresolved_import_t&i = unresolved_import_stub_list.back();
				i.f = se((uint32_t)I_unresolved_import);
				i.rtoc = se((uint32_t)&i);
				i.n = n;
				i.id = id;
				*p = se((uint32_t)&i);
			}
		}
		p_i = (uint32_t*)se(((uint32_t*)b)[7]);
		p_p = (uint32_t*)se(((uint32_t*)b)[8]);
		for (int i=0;i<var_c;i++) {
			uint32_t id = se(*p_i++);
			uint32_t*p = p_p++;
			//dbgf("var id %x, p %p\n",id,p);
			uint32_t addr = get_lib_var(n,id);
			//dbgf("addr %#x\n",addr);
			if (addr) {
				set_var_import(p,addr);
			} else {
				unresolved_import_var_map[n][id].push_back(p);
			}
		}

		b+= 4*11;
	}
}
void do_exports(uint64_t b,uint64_t e,uint32_t*start_entry=0,uint32_t*stop_entry=0,module_t*mod=0) {
	dbgf("export from %#x to %#x\n",b,e);
	while (b<e) {
		uint16_t c = se(((uint16_t*)b)[3]);
		
		uint16_t var_c = se(((uint16_t*)b)[4]);

		char*n = (char*)se(((uint32_t*)b)[4]);
		uint64_t ids = se(((uint32_t*)b)[5]);
		uint64_t pointers = se(((uint32_t*)b)[6]);

		if (mod && n) {
			boost::unique_lock<boost::mutex> l(module_name_map_mutex);
			auto i = module_name_map.insert(std::make_pair(n,mod));
			if (!i.second) xcept("'%s' already loaded",n);
		}

		dbgf("%d+%d from %s\n",c,var_c,n);

		uint32_t*p_i = (uint32_t*)ids;
		uint32_t*p_p = (uint32_t*)pointers;
		for (int i=0;i<c;i++) {
			uint32_t id = se(*p_i++);
			uint32_t p = se(*p_p++);
			//dbgf("func id %x, p %x\n",id,p);

			if (n) register_lib_func(n,id,p);
			else {
				if (id==0xbc9a0086) *start_entry = p;
				else if (id==0xAB779874) *stop_entry = p;
			}
		}
		for (int i=0;i<var_c;i++) {
			uint32_t id = se(*p_i++);
			uint32_t p = se(*p_p++);
			//dbgf("var id %x, p %p\n",id,p);
			if (n) register_lib_var(n,id,p);
		}

		if (n) resolve_unresolved_imports_from_lib(n);

		b+= 4*7;
	}
}

struct t_reloc {
	uint64_t P;
	uint64_t type;
	uint64_t S;
	uint64_t A;
};

void do_relocs(void*image,t_reloc*r) {

	dbgf("relocating elf image at %p\n",image);

	while (r->type) {
		uint64_t type = r->type;
		uint64_t P = r->P + (uint64_t)image;
		uint64_t S = r->S + (uint64_t)image;
		uint64_t A = r->A;
		r++;

		//dbgf("%x %x %x %x\n",type,P,S,A);

		auto checkfits = [&](uint64_t v,int bits) {
			if ((v&((1LL<<bits)-1))!=v && (v>>bits!=(1LL<<(64-bits))-1)) xcept("relocation failed; value %x does not fit in %d bits",v,bits);
		};

		auto r_word32 = [&](uint64_t v) {
			checkfits(v,32);
			*(uint32_t*)P = se((uint32_t)v);
		};
		auto r_low24 = [&](uint64_t v) {
			checkfits(v,24);
			*(uint32_t*)P = se((se(*(uint32_t*)P)&~0x3FFFFFC) | ((uint32_t)(v<<2)&0x3FFFFFC));
		};
		auto r_half16 = [&](uint64_t v) {
			checkfits(v,16);
			*(uint16_t*)P = se((uint16_t)v);
		};
		auto r_low14 = [&](uint64_t v) {
			checkfits(v,14);
			*(uint32_t*)P = se((se(*(uint32_t*)P)&~0xFFFC) | ((uint32_t)(v<<2)&0xFFFC));
		};

		auto lo = [&](uint64_t x) { return (x & 0xffff); };
		auto hi = [&](uint64_t x) { return ((x >> 16) & 0xffff);};
		auto ha = [&](uint64_t x) { return (((x >> 16) + ((x & 0x8000) ? 1 : 0)) & 0xffff); };

#define R_PPC64_NONE 0
#define R_PPC64_ADDR32 1
#define R_PPC64_ADDR24 2
#define R_PPC64_ADDR16 3
#define R_PPC64_ADDR16_LO 4
#define R_PPC64_ADDR16_HI 5
#define R_PPC64_ADDR16_HA 6
#define R_PPC64_ADDR14 7
#define R_PPC64_ADDR14_BRTAKEN 8
#define R_PPC64_ADDR14_BRNTAKEN 9
#define R_PPC64_REL24 10
#define R_PPC64_REL14 11
#define R_PPC64_REL14_BRTAKEN 12
#define R_PPC64_REL14_BRNTAKEN 13

		if (type==R_PPC64_NONE) ;
		else if (type==R_PPC64_ADDR32) r_word32(S+A);
		else if (type==R_PPC64_ADDR24) r_low24((int64_t)(S+A)>>2);
		else if (type==R_PPC64_ADDR16) r_half16(S+A);
		else if (type==R_PPC64_ADDR16_LO) r_half16(lo(S+A));
		else if (type==R_PPC64_ADDR16_HI) r_half16(hi(S+A));
		else if (type==R_PPC64_ADDR16_HA) r_half16(ha(S+A));
		else if (type==R_PPC64_ADDR14) r_low14((int64_t)(S+A)>>2);
		else if (type==R_PPC64_ADDR14_BRTAKEN) r_low14((int64_t)(S+A)>>2);
		else if (type==R_PPC64_ADDR14_BRNTAKEN) r_low14((int64_t)(S+A)>>2);
		else if (type==R_PPC64_REL24) r_low24((int64_t)(S + A - P) >> 2);
		else if (type==R_PPC64_REL14) r_low14((int64_t)(S + A - P) >> 2);
		else if (type==R_PPC64_REL14_BRTAKEN) r_low14((int64_t)(S + A - P) >> 2);
		else if (type==R_PPC64_REL14_BRNTAKEN) r_low14((int64_t)(S + A - P) >> 2);
		else xcept("unknown relocation type %d\n",type);

	}

}

void prx_init(module_t*m) {
	void*image = m->image;
	void*fixups = m->fixups;
	void*mod_info = (void*)m->mod_info;
	void*fix_got = m->fix_got;
	dbgf("prx_init: image is at %p, fixups at %p, module info at %p, fix_got at %p\n",image,fixups,mod_info,fix_got);
	
	do_relocs(image,(t_reloc*)fixups);

	((void(*)())fix_got)();

	module_info_t*i = (module_info_t*)mod_info;
	uint16_t attribs = se(i->attribs);
	uint16_t version = se(i->version);
	char name[28];
	memcpy(name,i->name,28);
	name[27] = 0;
	uint32_t gp = se(i->gp);
	uint32_t exports_begin = se(i->exports_begin);
	uint32_t exports_end = se(i->exports_end);
	uint32_t imports_begin = se(i->imports_begin);
	uint32_t imports_end = se(i->imports_end);

	dbgf("module info::\n");
	dbgf("attribs: %#x\n",attribs);
	dbgf("version: %#x\n",version);
	dbgf("name: %s\n",(const char*)name);
	dbgf("gp: %#x\n",gp);
	dbgf("exports_begin: %#x\n",exports_begin);
	dbgf("exports_end: %#x\n",exports_end);
	dbgf("imports_begin: %#x\n",imports_begin);
	dbgf("imports_end: %#x\n",imports_end);

	do_exports(exports_begin,exports_end,&m->start_entry,&m->stop_entry,m);
	do_imports(imports_begin,imports_end);

}

void set_prx_info(prx_info_t*i) {
	if (!i) {
		dbgf("prx info is null\n");
		memset(&prx_info,0,sizeof(prx_info));
		return;
	}
	dbgf("prx info is at %p\n",i);
	uint32_t size = se(i->size);
	if (size<sizeof(prx_info_t)) xcept("prx info size too small");
	uint32_t magic = se(i->magic);
	if (magic!=0x1b434cec) xcept("prx info magic mismatch; got %x, expected %x",magic,0x1b434cec);
	uint32_t struct_version = se(i->struct_version);
	uint32_t reserved1 = se(i->reserved1);
	uint32_t exports_begin = se(i->exports_begin);
	uint32_t exports_end = se(i->exports_end);
	uint32_t imports_begin = se(i->imports_begin);
	uint32_t imports_end = se(i->imports_end);
	uint16_t version = se(i->version);
	uint8_t reserved2[6];
	memcpy(reserved2,i->reserved2,sizeof(reserved2));
	dbgf("prx info::\n");
	dbgf("size: %d (vs %d)\n",size,sizeof(prx_info_t));
	dbgf("magic: %#x\n",magic);
	dbgf("struct_version: %#x\n",struct_version);
	dbgf("reserved1: %#x\n",reserved1);
	dbgf("libent_start: %#x\n",exports_begin);
	dbgf("libent_end: %#x\n",exports_end);
	dbgf("libstub_start: %#x\n",imports_begin);
	dbgf("libstub_end: %#x\n",imports_end);
	dbgf("version: %#x\n",version);
	dbgf("reserved2: %#x %#x %#x %#x %#x %#x\n",reserved2[0],reserved2[1],reserved2[2],reserved2[3],reserved2[4],reserved2[5]);
	prx_info.size = size;
	prx_info.magic = magic;
	prx_info.struct_version = struct_version;
	prx_info.reserved1 = reserved1;
	prx_info.exports_begin = exports_begin;
	prx_info.exports_end = exports_end;
	prx_info.imports_begin = imports_begin;
	prx_info.imports_end = imports_end;
	prx_info.version = version;
	memcpy(prx_info.reserved2,reserved2,sizeof(reserved2));

	do_exports(prx_info.exports_begin,prx_info.exports_end);
	do_imports(prx_info.imports_begin,prx_info.imports_end);
}

int sys_prx_register_module(const char*name,prx_register_module_info_t*info) {
	dbgf("register module %s\n",name);
	dbgf("size: %#x (vs %#x)\n",se(info->size),sizeof(prx_register_module_info_t));
	if (se(info->size)!=sizeof(prx_register_module_info_t)) dbgf(" !! size mismatch\n");
	dbgf("flags: %#x\n",se(info->flags));
	dbgf("toc addr: %#x\n",se(info->toc_addr));
	dbgf("toc size: %#x\n",se(info->toc_size));
	dbgf("exports addr: %#x\n",se(info->exports_addr));
	dbgf("exports size: %#x\n",se(info->exports_size));
	dbgf("imports addr: %#x\n",se(info->imports_addr));
	dbgf("imports size: %#x\n",se(info->imports_size));
	dbgf("reserved: %#x\n",se(info->reserved));

	uint64_t flags = se(info->flags);
	if (flags==1) {
		uint32_t exp_b = se(info->exports_addr);
		uint32_t exp_e = exp_b + se(info->exports_size);
		uint32_t imp_b = se(info->imports_addr);
		uint32_t imp_e = imp_b + se(info->imports_size);

		do_exports(exp_b,exp_e);
		do_imports(imp_b,imp_e);
	}

	return CELL_OK;
}

int sys_prx_register_library(void*p) {
	dbgf("register library at %p\n",p);
	uint16_t c = se(((uint16_t*)p)[3]);

	char*n = (char*)se(((uint32_t*)p)[4]);
	uint64_t ids = se(((uint32_t*)p)[5]);
	uint64_t pointers = se(((uint32_t*)p)[6]);

	dbgf("name: %s\n",n);
	dbgf("exports: %d\n",(int)c);

	return CELL_OK;
}

uint32_t sys_prx_load_module(const char*path,uint64_t flags,void*opt) {
	dbgf("load module %s, flags %x\n",path,flags);
	module_t*m = load_module(path);
	dbgf("m is %p, id is %d\n",m,m->id);
	return m->id;
}

int sys_prx_start_module(uint32_t id,uint64_t flags,prx_start_stop_module_info_t*i) {
	dbgf("start module %d\n",id);
	auto h = module_list.get(id);
	if (!h) {
		dbgf("no such module, returning ESRCH\n");
		return ESRCH;
	}
	module_t*m = &*h;
	dbgf("flags: %x\n",flags);
	dbgf(" size: %x (vs %x)\n",se(i->size),sizeof(prx_start_stop_module_info_t));
	if (se(i->size)!=sizeof(prx_start_stop_module_info_t)) dbgf(" !! size mismatch\n");
	uint64_t ff = se(i->flags);
	dbgf(" flags: %x (%s)\n",ff,ff==1?"pre":ff==2?"post":"?");
	dbgf(" addr: %x\n",se(i->addr));
	dbgf(" retval: %x\n",se(i->retval));
	if (m->start_entry) i->addr = se((uint64_t)m->start_entry);
	else i->addr = se((uint64_t)-1);
	dbgf(" addr set to %x\n",se(i->addr));
	return CELL_OK;
}

int sys_prx_stop_module(uint32_t id,uint64_t flags,prx_start_stop_module_info_t*i) {
	dbgf("stop module %d\n",id);
	auto h = module_list.get(id);
	if (!h) {
		dbgf("no such module, returning ESRCH\n");
		return ESRCH;
	}
	module_t*m = &*h;
	dbgf("flags: %x\n",flags);
	dbgf(" size: %x (vs %x)\n",se(i->size),sizeof(prx_start_stop_module_info_t));
	if (se(i->size)!=sizeof(prx_start_stop_module_info_t)) dbgf(" !! size mismatch\n");
	uint64_t ff = se(i->flags);
	dbgf(" flags: %x (%s)\n",ff,ff==1?"pre":ff==2?"post":"?");
	dbgf(" addr: %x\n",se(i->addr));
	dbgf(" retval: %x\n",se(i->retval));
	if (m->stop_entry) i->addr = se((uint64_t)m->stop_entry);
	else i->addr = se((uint64_t)-1);
	dbgf(" addr set to %x\n",se(i->addr));
	return CELL_OK;
}

int sys_prx_get_module_list(uint64_t flags,prx_get_module_list_info_t*list) {
	dbgf("get module list, flags %x\n",(long long)flags);
	dbgf("list->size is %d (vs %d)\n",(int)se(list->size),(int)sizeof(prx_get_module_list_info_t));
	if (flags==2) {
		
		dbgf("max is %d\n",se(list->max));
		dbgf("count is %d\n",se(list->count));

		list->pid = se(sys_process_getpid());
		
		uint32_t max = se(list->max);
		uint32_t*id_list = (uint32_t*)se(list->id_list);
		int last_id = -1;
		size_t i;
		for (i=0;i<max;i++) {
			auto h = module_list.get_next(last_id);
			if (!h) break;
			last_id = h.id();

			id_list[i] = se((uint32_t)h.id());
			dbgf("%d set to %d\n",i,h.id());
		}
		list->count = se((uint32_t)i);

	} else xcept("unknown flags %#x for sys_prx_get_module_list",flags);
	return CELL_OK;
}
int sys_prx_unload_module(uint32_t id,uint64_t flags,void*pOpt) {
	dbgf("unload module %d (flags %x, pOpt %x)\n",id,flags,pOpt);
	dbgf("(doing nothing, returning CELL_OK)\n");
	return CELL_OK;
}

int sys_prx_get_module_id_by_name(const char*name,uint64_t flags,void*opt) {
	dbgf("get module id by name '%s'\n",name);
	boost::unique_lock<boost::mutex> l(module_name_map_mutex);
	auto i = module_name_map.find(name);
	if (i==module_name_map.end()) return (int32_t)0x8001112e; // module not found
	return i->second->id;
}

int sys_prx_load_module_on_memcontainer(const char*path,uint32_t mem_id,uint64_t a3,uint64_t a4) {
	dbgf("load module on memory container, '%s', mem_id %d, a3 %#x, a4 %#x\n",path,mem_id,a3,a4);
	dbgf("(note: not supported, loading module as normal)\n");
	module_t*m = load_module(path);
	dbgf("m is %p, id is %d\n",m,m->id);
	return m->id;
}

int sys_prx_load_module_list(int count,uint64_t*paths,uint64_t flags,void*opt,uint32_t*ids) {
	dbgf("load module list count %d, flags %x, opt %p\n",count,flags,opt);
	for (int i=0;i<count;i++) {
		const char*p = (const char*)se(paths[i]);
		uint32_t*id = &ids[i];
		dbgf(" - %s\n",p);
		module_t*m = load_module(p);
		*id = se((uint32_t)m->id);
	}
	return CELL_OK;
}
