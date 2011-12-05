
#include <windows.h>

struct pe {



	struct t_import {
		std::string name;
		std::string import, dll;
		int ordinal;
		bool by_ordinal;
		uint64_t addr;
		t_import() : ordinal(-1), by_ordinal(false), addr(0) {}
	};

	struct t_symbol {
		std::string name;
		pe*obj;
		IMAGE_SYMBOL*sym;
		bool external;
		bool undefined;
		t_import*imp,*imp_stub;
		uint64_t addr;
		t_symbol() : obj(0), sym(0), external(false), undefined(false), imp(0), addr(0), imp_stub(0) {}
	};

	std::vector<IMAGE_SECTION_HEADER*> sections;
	std::map<std::string,t_symbol> extern_symbols;
	std::map<std::string,t_symbol> undefined_symbols;
	std::list<t_import> import_list;

	const char*data;
	size_t data_size;
	IMAGE_SYMBOL*symtab;
	size_t symtab_size;
	char*strtab;
	size_t strtab_size;

	std::string name;

	std::vector<std::pair<pe*,size_t>> xcu_sections;
	std::vector<std::pair<pe*,size_t>> xiu_sections;

	std::map<uint64_t,int> base_relocs;

	size_t section_alignment, file_alignment;
	uint64_t imports_addr, imports_size, exports_addr, exports_size;
	uint64_t relocs_addr, relocs_size;
	uint64_t resource_addr, resource_size;
	uint64_t entry_addr;
	uint64_t image_base;
	bool no_reloc, is_dll;
	pe() : section_alignment(0x1000), file_alignment(0x200), imports_addr(0), imports_size(0), exports_addr(0),
		exports_size(0), entry_addr(0), image_base(0), no_reloc(false), is_dll(false),
		relocs_addr(0), relocs_size(0), resource_addr(0), resource_size(0) {}

	std::string get_sym_name(IMAGE_SYMBOL*sym) {
		if (sym->N.Name.Short) return std::string((char*)sym->N.ShortName,strnlen((char*)sym->N.ShortName,8));
		else {
			DWORD off = sym->N.Name.Long;
			if (off>=strtab_size) xcept("bad strtab offset");
			return &strtab[off];
		}
	}
	void parse_import(const char*buf,size_t size) {
		data=0;
		data_size=0;

		if (size<sizeof(IMPORT_OBJECT_HEADER)) xcept("bad import header");

		IMPORT_OBJECT_HEADER*ih = (IMPORT_OBJECT_HEADER*)buf;

		if (ih->Sig1!=IMAGE_FILE_MACHINE_UNKNOWN || ih->Sig2!=IMPORT_OBJECT_HDR_SIG2) xcept("bad import header");

		if (ih->Machine!=0x8664) xcept("ih->Machine is %x, expected 0x8664",(int)ih->Machine);

		int ordinal = ih->Ordinal;
		int name_type = ih->NameType;

		const char*p = buf+sizeof(IMPORT_OBJECT_HEADER);
		const char*e = buf+size;
		auto getstr = [&]() -> std::string {
			const char*o=p;
			if (p>=e) xcept("bad import header");
			while (*p) {
				p++;
				if (p>=e) xcept("bad import header");
			}
			p++;
			return std::string(o,p-o-1);
		};
		
		std::string name = getstr();
		std::string dll = getstr();

		//printf("import '%s' from '%s' (name_type %d)\n",name.c_str(),dll.c_str(),(int)name_type);

		import_list.push_back(t_import());
		t_import&imp = import_list.back();
		
		imp.dll = dll;
		imp.name = name;
		imp.by_ordinal = false;
		const char*c = name.c_str();
		if (name_type==IMPORT_OBJECT_ORDINAL) {
			imp.by_ordinal = true;
			imp.ordinal = ordinal;
		} else if(name_type==IMPORT_OBJECT_NAME) {
			imp.import = name;
		} else if(name_type==IMPORT_OBJECT_NAME_NO_PREFIX) {
			if (*c=='?'||*c=='@'||*c=='_') c++;
			imp.import = c;
		} else if(name_type==IMPORT_OBJECT_NAME_UNDECORATE) {
			if (*c=='?'||*c=='@'||*c=='_') c++;
			const char*e=c;
			while (*e&&*e!='@') e++;
			imp.import = std::string(c,e-c);;
		}
		else xcept("unknown name type for import (%d)",(int)name_type);

		t_symbol s;
		s.imp = &imp;
		s.obj = this;
		s.name = "__imp_"+name;

		extern_symbols["__imp_" + name] = s;
		extern_symbols["__imp" + name] = s;
//		extern_symbols[name] = s;

		// is the linker really supposed to do this? 
		// if i don't, there will be a bunch of unresolved __imp_ symbols
		t_symbol is;
		is.imp_stub = &imp;
		is.obj = this;
		is.name = name;
// 		extern_symbols["__imp_" + name] = is;
// 		extern_symbols["__imp" + name] = is;
		extern_symbols[name] = is;
	}

