
#include <stdint.h>
#include <stdio.h>
#include <map>
#include <vector>
#include <string>
#include <functional>

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

#define ELF64_R_SYM(i)((i) >> 32)
#define ELF64_R_TYPE(i)((i) & 0xffffffffL)
#define ELF64_R_INFO(s, t)(((s) << 32) + ((t) & 0xffffffffL))

#ifdef __GNUC__
#include <sys/mman.h>
#endif

void*alloc_image(size_t size) {
	void*r = 0;
#ifdef __GNUC__
	static int n = 0;
	r = mmap((void*)(0x20000000+(0x01000000*n++)),size,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_32BIT|MAP_PRIVATE,0,0);
	if (r==MAP_FAILED) {
		printf("mmap failed: ");
		perror(0);
		r=0;
	}
#else
	r = VirtualAlloc((void*)0,(size_t)size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
#endif
	if (!r) xcept("failed to allocate image of size %llx\n",(long long)size);
	return r;
}

struct elf {
	char*data;
	size_t data_size;
	std::vector<char> vdata;
	//std::vector<char> image;
	char*image;
	size_t image_size;
	void*base;
	struct sect {
		uint64_t rvaddr;
		uint64_t memsz;
	};
	std::vector<sect> sections;

	typedef uint64_t elf64_addr;
	typedef uint64_t elf64_off;
	typedef uint16_t elf64_half;
	typedef uint32_t elf64_word;
	typedef int32_t elf64_sword;
	typedef uint64_t elf64_xword;
	typedef int64_t elf64_sxword;

	struct ehdr {
		unsigned char e_ident[16];
		elf64_half e_type;
		elf64_half e_machine;
		elf64_word e_version;
		elf64_addr e_entry;
		elf64_off e_phoff;
		elf64_off e_shoff;
		elf64_word e_flags;
		elf64_half e_ehsize;
		elf64_half e_phentsize;
		elf64_half e_phnum;
		elf64_half e_shentsize;
		elf64_half e_shnum;
		elf64_half e_shstrndx;
	};

	struct shdr {
		elf64_word sh_name;
		elf64_word sh_type;
		elf64_xword sh_flags;
		elf64_addr sh_addr;
		elf64_off sh_offset;
		elf64_xword sh_size;
		elf64_word sh_link;
		elf64_word sh_info;
		elf64_xword addralign;
		elf64_xword sh_entsize;
	};

	struct phdr {
		elf64_word p_type;
		elf64_word p_flags;
		elf64_off p_offset;
		elf64_addr p_vaddr;
		elf64_addr p_paddr;
		elf64_xword p_filesz;
		elf64_xword p_memsz;
		elf64_xword p_align;
	};

	struct rela {
		elf64_addr r_offset;
		elf64_xword r_info;
		elf64_sxword  r_addend;
	};

	uint64_t le_entry;
	bool is_prx;
	uint64_t exec_start, exec_end;
	bool relocated;
	void*prx_mod_info;
	void*process_params,*process_prx_info;
	uint64_t tls_vaddr, tls_filesz, tls_memsz;

	struct imp {
		uint32_t id;
		uint32_t*p;
	};
	struct exp {
		uint32_t id;
		uint32_t p;
	};
	std::map<std::string,std::vector<imp>> imports;
	std::map<std::string,std::vector<exp>> exports;

	std::vector<uint32_t*> gotlist;

	uint64_t reloc_base;

	elf() : data(0), data_size(0), reloc_base(0), is_prx(false), prx_mod_info(0), process_params(0), process_prx_info(0) {}

	size_t get_image_size() {
		if (data_size<sizeof(ehdr)) xcept("not enough data for header");
		ehdr&h = *(ehdr*)&data[0];
		if (h.e_ident[0]!=0x7f||h.e_ident[1]!='E'||h.e_ident[2]!='L'||h.e_ident[3]!='F') xcept("bad magic");
		image_size = 0;

		for (int i=0;i<(int)se(h.e_phnum);i++) {
			phdr&p = *(phdr*)&data[(size_t)se(h.e_phoff) + i*se(h.e_phentsize)];
			uint64_t vaddr = se(p.p_vaddr);

			uint64_t memsz = se(p.p_memsz);
			if (memsz) {
				if (image_size<=(size_t)(vaddr+memsz)) image_size = (size_t)(vaddr+memsz);
			}
		}
		return image_size;
	}

	void parse() {
		if (data_size<sizeof(ehdr)) xcept("not enough data for header");
		ehdr&h = *(ehdr*)&data[0];
		if (h.e_ident[0]=='S'&&h.e_ident[1]=='C'&&h.e_ident[2]=='E'&&h.e_ident[3]==0) {
			unself();
		}
		if (h.e_ident[0]!=0x7f||h.e_ident[1]!='E'||h.e_ident[2]!='L'||h.e_ident[3]!='F') xcept("bad magic");
		printf("EI_CLASS is %d\n",(int)h.e_ident[4]);
		printf("EI_DATA is %d\n",(int)h.e_ident[5]);
		printf("EI_VERSION is %d\n",(int)h.e_ident[6]);
		printf("EI_OSABI is %d\n",(int)h.e_ident[7]);
		printf("EI_ABIVERSION is %d\n",(int)h.e_ident[8]);
		printf("EI_PAD is %d\n",(int)h.e_ident[9]);
		printf("e_type is %d\n",(int)se(h.e_type));
		printf("e_machine is %d\n",(int)se(h.e_machine));
		printf("e_version is %d\n",(int)se(h.e_version));
		printf("e_entry is at %016llX\n",(long long)se(h.e_entry));
		printf("e_shstrndx is %d\n",(int)se(h.e_shstrndx));

		if (h.e_ident[4]!=2 || se(h.e_machine)!=21) xcept("not a ppu elf");

		printf("at %016llx, %d sections of %d bytes\n",(long long)se(h.e_shoff),(int)se(h.e_shnum),(int)se(h.e_shentsize));
		printf("at %016llx, %d program entries of %d bytes\n",(long long)se(h.e_phoff),(int)se(h.e_phnum),(int)se(h.e_phentsize));

		le_entry = se(h.e_entry);

		relocated = false;
		exec_start = -1;
		exec_end = 0;

		base = 0;

		std::map<uint64_t,uint64_t> smap;

		image_size = 0;

		for (int i=0;i<(int)se(h.e_phnum);i++) {
			phdr&p = *(phdr*)&data[(size_t)se(h.e_phoff) + i*se(h.e_phentsize)];
			printf("ph %d: type %08x, flags %08x, offset %llx, vaddr %llx, paddr %llx, filesz %llx, memsz %llx, align %llx\n",i,(int)se(p.p_type),(int)se(p.p_flags),(long long)se(p.p_offset),(long long)se(p.p_vaddr),(long long)se(p.p_paddr),(long long)se(p.p_filesz),(long long)se(p.p_memsz),(long long)se(p.p_align));
			uint64_t vaddr = se(p.p_vaddr);

			uint64_t memsz = se(p.p_memsz);
			if (memsz) {
				if (image_size<=(size_t)(vaddr+memsz)) image_size = (size_t)(vaddr+memsz);
				memsz += vaddr&0xfff;
				vaddr = vaddr&~0xfff;
				if (smap[vaddr]<vaddr+memsz) smap[vaddr] = vaddr+memsz;
			}
			sect s;
			s.rvaddr = vaddr;
			s.memsz = memsz;
			sections.push_back(s);
		}
		printf("image size is %x\n",(int)image_size);
		if (!is_prx && false) {
			for (auto i=smap.begin();i!=smap.end();++i) {
				uint64_t vaddr = i->first;
				uint64_t memsz = i->second-i->first;
				if (memsz==0) return;
#ifdef __GNUC__
				if (vaddr>=0x10000 && (vaddr+memsz)<0x10000+1024*1024*40) continue;
				void*t = mmap((void*)vaddr,(size_t)memsz,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_32BIT|MAP_PRIVATE,0,0);
				if (t!=(void*)vaddr) {
					xcept("mmap %p %d returned %p\n",(void*)vaddr,(int)memsz,t);
				}
				printf("mmap %p %d -> %p\n",(void*)vaddr,(int)memsz,t);
#else
				void*t = VirtualAlloc((void*)vaddr,(size_t)memsz,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
				if (t!=(void*)vaddr) {
					xcept("VirtualAlloc %p %d returned %p\n",(void*)vaddr,(int)memsz,t);
				}
				printf("VirtualAlloc %p %d -> %p\n",(void*)vaddr,(int)memsz,t);
#endif
			}
			image = 0;
		} else {
			image = (char*)alloc_image(image_size);
		}

// 		for (int i=0;i<(int)se(h.e_shnum);i++) {
// 			shdr&s = *(shdr*)&data[(size_t)se(h.e_shoff) + i*se(h.e_shentsize)];
// 			if (!se(s.sh_type)) printf("section %d: unused\n",i);
// 			else printf("section %d: at %016llx, offset %016llx, size %016llx, type %d, flags %016llx\n",i,(long long)se(s.sh_addr),(long long)se(s.sh_offset),(long long)se(s.sh_size),(int)se(s.sh_type),(long long )se(s.sh_flags));
// 			if (se(s.sh_type) && se(s.sh_addr)) {
// 				uint64_t addr = se(s.sh_addr);
// 				uint64_t offset = se(s.sh_offset);
// 				uint64_t size = se(s.sh_size);
// 				uint64_t type = se(s.sh_type);
// 				if (image.size()<=(size_t)(addr+size)) image.resize((size_t)(addr+size));
// 				if (type!=8) {
// 					if (data_size<=(size_t)(offset+size)) xcept("bad section header");
// 					if (type!=8) memcpy(&image[(size_t)addr],&data[(size_t)offset],(size_t)size);
// 				}
// 			}
// 		}
		for (int i=0;i<(int)se(h.e_phnum);i++) {
			phdr&p = *(phdr*)&data[(size_t)se(h.e_phoff) + i*se(h.e_phentsize)];
			//printf("ph %d: type %08x, flags %08x, offset %llx, vaddr %llx, paddr %llx, filesz %llx, memsz %llx, align %llx\n",i,(int)se(p.p_type),(int)se(p.p_flags),(long long)se(p.p_offset),(long long)se(p.p_vaddr),(long long)se(p.p_paddr),(long long)se(p.p_filesz),(long long)se(p.p_memsz),(long long)se(p.p_align));
			uint64_t type = se(p.p_type);
			uint64_t flags = se(p.p_flags);
			uint64_t offset = se(p.p_offset);
			uint64_t vaddr = se(p.p_vaddr);
			uint64_t paddr = se(p.p_paddr);
			uint64_t filesz = se(p.p_filesz);
			uint64_t memsz = se(p.p_memsz);
			if (memsz) {
				if (flags&1) {
					if (vaddr<exec_start) exec_start = vaddr;
					if (vaddr+(memsz<filesz?memsz:filesz)>exec_end) exec_end = vaddr+(memsz<filesz?memsz:filesz);
					printf("exec_start %llx, exec_end %llx\n",(long long)exec_start,(long long)exec_end);
				}
				//if (image.size()<=(size_t)(vaddr+memsz)) image.resize((size_t)(vaddr+memsz));
				if (data_size<(size_t)(offset+filesz)) xcept("bad program header");
				memcpy(&image[(size_t)vaddr],&data[(size_t)offset],(size_t)(memsz>filesz?filesz:memsz));
			}
		}
		printf("le_entry is %llx + %llx\n",(long long)le_entry,(long long)&image[0]);
		le_entry += (uint64_t)&image[0];
		exec_start += (uint64_t)&image[0];
		exec_end += (uint64_t)&image[0];

		tls_vaddr = tls_filesz = tls_memsz = 0;

		uint32_t*first_export = 0;

		auto prx_import = [&](uint64_t b,uint64_t e) {
			printf("import from %llx to %llx\n",(long long)b,(long long)e);
			if (!relocated) {
				b += (uint64_t)&image[0];
				e += (uint64_t)&image[0];
			}
			while (b<e) {
				uint16_t c = se(((uint16_t*)b)[3]);

				char*n = (char*)se(((uint32_t*)b)[4]);
				uint64_t ids = se(((uint32_t*)b)[5]);
				uint64_t pointers = se(((uint32_t*)b)[6]);

				if (!relocated) {
					if (n) n += (uint64_t)&image[0];
					ids += (uint64_t)&image[0];
					pointers += (uint64_t)&image[0];
				}

				printf("%d from %s\n",(int)c,n);

				uint32_t*p_i = (uint32_t*)ids;
				uint32_t*p_p = (uint32_t*)pointers;
				std::vector<imp>&list = imports[n?n:""];
				for (int i=0;i<c;i++) {
					imp r;
					r.id = se(*p_i++);
					r.p = p_p++;
					list.push_back(r);
					printf("id %x, p %p\n",r.id,r.p);
				}

				b+= 4*11;
			}
		};
		auto prx_export = [&](uint64_t b,uint64_t e) {
			printf("export from %llx to %llx\n",(long long)b,(long long)e);
			if (!relocated) {
				b += (uint64_t)&image[0];
				e += (uint64_t)&image[0];
			}
			while (b<e) {
				uint16_t c = se(((uint16_t*)b)[3]);

				char*n = (char*)se(((uint32_t*)b)[4]);
				uint64_t ids = se(((uint32_t*)b)[5]);
				uint64_t pointers = se(((uint32_t*)b)[6]);

				if (!relocated) {
					if (n) n += (uint64_t)&image[0];
					ids += (uint64_t)&image[0];
					pointers += (uint64_t)&image[0];
				}

				printf("%d from %s\n",(int)c,n);

				uint32_t*p_i = (uint32_t*)ids;
				uint32_t*p_p = (uint32_t*)pointers;
				std::vector<exp>&list = exports[n?n:""];
				for (int i=0;i<c;i++) {
					if (!first_export) {
						first_export=(uint32_t*)se(*p_p);
						if (!relocated) first_export=(uint32_t*)((uint64_t)first_export + (uint64_t)&image[0]);
					}
					exp r;
					r.id = se(*p_i++);
					r.p = se(*p_p++);
					if (!relocated) r.p = (uint32_t)((uint64_t)r.p + (uint64_t)&image[0]);
					list.push_back(r);
					printf("id %x, p %x\n",r.id,r.p);
				}

				b+= 4*7;
			}
		};
		
		if (is_prx) {
			reloc_base = (uint64_t)&image[0];
			relocate();
		} else {
			reloc_base = 0;
		}

		auto p_to_v = [&](uint64_t pa) -> uint64_t {
			for (int i=0;i<(int)se(h.e_phnum);i++) {
				phdr&p = *(phdr*)&data[(size_t)se(h.e_phoff) + i*se(h.e_phentsize)];
				//printf("ph %d: type %08x, flags %08x, offset %llx, vaddr %llx, paddr %llx, filesz %llx, memsz %llx, align %llx\n",i,(int)se(p.p_type),(int)se(p.p_flags),(long long)se(p.p_offset),(long long)se(p.p_vaddr),(long long)se(p.p_paddr),(long long)se(p.p_filesz),(long long)se(p.p_memsz),(long long)se(p.p_align));
				uint64_t type = se(p.p_type);
				uint64_t flags = se(p.p_flags);
				uint64_t offset = se(p.p_offset);
				uint64_t vaddr = se(p.p_vaddr);
				uint64_t paddr = se(p.p_paddr);
				uint64_t filesz = se(p.p_filesz);
				uint64_t memsz = se(p.p_memsz);
				if (pa>=offset&&pa<offset+filesz) {
					if (pa>=offset+memsz) break;
					return vaddr + (pa-offset);
				}
			}
			xcept("physical address %llx does not map to memory",(long long)pa);
			return 0;
		};

		for (int i=0;i<(int)se(h.e_phnum);i++) {
			phdr&p = *(phdr*)&data[(size_t)se(h.e_phoff) + i*se(h.e_phentsize)];
			//printf("ph %d: type %08x, flags %08x, offset %llx, vaddr %llx, paddr %llx, filesz %llx, memsz %llx, align %llx\n",i,(int)se(p.p_type),(int)se(p.p_flags),(long long)se(p.p_offset),(long long)se(p.p_vaddr),(long long)se(p.p_paddr),(long long)se(p.p_filesz),(long long)se(p.p_memsz),(long long)se(p.p_align));
			uint64_t type = se(p.p_type);
			uint64_t flags = se(p.p_flags);
			uint64_t offset = se(p.p_offset);
			uint64_t vaddr = se(p.p_vaddr);
			uint64_t paddr = se(p.p_paddr);
			uint64_t filesz = se(p.p_filesz);
			uint64_t memsz = se(p.p_memsz);
			if (is_prx && i==0 && paddr!=0) {
				printf("module info is at %llx\n",(long long)paddr);
				uint64_t a = (uint64_t)&image[p_to_v(paddr)];
				prx_mod_info = (void*)a;
				printf("vaddr %llx\n",(long long)a);
				printf("module attributes: %x\n",(int)se(*(uint16_t*)a));
				a+=2;
				printf("module version: %x\n",(int)se(*(uint16_t*)a));
				a+=2;
				printf("module name: %s\n",(char*)a);
				a+=28;
				uint32_t gp = se(*(uint32_t*)a);
				a+=4;
				uint32_t e_start = se(*(uint32_t*)a);
				a+=4;
				uint32_t e_end = se(*(uint32_t*)a);
				a+=4;
				uint32_t i_start = se(*(uint32_t*)a);
				a+=4;
				uint32_t i_end = se(*(uint32_t*)a);
				a+=4;

				prx_import(i_start,i_end);
				prx_export(e_start,e_end);
			}

			if (type==0x60000001) { // process parameters
				uint32_t*p = (uint32_t*)&image[(size_t)vaddr];
				if (memsz==0) process_params = 0;
				else process_params = p;
			} else if (type==0x60000002) { // prx info
				if (memsz==0) process_prx_info=0;
				else {
					uint32_t*p = (uint32_t*)&image[(size_t)vaddr];
					process_prx_info = p;
					uint32_t e_start = se(p[4]);
					uint32_t e_end = se(p[5]);
					uint32_t i_start = se(p[6]);
					uint32_t i_end = se(p[7]);

					prx_import(i_start,i_end);
					prx_export(e_start,e_end);
				}
			} else if (type==7) { // tls data
				if (is_prx) xcept("prx with tls data?");
				// note: these are RVAs!
				tls_vaddr = vaddr;
				tls_filesz = filesz;
				tls_memsz = memsz;
			}
		}

		{

			uint32_t*p = (uint32_t*)le_entry;
			if (is_prx) p = first_export;
			if (!p) xcept("no exports or entry!");
			uint32_t rtoc = p[1];
			printf("rtoc is %x\n",(int)se(rtoc));
			while (p[1]==rtoc) {
				p-=2;
			}
			p+=2;
			printf("got located at %p\n",p);
			while (p[1]==rtoc) {
				if (se(p[0])==0x79650020) xcept("??");
				gotlist.push_back(p);
				p+=2;
			}
		}
		printf("gotlist.size() is %d\n",(int)gotlist.size());
// 
// 		std::vector<char> out_data = data;
// 		for (int i=0;i<(int)se(h.e_phnum);i++) {
// 			phdr&p = *(phdr*)&out_data[(size_t)se(h.e_phoff) + i*se(h.e_phentsize)];
// 			//printf("ph %d: type %08x, flags %08x, offset %llx, vaddr %llx, paddr %llx, filesz %llx, memsz %llx, align %llx\n",i,(int)se(p.p_type),(int)se(p.p_flags),(long long)se(p.p_offset),(long long)se(p.p_vaddr),(long long)se(p.p_paddr),(long long)se(p.p_filesz),(long long)se(p.p_memsz),(long long)se(p.p_align));
// 			uint64_t type = se(p.p_type);
// 			uint64_t flags = se(p.p_flags);
// 			uint64_t offset = se(p.p_offset);
// 			uint64_t vaddr = se(p.p_vaddr);
// 			uint64_t paddr = se(p.p_paddr);
// 			uint64_t filesz = se(p.p_filesz);
// 			uint64_t memsz = se(p.p_memsz);
// 			memcpy(&out_data[(size_t)offset],&image[(size_t)vaddr],(size_t)(filesz<memsz?filesz:memsz));
// 			p.p_vaddr = se((uint64_t)&image[(size_t)vaddr]);
// 		}
// 		FILE*f = fopen("out.prx","wb");
// 		if (!f) xcept("failed to open %s\n","out.prx");
// 		fwrite(&out_data[0],out_data.size(),1,f);
// 		fclose(f);
// 		printf("dumped to out.prx\n");
	}

	void load(const char*fn) {
		FILE*f = fopen(fn,"rb");
		if (!f) xcept("failed to open %s\n",fn);
		fseek(f,0,SEEK_END);
		long fs = ftell(f);
		fseek(f,0,SEEK_SET);
		vdata.resize(fs);
		if (fread(&vdata[0],1,fs,f)!=fs) xcept("bad read");
		fclose(f);
		data = &vdata[0];
		data_size = vdata.size();
		parse();
	}

	struct reloc_t {
		int type;
		uint64_t S,A;
	};
	std::map<uint64_t,reloc_t> reloc_map;

	void relocate(bool nomap=false) {
		if (!nomap) reloc_map.clear();
		if (data_size<sizeof(ehdr)) xcept("not enough data for header");
		ehdr&h = *(ehdr*)&data[0];
		if (h.e_ident[0]!=0x7f||h.e_ident[1]!='E'||h.e_ident[2]!='L'||h.e_ident[3]!='F') xcept("bad magic");
		for (int i=0;i<(int)se(h.e_phnum);i++) {
			phdr&p = *(phdr*)&data[(size_t)se(h.e_phoff) + i*se(h.e_phentsize)];
			//printf("ph %d: type %08x, flags %08x, offset %llx, vaddr %llx, paddr %llx, filesz %llx, memsz %llx, align %llx\n",i,(int)se(p.p_type),(int)se(p.p_flags),(long long)se(p.p_offset),(long long)se(p.p_vaddr),(long long)se(p.p_paddr),(long long)se(p.p_filesz),(long long)se(p.p_memsz),(long long)se(p.p_align));
			uint64_t type = se(p.p_type);
			uint64_t flags = se(p.p_flags);
			uint64_t offset = se(p.p_offset);
			uint64_t vaddr = se(p.p_vaddr);
			uint64_t paddr = se(p.p_paddr);
			uint64_t filesz = se(p.p_filesz);
			uint64_t memsz = se(p.p_memsz);

			auto ph_vaddr = [&](size_t n) -> uint64_t {
				if (n>=se(h.e_phnum)) xcept("bad relocation");
				return se(((phdr*)&data[(size_t)se(h.e_phoff) + n*se(h.e_phentsize)])->p_vaddr);
			};

			if (type==0x700000a4) { // relocation
				printf("relocating, image is at %llx, reloc_base is %llx\n",(long long)&image[0],(long long)reloc_base);
				relocated = true;
				if (reloc_base==-1) reloc_base = (uint64_t)&image[0];
				rela*r = (rela*)&data[(size_t)offset];
				while (r<(rela*)&data[(size_t)(offset+filesz)]) {
					uint64_t offset = se(r->r_offset);
					uint32_t sym = ELF64_R_SYM(se(r->r_info));
					size_t o_base = sym&0xff;
					size_t a_base = sym>>8;
					uint32_t type = ELF64_R_TYPE(se(r->r_info));
					int64_t addend = se((uint64_t)r->r_addend);
					//printf("rel, offset %llx, o_base %x, a_base %x, type %x, addend %llx\n",(long long)offset,(int)o_base,(int)a_base,(int)type,(long long)addend);

					// TODO: figure out what a_base ff actually means
					//       this seems to be right for the extremely few cases i've found
					uint64_t S = (a_base==0xff ? 0 : ph_vaddr(a_base)) + reloc_base;
					int64_t A = addend;

					uint64_t RP = ph_vaddr(o_base) + (uint64_t)&image[0] + offset;
					if (RP>=(uint64_t)&image[image_size]) xcept("bad RP %llx",RP);
					uint64_t P = ph_vaddr(o_base) + reloc_base + offset;

					auto checkfits = [&](uint64_t v,int bits) {
						if ((v&((1LL<<bits)-1))!=v && (v>>bits!=(1LL<<(64-bits))-1)) xcept("relocation failed; value %llx does not fit in %d bits",(long long)v,bits);
					};


					auto r_word32 = [&](uint64_t v) {
						checkfits(v,32);
						*(uint32_t*)RP = se((uint32_t)v);
					};
					auto r_low24 = [&](uint64_t v) {
						checkfits(v,24);
						*(uint32_t*)RP = se((se(*(uint32_t*)RP)&~0x3FFFFFC) | ((uint32_t)(v<<2)&0x3FFFFFC));
					};
					auto r_half16 = [&](uint64_t v) {
						checkfits(v,16);
						*(uint16_t*)RP = se((uint16_t)v);
					};
					auto r_low14 = [&](uint64_t v) {
						checkfits(v,14);
						*(uint32_t*)RP = se((se(*(uint32_t*)RP)&~0xFFFC) | ((uint32_t)(v<<2)&0xFFFC));
					};

					auto lo = [&](uint64_t x) { return (x & 0xffff); };
					auto hi = [&](uint64_t x) { return ((x >> 16) & 0xffff);};
					auto ha = [&](uint64_t x) { return (((x >> 16) + ((x & 0x8000) ? 1 : 0)) & 0xffff); };

					//printf("RP is %x, P is %x\n",(int)RP,(int)P);

					if (!nomap && type) {
						reloc_t r;
						r.type = type;
						r.S = S - reloc_base;
						r.A = A;
						reloc_map[P] = r;
					}

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
					else xcept("unknown relocation type %d\n",(int)type);

					r++;
				}
			}
		}
		if (!relocated) xcept("no relocation section found");


// 		std::vector<char> out_data;
// 		out_data.resize(data_size);
// 		memcpy(&out_data[0],data,data_size);
// 		for (int i=0;i<(int)se(h.e_phnum);i++) {
// 			phdr&p = *(phdr*)&out_data[(size_t)se(h.e_phoff) + i*se(h.e_phentsize)];
// 			//printf("ph %d: type %08x, flags %08x, offset %llx, vaddr %llx, paddr %llx, filesz %llx, memsz %llx, align %llx\n",i,(int)se(p.p_type),(int)se(p.p_flags),(long long)se(p.p_offset),(long long)se(p.p_vaddr),(long long)se(p.p_paddr),(long long)se(p.p_filesz),(long long)se(p.p_memsz),(long long)se(p.p_align));
// 			uint64_t type = se(p.p_type);
// 			uint64_t flags = se(p.p_flags);
// 			uint64_t offset = se(p.p_offset);
// 			uint64_t vaddr = se(p.p_vaddr);
// 			uint64_t paddr = se(p.p_paddr);
// 			uint64_t filesz = se(p.p_filesz);
// 			uint64_t memsz = se(p.p_memsz);
// 			memcpy(&out_data[(size_t)offset],&image[(size_t)vaddr],(size_t)(filesz<memsz?filesz:memsz));
// 			p.p_vaddr = se((uint64_t)&image[(size_t)vaddr]);
// 		}
// 		FILE*f = fopen("out.prx","wb");
// 		if (!f) xcept("failed to open %s\n","out.prx");
// 		fwrite(&out_data[0],out_data.size(),1,f);
// 		fclose(f);
// 		printf("dumped to out.prx\n");
	}

	struct self_header {
		uint32_t magic;                   // "SCE\0"
		uint32_t version;                 // always 2 
		uint16_t attribute;               // 0x8000 - fself
		uint16_t category;
		uint32_t metadataInfoOffset;
		uint64_t fileOffset;
		uint64_t fileSize;
		uint64_t unknown06;
		uint64_t programInfoOffset;
		uint64_t elfHeaderOffset;
		uint64_t elfProgramHeadersOffset;
		uint64_t elfSectionHeadersOffset;
		uint64_t sInfoOffset;
		uint64_t versionInfoOffset;
		uint64_t controlInfoOffset;
		uint64_t controlInfoSize;
		uint32_t unknown15;
	};

	std::vector<char> elf_data;
	void unself() {

		self_header&h = (self_header&)data[0];
		if (data_size<sizeof(h)) xcept("bad self1");
		printf("version: %#x\n",(int)se(h.version));
		printf("attribute: %#x\n",(int)se(h.attribute));
		printf("category: %#x\n",(int)se(h.category));
		printf("metadataInfoOffset: %#x\n",(int)se(h.metadataInfoOffset));
		printf("fileOffset: %#llx\n",(long long)se(h.fileOffset));
		printf("unknown06: %#llx\n",(long long)se(h.unknown06));
		printf("programInfoOffset: %#llx\n",(long long)se(h.programInfoOffset));
		printf("elfHeaderOffset: %#llx\n",(long long)se(h.elfHeaderOffset));
		printf("elfProgramHeadersOffset: %#llx\n",(long long)se(h.elfProgramHeadersOffset));
		printf("elfSectionheadersOffset: %#llx\n",(long long)se(h.elfSectionHeadersOffset));
		printf("sInfoOffset: %#llx\n",(long long)se(h.sInfoOffset));
		printf("versionInfoOffset: %#llx\n",(long long)se(h.versionInfoOffset));
		printf("controlInfoOffset: %#llx\n",(long long)se(h.controlInfoOffset));
		printf("controlInfoSize: %#llx\n",(long long)se(h.controlInfoSize));
		printf("unknown15: %#x\n",(int)se(h.unknown15));
		if (se(h.version)!=2) printf("version(%d)!=2!\n",(int)h.version);
		uint64_t ehdr_offset = se(h.elfHeaderOffset);
		uint64_t ph_offset = se(h.elfProgramHeadersOffset);
		ehdr&eh = (ehdr&)data[ehdr_offset];
		printf("eh.e_phnum is %d, eh.e_phentsize is %d\n",eh.e_phnum,eh.e_phentsize);
		if (data_size<ehdr_offset+sizeof(ehdr) || data_size<ph_offset+eh.e_phnum*eh.e_phentsize) xcept("bad self2");
		elf_data.resize(eh.e_phoff+eh.e_phnum*eh.e_phentsize);
		if (elf_data.size()<sizeof(ehdr)) xcept("bad self3");
		if (sizeof(phdr)>eh.e_phentsize) xcept("bad self4");
		(ehdr&)elf_data[0] = eh;
		for (int i=0;i<(int)eh.e_phnum;i++) {
			phdr&p = (phdr&)data[ph_offset+eh.e_phentsize*i];
			(phdr&)elf_data[eh.e_phoff+eh.e_phentsize*i] = p;
		}

		data = &elf_data[0];
		data_size = elf_data.size();
	}

};

struct elf32 {
	char*data;
	size_t data_size;
	std::vector<char> vdata;
	//std::vector<char> image;
	//char*image;
	//size_t image_size;

	typedef uint32_t elf32_addr;
	typedef uint32_t elf32_off;
	typedef uint16_t elf32_half;
	typedef uint32_t elf32_word;
	typedef int32_t elf32_sword;

	struct ehdr {
		unsigned char e_ident[16];
		elf32_half e_type;
		elf32_half e_machine;
		elf32_word e_version;
		elf32_addr e_entry;
		elf32_off e_phoff;
		elf32_off e_shoff;
		elf32_word e_flags;
		elf32_half e_ehsize;
		elf32_half e_phentsize;
		elf32_half e_phnum;
		elf32_half e_shentsize;
		elf32_half e_shnum;
		elf32_half e_shstrndx;
	};

	struct shdr {
		elf32_word sh_name;
		elf32_word sh_type;
		elf32_word sh_flags;
		elf32_addr sh_addr;
		elf32_off sh_offset;
		elf32_word sh_size;
		elf32_word sh_link;
		elf32_word sh_info;
		elf32_word addralign;
		elf32_word sh_entsize;
	};

	struct phdr {
		elf32_word p_type;
		elf32_off p_offset;
		elf32_addr p_vaddr;
		elf32_addr p_paddr;
		elf32_word p_filesz;
		elf32_word p_memsz;
		elf32_word p_flags;
		elf32_word p_align;
	};

	struct rela {
		elf32_addr r_offset;
		elf32_word r_info;
		elf32_word r_addend;
	};

	uint32_t entry;

	elf32() : data(0), data_size(0) {}


	typedef std::function<void(int type,uint32_t vaddr,uint32_t filesz,uint32_t memsz,char*data)>cb_t;

	void parse(const cb_t&cb) {
		if (data_size<sizeof(ehdr)) xcept("not enough data for header");
		ehdr&h = *(ehdr*)&data[0];
		if (h.e_ident[0]!=0x7f||h.e_ident[1]!='E'||h.e_ident[2]!='L'||h.e_ident[3]!='F') xcept("bad magic");
		printf("EI_CLASS is %d\n",(int)h.e_ident[4]);
		printf("EI_DATA is %d\n",(int)h.e_ident[5]);
		printf("EI_VERSION is %d\n",(int)h.e_ident[6]);
		printf("EI_OSABI is %d\n",(int)h.e_ident[7]);
		printf("EI_ABIVERSION is %d\n",(int)h.e_ident[8]);
		printf("EI_PAD is %d\n",(int)h.e_ident[9]);
		printf("e_type is %d\n",(int)se(h.e_type));
		printf("e_machine is %d\n",(int)se(h.e_machine));
		printf("e_version is %d\n",(int)se(h.e_version));
		printf("e_entry is at %08llX\n",(int)se(h.e_entry));
		printf("e_shstrndx is %d\n",(int)se(h.e_shstrndx));

		if (h.e_ident[4]!=1 || se(h.e_machine)!=23) xcept("not an spu elf");

		printf("at %08llx, %d sections of %d bytes\n",(int)se(h.e_shoff),(int)se(h.e_shnum),(int)se(h.e_shentsize));
		printf("at %08llx, %d program entries of %d bytes\n",(int)se(h.e_phoff),(int)se(h.e_phnum),(int)se(h.e_phentsize));

		entry = se(h.e_entry);

		for (int i=0;i<(int)se(h.e_phnum);i++) {
			phdr&p = *(phdr*)&data[(size_t)se(h.e_phoff) + i*se(h.e_phentsize)];
			uint32_t type = se(p.p_type);
			uint32_t flags = se(p.p_flags);
			uint32_t offset = se(p.p_offset);
			uint32_t vaddr = se(p.p_vaddr);
			uint32_t paddr = se(p.p_paddr);
			uint32_t filesz = se(p.p_filesz);
			uint32_t memsz = se(p.p_memsz);
			uint32_t align = se(p.p_align);
			printf("ph %d: type %08x, flags %08x, offset %x, vaddr %x, paddr %x, filesz %x, memsz %x, align %x\n",i,(int)type,(int)flags,(long long)offset,(long long)vaddr,(long long)paddr,(long long)filesz,(long long)memsz,(long long)align);
			cb(type,vaddr,filesz,memsz,&data[(size_t)offset]);
// 			sect s;
// 			s.rvaddr = vaddr;
// 			s.memsz = memsz;
// 			sections.push_back(s);
		}

		// 		for (int i=0;i<(int)se(h.e_shnum);i++) {
		// 			shdr&s = *(shdr*)&data[(size_t)se(h.e_shoff) + i*se(h.e_shentsize)];
		// 			if (!se(s.sh_type)) printf("section %d: unused\n",i);
		// 			else printf("section %d: at %016llx, offset %016llx, size %016llx, type %d, flags %016llx\n",i,(long long)se(s.sh_addr),(long long)se(s.sh_offset),(long long)se(s.sh_size),(int)se(s.sh_type),(long long )se(s.sh_flags));
		// 			if (se(s.sh_type) && se(s.sh_addr)) {
		// 				uint64_t addr = se(s.sh_addr);
		// 				uint64_t offset = se(s.sh_offset);
		// 				uint64_t size = se(s.sh_size);
		// 				uint64_t type = se(s.sh_type);
		// 				if (image.size()<=(size_t)(addr+size)) image.resize((size_t)(addr+size));
		// 				if (type!=8) {
		// 					if (data_size<=(size_t)(offset+size)) xcept("bad section header");
		// 					if (type!=8) memcpy(&image[(size_t)addr],&data[(size_t)offset],(size_t)size);
		// 				}
		// 			}
		// 		}
// 		for (int i=0;i<(int)se(h.e_phnum);i++) {
// 			phdr&p = *(phdr*)&data[(size_t)se(h.e_phoff) + i*se(h.e_phentsize)];
// 			//printf("ph %d: type %08x, flags %08x, offset %llx, vaddr %llx, paddr %llx, filesz %llx, memsz %llx, align %llx\n",i,(int)se(p.p_type),(int)se(p.p_flags),(long long)se(p.p_offset),(long long)se(p.p_vaddr),(long long)se(p.p_paddr),(long long)se(p.p_filesz),(long long)se(p.p_memsz),(long long)se(p.p_align));
// 			uint64_t type = se(p.p_type);
// 			uint64_t flags = se(p.p_flags);
// 			uint64_t offset = se(p.p_offset);
// 			uint64_t vaddr = se(p.p_vaddr);
// 			uint64_t paddr = se(p.p_paddr);
// 			uint64_t filesz = se(p.p_filesz);
// 			uint64_t memsz = se(p.p_memsz);
// 			if (memsz) {
// 				if (flags&1) {
// 					if (vaddr<exec_start) exec_start = vaddr;
// 					if (vaddr+(memsz<filesz?memsz:filesz)>exec_end) exec_end = vaddr+(memsz<filesz?memsz:filesz);
// 					printf("exec_start %llx, exec_end %llx\n",(long long)exec_start,(long long)exec_end);
// 				}
// 				//if (image.size()<=(size_t)(vaddr+memsz)) image.resize((size_t)(vaddr+memsz));
// 				if (data_size<(size_t)(offset+filesz)) xcept("bad program header");
// 				memcpy(&image[(size_t)vaddr],&data[(size_t)offset],(size_t)(memsz>filesz?filesz:memsz));
// 			}
// 		}

		// 
		// 		std::vector<char> out_data = data;
		// 		for (int i=0;i<(int)se(h.e_phnum);i++) {
		// 			phdr&p = *(phdr*)&out_data[(size_t)se(h.e_phoff) + i*se(h.e_phentsize)];
		// 			//printf("ph %d: type %08x, flags %08x, offset %llx, vaddr %llx, paddr %llx, filesz %llx, memsz %llx, align %llx\n",i,(int)se(p.p_type),(int)se(p.p_flags),(long long)se(p.p_offset),(long long)se(p.p_vaddr),(long long)se(p.p_paddr),(long long)se(p.p_filesz),(long long)se(p.p_memsz),(long long)se(p.p_align));
		// 			uint64_t type = se(p.p_type);
		// 			uint64_t flags = se(p.p_flags);
		// 			uint64_t offset = se(p.p_offset);
		// 			uint64_t vaddr = se(p.p_vaddr);
		// 			uint64_t paddr = se(p.p_paddr);
		// 			uint64_t filesz = se(p.p_filesz);
		// 			uint64_t memsz = se(p.p_memsz);
		// 			memcpy(&out_data[(size_t)offset],&image[(size_t)vaddr],(size_t)(filesz<memsz?filesz:memsz));
		// 			p.p_vaddr = se((uint64_t)&image[(size_t)vaddr]);
		// 		}
		// 		FILE*f = fopen("out.prx","wb");
		// 		if (!f) xcept("failed to open %s\n","out.prx");
		// 		fwrite(&out_data[0],out_data.size(),1,f);
		// 		fclose(f);
		// 		printf("dumped to out.prx\n");
	}

	void load(const char*fn,const cb_t&cb) {
		FILE*f = fopen(fn,"rb");
		if (!f) xcept("failed to open %s\n",fn);
		fseek(f,0,SEEK_END);
		long fs = ftell(f);
		fseek(f,0,SEEK_SET);
		vdata.resize(fs);
		if (fread(&vdata[0],1,fs,f)!=fs) xcept("bad read");
		fclose(f);
		data = &vdata[0];
		data_size = vdata.size();
		parse(cb);
	}

};



