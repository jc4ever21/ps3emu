
#define _CRT_SECURE_NO_WARNINGS
#define _SCL_SECURE_NO_WARNINGS

#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#include <cstdio>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <list>
#include <stack>
#include <functional>
#include <utility>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

#include "pe.h"
#include "ar.h"

struct linker {

	pe out;
	std::list<pe> obj_list;
	std::list<ar> lib_list;
	std::list<std::string> file_data;
	std::string out_fn, entry_symbol;

	linker()  {
		out.image_base = 0x10000;
		out.is_dll = false;
		out.no_reloc = true;
		entry_symbol = "start";
		out_fn = "out.exe";
	}

	pe&load_obj(const void*data,size_t len,const char*name) {
		obj_list.push_back(pe());
		pe&obj = obj_list.back();
		obj.name = name;
		obj.parse_obj((const char*)data,len);
		bool b=false;
		for (auto i=obj.sections.begin();i!=obj.sections.end();++i) {
			if ((*i)->VirtualAddress) {
				b=true;
				break;
			}
		}
		if (b) out.add_sections(obj);
		return obj;
	}
	pe&load_obj(const char*path) {
		auto load_file = [&](const char*path) -> std::string& {
			FILE*f = fopen(path,"rb");
			if (!f) xcept("failed to open %s",path);
			file_data.push_back(std::string());
			std::string&s = file_data.back();
			fseek(f,0,SEEK_END);
			long fs = ftell(f);
			fseek(f,0,SEEK_SET);
			s.resize(fs);
			if (fread((void*)s.c_str(),fs,1,f)!=1) xcept("failed to read from %s",path);
			fclose(f);
			return s;
		};
		std::string&s = load_file(path);
		return load_obj(s.c_str(),s.size(),path);
	}
	void load_lib(const char*path) {
		lib_list.push_back(ar());
		ar&lib = lib_list.back();
		lib.load_file(path);
		for (auto i=lib.files.begin();i!=lib.files.end();++i) {
			obj_list.push_back(pe());
			pe&obj = obj_list.back();
			auto&f = *i;
			//printf("loading %s\n",f.name.c_str());
			obj.name = f.name;
			obj.parse_obj(f.data,f.size);
		}
	}

	void link() {
		printf("linking...\n");

		load_lib("ps3libs.lib");

		load_lib("msvcrt.lib"); // for memcpy/memset


		std::map<pe*,bool> link_map;
		std::map<std::string,pe::t_symbol> symbol_map;

		for (auto i=obj_list.begin();i!=obj_list.end();++i) {
			pe&obj = *i;
			for (auto i=obj.extern_symbols.begin();i!=obj.extern_symbols.end();++i) {
				const std::string&name = i->first;
				pe::t_symbol&s = i->second;
				{
					auto i = symbol_map.find(name);
					if (i!=symbol_map.end()) {
						if (i->second.sym) {
							size_t sn = i->second.sym->SectionNumber;
							if (sn<i->second.obj->sections.size()) {
								IMAGE_SECTION_HEADER*sh = i->second.obj->sections[sn];
								if (sh->Characteristics&IMAGE_SCN_LNK_COMDAT) {
									//printf("comdat symbol %s\n",name.c_str());
									continue;
								}
							}
						}
						//printf("duplicate defined symbol %s (in %s and %s)\n",name.c_str(),i->second.obj?i->second.obj->name.c_str():"?",s.obj?s.obj->name.c_str():"?");
					} else {
						symbol_map[name] = s;
					}
				}
			}
		}

		int unresolved_count = 0;

		std::function<void(pe*)> pullin = [&](pe*obj) {
			if (link_map[obj]) return;
			link_map[obj] = true;
			//printf("pulled in obj %s\n",obj->name.c_str());
			for (auto i=obj->undefined_symbols.begin();i!=obj->undefined_symbols.end();++i) {
				const std::string&name = i->first;
				if (name=="__ImageBase") continue;
				{
					auto i = symbol_map.find(name);
					if (i==symbol_map.end()) {
						printf("%s: unresolved symbol %s\n",obj->name.c_str(),name.c_str());
						unresolved_count++;
					} else {
						pullin(i->second.obj);
					}
				}
			}
		};

		auto i = symbol_map.find(entry_symbol);
		if (i!=symbol_map.end()) {
			printf("%s is in %s\n",entry_symbol,i->second.obj->name.c_str());

			pullin(i->second.obj);

		} else xcept("%s does not exist",entry_symbol);

		if (unresolved_count) {
			xcept("%d unresolved symbols\n",unresolved_count);
		}
		for (auto i=link_map.begin();i!=link_map.end();++i) {
			out.add_symbols(*i->first);
		}
		for (auto i=link_map.begin();i!=link_map.end();++i) {
			out.add_shared_sections(*i->first);
		}
		out.mk_shared_sections();

		for (auto i=link_map.begin();i!=link_map.end();++i) {
			out.add_sections(*i->first);
		}
		out.mk_import_section();
		out.mk_resource();
		out.merge_sections();

		for (auto i=link_map.begin();i!=link_map.end();++i) {
			out.relocate(*i->first);
		}
		out.mk_base_relocs();

		out.entry_addr = out.find_symbol_addr(entry_symbol.c_str());

		printf("entry_addr is %p\n",(void*)out.entry_addr);

		out.make_exe(out_fn.c_str());

		printf("-> %s\n",out_fn.c_str());
	}
};