	void parse_obj(const char*buf,size_t size) {

		auto test = [&](size_t offset,size_t len) {
			if (size<offset+len) xcept("bad object file (attempt to read %d bytes at offset %d, size %d)",(int)len,(int)offset,(int)size);
		};

		data = buf;
		data_size = size;

		test(0,4);
		if (((WORD*)buf)[0]==IMAGE_FILE_MACHINE_UNKNOWN && ((WORD*)buf)[1]==IMPORT_OBJECT_HDR_SIG2) {
			parse_import(buf,size);
			return;
		}

		test(0,sizeof(IMAGE_FILE_HEADER));
		IMAGE_FILE_HEADER*fh = (IMAGE_FILE_HEADER*)&buf[0];

		if (fh->Machine==0) {
			//printf("hmm, old import library format?\n");
		} else if (fh->Machine != 0x8664) xcept("fh->Machine is %x, expected 0x8664",(int)fh->Machine);

		no_reloc = fh->Characteristics&IMAGE_FILE_RELOCS_STRIPPED ? true : false;
		
		size_t spos = sizeof(IMAGE_FILE_HEADER) + fh->SizeOfOptionalHeader;
		test(spos,sizeof(IMAGE_SECTION_HEADER)*fh->NumberOfSections);
		IMAGE_SECTION_HEADER*sh = (IMAGE_SECTION_HEADER*)&buf[spos];

		for (size_t i=0;i<fh->NumberOfSections;i++) {
			//printf("section %d: vaddr %p, SizeOfRawData %d, PointerToRawData %d, Characteristics %x\n",(int)i,(void*)sh[i].VirtualAddress,(int)sh[i].SizeOfRawData,(int)sh[i].PointerToRawData,(int)sh[i].Characteristics);
			sections.push_back(&sh[i]);
		}

		if (fh->PointerToSymbolTable) {
			test(fh->PointerToSymbolTable,fh->NumberOfSymbols*sizeof(IMAGE_SYMBOL));
			test(fh->PointerToSymbolTable+fh->NumberOfSymbols*sizeof(IMAGE_SYMBOL),4);
			strtab = (char*)&buf[fh->PointerToSymbolTable+fh->NumberOfSymbols*sizeof(IMAGE_SYMBOL)];
			strtab_size = *(DWORD*)strtab;
			test(fh->PointerToSymbolTable+fh->NumberOfSymbols*sizeof(IMAGE_SYMBOL),strtab_size);
		} else {
			strtab = 0;
			strtab_size = 0;
		}

		IMAGE_SYMBOL*sym = (IMAGE_SYMBOL*)&buf[fh->PointerToSymbolTable];
		symtab = sym;
		symtab_size = fh->NumberOfSymbols;
		for (size_t i=0;i<fh->NumberOfSymbols;i++) {
			std::string name = get_sym_name(&sym[i]);
			t_symbol s;
			s.name = name;
			s.obj = this;
			s.sym = &sym[i];
			s.external = sym[i].StorageClass==IMAGE_SYM_CLASS_EXTERNAL;
			s.undefined = sym[i].SectionNumber==IMAGE_SYM_UNDEFINED;

			if (name==".CRT$XCU") {
				size_t sn = sym[i].SectionNumber-1;
				if (sn>=sections.size()) xcept("bad section for %s",name.c_str());
				xcu_sections.push_back(std::make_pair(this,sn));
			}
			if (name==".CRT$XIU") {
				size_t sn = sym[i].SectionNumber-1;
				if (sn>=sections.size()) xcept("bad section for %s",name.c_str());
				xcu_sections.push_back(std::make_pair(this,sn));
			}
			if (name==".CRT$XIA" || name==".CRT$XIZ" || name==".CRT$XCA" || name==".CRT$XCZ") {
				extern_symbols[name] = s;
			}

			if ((sym[i].StorageClass==IMAGE_SYM_CLASS_EXTERNAL/* || sym[i].StorageClass==IMAGE_SYM_CLASS_WEAK_EXTERNAL*/) && sym[i].SectionNumber>0) {
				if (extern_symbols.find(name)!=extern_symbols.end()) xcept("duplicate symbol %s in object file",name.c_str());
				extern_symbols[name] = s;
			}
			if ((sym[i].StorageClass==IMAGE_SYM_CLASS_EXTERNAL/* || sym[i].StorageClass==IMAGE_SYM_CLASS_WEAK_EXTERNAL*/) && sym[i].SectionNumber==IMAGE_SYM_UNDEFINED) {
				//if (sym[i].StorageClass!=IMAGE_SYM_CLASS_EXTERNAL) xcept("symbol %s is undefined but not external",name.c_str());
				if (undefined_symbols.find(name)!=undefined_symbols.end()) {
					//printf("duplicate (undefined) symbol %s in object file\n",name.c_str());
				} else undefined_symbols[name] = s;
			}

			i += sym[i].NumberOfAuxSymbols;
		}

	}

	struct t_section {
		uint64_t vaddr;
		uint64_t filesz;
		uint64_t memsz;
		char*data;
	};

	std::vector<t_section> out_sections;

	bool test_overlapping_section(uint64_t vaddr,uint64_t memsz) {
		for (size_t i=0;i<out_sections.size();i++) {
			if ((out_sections[i].vaddr>=vaddr&&out_sections[i].vaddr<vaddr+memsz) || (vaddr>=out_sections[i].vaddr&&vaddr<out_sections[i].vaddr+out_sections[i].memsz)) return true;
		}
		return false;
	}

	void add_section(uint64_t vaddr,uint64_t filesz,uint64_t memsz,char*data) {
		if (test_overlapping_section(vaddr,memsz)) xcept("overlapping sections");
		t_section s;
		s.vaddr = vaddr;
		s.filesz = filesz;
		s.memsz = memsz;
		s.data = data;
		out_sections.push_back(s);

		std::sort(out_sections.begin(),out_sections.end(),[&](const t_section&a,const t_section&b) {
			return a.vaddr<b.vaddr;
		});
	}

	std::vector<IMAGE_SYMBOL> added_symbols;
	void add_symbol(const IMAGE_SYMBOL&sym) {
		added_symbols.push_back(sym);
	}

	void dump_obj(const std::function<void(const void*,size_t)>&put,const std::function<void(size_t)>&seek,const std::function<size_t()>&pos) {

		IMAGE_FILE_HEADER fh;
		memset(&fh,0,sizeof(fh));
		fh.Characteristics = 0;
		fh.Machine = 0x8664;
		fh.SizeOfOptionalHeader = 0;
		fh.Characteristics = no_reloc ? IMAGE_FILE_RELOCS_STRIPPED : 0;

		fh.NumberOfSections = (WORD)out_sections.size();

		size_t fh_pos = pos();
		put(&fh,sizeof(fh));

		size_t sh_pos = pos();
		for (size_t i=0;i<out_sections.size();i++) {
			IMAGE_SECTION_HEADER sh;
			memset(&sh,0,sizeof(sh));

			put(&sh,sizeof(sh));
		}


		std::map<size_t,size_t> section_addr_map;
		for (size_t i=0;i<out_sections.size();i++) {
			t_section&s = out_sections[i];
			if (s.filesz) {
				section_addr_map[i] = pos();
				put(s.data,s.filesz);
			} else {
				section_addr_map[i] = 0;
			}
		}

		size_t symtab_pos = pos();
		for (size_t i=0;i<added_symbols.size();i++) {
			put(&added_symbols[i],sizeof(IMAGE_SYMBOL));
		}
		DWORD strtab_size = 0;
		put(&strtab_size,4);

		seek(fh_pos);
		fh.NumberOfSymbols = (DWORD)added_symbols.size();
		fh.PointerToSymbolTable = (DWORD)symtab_pos;
		put(&fh,sizeof(fh));

		seek(sh_pos);
		for (size_t i=0;i<out_sections.size();i++) {
			IMAGE_SECTION_HEADER sh;
			memset(&sh,0,sizeof(sh));
			t_section&s = out_sections[i];
			sh.VirtualAddress = (DWORD)(s.vaddr); // note: object files should always have 0 in VirtualAddress and Misc.VirtualSize,
			sh.SizeOfRawData = (DWORD)s.filesz;   //       so this doesn't actually generate a valid object file (but we handle it specially when linking)
			sh.Misc.VirtualSize = (DWORD)s.memsz;
			sh.Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
			sh.PointerToRawData = (DWORD)section_addr_map[i];
			//memcpy(sh.Name,".text",6);

			put(&sh,sizeof(sh));
		}
	}

	void make_obj_to_str(std::string&outs) {
		size_t cpos=0;
		auto put = [&](const void*data,size_t len) {
			if (cpos+len>outs.size()) outs.resize(cpos+len);
			memcpy(&outs[cpos],data,len);
			cpos+=len;
		};
		auto seek = [&](size_t offset) {
			cpos = offset;
		};
		auto pos = [&]() -> size_t {
			return cpos;
		};
		dump_obj(put,seek,pos);
	}

	void make_obj(const char*fn) {
		FILE*f = fopen(fn,"wb");
		if (!f) xcept("failed to open %s for writing\n",fn);

		auto put = [&](const void*data,size_t len) {
			if (fwrite(data,1,len,f)!=len) xcept("failed to write %d bytes to %s\n",len,fn);
		};
		auto seek = [&](size_t offset) {
			if (fseek(f,(int)offset,SEEK_SET)) xcept("seek to %d failed\n",(int)offset);
		};
		auto pos = [&]() -> size_t {
			return ftell(f);
		};
		dump_obj(put,seek,pos);

		fclose(f);
	}

	void make_exe(const char*fn) {

		FILE*f = fopen(fn,"wb");
		if (!f) xcept("failed to open %s for writing\n",fn);
		
		auto put = [&](const void*data,size_t len) {
			if (fwrite(data,1,len,f)!=len) xcept("failed to write %d bytes to %s\n",len,fn);
		};
		auto seek = [&](size_t offset) {
			if (fseek(f,(int)offset,SEEK_SET)) xcept("seek to %d failed\n",(int)offset);
		};
		auto pos = [&]() -> size_t {
			return ftell(f);
		};
		IMAGE_DOS_HEADER dos;
		memset(&dos,0,sizeof(dos));
		dos.e_magic = 0x5a4d;
		dos.e_lfanew = sizeof(IMAGE_DOS_HEADER);
		put(&dos,sizeof(IMAGE_DOS_HEADER));

		DWORD sig = 0x00004550;
		put(&sig,4);
		IMAGE_FILE_HEADER fh;
		memset(&fh,0,sizeof(fh));
		fh.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE;
		if (no_reloc) fh.Characteristics |= IMAGE_FILE_RELOCS_STRIPPED;
		if (is_dll) fh.Characteristics |= IMAGE_FILE_DLL;
		fh.Machine = 0x8664;
		fh.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);

		IMAGE_OPTIONAL_HEADER64 oh;
		memset(&oh,0,sizeof(oh));
		oh.Magic = IMAGE_NT_OPTIONAL_HDR_MAGIC;
		oh.ImageBase = 0x0;
		oh.FileAlignment = (DWORD)file_alignment;
		oh.SectionAlignment = (DWORD)section_alignment;
		oh.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
		oh.SizeOfStackReserve = 0x100000;
		oh.SizeOfStackCommit = 0x1000;
		oh.SizeOfHeapReserve = 0x100000;
		oh.SizeOfHeapCommit = 0x1000;
		oh.NumberOfRvaAndSizes = 0x10;
		oh.MajorOperatingSystemVersion = 5;
		oh.MinorOperatingSystemVersion = 2;
		oh.MajorSubsystemVersion = 5;
		oh.MinorSubsystemVersion = 2;

		auto section_align = [&](size_t addr) -> size_t {
			return ((addr-1)&~(oh.SectionAlignment-1))+oh.SectionAlignment;
		};

		size_t first_section_addr = 0x1000;

		//uint64_t image_base = (out_sections[0].vaddr - first_section_addr)&~oh.SectionAlignment;
		//uint64_t image_base = 0x10000;
		// image_base needs to be a multiple of 64k (and not 0)
		// unfortunately, the first section is usually at 64k too
		// however, the data at this address is usually the elf header and translated code, so we can safely remove it
		if (out_sections[0].vaddr==image_base) {
			t_section&s = out_sections[0];
			s.vaddr += oh.SectionAlignment;
			if (s.memsz>=oh.SectionAlignment) s.memsz -= oh.SectionAlignment;
			else s.memsz=0;
			if (s.filesz>=oh.SectionAlignment) s.filesz -= oh.SectionAlignment;
			else s.filesz=0;
			s.data += oh.SectionAlignment;
		}

		if (out_sections[0].vaddr<image_base) xcept("out_sections[0].vaddr==image_base");

		first_section_addr += image_base;

		oh.ImageBase = image_base;

		printf("image_base is %x\n",(int)image_base);

		oh.AddressOfEntryPoint = (DWORD)(entry_addr - image_base);

		oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = imports_addr ? (DWORD)(imports_addr - image_base) : 0;
		oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = (DWORD)imports_size;
		oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress = exports_addr ? (DWORD)(exports_addr - image_base) : 0;
		oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size = (DWORD)exports_size;
		oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress = relocs_addr ? (DWORD)(relocs_addr - image_base) : 0;
		oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size = (DWORD)relocs_size;
		oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress = resource_addr ? (DWORD)(resource_addr - image_base) : 0;
		oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].Size = (DWORD)resource_size;

		fh.NumberOfSections = (WORD)out_sections.size();
		oh.SizeOfHeaders = (((sizeof(IMAGE_DOS_HEADER)+4+sizeof(IMAGE_FILE_HEADER)+sizeof(IMAGE_OPTIONAL_HEADER64)+out_sections.size()*sizeof(IMAGE_SECTION_HEADER))-1)&~(oh.FileAlignment-1))+oh.FileAlignment;

		put(&fh,sizeof(fh));
		size_t oh_pos = pos();
		put(&oh,sizeof(IMAGE_OPTIONAL_HEADER64));

		for (size_t i=0;i<out_sections.size();i++) {
			IMAGE_SECTION_HEADER sh;
			memset(&sh,0,sizeof(sh));

			put(&sh,sizeof(sh));
		}

		size_t data_begin = 0;
		size_t data_end = out_sections.back().vaddr + out_sections.back().memsz - image_base;

		std::map<size_t,size_t> section_addr_map;
		for (size_t i=0;i<out_sections.size();i++) {
			t_section&s = out_sections[i];
			if (s.filesz) {
				while (pos()%oh.FileAlignment) {
					char c=0;
					put(&c,1);
				}
				section_addr_map[i] = pos();
				put(s.data,s.filesz);
			} else {
				section_addr_map[i] = 0;
			}
		}

		while (pos()%oh.FileAlignment) {
			char c=0;
			put(&c,1);
		}

// 		oh.BaseOfCode = (DWORD)data_begin;
// 		oh.SizeOfCode = (DWORD)(data_end-data_begin);
// 		oh.SizeOfInitializedData = 0;
// 		//oh.BaseOfData = data_begin;
// 		oh.SizeOfUninitializedData = 0;
		oh.SizeOfImage = (DWORD)(data_end-data_begin);

		seek(oh_pos);
		put(&oh,sizeof(IMAGE_OPTIONAL_HEADER64));
		for (size_t i=0;i<out_sections.size();i++) {
			IMAGE_SECTION_HEADER sh;
			memset(&sh,0,sizeof(sh));
			t_section&s = out_sections[i];
			sh.VirtualAddress = (DWORD)(s.vaddr - image_base);
			sh.SizeOfRawData = (DWORD)s.filesz;
			sh.Misc.VirtualSize = (DWORD)s.memsz;
			sh.Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
			sh.PointerToRawData = (DWORD)section_addr_map[i];
			//memcpy(sh.Name,".text",6);

			put(&sh,sizeof(sh));
		}
		
		fclose(f);

	}

	uint64_t find_symbol_addr(const char*name) {
		auto i = extern_symbols.find(name);
		if (i==extern_symbols.end()) return 0;
		t_symbol&s = i->second;
		if (!s.sym) return 0;
		uint64_t section_addr = obj_section_addr[s.obj][(size_t)s.sym->SectionNumber-1];
		return section_addr + s.sym->Value;
	}

	uint64_t get_vaddr(size_t size,size_t align) {
		uint64_t addr = addr = image_base + 0x1000;
		for (size_t i=0;i<out_sections.size();i++) {
			t_section&s = out_sections[i];

			if (s.vaddr>=addr+size) {
				//if (test_overlapping_section(addr,size)) xcept("get_vaddr returning overlapping section at %p, size %x",(void*)addr,(int)size);
				return addr;
			}
			
			addr = ((s.vaddr+s.memsz-1)&~(align-1))+align;
		}
		return addr;
	}

	std::vector<std::vector<char>> mapped_buf;
	std::map<pe*,std::map<size_t,uint64_t>> obj_section_addr;

	size_t get_section_n_for_addr(uint64_t addr) {
		for (size_t i=0;i<out_sections.size();i++) {
			t_section&s = out_sections[i];
			if (addr>=s.vaddr&&addr<s.vaddr+s.memsz) return i;
		}
		xcept("no section for address %p",(void*)addr);
		return -1;
	}

	void add_symbols(pe&obj) {
		for (auto i=obj.extern_symbols.begin();i!=obj.extern_symbols.end();++i) {
			const std::string&name = i->first;
			t_symbol&s = i->second;
			if (extern_symbols.find(name)!=extern_symbols.end()) {
				//printf("duplicate symbol %s\n",name.c_str());
			} else extern_symbols[name] = s;
		}
	}

	void add_shared_sections(pe&obj) {
		for (auto i=obj.xcu_sections.begin();i!=obj.xcu_sections.end();++i) {
			xcu_sections.push_back(*i);
		}
		for (auto i=obj.xiu_sections.begin();i!=obj.xiu_sections.end();++i) {
			xiu_sections.push_back(*i);
		}
	}

	std::pair<pe*,size_t> get_symbol_obj_and_section(const char*name) {
		auto i = extern_symbols.find(name);
		if (i==extern_symbols.end()) xcept("no such symbol: %s",name);
		pe*obj = i->second.obj;
		IMAGE_SYMBOL*sym = i->second.sym;
		if (!obj || !sym) xcept("null object or sym for symbol %s",name);
		size_t sn = sym->SectionNumber-1;
		if (sn>=obj->sections.size()) xcept("bad SectionNumber for symbol %s",name);
		return std::make_pair(obj,sn);
	}

	void mk_shared_sections() {

		auto merge = [&](std::pair<pe*,size_t>pre,std::vector<std::pair<pe*,size_t>>&list,std::pair<pe*,size_t>post) {

			size_t total_size = 0;

			total_size += pre.first->sections[pre.second]->SizeOfRawData;
			for (auto i=list.begin();i!=list.end();++i) {
				total_size += i->first->sections[i->second]->SizeOfRawData;
			}
			total_size += post.first->sections[post.second]->SizeOfRawData;

			uint64_t addr = get_vaddr(total_size,16);

			//printf("merging 2+%d sections addr %x size %x\n",(int)list.size(),(int)addr,(int)total_size);

			size_t idx = 0;

			obj_section_addr[pre.first][pre.second] = addr+idx;
			add_section(addr+idx,0,pre.first->sections[pre.second]->SizeOfRawData,0);
			idx += pre.first->sections[pre.second]->SizeOfRawData;
			for (auto i=list.begin();i!=list.end();++i) {
				obj_section_addr[i->first][i->second] = addr+idx;
				add_section(addr+idx,0,i->first->sections[i->second]->SizeOfRawData,0);
				idx += i->first->sections[i->second]->SizeOfRawData;
			}
			obj_section_addr[post.first][post.second] = addr+idx;
			add_section(addr+idx,0,post.first->sections[post.second]->SizeOfRawData,0);
			idx += post.first->sections[post.second]->SizeOfRawData;

		};

		auto i = extern_symbols.find(".CRT$XIA");
		if (i!=extern_symbols.end()) {
			auto xia = get_symbol_obj_and_section(".CRT$XIA");
			auto xiz = get_symbol_obj_and_section(".CRT$XIZ");
			merge(xia,xiu_sections,xiz);
		}
		i = extern_symbols.find(".CRT$XCA");
		if (i!=extern_symbols.end()) {
			auto xca = get_symbol_obj_and_section(".CRT$XCA");
			auto xcz = get_symbol_obj_and_section(".CRT$XCZ");
			merge(xca,xcu_sections,xcz);
		}

	}

	void add_sections(pe&obj) {
		uint64_t base = -1;
		for (size_t i=0;i<obj.sections.size();i++) {
			IMAGE_SECTION_HEADER*sh = obj.sections[i];
			//size_t size = sh->Misc.VirtualSize;
			size_t size = sh->SizeOfRawData;

			if (!size) {
				size=1;
			}

			//printf("sh->VirtualAddress is %x, size is %x\n",(int)sh->VirtualAddress,(int)size);

			//if (sh->Characteristics&IMAGE_SCN_CNT_UNINITIALIZED_DATA) xcept("uninitialized data? :o\n");

			size_t align = 4;

			if (sh->Characteristics&IMAGE_SCN_ALIGN_8192BYTES) align=8192;
			if (sh->Characteristics&IMAGE_SCN_ALIGN_4096BYTES) align=4096;
			if (sh->Characteristics&IMAGE_SCN_ALIGN_2048BYTES) align=2048;
			if (sh->Characteristics&IMAGE_SCN_ALIGN_1024BYTES) align=1024;
			if (sh->Characteristics&IMAGE_SCN_ALIGN_512BYTES) align=512;
			if (sh->Characteristics&IMAGE_SCN_ALIGN_256BYTES) align=256;
			if (sh->Characteristics&IMAGE_SCN_ALIGN_128BYTES) align=128;
			if (sh->Characteristics&IMAGE_SCN_ALIGN_64BYTES) align=64;
			if (sh->Characteristics&IMAGE_SCN_ALIGN_32BYTES) align=32;
			if (sh->Characteristics&IMAGE_SCN_ALIGN_16BYTES) align=16;
			if (sh->Characteristics&IMAGE_SCN_ALIGN_8BYTES) align=8;
			if (sh->Characteristics&IMAGE_SCN_ALIGN_4BYTES) align=4;
			if (sh->Characteristics&IMAGE_SCN_ALIGN_2BYTES) align=2;
			if (sh->Characteristics&IMAGE_SCN_ALIGN_1BYTES) align=1;

			uint64_t addr = obj_section_addr[&obj][i];
			
			if (!addr) {
				if (sh->VirtualAddress || obj.no_reloc) {
					if (base==-1) base = get_vaddr(size,align) - sh->VirtualAddress;
					addr = (obj.no_reloc ? 0 : base) + sh->VirtualAddress;
					add_section(addr,0,sh->Misc.VirtualSize,0);
				} else {
					addr = get_vaddr(size,align);
					add_section(addr,0,size,0);
					base = addr;
				}
			}
			// else printf("section already mapped\n");

			//printf("addr is %x, size is %x\n",addr,size);

			obj_section_addr[&obj][i] = addr;

			size_t filesz = sh->SizeOfRawData;

			char*data=0;
			if (!sh->PointerToRawData) { // assume uninitialized data. would it be more correct to check for sh->Characteristics&IMAGE_SCN_CNT_UNINITIALIZED_DATA ?
				filesz = 0;
			} else {
				mapped_buf.push_back(std::vector<char>());
				std::vector<char>&buf = mapped_buf.back();

				buf.resize(sh->SizeOfRawData);

				data = &buf[0];
				if (obj.data_size<sh->PointerToRawData+sh->SizeOfRawData) xcept("%s: section data outside data",obj.name.c_str());
				memcpy(data,&obj.data[sh->PointerToRawData],sh->SizeOfRawData);
			}
			
			t_section&s = out_sections[get_section_n_for_addr(addr)];
			if (s.vaddr!=addr) xcept("s.vaddr(%x)!=addr(%x)",(int)s.vaddr,(int)addr);
			s.filesz = filesz;
			s.data = data;
		}
	}

	std::vector<std::vector<char>> data_holders;

	std::vector<char> import_pointers_data;
	std::vector<char> import_descriptors_data;
	std::vector<char> import_strings_data;

	std::list<uint64_t> stub_holders;

	void mk_import_section() {

		std::map<t_import*,std::pair<uint64_t,char*>> imp_stub_map;
		std::map<std::string,std::set<t_import*>> imports;

		size_t import_count=0;
		for (auto i=extern_symbols.begin();i!=extern_symbols.end();++i) {
			t_symbol&s = i->second;
			if (s.imp) {
				imports[s.imp->dll].insert(s.imp);
			} else if (s.imp_stub) {
				auto&i = imp_stub_map[s.imp_stub];
				char*&d = i.second;
				if (!d) {
					stub_holders.push_back(uint64_t());
					d = (char*)&stub_holders.back();
					i.first = get_vaddr(6,4);
					add_section(i.first,6,6,(char*)d);
				}
				*(uint16_t*)d = 0x25ff;
				s.addr = i.first;
				//printf("import stub for %s is at %p\n",s.name.c_str(),s.addr);
			}
			import_count++;
		}
		for (auto i=imports.begin();i!=imports.end();++i) {
			auto&l = i->second;
			for (size_t i=0;i<l.size();i++) {
				import_count++;
			}
			import_count++;
		}
		import_pointers_data.resize(import_count*8);
		uint64_t imp_p_addr = get_vaddr(import_pointers_data.size(),8);
		add_section(imp_p_addr,import_pointers_data.size(),import_pointers_data.size(),&import_pointers_data[0]);
		uint64_t imp_p_addr_orig = get_vaddr(import_pointers_data.size(),8);
		add_section(imp_p_addr_orig,import_pointers_data.size(),import_pointers_data.size(),&import_pointers_data[0]);

		uint64_t p = imp_p_addr;
		for (auto i=imports.begin();i!=imports.end();++i) {
			auto&l = i->second;
			for (auto i=l.begin();i!=l.end();++i) {
				t_import*imp = *i;
				imp->addr = p;

				char*c = imp_stub_map[imp].second;
				if (c) {
					*(uint32_t*)(c+2) = (uint32_t)(p - imp_stub_map[imp].first-6);
				}

				p+=8;
			}
			p+=8;
		}

		auto put_data = [&](std::vector<char>&buf,const void*p,size_t len) -> size_t {
			size_t s = buf.size();
			buf.resize(s+len);
			memcpy(&buf[s],p,len);
			return s;
		};

		auto addstr = [&](const char*str) -> size_t {
			return put_data(import_strings_data,str,strlen(str)+1);
		};

		for (auto i=imports.begin();i!=imports.end();++i) {
			addstr(i->first.c_str());
			auto&l = i->second;
			for (auto i=l.begin();i!=l.end();++i) {
				if (!(*i)->by_ordinal) {
					uint16_t d;
					put_data(import_strings_data,&d,2);
					addstr((*i)->import.c_str());
				}
			}
		}
		uint64_t imp_s_addr = get_vaddr(import_strings_data.size(),1);
		add_section(imp_s_addr,import_strings_data.size(),import_strings_data.size(),0);

		import_strings_data.resize(0);

		uint64_t p_idx = 0;
		for (auto i=imports.begin();i!=imports.end();++i) {
			uint32_t dll_name_addr = (uint32_t)(imp_s_addr + addstr(i->first.c_str()));
			IMAGE_IMPORT_DESCRIPTOR id;
			memset(&id,0,sizeof(id));
			id.OriginalFirstThunk = (DWORD)(imp_p_addr_orig - image_base + p_idx*8);
			id.FirstThunk = (DWORD)(imp_p_addr - image_base + p_idx*8);
			id.ForwarderChain = -1;
			id.Name = (DWORD)(dll_name_addr - image_base);
			put_data(import_descriptors_data,&id,sizeof(id));
			auto&l = i->second;
			for (auto i=l.begin();i!=l.end();++i) {
				if ((*i)->by_ordinal) {
					*(uint64_t*)&import_pointers_data[p_idx*8] = IMAGE_ORDINAL_FLAG64 | (*i)->ordinal;
				} else {
					uint16_t d = (*i)->ordinal;
					put_data(import_strings_data,&d,2);
					uint32_t name_addr = (uint32_t)(imp_s_addr + addstr((*i)->import.c_str()));
					*(uint64_t*)&import_pointers_data[p_idx*8] = name_addr-2 - image_base;
				}
				p_idx++;
			}
			*(uint64_t*)&import_pointers_data[p_idx*8] = 0;
			p_idx++;
		}
		out_sections[get_section_n_for_addr(imp_s_addr)].data = &import_strings_data[0];

		IMAGE_IMPORT_DESCRIPTOR id;
		memset(&id,0,sizeof(id));
		put_data(import_descriptors_data,&id,sizeof(id));

		imports_size = import_descriptors_data.size();
		imports_addr = get_vaddr(imports_size,16);
		add_section(imports_addr,imports_size,imports_size,&import_descriptors_data[0]);

	}

	std::vector<std::pair<std::string,uint64_t>> export_list;

	void add_export(const std::string&name,uint64_t addr) {
		export_list.push_back(std::make_pair(name,addr));
	}

	std::vector<char> export_pointers_data;
	std::vector<char> export_ordinals_data;
	std::vector<char> export_strings_data, export_string_pointers_data;
	std::vector<char> export_directory_data;

	void mk_export_section() {

		size_t export_count=export_list.size();
		
// 		export_pointers_data.resize(export_count*8);
// 		uint64_t exp_p_addr = get_vaddr(export_pointers_data.size(),8);
// 		add_section(exp_p_addr,export_pointers_data.size(),export_pointers_data.size(),&export_pointers_data[0]);

		//uint64_t p = exp_p_addr;

		auto put_data = [&](std::vector<char>&buf,const void*p,size_t len) -> size_t {
			size_t s = buf.size();
			buf.resize(s+len);
			memcpy(&buf[s],p,len);
			return s;
		};

		auto addstr = [&](const char*str) -> size_t {
			return put_data(export_strings_data,str,strlen(str)+1);
		};
		
		addstr("wee");
		for (size_t i=0;i<export_list.size();i++) {
			addstr(export_list[i].first.c_str());
		}
		uint64_t exp_s_addr = get_vaddr(export_strings_data.size(),1);
		add_section(exp_s_addr,export_strings_data.size(),export_strings_data.size(),0);

		export_strings_data.resize(0);
		addstr("wee");
		for (size_t i=0;i<export_list.size();i++) {
			uint64_t v = (exp_s_addr + addstr(export_list[i].first.c_str())) - image_base;
			put_data(export_string_pointers_data,&v,8);
			v = i+1;
			put_data(export_ordinals_data,&v,2);
			v = export_list[i].second - image_base;
			put_data(export_pointers_data,&v,8);
		}
		out_sections[get_section_n_for_addr(exp_s_addr)].data = &export_strings_data[0];

		uint64_t exp_o_addr = get_vaddr(export_ordinals_data.size(),1);
		add_section(exp_o_addr,export_ordinals_data.size(),export_ordinals_data.size(),&export_ordinals_data[0]);
		uint64_t exp_p_addr = get_vaddr(export_pointers_data.size(),1);
		add_section(exp_p_addr,export_pointers_data.size(),export_pointers_data.size(),&export_pointers_data[0]);
		uint64_t exp_sp_addr = get_vaddr(export_string_pointers_data.size(),1);
		add_section(exp_sp_addr,export_string_pointers_data.size(),export_string_pointers_data.size(),&export_string_pointers_data[0]);

		IMAGE_EXPORT_DIRECTORY ed;
		memset(&ed,0,sizeof(ed));

		ed.Base = 1;
		ed.Name = (DWORD)(exp_s_addr - image_base);
		ed.NumberOfFunctions = (DWORD)export_count;
		ed.NumberOfNames = (DWORD)export_count;
		ed.AddressOfFunctions = (DWORD)(exp_p_addr - image_base);
		ed.AddressOfNameOrdinals = (DWORD)(exp_o_addr - image_base);
		ed.AddressOfNames = (DWORD)(exp_sp_addr - image_base);

		put_data(export_directory_data,&ed,sizeof(ed));

		exports_size = export_directory_data.size();
		exports_addr = get_vaddr(exports_size,16);
		add_section(exports_addr,exports_size,exports_size,&export_directory_data[0]);

	}

	void merge_sections() {

		auto section_align = [&](size_t addr) -> size_t {
			return ((addr-1)&~(section_alignment-1))+section_alignment;
		};
		std::vector<t_section> new_sections;

		size_t first_section_addr = image_base + 0x1000;

		uint64_t addr = first_section_addr;
		for (size_t i=0;i<out_sections.size();i++) {
			t_section&s = out_sections[i];
			if (s.vaddr>addr) {
				t_section ns;
				ns.vaddr = addr;
				uint64_t to = s.vaddr;
				if ((to&~(section_alignment-1))>addr) to = to&~(section_alignment-1);
				ns.memsz = to-addr;
				ns.filesz = 0;
				ns.data = 0;
				out_sections.insert(out_sections.begin()+i,ns);
				//printf("added filler from %x to %x\n",(int)ns.vaddr,(int)(ns.vaddr+ns.memsz));
				addr = ns.vaddr+ns.memsz;
				continue;
			}
			addr = s.vaddr+s.memsz;
		}
		for (size_t i=0;i<out_sections.size();i++) {
			t_section&s = out_sections[i];
			//printf("section %d: vaddr %x filesz %x memsz %x\n",(int)i,(int)s.vaddr,(int)s.filesz,(int)s.memsz);
		}

		addr = first_section_addr;

		size_t n = 0;

		auto merge = [&](size_t from,size_t to) {
			//printf("merge sections %d to %d\n",(int)from,(int)to);
			if (from==to) {
				new_sections.push_back(out_sections[from]);
				return;
			}
			data_holders.push_back(std::vector<char>());
			std::vector<char>&buf = data_holders.back();
			size_t space = 0;
			size_t filesz = 0;
			size_t memsz = 0;
			size_t addr = out_sections[from].vaddr;
			for (size_t i=from;i<=to;i++) {
				t_section&s = out_sections[i];
				if (s.vaddr!=addr) xcept("merge sections not consecutive! (s.vaddr is %x, expected %x)",(int)s.vaddr,(int)addr);
				if (s.filesz>s.memsz) xcept("s.filesz>s.memsz!");
				addr = s.vaddr + s.memsz;
				if (s.filesz) {
					if (space) {
						buf.resize(buf.size()+space);
						space=0;
					}
					size_t p = buf.size();
					buf.resize(buf.size()+s.filesz);
					memcpy(&buf[p],s.data,s.filesz);
				}
				space += s.memsz - s.filesz;
				memsz += s.memsz;
			}
			filesz = buf.size();
			t_section ns;
			ns.data = &buf[0];
			ns.filesz = filesz;
			ns.memsz = memsz;
			ns.vaddr = out_sections[from].vaddr;
			new_sections.push_back(ns);
		};

		for (size_t i=0;i<out_sections.size();i++) {
			t_section&s = out_sections[i];
			if (section_align(s.vaddr+s.memsz)==s.vaddr+s.memsz || i==out_sections.size()-1) {
				merge(n,i);
				n = i+1;
			}
		}

		out_sections = new_sections;
	}

	std::vector<char> base_relocs_data;

	void mk_base_relocs() {
		if (base_relocs.empty()) return;
		uint64_t va = 0;
		std::vector<std::pair<uint64_t,int>> buf;
		auto put = [&](const void*p,size_t len) {
			size_t pos = base_relocs_data.size();
			base_relocs_data.resize(pos+len);
			memcpy(&base_relocs_data[pos],p,len);
		};
		auto flush = [&]() {
			//printf("flush %d relocs at %p\n",(int)buf.size(),(void*)va);
			IMAGE_BASE_RELOCATION br;
			br.SizeOfBlock = (DWORD)(buf.size()*2 + sizeof(br));
			br.VirtualAddress = (DWORD)(va-image_base);
			put(&br,sizeof(br));
			for (size_t i=0;i<buf.size();i++) {
				if (buf[i].first - va>=0x1000) xcept("buf[i].first - va>=0x1000");
				WORD w = (WORD)(buf[i].first - va);
				int bits = buf[i].second;
				if (bits==32) w |= IMAGE_REL_BASED_HIGHLOW<<12;
				else if (bits==64) w |= IMAGE_REL_BASED_DIR64<<12;
				else xcept("relocs: bad bits %d",bits);
				put(&w,2);
			}
			buf.clear();
			va = 0;
		};
		for (auto i=base_relocs.begin();i!=base_relocs.end();++i) {
			uint64_t addr = i->first;
			int bits = i->second;
			if (va==0) va = addr;
			if (addr-va>=0x1000) {
				flush();
				va = addr;
			}
			buf.push_back(std::make_pair(addr,bits));
		}
		flush();
		size_t align = section_alignment;
		relocs_addr = ((out_sections[out_sections.size()-1].vaddr+out_sections[out_sections.size()-1].memsz-1)&~(align-1))+align;
		relocs_size = base_relocs_data.size();
		add_section(relocs_addr,relocs_size,relocs_size,&base_relocs_data[0]);
	}
	std::vector<char> resource_data;
	void mk_resource() {
		auto put = [&](const void*p,size_t len) {
			size_t pos = resource_data.size();
			resource_data.resize(pos+len);
			memcpy(&resource_data[pos],p,len);
		};

		const char*manifest = ""
			"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\r\n"
			"  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\r\n"
			"    <security>\r\n"
			"      <requestedPrivileges>\r\n"
			"        <requestedExecutionLevel level=\"requireAdministrator\"></requestedExecutionLevel>\r\n"
			"      </requestedPrivileges>\r\n"
			"    </security>\r\n"
			"  </trustInfo>\r\n"
			"</assembly>";

		IMAGE_RESOURCE_DIRECTORY rd;
		memset(&rd,0,sizeof(rd));
		rd.MajorVersion = 4;
		rd.NumberOfIdEntries = 1;
		put(&rd,sizeof(rd));
		IMAGE_RESOURCE_DIRECTORY_ENTRY de;
		memset(&de,0,sizeof(de));
		de.Id = 0x18;
		de.DataIsDirectory = 1;
		de.OffsetToDirectory = (DWORD)resource_data.size()+sizeof(de);
		put(&de,sizeof(de));
		put(&rd,sizeof(rd));
		de.Id = 2;
		de.OffsetToDirectory = (DWORD)resource_data.size()+sizeof(de);
		put(&de,sizeof(de));
		put(&rd,sizeof(rd));
		de.Id = 0x409;
		de.OffsetToData = (DWORD)resource_data.size()+sizeof(de);
		put(&de,sizeof(de));
		size_t e_offset = resource_data.size();
		IMAGE_RESOURCE_DATA_ENTRY e;
		memset(&e,0,sizeof(e));
		e.OffsetToData = 0;
		e.Size = (DWORD)strlen(manifest);
		e.CodePage = 0x4e9;
		put(&e,sizeof(e));
		put(manifest,e.Size);

		resource_size = resource_data.size();
		resource_addr = get_vaddr(resource_size,4);
		add_section(resource_addr,resource_size,resource_size,&resource_data[0]);

		((IMAGE_RESOURCE_DATA_ENTRY&)resource_data[e_offset]).OffsetToData = (DWORD)((resource_addr-image_base)+e_offset+sizeof(e));
	}

	void relocate(pe&obj) {
		//printf("relocate %s\n",obj.name.c_str());
		for (size_t i=0;i<obj.sections.size();i++) {
			IMAGE_SECTION_HEADER*sh = obj.sections[i];

			uint64_t data_addr = obj_section_addr[&obj][i];
			uint64_t data_section_n = get_section_n_for_addr(data_addr);
			t_section&s = out_sections[data_section_n];
			if (data_addr-s.vaddr+sh->SizeOfRawData>s.memsz) xcept("bad section mapping");
			char*data = &s.data[data_addr-s.vaddr];

			//printf("section is %p, offset %x (%p)\n",(void*)s.vaddr,data_addr-s.vaddr,(void*)data_addr);

			size_t data_size = sh->SizeOfRawData;

			if (sh->PointerToRelocations) {
				IMAGE_RELOCATION*rel = (IMAGE_RELOCATION*)&obj.data[sh->PointerToRelocations];
				size_t reloc_count = sh->NumberOfRelocations;
				if ((short)reloc_count==-1) reloc_count=-1;
				//printf("sh->NumberOfRelocations is %d\n",(int)sh->NumberOfRelocations);
				for (size_t i=0;i<reloc_count;i++) {
					if (obj.data_size<sh->PointerToRelocations+sizeof(IMAGE_RELOCATION)*i) xcept("relocations outside data");
					DWORD addr = rel[i].VirtualAddress;
					WORD type = rel[i].Type;
					DWORD sym_idx = rel[i].SymbolTableIndex;
					if (addr==-1&&reloc_count==-1) break;
					//printf("rel %p %d %d\n",(void*)addr,(int)type,(int)sym_idx);

					uint64_t vaddr = data_addr + addr;

					//printf("vaddr %p\n",(void*)vaddr);

					uint64_t sym_addr = 0;
					uint32_t sym_offset = 0;
					uint32_t sym_section_n = 0;

					if (sym_idx>=obj.symtab_size) xcept("bad symbol index %d\n",(int)sym_idx);
					IMAGE_SYMBOL*sym = &obj.symtab[sym_idx];

					if (sym->StorageClass!=IMAGE_SYM_CLASS_EXTERNAL &&
						sym->StorageClass!=IMAGE_SYM_CLASS_STATIC && 
						sym->StorageClass!=IMAGE_SYM_CLASS_LABEL &&
						sym->StorageClass!=IMAGE_SYM_CLASS_WEAK_EXTERNAL
						) xcept("bad storage class %d for symbol during relocation\n",sym->StorageClass);
					
					pe*sym_obj=0;

					int load_sym_n=0;
load_sym:
					if (load_sym_n++>=100) xcept("load_sym_n overflow");
					if (sym->SectionNumber==IMAGE_SYM_UNDEFINED) {
						std::string n = obj.get_sym_name(sym);
						if (n=="__ImageBase") {
							sym = 0;
							sym_addr = image_base;
							sym_section_n = 0;
							sym_offset = 0;
							//printf("resolved __ImageBase\n");
						} else {
							auto i = extern_symbols.find(n);
							if (i==extern_symbols.end()) {
								if (sym->StorageClass==IMAGE_SYM_CLASS_WEAK_EXTERNAL) {
									//printf("symbol %s, SectionNumber %d, StorageClass %d, Type %d, Value %d, NumberOfAuxSymbols %d\n",n.c_str(),(int)sym[0].SectionNumber,(int)sym[0].StorageClass,(int)sym[0].Type,(int)sym[0].Value,(int)sym[0].NumberOfAuxSymbols);
									IMAGE_AUX_SYMBOL_EX*aux = (IMAGE_AUX_SYMBOL_EX*)(sym+1);
									//printf("search type is %d\n",(int)aux->Sym.WeakSearchType);
									//printf("index is %d\n",(int)aux->Sym.WeakDefaultSymIndex);
									sym_idx = aux->Sym.WeakDefaultSymIndex;
									if (sym_idx>=obj.symtab_size) xcept("bad symbol index %d\n",(int)sym_idx);
									sym = &obj.symtab[sym_idx];
									//printf("symbol %s, SectionNumber %d, StorageClass %d, Type %d, Value %d, NumberOfAuxSymbols %d\n",obj.get_sym_name(sym).c_str(),(int)sym[0].SectionNumber,(int)sym[0].StorageClass,(int)sym[0].Type,(int)sym[0].Value,(int)sym[0].NumberOfAuxSymbols);
									goto load_sym;
								} else {
									printf("unresolved symbol %s (referenced at %p)\n",n.c_str(),(void*)(data_addr+addr));
									continue;
								}
							}
							//printf("resolved symbol %s (referenced at %p)\n",n.c_str(),(void*)(data_addr+addr));

							sym_obj = i->second.obj;
							sym = i->second.sym;

							if (i->second.imp) {
								t_import*imp = i->second.imp;
								sym = 0;

								sym_addr = imp->addr;
								sym_section_n = (uint32_t)get_section_n_for_addr(sym_addr);
								sym_offset = (uint32_t)(sym_addr - out_sections[sym_section_n].vaddr);

								//printf("import %s at %p\n",imp->name.c_str(),sym_addr);
							} else if (i->second.imp_stub) {
								t_import*imp = i->second.imp_stub;
								sym = 0;

								sym_addr = i->second.addr;
								sym_section_n = (uint32_t)get_section_n_for_addr(sym_addr);
								sym_offset = (uint32_t)(sym_addr - out_sections[sym_section_n].vaddr);

								//printf("import stub for %s at %p\n",imp->name.c_str(),sym_addr);
							} else {
								t_symbol&s = i->second;

								//printf("symbol %s, SectionNumber %d, StorageClass %d, Type %d, Value %d, NumberOfAuxSymbols %d\n",s.name.c_str(),(int)sym[0].SectionNumber,(int)sym[0].StorageClass,(int)sym[0].Type,(int)sym[0].Value,(int)sym[0].NumberOfAuxSymbols);
							}
						}
					} else {
						sym_obj = &obj;
					}
					if (sym) {
						if (sym->SectionNumber==IMAGE_SYM_ABSOLUTE) {
							xcept("IMAGE_SYM_ABSOLUTE, value is %p\n",(void*)sym->Value);
							sym_section_n = -1;
							sym_addr = sym->Value;
							sym_offset = sym->Value;
						} else {
							if (sym->SectionNumber<=0) xcept("sym->SectionNumber is %d",(int)sym->SectionNumber);
							size_t sn = (size_t)sym->SectionNumber-1;
							uint64_t section_addr = obj_section_addr[sym_obj][sn];
							sym_section_n = (uint32_t)get_section_n_for_addr(section_addr);
							sym_addr = section_addr + (int32_t)sym->Value;
							sym_offset = sym->Value;
							//printf("sn is %d, sym->Value is %d, section_addr is %x\n",(int)sn,(int)sym->Value,(int)section_addr);
						}
					}

					int dis = 0;
					switch (type) {
						case IMAGE_REL_AMD64_ADDR64:
							if (addr+8>data_size) xcept("relocation address outside data\n");
							*(uint64_t*)&data[addr] += sym_addr;
							base_relocs[vaddr] = 64;
							break;
						case IMAGE_REL_AMD64_ADDR32:
							if (addr+4>data_size) xcept("relocation address outside data\n");
							*(uint32_t*)&data[addr] += (uint32_t)sym_addr;
							base_relocs[vaddr] = 32;
							break;
						case IMAGE_REL_AMD64_ADDR32NB:
							if (addr+4>data_size) xcept("relocation address outside data\n");
							*(uint32_t*)&data[addr] += (uint32_t)(sym_addr - image_base);
							break;
						case IMAGE_REL_AMD64_REL32_5:
							dis++;
						case IMAGE_REL_AMD64_REL32_4:
							dis++;
						case IMAGE_REL_AMD64_REL32_3:
							dis++;
						case IMAGE_REL_AMD64_REL32_2:
							dis++;
						case IMAGE_REL_AMD64_REL32_1:
							dis++;
						case IMAGE_REL_AMD64_REL32:
							if (addr+4>data_size) xcept("relocation address outside data\n");
							*(uint32_t*)&data[addr] += (uint32_t)(sym_addr - vaddr - dis - 4);
							break;
						case IMAGE_REL_AMD64_SECTION:
							if (addr+4>data_size) xcept("relocation address outside data\n");
							*(uint32_t*)&data[addr] += sym_section_n;
							break;
						case IMAGE_REL_AMD64_SECREL:
							if (addr+4>data_size) xcept("relocation address outside data\n");
							*(uint32_t*)&data[addr] += (uint32_t)(sym_addr);
							break;
						default:
							printf("unknown relocation type %d\n",(int)type);
							//xcept("unknown relocation type %d",(int)type);
					}
				}
			}

		}

	}

};

