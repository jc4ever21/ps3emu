#pragma warning(disable:4146) // warning C4146: unary minus operator applied to unsigned type, result still unsigned
#pragma warning(disable:4355) //  warning C4355: 'this' : used in base member initializer list

#define _CRT_SECURE_NO_WARNINGS
#define _SCL_SECURE_NO_WARNINGS

#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#include "llvm/Linker.h"
#include "llvm/Assembly/Parser.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/DerivedTypes.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/ExecutionEngine/JITMemoryManager.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/CodeGen/GCs.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Intrinsics.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/ManagedStatic.h"
using namespace llvm;

#ifdef _MSC_VER
#pragma comment(lib,"EnhancedDisassembly.lib")
#pragma comment(lib,"LLVMAnalysis.lib")
#pragma comment(lib,"LLVMArchive.lib")
#pragma comment(lib,"LLVMAsmParser.lib")
#pragma comment(lib,"LLVMAsmPrinter.lib")
#pragma comment(lib,"LLVMBitReader.lib")
#pragma comment(lib,"LLVMBitWriter.lib")
#pragma comment(lib,"LLVMCodeGen.lib")
#pragma comment(lib,"LLVMCore.lib")
#pragma comment(lib,"LLVMDebugInfo.lib")
#pragma comment(lib,"LLVMExecutionEngine.lib")
#pragma comment(lib,"LLVMInstCombine.lib")
#pragma comment(lib,"LLVMInstrumentation.lib")
#pragma comment(lib,"LLVMInterpreter.lib")
#pragma comment(lib,"LLVMJIT.lib")
#pragma comment(lib,"LLVMLinker.lib")
#pragma comment(lib,"LLVMMC.lib")
#pragma comment(lib,"LLVMMCDisassembler.lib")
#pragma comment(lib,"LLVMMCJIT.lib")
#pragma comment(lib,"LLVMMCParser.lib")
#pragma comment(lib,"LLVMObject.lib")
#pragma comment(lib,"LLVMRuntimeDyld.lib")
#pragma comment(lib,"LLVMScalarOpts.lib")
#pragma comment(lib,"LLVMSelectionDAG.lib")
#pragma comment(lib,"LLVMSupport.lib")
#pragma comment(lib,"LLVMTarget.lib")
#pragma comment(lib,"LLVMTransformUtils.lib")
#pragma comment(lib,"LLVMX86AsmParser.lib")
#pragma comment(lib,"LLVMX86AsmPrinter.lib")
#pragma comment(lib,"LLVMX86CodeGen.lib")
#pragma comment(lib,"LLVMX86Desc.lib")
#pragma comment(lib,"LLVMX86Disassembler.lib")
#pragma comment(lib,"LLVMX86Info.lib")
#pragma comment(lib,"LLVMX86Utils.lib")
#pragma comment(lib,"LLVMipa.lib")
#pragma comment(lib,"LLVMipo.lib")
#endif

#include <cstdio>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <list>
#include <stack>
#include <functional>

//#include "jitallocator.h"
#include "win32_coff_object_writer.h"

#ifdef _WIN32
#include <windows.h>
#endif

static char xcept_str[0x200];
void xcept(const char*fmt,...) {
	va_list args;
	va_start(args,fmt);
	vsnprintf(xcept_str,sizeof(xcept_str),fmt,args);
	va_end(args);
	printf("about to throw exception %s\n",xcept_str);
	throw (const char*)xcept_str;
}


static std::string format(const char*fmt,...) {
	char buf[0x1000];
	va_list args;
	va_start(args,fmt);
	vsnprintf(buf,sizeof(buf),fmt,args);
	va_end(args);
	return buf;
}


template<typename t>
t se(t v);
template<>
uint64_t se(uint64_t v) {
	return (v>>56) | ((v&0xff000000000000)>>40) | ((v&0xff0000000000)>>24) | ((v&0xff00000000)>>8) | ((v&0xff000000)<<8) | ((v&0xff0000)<<24) | ((v&0xff00)<<40) | ((v&0xff)<<56);
}
template<>
uint32_t se(uint32_t v) {
	return v>>24 | (v&0xff0000)>>8 | (v&0xff00)<<8 | (v&0xff)<<24;
}
template<>
uint16_t se(uint16_t v) {
	return v>>8 | (v&0xff)<<8;
}
template<typename t>
t se24(t v) {
	return (v&0xff)<<16 | (v&0xff00) | (v&0xff0000)>>16;
}
#include "elf.h"
#include "ld.h"

struct function_info {
	uint64_t addr;
	uint64_t rtoc;
	Function*f;
	const char*name;
};

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

struct regval {
	enum {t_unknown, t_mem_reg_offset, t_mem_reg_plus_reg};
	int t;
	int r, r2, o;
};

struct t_section {
	uint64_t addr, memsz, filesz;
};

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include <iostream>

int main(int argc,char**argv) {

	try {
	bool debug_output = false;
	bool debug_superverbose = false;
	bool debug_print_calls = true;
	bool debug_tests = true;
	bool debug_store_ip = false;

	bool enable_longjmp = true;
	bool enable_safe_switch_detection = true;
	bool use_only_safe_switch_detection = false;

	std::string out_fn;
	std::string out_data_fn;
	std::vector<std::string> input_files;

	po::options_description desc("Allowed options");
	desc.add_options()
		("help,h", "produce help message")
		("output,o", po::value(&out_fn), "output file")
		("input-file,i", po::value(&input_files), "input file")
		("debug-output,d", po::value(&debug_output), "enable debug output")
		("longjmp,l", po::value(&enable_longjmp), "enable setjmp/longjmp and exception support")
		;

	po::positional_options_description p;
	p.add("input-file", -1);

	po::variables_map vm;
	po::store(po::command_line_parser(argc,argv).options(desc).positional(p).run(),vm);
	po::notify(vm);

	if (vm.count("help")) {
		std::cout << desc << "\n";
		return 1;
	}

	std::string elf_fn, prx_fn;

	for (size_t i=0;i<input_files.size();i++) {
		std::string&fn = input_files[i];
		size_t p = fn.rfind(".");
		if (p!=std::string::npos) {
			std::string ext = fn.substr(p+1);
			if (ext=="elf"||ext=="self") elf_fn = fn;
			if (ext=="prx"||ext=="sprx") prx_fn = fn;
		}
	}

	if (elf_fn.empty()==prx_fn.empty()) xcept("specify an .elf or .prx file");

	if (out_fn.empty()) {
		std::string s = elf_fn;
		if (s.empty()) s = prx_fn;
		size_t p = s.rfind(".");
		if (p!=std::string::npos) {
			s = s.substr(0,p);
		}
		out_fn = s + ".obj";
	}
	{
		size_t p = out_fn.rfind(".");
		if (p!=std::string::npos) {
			std::string ext = out_fn.substr(p+1);
			out_data_fn = out_fn.substr(0,p) + "-data." + ext;
		} else out_data_fn = out_fn+"-data";
	}


	elf main_e;
	main_e.is_prx = false;
	if (!elf_fn.empty()) main_e.load(elf_fn.c_str());
	else {
		main_e.is_prx = true;
		main_e.load(prx_fn.c_str());
	}

	std::vector<std::pair<uint64_t,uint64_t>> code_regions;

	std::vector<uint32_t*> got_list;

	std::map<uint64_t,std::pair<std::string,uint32_t>> debug_export_func_map;

	for (auto i=main_e.exports.begin();i!=main_e.exports.end();++i) {
		const auto&m = i->first;
		const auto&l = i->second;
		if (m=="") continue;
		for (auto i=l.begin();i!=l.end();++i) {
			auto p = std::make_pair(m,i->id);

			debug_export_func_map[se(*(uint32_t*)i->p)] = std::make_pair(m,i->id);
		}
	}
	for (size_t i=0;i<main_e.gotlist.size();i++) got_list.push_back(main_e.gotlist[i]);
	code_regions.push_back(std::make_pair(main_e.exec_start,main_e.exec_end));


	pe out;
	out.image_base = 0x10000;

	auto make_elf_sections = [&](elf&e) -> std::vector<t_section> {
		elf::ehdr&h = *(elf::ehdr*)&e.data[0];
		std::vector<t_section> r;
		for (int i=0;i<(int)se(h.e_phnum);i++) {
			elf::phdr&p = *(elf::phdr*)&e.data[(size_t)se(h.e_phoff) + i*se(h.e_phentsize)];
			if (se(p.p_type)!=1) continue;
			t_section s;
			s.addr = se(p.p_vaddr);
			s.filesz = se(p.p_filesz);
			s.memsz = se(p.p_memsz);
			if (!s.memsz) continue;
			r.push_back(s);
		}
		std::sort(r.begin(),r.end(),[&](const t_section&a,const t_section&b) {
			return a.addr<b.addr;
		});
		for (size_t i=1;i<r.size();i++) {
			t_section&prev = r[i-1];
			t_section&p = r[i];
			if (p.addr<prev.addr+prev.memsz && p.addr+p.memsz<prev.addr+prev.memsz) {
				if (p.addr>=prev.addr+prev.filesz) {
					prev.memsz = p.addr-prev.addr;
				} else xcept("section in another section; remove?");
			}
			if (prev.addr+prev.memsz>p.addr) {
				prev.memsz = p.addr-prev.addr;
			}
			if (prev.addr+prev.filesz>p.addr) {
				prev.filesz = p.addr-prev.addr;
			}
		}
		return r;
	};
	std::vector<std::pair<uint64_t,uint64_t>> g_image_section_list;
	auto add_out_elf = [&](elf&e,uint64_t base,uint64_t size) {
		std::vector<t_section> l = make_elf_sections(e);
		bool first=true;
		for (size_t i=0;i<l.size();i++) {
			t_section&p = l[i];
			uint64_t vaddr = p.addr;
			uint64_t filesz = p.filesz;
			uint64_t memsz = p.memsz;

			printf("vaddr %p, filesz %p, memsz %p\n",(void*)vaddr,(void*)filesz,(void*)memsz);

			if (!memsz) continue;

			if (filesz>memsz) filesz=memsz;

			if (vaddr+memsz>size) xcept("vaddr+memsz>size");

			g_image_section_list.push_back(std::make_pair(vaddr,memsz));
			out.add_section(base + vaddr,filesz,memsz,&e.image[vaddr]);
			if (first) {
				IMAGE_SYMBOL sym;
				memcpy(sym.N.ShortName,"g_image",8);
				sym.NumberOfAuxSymbols = 0;
				sym.SectionNumber = 1;
				sym.StorageClass = IMAGE_SYM_CLASS_EXTERNAL;
				sym.Type = 0;
				sym.Value = (DWORD)-vaddr;
				out.add_symbol(sym);

				first=false;
			}
		}
	};
	add_out_elf(main_e,0,main_e.image_size);

	uint64_t xx_ip = 0;

	auto can_translate_addr = [&](uint64_t addr) -> bool {
		elf&e = main_e;
		if (addr>=e.reloc_base&&addr<e.reloc_base+e.image_size) {
			return true;
		}
		return false;
	};

	auto translate_addr = [&](uint64_t addr) -> uint64_t {
		elf&e = main_e;
		for (auto i=e.sections.begin();i!=e.sections.end();++i) {
			uint64_t vaddr = e.reloc_base + i->rvaddr;
			uint64_t size = i->memsz;
			if (addr>=vaddr&&addr<vaddr+size) {
				return addr - e.reloc_base + (uint64_t)&e.image[0];
			}
		}
		printf("%p: cannot translate unknown address %llx\n",(void*)xx_ip,(long long)addr);
		return -1;
	};

	auto valid_ip = [&](uint64_t ip) -> bool {
		ip = translate_addr(ip);
		for (size_t i=0;i<code_regions.size();i++) {
			if (ip>=code_regions[i].first&&ip<code_regions[i].second) return true;
		}
		return false;
	};
	auto valid_data = [&](uint64_t addr) -> bool {
		// TODO
		return true;
	};

	auto read_32 = [&](uint64_t addr) -> uint32_t {
		addr = translate_addr(addr);
		if (addr==-1)  return -1;
		return se(*(uint32_t*)addr);
	};

	//printf("base is %llx\n",(long long)base);
	printf("image size is %llx\n",(long long)main_e.image_size);
	//printf("mem_offset is %llx\n",(long long)mem_offset);
	printf("e.le_entry is %llx\n",(long long)main_e.le_entry);

	llvm_shutdown_obj Y;
	LLVMContext&context =  getGlobalContext();

	PassRegistry &Registry = *PassRegistry::getPassRegistry();
	initializeCore(Registry);
	initializeScalarOpts(Registry);
	initializeIPO(Registry);
	initializeAnalysis(Registry);
	initializeIPA(Registry);
	initializeTransformUtils(Registry);
	initializeInstCombine(Registry);
	initializeInstrumentation(Registry);
	initializeTarget(Registry);
	
	InitializeNativeTarget();
	

	Module*mod = new Module("test",context);

	IRBuilder<> B(context);

	Type*t_void = Type::getVoidTy(context);
	IntegerType*t_i1 = Type::getInt1Ty(context);
	IntegerType*t_i5 = Type::getIntNTy(context,5);
	IntegerType*t_i6 = Type::getIntNTy(context,6);
	IntegerType*t_i8 = Type::getInt8Ty(context);
	IntegerType*t_i16 = Type::getInt16Ty(context);
	IntegerType*t_i24 = Type::getIntNTy(context,24);
	IntegerType*t_i30 = Type::getIntNTy(context,30);
	IntegerType*t_i32 = Type::getInt32Ty(context);
	IntegerType*t_i64 = Type::getInt64Ty(context);
	IntegerType*t_i128 = Type::getIntNTy(context,128);
	IntegerType*t_i256 = Type::getIntNTy(context,256);

	Type*t_double = Type::getDoubleTy(context);
	Type*t_float = Type::getFloatTy(context);
	
	PointerType*p_t_i8 = Type::getInt8PtrTy(context);
	PointerType*p_t_i16 = Type::getInt16PtrTy(context);
	PointerType*p_t_i32 = Type::getInt32PtrTy(context);
	PointerType*p_t_i64 = Type::getInt64PtrTy(context);
	PointerType*p_t_i128 = Type::getIntNPtrTy(context,128);

	std::map<int,IntegerType*> int_t_map;
	auto get_int_type = [&](int bits) -> IntegerType* {
		IntegerType*&r = int_t_map[bits];
		if (!r) r = Type::getIntNTy(context,bits);
		return r;
	};

	std::vector<Type*> args;

	args.clear();
	args.push_back(t_i64);
	Function*F_bad_insn = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_bad_insn",mod);
	Function*F_outside_code = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_outside_code",mod);

	args.clear();
	args.push_back(t_i64);
	Function*F_dump_addr = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_dump_addr",mod);
	Function*F_stack_call = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_stack_call",mod);
	Function*F_stack_ret = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_stack_ret",mod);
	args.clear();
	args.push_back(t_i64);
	args.push_back(p_t_i8);
	Function*F_err = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_err",mod);
	Function*F_print = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_print",mod);
	args.clear();
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	Function*F_dump_regs = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_dump_regs",mod);
	args.clear();
	Function*F_tmp = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_tmp",mod);

	args.clear();
	args.push_back(t_i64);
	args.push_back(p_t_i128);
	Function*F_syscall = Function::Create(FunctionType::get(t_i64,args,false),Function::ExternalLinkage,"F_syscall",mod);

	args.clear();
	args.push_back(p_t_i8);
	args.push_back(p_t_i8);
	args.push_back(p_t_i8);
	args.push_back(p_t_i8);
	args.push_back(p_t_i8);
	args.push_back(p_t_i64);
	args.push_back(p_t_i64);
	Function*F_proc_init = Function::Create(FunctionType::get(p_t_i8,args,false),Function::ExternalLinkage,"F_proc_init",mod);

	args.clear();
	args.push_back(p_t_i8);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	Function*F_add_module = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_add_module",mod);

	args.clear();
	Function*F_alloc_main_stack = Function::Create(FunctionType::get(t_i64,args,false),Function::ExternalLinkage,"F_alloc_main_stack",mod);

	args.clear();
	args.push_back(p_t_i8);
	Function*F_entry = Function::Create(FunctionType::get(p_t_i8,args,false),Function::ExternalLinkage,"F_entry",mod);
	F_entry->addFnAttr(Attribute::NoReturn);

	args.clear();
	args.push_back(p_t_i8);
	args.push_back(t_i32);
	args.push_back(p_t_i8);
	args.push_back(p_t_i8);
	args.push_back(p_t_i8);
	args.push_back(p_t_i8);
	args.push_back(p_t_i8);
	args.push_back(p_t_i8);
	Function*F_dll_main = Function::Create(FunctionType::get(t_i64,args,false),Function::ExternalLinkage,"F_dll_main",mod);

	args.clear();
	args.push_back(t_i64);
	Function*F_unresolved_import = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_unresolved_import",mod);
	args.clear();
	args.push_back(t_i64);
	Function*F_libcall_print = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_libcall_print",mod);

	args.clear();
	args.push_back(t_float);
	Function*F_RoundToSPIntFloor = Function::Create(FunctionType::get(t_float,args,false),Function::ExternalLinkage,"F_RoundToSPIntFloor",mod);
	Function*F_RoundToSPIntNear = Function::Create(FunctionType::get(t_float,args,false),Function::ExternalLinkage,"F_RoundToSPIntNear",mod);
	Function*F_RoundToSPIntCeil = Function::Create(FunctionType::get(t_float,args,false),Function::ExternalLinkage,"F_RoundToSPIntCeil",mod);
	Function*F_RoundToSPIntTrunc = Function::Create(FunctionType::get(t_float,args,false),Function::ExternalLinkage,"F_RoundToSPIntTrunc",mod);

	args.clear();
	args.push_back(t_float);
	Function*F_sqrt32 = Function::Create(FunctionType::get(t_float,args,false),Function::ExternalLinkage,"F_sqrt32",mod);
	args.clear();
	args.push_back(t_double);
	Function*F_sqrt64 = Function::Create(FunctionType::get(t_double,args,false),Function::ExternalLinkage,"F_sqrt64",mod);


// 	args.clear();
// 	args.push_back(p_t_i8);
// 	args.push_back(p_t_i8);
// 	args.push_back(t_i64);
// 	args.push_back(t_i32);
// 	args.push_back(t_i1);
// 	Function*F_llvm_memmove = Function::Create(FunctionType::get(Type::getVoidTy(context),args,false),Function::ExternalLinkage,"llvm.memmove.p0i8.p0i8.i64",mod);

	args.clear();
	args.push_back(t_i16);
	Function*F_llvm_bswap16 = Function::Create(FunctionType::get(t_i16,args,false),Function::ExternalLinkage,"llvm.bswap.i16",mod);
	args.clear();
	args.push_back(t_i32);
	Function*F_llvm_bswap32 = Function::Create(FunctionType::get(t_i32,args,false),Function::ExternalLinkage,"llvm.bswap.i32",mod);
	args.clear();
	args.push_back(t_i64);
	Function*F_llvm_bswap64 = Function::Create(FunctionType::get(t_i64,args,false),Function::ExternalLinkage,"llvm.bswap.i64",mod);
	args.clear();
	args.push_back(t_i128);
	Function*F_llvm_bswap128 = Function::Create(FunctionType::get(t_i128,args,false),Function::ExternalLinkage,"llvm.bswap.i128",mod);

	std::vector<Type*> tys;
	tys.clear();
	tys.push_back(t_i64);
	Function*F_llvm_sadd_with_overflow64 = Intrinsic::getDeclaration(mod,Intrinsic::sadd_with_overflow,tys);
	Function*F_llvm_uadd_with_overflow64 = Intrinsic::getDeclaration(mod,Intrinsic::uadd_with_overflow,tys);
	Function*F_llvm_ssub_with_overflow64 = Intrinsic::getDeclaration(mod,Intrinsic::ssub_with_overflow,tys);
	Function*F_llvm_usub_with_overflow64 = Intrinsic::getDeclaration(mod,Intrinsic::usub_with_overflow,tys);
	Function*F_llvm_smul_with_overflow64 = Intrinsic::getDeclaration(mod,Intrinsic::smul_with_overflow,tys);
	Function*F_llvm_umul_with_overflow64 = Intrinsic::getDeclaration(mod,Intrinsic::umul_with_overflow,tys);
	tys.push_back(p_t_i64);
	Function*F_llvm_atomic_cmp_swap64 = Intrinsic::getDeclaration(mod,Intrinsic::atomic_cmp_swap,tys);
	tys.clear();
	tys.push_back(t_i32);
	tys.push_back(p_t_i32);
	Function*F_llvm_atomic_cmp_swap32 = Intrinsic::getDeclaration(mod,Intrinsic::atomic_cmp_swap,tys);
	tys.clear();
	Function*F_llvm_memory_barrier = Intrinsic::getDeclaration(mod,Intrinsic::memory_barrier,tys);
	Function*F_llvm_readcyclecounter = Intrinsic::getDeclaration(mod,Intrinsic::readcyclecounter,tys);
	Function*F_llvm_trap = Intrinsic::getDeclaration(mod,Intrinsic::trap,tys);
	tys.clear();
	tys.push_back(t_i32);
	Function*F_llvm_ctlz32 = Intrinsic::getDeclaration(mod,Intrinsic::ctlz,tys);
	tys.clear();
	tys.push_back(t_i64);
	Function*F_llvm_ctlz64 = Intrinsic::getDeclaration(mod,Intrinsic::ctlz,tys);

	tys.clear();
	tys.push_back(t_float);
	Function*F_llvm_sqrt32 = Intrinsic::getDeclaration(mod,Intrinsic::sqrt,tys);
	Function*F_llvm_fma32 = Intrinsic::getDeclaration(mod,Intrinsic::fma,tys);
	tys.clear();
	tys.push_back(t_double);
	Function*F_llvm_sqrt64 = Intrinsic::getDeclaration(mod,Intrinsic::sqrt,tys);
	Function*F_llvm_fma64 = Intrinsic::getDeclaration(mod,Intrinsic::fma,tys);

	bool use_global_r = false;
	bool use_context_r = true;
	bool use_arg_r = false;

	bool use_local_cr = false;
	bool use_bitwise_cr = true;

	bool use_inline_rtoc = false;

	Value*cr_bits[32];
	Value*inline_rtoc=0;
	BasicBlock*bb_landing_pad;
	Value*landing_pad_value;

	args.clear();
	if (use_context_r) {
		args.push_back(p_t_i128);
	}
	if (use_arg_r) {
		args.push_back(p_t_i32);
		args.push_back(p_t_i64);
		args.push_back(p_t_i64);
		for (int i=0;i<32;i++) {
			args.push_back(p_t_i64);
		}
		args.push_back(p_t_i64);
		for (int i=0;i<32;i++) {
			args.push_back(p_t_i64);
		}
		args.push_back(p_t_i32);
	}
	FunctionType*FT = FunctionType::get(enable_longjmp ? t_i64 : t_void,args,false);
	PointerType*p_FT = PointerType::getUnqual(FT);

	//auto g_r_linkage = GlobalVariable::PrivateLinkage;
	auto g_r_linkage = GlobalVariable::ExternalLinkage;

	GlobalVariable*g_cr = new GlobalVariable(t_i32,false,g_r_linkage,UndefValue::get(t_i32),"g_cr");
	mod->getGlobalList().push_back(g_cr);
	GlobalVariable*g_lr = new GlobalVariable(t_i64,false,g_r_linkage,UndefValue::get(t_i64),"g_lr");
	mod->getGlobalList().push_back(g_lr);
	GlobalVariable*g_ctr = new GlobalVariable(t_i64,false,g_r_linkage,UndefValue::get(t_i64),"g_ctr");
	mod->getGlobalList().push_back(g_ctr);
	GlobalVariable*g_gpr[32];
	for (int i=0;i<32;i++) {
		g_gpr[i] = new GlobalVariable(t_i64,false,g_r_linkage,UndefValue::get(t_i64),format("g_gpr%d",i));
		mod->getGlobalList().push_back(g_gpr[i]);
	}
	GlobalVariable*g_xer = new GlobalVariable(t_i64,false,g_r_linkage,UndefValue::get(t_i64),"g_xer");
	mod->getGlobalList().push_back(g_xer);
	GlobalVariable*g_fpr[32];
	for (int i=0;i<32;i++) {
		g_fpr[i] = new GlobalVariable(t_i64,false,g_r_linkage,UndefValue::get(t_i64),format("g_fpr%d",i));
		mod->getGlobalList().push_back(g_fpr[i]);
	}
	GlobalVariable*g_fpscr = new GlobalVariable(t_i32,false,g_r_linkage,UndefValue::get(t_i32),"g_fpscr");
	mod->getGlobalList().push_back(g_fpscr);
	GlobalVariable*g_vr[32];
	for (int i=0;i<32;i++) {
		g_vr[i] = new GlobalVariable(t_i128,false,g_r_linkage,UndefValue::get(t_i128),format("g_vr%d",i));
		mod->getGlobalList().push_back(g_vr[i]);
	}
	GlobalVariable*g_vscr = new GlobalVariable(t_i32,false,g_r_linkage,UndefValue::get(t_i32),"g_vscr");
	mod->getGlobalList().push_back(g_vscr);

	GlobalVariable*g_ip = new GlobalVariable(t_i64,false,GlobalVariable::ExternalLinkage,UndefValue::get(t_i64),"g_ip");
	mod->getGlobalList().push_back(g_ip);

// 	GlobalVariable*g_debug_write_addr = new GlobalVariable(t_i64,false,GlobalVariable::ExternalLinkage,ConstantInt::get(t_i64,0),"g_debug_write_addr",false);
// 	mod->getGlobalList().push_back(g_debug_write_addr);

	ArrayType*t_image = ArrayType::get(t_i8,main_e.image_size);
	GlobalVariable*g_image = new GlobalVariable(t_image,false,GlobalVariable::ExternalLinkage,0,"g_image");
	mod->getGlobalList().push_back(g_image);

	auto translate_addr_back = [&](uint64_t addr) -> Value* {
		elf&e = main_e;
		for (auto i=e.sections.begin();i!=e.sections.end();++i) {
			uint64_t vaddr = (uint64_t)&e.image[0] + i->rvaddr;
			uint64_t size = i->memsz;
			if (addr>=vaddr&&addr<vaddr+size) {
				return B.CreateBitCast(B.CreateConstGEP2_64(g_image,0,addr - (uint64_t)&e.image[0]),t_i64);
			}
		}
		xcept("%p: cannot translate back unknown address %llx\n",(void*)xx_ip,(long long)addr);
		return 0;
	};

	auto r_name = [&](int n) -> std::string {
		if (n==r_cr) return "cr";
		else if (n==r_lr) return "lr";
		else if (n==r_ctr) return "ctr";
		else if (n<r_xer) return format("gpr[%d]",n-r_gpr);
		else if (n==r_xer) return "xer";
		else if (n<r_fpscr) return format("fpr[%d]",n-r_fpr);
		else if (n==r_fpscr) return "fpscr";
		else if (n<r_vscr) return format("vr[%d]",n-r_vr);
		else if (n==r_vscr) return "vscr";
		else if (n==r_vrsave) return "vrsave";
		else return "invalid register";
	};

	Function*F = 0;
	BasicBlock*BB = 0;
	BasicBlock*BB_entry = 0;
	auto switch_bb = [&](BasicBlock*new_bb) {
		B.SetInsertPoint(new_bb);
		BB = new_bb;
	};
	Value*context_r = 0;
	Value*cur_regs[r_count];
	auto alloc_cur_regs = [&]() {
		if (use_global_r) {
			return;
		}
		if (use_context_r) {
			for (int i=0;i<r_count;i++) {
				cur_regs[i] = B.CreateConstGEP1_32(context_r,i,r_name(i));
				if (i==r_cr || i==r_fpscr || i==r_vscr || i==r_vrsave) {
					cur_regs[i] = B.CreateBitCast(cur_regs[i],p_t_i32,r_name(i));
				} else if (i>=r_vr && i<r_vscr) {
					cur_regs[i] = B.CreateBitCast(cur_regs[i],p_t_i128,r_name(i));
				} else {
					cur_regs[i] = B.CreateBitCast(cur_regs[i],p_t_i64,r_name(i));
				}
			}
		}
		if (use_arg_r) {
			cur_regs[r_cr] = B.CreateAlloca(t_i32,0,"cr");
			cur_regs[r_lr] = B.CreateAlloca(t_i64,0,"lr");
			cur_regs[r_ctr] = B.CreateAlloca(t_i64,0,"ctr");
			for (int i=0;i<32;i++) {
				cur_regs[r_gpr+i] = B.CreateAlloca(t_i64,0,format("gpr%d",i));
			}
			cur_regs[r_xer] = B.CreateAlloca(t_i64,0,"xer");
			for (int i=0;i<32;i++) {
				cur_regs[r_fpr+i] = B.CreateAlloca(t_i64,0,format("fpr%d",i));
			}
			cur_regs[r_fpscr] = B.CreateAlloca(t_i32,0,"fpscr");
			xcept("vr");
			xcept("vscr");
		}
		if (use_local_cr) cur_regs[r_cr] = B.CreateAlloca(t_i32,0,"cr");
		if (use_bitwise_cr) {
			for (int i=0;i<32;i++) cr_bits[i] = B.CreateAlloca(t_i1,0,format("cr[bit%d]",i));
		}
	};
	//std::stack<Value*[r_count]> cur_regs_stack;
	auto enter_function = [&]() {
		if (use_context_r) {
			context_r = F->getArgumentList().begin();
		}
		alloc_cur_regs();
		if (use_arg_r) {
			auto i = F->getArgumentList().begin();
			for (int n=0;n<r_count;n++) B.CreateStore(B.CreateLoad((Argument*)i++),cur_regs[n]);
		}
	};
	auto leave_function = [&]() {
		//cur_regs = cur_regs_stack.top();
		//cur_regs_stack.pop();
	};

	int xer_so = 63-32;
	int xer_ov = 63-33;
	int xer_ca = 63-34;

	int vscr_sat = 127-127;

	auto get_global_r = [&](int n) -> GlobalVariable* {
		if (n==r_cr) return g_cr;
		else if (n==r_lr) return g_lr;
		else if (n==r_ctr) return g_ctr;
		else if (n<r_xer) return g_gpr[n-r_gpr];
		else if (n==r_xer) return g_xer;
		else if (n<r_fpscr) return g_fpr[n-r_fpr];
		else if (n==r_fpscr) return g_fpscr;
		else if (n<r_vscr) return g_vr[n-r_vr];
		else if (n==r_vscr) return g_vscr;
		else xcept("invalid register %d\n",n);
		return 0;
	};
	auto get_r = [&](int n) -> Value* {
		if (use_bitwise_cr && n==r_cr) xcept("get_r on bitwise cr");
		if (use_local_cr && n==r_cr) return cur_regs[r_cr];
		if (use_global_r) {
			return get_global_r(n);
		} else return cur_regs[n];
	};

	regval regvals[r_count];

	std::function<Value*(Value*,Value*)> AND,OR,LSHR,ASHR,SHL,MUL;
	std::function<Value*(Value*,uint64_t)> ANDI,ORI,LSHRI,ASHRI,SHLI,MULI;
	std::function<Value*(Value*,int)> TRUNC;
	std::function<Value*(Value*)> NOT;

	auto rr = [&](int n) -> Value* {
		if (use_bitwise_cr && n==r_cr) {
			Value*v = ConstantInt::get(t_i32,0);
			for (int i=0;i<32;i++) {
				v = B.CreateOr(v,B.CreateShl(B.CreateZExt(B.CreateLoad(cr_bits[i]),t_i32),ConstantInt::get(t_i32,i)));
			}
			return v;
		}
		return B.CreateLoad(get_r(n));
	};
	auto wr = [&](int n,Value*v) {
		regvals[n].t = regval::t_unknown;
		if (use_bitwise_cr && n==r_cr) {
			for (int i=0;i<32;i++) {
				B.CreateStore(B.CreateTrunc(v,t_i1),cr_bits[i]);
				v = B.CreateLShr(v,ConstantInt::get(t_i32,1));
			}
			return;
		}
		B.CreateStore(v,get_r(n));
	};
	auto rrbit = [&](int r,int n) -> Value* {
		if (use_bitwise_cr && r==r_cr) return B.CreateLoad(cr_bits[n]);
		return B.CreateTrunc(B.CreateLShr(B.CreateLoad(get_r(r)),n),t_i1);
	};
	auto wrbit = [&](int r,int n,Value*v) {
		if (v->getType()!=t_i1) xcept("wrbit type is not i1");
		regvals[n].t = regval::t_unknown;
		if (use_bitwise_cr && r==r_cr) {
			B.CreateStore(v,cr_bits[n]);
			return;
		}
		Value*rv = rr(r);
		wr(r,B.CreateOr(B.CreateAnd(rv,ConstantInt::get(rv->getType(),~(1<<n))),B.CreateShl(B.CreateZExt(v,rv->getType()),n)));
	};
// 	auto rr_sub = [&](int n,Value*subn,int bytes) -> Value* {
// 		return TRUNC(LSHR(rr(n),MULI(subn,bytes*8)),bytes);
// 	};
// 	auto wr_sub = [&](int n,Value*v,Value*subn,int bytes) {
// 		int b_m;
// 		if (bytes==1) b_m=0xff;
// 		else if (bytes==2) b_m=0xffff;
// 		else if (bytes==4) b_m=0xffffffff;
// 		else xcept("wr_sub: bad bytes %d",bytes);
// 		Value*ov = rr(n);
// 		Value*shift_n = MULI(subn,bytes*8);
// 		Value*m = SHL(ConstantInt::get(ov->getType(),b_m),shift_n);
// 		wr(n,OR(AND(ov,NOT(m)),SHL(B.CreateZExt(v,ov->getType()),shift_n)));
// 	};
	auto rr_subi = [&](int n,int subn,int bytes) -> Value* {
		Value*ov = rr(n);
		return TRUNC(LSHRI(ov,(ov->getType()->getPrimitiveSizeInBits()-bytes*8)-subn*bytes*8),bytes);
	};
	auto wr_subi = [&](int n,Value*v,int subn,int bytes) {
		uint64_t b_m;
		if (bytes==1) b_m=0xff;
		else if (bytes==2) b_m=0xffff;
		else if (bytes==4) b_m=0xffffffff;
		else if (bytes==8) b_m=0xffffffffffffffff;
		else xcept("wr_sub: bad bytes %d",bytes);
		Value*ov = rr(n);
		int shift_n = (ov->getType()->getPrimitiveSizeInBits()-bytes*8)-subn*bytes*8;
		Value*m = SHLI(ConstantInt::get(ov->getType(),b_m),shift_n);
		wr(n,OR(AND(ov,NOT(m)),SHLI(B.CreateZExt(v,ov->getType()),shift_n)));
	};

	auto rgr = [&](int n) -> Value* {
		if (n==2&&use_inline_rtoc) return inline_rtoc;
		return rr(r_gpr+n);
	};
	auto wgr = [&](int n,Value*v) {
		wr(r_gpr+n,v);
	};

	auto CAST32 = [&](Value*v) -> Value* {
		return B.CreateBitCast(v,t_i32);
	};
	auto CASTFP = [&](Value*v) -> Value* {
		return B.CreateBitCast(v,t_float);
	};

	auto rvr = [&](int n) -> Value* {
		return rr(r_vr+n);
	};
	auto rvr_subi = [&](int n,int subn,int bytes) -> Value* {
		return rr_subi(r_vr+n,subn,bytes);
	};
	auto rvrf_subi = [&](int n,int subn) -> Value* {
		return CASTFP(rr_subi(r_vr+n,subn,4));
	};
	auto wvr_subi = [&](int n,Value*v,int subn,int bytes) {
		wr_subi(r_vr+n,v,subn,bytes);
	};
	auto wvrf_subi = [&](int n,Value*v,int subn) {
		wr_subi(r_vr+n,CAST32(v),subn,4);
	};
// 	auto rvrb = [&](int n,Value*n2) -> Value* {
// 		// todo: debug_test_value_in_range(n2,0,31)
// 		return rr_sub(r_vr+n,n2,1);
// 	};
// 	auto rvrh = [&](int n,Value*n2) -> Value* {
// 		return rr_sub(r_vr+n,n2,2);
// 	};
// 	auto rvrw = [&](int n,Value*n2) -> Value* {
// 		return rr_sub(r_vr+n,n2,4);
// 	};
	auto wvr = [&](int n,Value*v) {
		wr(r_vr+n,v);
	};
// 	auto wvrb = [&](int n,Value*v,Value*n2) {
// 		wr_sub(r_vr+n,v,n2,1);
// 	};
// 	auto wvrh = [&](int n,Value*v,Value*n2) {
// 		wr_sub(r_vr+n,v,n2,2);
// 	};
// 	auto wvrw = [&](int n,Value*v,Value*n2) {
// 		wr_sub(r_vr+n,v,n2,4);
// 	};
	auto rfr = [&](int n) -> Value* {
		return B.CreateBitCast(rr(r_fpr+n),t_double);
	};
	auto wfr = [&](int n,Value*v) {
		wr(r_fpr+n,B.CreateBitCast(v,t_i64));
	};

	auto new_bb = [&]() -> BasicBlock* {
		return BasicBlock::Create(context, "", F);
	};

	typedef std::function<void()> voidcb;
	typedef std::function<Value*()> valuecb;

	auto k_if = [&](Value*cond,voidcb if_true,voidcb if_false) {
		BasicBlock*b_true = BasicBlock::Create(context, "", F);
		BasicBlock*b_false = BasicBlock::Create(context, "", F);
		BasicBlock*b_post = BasicBlock::Create(context, "", F);
		B.CreateCondBr(cond,b_true,b_false);
		switch_bb(b_true);
		if_true();
		B.CreateBr(b_post);
		switch_bb(b_false);
		if_false();
		B.CreateBr(b_post);
		switch_bb(b_post);
	};

	auto ci1 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i1,v);
	};
	auto ci5 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i5,v);
	};
	auto ci8 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i8,v);
	};
	auto ci16 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i16,v);
	};
	auto ci30 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i30,v);
	};
	auto ci32 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i32,v);
	};
	auto ci64 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i64,v);
	};
	auto ci128 = [&](uint64_t low,uint64_t high) -> Constant* {
		uint64_t buf[2];
		buf[0] = low;
		buf[1] = high;
		return ConstantInt::get(t_i128,APInt(128,2,buf));
	};
	auto ci256 = [&](uint64_t very_low,uint64_t low,uint64_t high,uint64_t very_high) -> Constant* {
		uint64_t buf[4];
		buf[0] = very_low;
		buf[1] = low;
		buf[2] = high;
		buf[3] = very_high;
		return ConstantInt::get(t_i256,APInt(256,4,buf));
	};

	auto image_offset = [&](uint64_t addr) -> Value* {
		return B.CreateBitCast(B.CreateConstGEP2_64(g_image,0,addr),t_i64);
	};


	std::map<std::string,Value*> str_map;
	auto get_str_ptr = [&](const char*str) -> Value* {
		Value*&v = str_map[str];
		if (!v) v = B.CreateGlobalStringPtr(str);
		return v;
	};

	auto mkerr = [&](const char*str) {
		B.CreateCall2(F_err,ci64(xx_ip),get_str_ptr(str));
	};
	auto mkprint = [&](const char*str) {
		B.CreateCall2(F_print,ci64(xx_ip),get_str_ptr(str));
	};
	auto mkdumpregs = [&]() {
		Value*args[13];
		args[0] = rgr(0);
		args[1] = rgr(1);
		args[2] = rr(r_gpr+2); // dodge rtoc optimization
		args[3] = rgr(3);
		args[4] = rgr(4);
		args[5] = rgr(5);
		args[6] = rgr(6);
		args[7] = rgr(7);
		args[8] = rgr(8);
		args[9] = rgr(9);
		args[10] = rgr(10);
		args[11] = rgr(11);
		args[12] = rgr(13);
		B.CreateCall(F_dump_regs,ArrayRef<Value*>(args,args+13));
	};

	args.clear();
	Function*I_personality = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"I_personality",mod);
	{
		F = I_personality;
		switch_bb(new_bb());
		mkerr("personality!");
		B.CreateRetVoid();
		verifyFunction(*F);
	}
	GlobalVariable*g_longjmp_t = new GlobalVariable(t_i64,false,GlobalVariable::ExternalLinkage,UndefValue::get(t_i64),"g_longjmp_t",false);
	mod->getGlobalList().push_back(g_longjmp_t);
	
	int current_function_call_count = 0;
	bool call_no_longjmp = false;
	bool call_no_lr = false;
	auto do_call = [&](Value*v) -> Value* {
		current_function_call_count++;
		//B.CreateCall(F_dump_addr,B.CreateLoad(B.CreateIntToPtr(ConstantInt::get(t_i64,0x10011380+0x10),p_t_i64)));
		if (debug_output&&debug_print_calls) B.CreateCall(F_stack_call,ConstantInt::get(t_i64,xx_ip));
		//if (debug_output&&debug_superverbose) B.CreateCall(F_stack_call,B.CreatePtrToInt(v,t_i64));
		if (!call_no_lr) wr(r_lr,image_offset(xx_ip+4 - main_e.reloc_base));
		Value*args[0x100];
		int arg_count=0;
		if (use_global_r) {
			arg_count=0;
		}
		if (use_context_r) {
			args[0] = context_r;
			arg_count=1;
		}
		if (use_arg_r) {
			for (int i=0;i<r_count;i++) args[i] = cur_regs[i];
			arg_count = r_count;
		}
		if (call_no_lr&&!call_no_longjmp) xcept("call_no_lr without call_no_longjmp makes no sense...");
		Value*rv=0;
		if (enable_longjmp && !call_no_longjmp) {
// 			BasicBlock*bb_normal=new_bb(),*bb_unwind=new_bb();
// 			B.CreateInvoke(v,bb_normal,bb_unwind,ArrayRef<Value*>(args,args+arg_count));
// 			switch_bb(bb_unwind);
// 			LandingPadInst*lp = B.CreateLandingPad(t_i1,I_personality,1);
// 			lp->addClause(g_longjmp_t);
// 			mkerr("unwind!");
// 			B.CreateBr(bb_normal);
// 			switch_bb(bb_normal);
			CallInst*r = B.CreateCall(v,ArrayRef<Value*>(args,args+arg_count));
			if (call_no_lr) r->setTailCall();
			rv=r;
			BasicBlock*bb_y=new_bb(),*bb_n=new_bb();
			B.CreateCondBr(B.CreateICmpNE(r,image_offset(xx_ip+4 - main_e.reloc_base)),bb_y,bb_n);
			switch_bb(bb_y);
			B.CreateStore(r,landing_pad_value);
			B.CreateBr(bb_landing_pad);
			switch_bb(bb_n);
		} else {
			CallInst*c = B.CreateCall(v,ArrayRef<Value*>(args,args+arg_count));
			if (call_no_lr) c->setTailCall();
			rv=c;
		}
		if (debug_output&&debug_print_calls&&!call_no_lr) B.CreateCall(F_stack_ret,ConstantInt::get(t_i64,xx_ip+4));
		return rv;
	};
	auto do_ret = [&]() {
		if (use_arg_r) {
			auto i = F->getArgumentList().begin();
			for (int n=0;n<r_count;n++) B.CreateStore(B.CreateLoad(cur_regs[n]),(Argument*)i++);
		}
		if (enable_longjmp) {
			B.CreateRet(rr(r_lr));
		} else {
			B.CreateRetVoid();
		}
	};

	auto do_longjmp = [&](Value*dst) {
// 		mkprint("longjmp");
// 		B.CreateCall(F_dump_addr,dst);
		//B.Insert(new UnwindInst(context));
		B.CreateRet(dst);
	};
	auto do_call_no_longjmp = [&](Value*v) {
		call_no_longjmp = true;
		do_call(v);
		call_no_longjmp = false;
	};
	auto do_tail_call = [&](Value*v) {
		if (use_arg_r) xcept("use_arg_r");
		call_no_longjmp = true;
		call_no_lr = true;
		Value*rv = do_call(v);
		call_no_lr = false;
		call_no_longjmp = false;
		if (enable_longjmp) B.CreateRet(rv);
		else B.CreateRetVoid();
	};

	auto EXTS = [&](Value*v) -> Value* {
		return B.CreateSExt(v,t_i64);
	};
	auto EXTS8 = [&](Value*v) -> Value* {
		return B.CreateSExt(v,t_i8);
	};
	auto EXTS32 = [&](Value*v) -> Value* {
		return B.CreateSExt(v,t_i32);
	};
	auto EXTS128 = [&](Value*v) -> Value* {
		return B.CreateSExt(v,t_i128);
	};
	auto EXTZ = [&](Value*v) -> Value* {
		return B.CreateZExtOrBitCast(v,t_i64);
	};
	auto EXTZ32 = [&](Value*v) -> Value* {
		return B.CreateZExtOrBitCast(v,t_i32);
	};
	auto EXTZ128 = [&](Value*v) -> Value* {
		return B.CreateZExtOrBitCast(v,t_i128);
	};
	auto EXTZ256 = [&](Value*v) -> Value* {
		return B.CreateZExtOrBitCast(v,t_i256);
	};
	auto check_safe_shift = [&](Value*a,Value*b) {
		if (!debug_tests) return;
		int bits = a->getType()->getPrimitiveSizeInBits();
		k_if(B.CreateICmpUGE(b,ConstantInt::get(b->getType(),bits)),[&]() {
			mkerr("bad shift");
		},[&](){});
	};

	auto ROTL = [&](Value*a,Value*b) -> Value* {
		if (a->getType()!=b->getType()) xcept("ROTL with mismatching types (ip %llx)",(long long)xx_ip);
		return B.CreateSelect(B.CreateICmpEQ(b,ConstantInt::get(b->getType(),0)),a,OR(SHL(a,b),B.CreateLShr(a,B.CreateSub(ConstantInt::get(b->getType(),a->getType()->getPrimitiveSizeInBits()),b))));
	};

	SHL = [&](Value*a,Value*b) -> Value* {
		if (a->getType()!=b->getType()) xcept("SHL with mismatching types (ip %llx)",(long long)xx_ip);
		check_safe_shift(a,b);
		return B.CreateShl(a,b);
	};
	LSHR = [&](Value*a,Value*b) -> Value* {
		if (a->getType()!=b->getType()) xcept("LSHR with mismatching types (ip %llx)",(long long)xx_ip);
		check_safe_shift(a,b);
		return B.CreateLShr(a,b);
	};
	ASHR = [&](Value*a,Value*b) -> Value* {
		if (a->getType()!=b->getType()) xcept("ASHR with mismatching types (ip %llx)",(long long)xx_ip);
		check_safe_shift(a,b);
		return B.CreateAShr(a,b);
	};
	SHLI = [&](Value*v,uint64_t n) {
		return SHL(v,ConstantInt::get(v->getType(),n));
	};
	LSHRI = [&](Value*v,uint64_t n) {
		return LSHR(v,ConstantInt::get(v->getType(),n));
	};
	ASHRI = [&](Value*v,uint64_t n) {
		return ASHR(v,ConstantInt::get(v->getType(),n));
	};
	TRUNC = [&](Value*v,int n) -> Value* {
		if (n==1) return B.CreateTrunc(v,t_i8);
		if (n==2) return B.CreateTrunc(v,t_i16);
		if (n==4) return B.CreateTrunc(v,t_i32);
		if (n==8) return B.CreateTruncOrBitCast(v,t_i64);
		if (n==16) return B.CreateTruncOrBitCast(v,t_i128);
		xcept("TRUNC bad n %d",n);
		return 0;
	};

	auto overflow_test = [&](Value*v) -> Value* {
		Value*r = B.CreateExtractValue(v,0);
		Value*o = B.CreateExtractValue(v,1);
		wrbit(r_xer,xer_so,B.CreateOr(rrbit(r_xer,xer_so),o));
		wrbit(r_xer,xer_ov,o);
		return r;
	};
	auto carry_test = [&](Value*v) -> Value* {
		Value*r = B.CreateExtractValue(v,0);
		Value*o = B.CreateExtractValue(v,1);
		wrbit(r_xer,xer_ca,o);
		return r;
	};
	auto overflow_test2 = [&](Value*v) -> Value* {
		Value*r = B.CreateExtractValue(v,0);
		Value*o = B.CreateExtractValue(v,1);
		wrbit(r_xer,xer_so,B.CreateOr(rrbit(r_xer,xer_so),o));
		wrbit(r_xer,xer_ov,B.CreateOr(rrbit(r_xer,xer_ov),o));
		return r;
	};
	auto carry_test2 = [&](Value*v) -> Value* {
		Value*r = B.CreateExtractValue(v,0);
		Value*o = B.CreateExtractValue(v,1);
		wrbit(r_xer,xer_ca,B.CreateOr(rrbit(r_xer,xer_ca),o));
		return r;
	};

	auto SADDO = [&](Value*a,Value*b) -> Value* {
		return B.CreateCall2(F_llvm_sadd_with_overflow64,a,b);
	};
	auto UADDO = [&](Value*a,Value*b) -> Value* {
		return B.CreateCall2(F_llvm_uadd_with_overflow64,a,b);
	};
	auto SADDO3 = [&](Value*a,Value*b,Value*c) -> Value* {
		Value*t = B.CreateCall2(F_llvm_sadd_with_overflow64,a,b);
		Value*r = B.CreateCall2(F_llvm_sadd_with_overflow64,B.CreateExtractValue(t,0),c);
		return B.CreateInsertValue(r,B.CreateOr(B.CreateExtractValue(t,1),B.CreateExtractValue(r,1)),1);
	};
	auto UADDO3 = [&](Value*a,Value*b,Value*c) -> Value* {
		Value*t = B.CreateCall2(F_llvm_uadd_with_overflow64,a,b);
		Value*r = B.CreateCall2(F_llvm_uadd_with_overflow64,B.CreateExtractValue(t,0),c);
		return B.CreateInsertValue(r,B.CreateOr(B.CreateExtractValue(t,1),B.CreateExtractValue(r,1)),1);
	};
	auto SSUBO = [&](Value*a,Value*b) -> Value* {
		return B.CreateCall2(F_llvm_ssub_with_overflow64,a,b);
	};
	auto USUBO = [&](Value*a,Value*b) -> Value* {
		return B.CreateCall2(F_llvm_usub_with_overflow64,a,b);
	};

	auto SMULO = [&](Value*a,Value*b) -> Value* {
		return B.CreateCall2(F_llvm_smul_with_overflow64,a,b);
	};
	auto UMULO = [&](Value*a,Value*b) -> Value* {
		return B.CreateCall2(F_llvm_umul_with_overflow64,a,b);
	};

	auto SUB = [&](Value*a,Value*b) -> Value* {
		return B.CreateSub(a,b);
	};

	auto ADD = [&](Value*a,Value*b) -> Value* {
		return B.CreateAdd(a,b);
	};
	auto ADDO = [&](Value*a,Value*b) -> Value* {
		return overflow_test(SADDO(a,b));
	};
	auto ADDC = [&](Value*a,Value*b) -> Value* {
		return carry_test(UADDO(a,b));
	};
	auto ADDCO = [&](Value*a,Value*b) -> Value* {
		ADDO(a,b);
		return ADDC(a,b);
	};
	auto ADDE = [&](Value*a,Value*b) -> Value* {
		return carry_test(UADDO3(a,b,EXTZ(rrbit(r_xer,xer_ca))));
	};
	auto ADDEO = [&](Value*a,Value*b) -> Value* {
		overflow_test(SADDO3(a,b,EXTZ(rrbit(r_xer,xer_ca))));
		return ADDE(a,b);
	};
	auto ADDME = [&](Value*a) -> Value* {
		Value*t = carry_test(UADDO(a,EXTZ(rrbit(r_xer,xer_ca))));
		return carry_test2(USUBO(t,ci64(1)));
	};
	auto ADDMEO = [&](Value*a) -> Value* {
		Value*t = overflow_test(UADDO(a,EXTZ(rrbit(r_xer,xer_ca))));
		overflow_test2(USUBO(t,ci64(1)));
		return ADDME(a);
	};
	auto ADDZE = [&](Value*a) -> Value* {
		return carry_test(UADDO(a,EXTZ(rrbit(r_xer,xer_ca))));
	};
	auto ADDZEO = [&](Value*a) -> Value* {
		Value*t = overflow_test(UADDO(a,EXTZ(rrbit(r_xer,xer_ca))));
		return ADDZE(a);
	};
	auto SUBF = [&](Value*a,Value*b) -> Value* { // note: subf is b - a, not a - b (implemented as ~a + b + 1)
		return B.CreateAdd(B.CreateAdd(B.CreateNot(a),b),ci64(1));
	};
	auto SUBFO = [&](Value*a,Value*b) -> Value* {
		return overflow_test(SADDO3(B.CreateNot(a),b,ci64(1)));
	};
	auto SUBFC = [&](Value*a,Value*b) -> Value* {
		return carry_test(SADDO3(B.CreateNot(a),b,ci64(1)));
	};
	auto SUBFCO = [&](Value*a,Value*b) -> Value* {
		SUBFO(a,b);
		return SUBFC(a,b);
	};
	auto SUBFE = [&](Value*a,Value*b) -> Value* {
		return carry_test(UADDO3(B.CreateNot(a),b,EXTZ(rrbit(r_xer,xer_ca))));
	};
	auto SUBFEO = [&](Value*a,Value*b) -> Value* {
		overflow_test(SADDO3(B.CreateNot(a),b,EXTZ(rrbit(r_xer,xer_ca))));
		return SUBFE(a,b);
	};
	auto SUBFME = [&](Value*a) -> Value* {
		Value*t = carry_test(UADDO(B.CreateNot(a),EXTZ(rrbit(r_xer,xer_ca))));
		return carry_test2(USUBO(t,ci64(1)));
	};
	auto SUBFMEO = [&](Value*a) -> Value* {
		Value*t = overflow_test(UADDO(B.CreateNot(a),EXTZ(rrbit(r_xer,xer_ca))));
		overflow_test2(USUBO(t,ci64(1)));
		return SUBFME(a);
	};
	auto SUBFZE = [&](Value*a) -> Value* {
		return carry_test(UADDO(B.CreateNot(a),EXTZ(rrbit(r_xer,xer_ca))));
	};
	auto SUBFZEO = [&](Value*a) -> Value* {
		Value*t = overflow_test(UADDO(B.CreateNot(a),EXTZ(rrbit(r_xer,xer_ca))));
		return ADDZE(a);
	};
	auto NEG = [&](Value*a) -> Value* {
		return B.CreateAdd(B.CreateNot(a),ConstantInt::get(a->getType(),1));
	};
	auto NEGO = [&](Value*a) -> Value* {
		return overflow_test(SADDO(B.CreateNot(a),ci64(1)));
	};
	MUL = [&](Value*a,Value*b) -> Value* {
		return B.CreateMul(a,b);
	};
	MULI = [&](Value*a,uint64_t n) -> Value* {
		return B.CreateMul(a,ConstantInt::get(a->getType(),n));
	};
	auto MULO = [&](Value*a,Value*b) -> Value* {
		return overflow_test(SMULO(a,b));
	};
	auto DIVS = [&](Value*a,Value*b) -> Value* {
		return B.CreateSDiv(a,b);
	};
	auto DIVU = [&](Value*a,Value*b) -> Value* {
		return B.CreateUDiv(a,b);
	};

	auto MULHW = [&](Value*a,Value*b) -> Value* {
		a = EXTS(TRUNC(a,4));
		b = EXTS(TRUNC(b,4));
		return ASHRI(MUL(a,b),32);
	};
	auto MULLW = [&](Value*a,Value*b) -> Value* {
		a = EXTS(TRUNC(a,4));
		b = EXTS(TRUNC(b,4));
		return MUL(a,b);
	};
	auto MULLWO = [&](Value*a,Value*b) -> Value* {
		a = EXTS(TRUNC(a,4));
		b = EXTS(TRUNC(b,4));
		MULO(a,b);
		return MUL(a,b);
	};
	auto MULHWU = [&](Value*a,Value*b) -> Value* {
		a = EXTZ(TRUNC(a,4));
		b = EXTZ(TRUNC(b,4));
		return ASHRI(MUL(a,b),32);
	};
	auto DIVW = [&](Value*a,Value*b) -> Value* {
		a = EXTS(TRUNC(a,4));
		b = EXTS(TRUNC(b,4));
		return DIVS(a,b);
	};
	auto DIVWU = [&](Value*a,Value*b) -> Value* {
		a = EXTZ(TRUNC(a,4));
		b = EXTZ(TRUNC(b,4));
		return DIVU(a,b);
	};

	auto MULLD = [&](Value*a,Value*b) -> Value* {
		return MUL(a,b);
	};
	auto MULLDO = [&](Value*a,Value*b) -> Value* {
		return MULO(a,b);
	};
	auto MULHD = [&](Value*a,Value*b) -> Value* {
		a = EXTS128(a);
		b = EXTS128(b);
		return TRUNC(ASHRI(MUL(a,b),64),8);
	};
	auto MULHDU = [&](Value*a,Value*b) -> Value* {
		a = EXTZ128(a);
		b = EXTZ128(b);
		return TRUNC(ASHRI(MUL(a,b),64),8);
	};
	auto DIVD = [&](Value*a,Value*b) -> Value* {
		return DIVS(a,b);
	};
	auto DIVDU = [&](Value*a,Value*b) -> Value* {
		return DIVU(a,b);
	};

	auto ADDI = [&](Value*v,uint64_t n) -> Value* {
		return ADD(v,ConstantInt::get(v->getType(),n));
	};
	auto SUBI = [&](Value*v,uint64_t n) -> Value* {
		return SUB(v,ConstantInt::get(v->getType(),n));
	};

	NOT = [&](Value*a) -> Value* {
		return B.CreateNot(a);
	};
	AND = [&](Value*a,Value*b) -> Value* {
		return B.CreateAnd(a,b);
	};
	OR = [&](Value*a,Value*b) -> Value* {
		return B.CreateOr(a,b);
	};
	auto XOR = [&](Value*a,Value*b) -> Value* {
		return B.CreateXor(a,b);
	};
	auto NAND = [&](Value*a,Value*b) -> Value* {
		return NOT(AND(a,b));
	};
	auto NOR = [&](Value*a,Value*b) -> Value* {
		return NOT(OR(a,b));
	};
	auto EQV = [&](Value*a,Value*b) -> Value* {
		return NOT(XOR(a,b));
	};
	auto ANDC = [&](Value*a,Value*b) -> Value* {
		return AND(a,NOT(b));
	};
	auto ORC = [&](Value*a,Value*b) -> Value* {
		return OR(a,NOT(b));
	};

	ANDI = [&](Value*v,uint64_t n) {
		return B.CreateAnd(v,ConstantInt::get(v->getType(),n));
	};

	auto SLW = [&](Value*a,Value*b) -> Value* {
		Value*z = B.CreateICmpNE(ANDI(b,32),ConstantInt::get(b->getType(),0));
		b = ANDI(b,31);
		Value*r = SHL(TRUNC(a,4),TRUNC(b,4));
		r = B.CreateSelect(z,ci32(0),r);
		return EXTZ(r);
	};
	auto SRW = [&](Value*a,Value*b) -> Value* {
		Value*z = B.CreateICmpNE(ANDI(b,32),ConstantInt::get(b->getType(),0));
		b = ANDI(b,31);
		Value*r = LSHR(TRUNC(a,4),TRUNC(b,4));
		r = B.CreateSelect(z,ci32(0),r);
		return EXTZ(r);
	};
	auto SRAW = [&](Value*a,Value*b) -> Value* {
		Value*z = B.CreateICmpNE(ANDI(b,32),ConstantInt::get(b->getType(),0));
		b = ANDI(b,31);
		Value*s = TRUNC(b,4);
		Value*ov = TRUNC(a,4);
		Value*r = ASHR(ov,s);
		Value*v = SHL(r,s);
		wrbit(r_xer,xer_ca,AND(B.CreateICmpSLT(ov,ci32(0)),B.CreateICmpNE(ov,v)));
		r = B.CreateSelect(z,ci32(0),r);
		return EXTS(r);
	};
	auto SLD = [&](Value*a,Value*b) -> Value* {
		Value*z = B.CreateICmpNE(ANDI(b,64),ConstantInt::get(b->getType(),0));
		b = ANDI(b,63);
		Value*r = SHL(a,b);
		r = B.CreateSelect(z,ci64(0),r);
		return r;
	};
	auto SRD = [&](Value*a,Value*b) -> Value* {
		Value*z = B.CreateICmpNE(ANDI(b,64),ConstantInt::get(b->getType(),0));
		b = ANDI(b,63);
		Value*r = LSHR(a,b);
		r = B.CreateSelect(z,ci64(0),r);
		return r;
	};
	auto SRAD = [&](Value*a,Value*b) -> Value* {
		Value*z = B.CreateICmpNE(ANDI(b,64),ConstantInt::get(b->getType(),0));
		b = ANDI(b,63);
		Value*s = b;
		Value*ov = a;
		Value*r = ASHR(ov,s);
		Value*v = SHL(r,s);
		wrbit(r_xer,xer_ca,AND(B.CreateICmpSLT(ov,ci64(0)),B.CreateICmpNE(ov,v)));
		r = B.CreateSelect(z,ci64(0),r);
		return r;
	};

	auto BSL = [&](Value*v,uint64_t mask,uint64_t shift) -> Value* {
		return SHLI(ANDI(v,mask),shift);
	};
	auto BSR = [&](Value*v,uint64_t mask,uint64_t shift) -> Value* {
		return LSHRI(ANDI(v,mask),shift);
	};
	auto BSWAP16 = [&](Value*v) -> Value* {
		return B.CreateCall(F_llvm_bswap16,v);
		//return OR(BSR(v,0xff00,8),BSL(v,0xff,8));
	};
	auto BSWAP32 = [&](Value*v) -> Value* {
		return B.CreateCall(F_llvm_bswap32,v);
// 		return
// 			OR(BSR(v,0xff000000,24),
// 			OR(BSR(v,0xff0000,8),
// 			OR(BSL(v,0xff00,8),
// 			   BSL(v,0xff,24)
// 			)));
	};
	auto BSWAP64 = [&](Value*v) -> Value* {
		return B.CreateCall(F_llvm_bswap64,v);
// 		return
// 			OR(BSR(v,0xff00000000000000,56),
// 			OR(BSR(v,0xff000000000000,40),
// 			OR(BSR(v,0xff0000000000,24),
// 			OR(BSR(v,0xff00000000,8),
// 			OR(BSL(v,0xff000000,8),
// 			OR(BSL(v,0xff0000,24),
// 			OR(BSL(v,0xff00,40),
// 			   BSL(v,0xff,56)
// 			)))))));
	};
	auto BSWAP128 = [&](Value*v) -> Value* {
		return B.CreateCall(F_llvm_bswap128,v);
	};

	auto RMEM = [&](Value*ea,int n) -> Value* {
		if (ea->getType()!=t_i64) xcept("RMEM ea not 64 bits");
		if (debug_tests) {
			k_if(B.CreateICmpEQ(ea,ConstantInt::get(ea->getType(),0)),[&]() {
				mkerr("attempt to read from null address");
			},[&](){});
		}
		if (debug_output&&debug_superverbose) {
 			B.CreateCall(F_dump_addr,ea);
		}
		Value*v = 0;
		if (n==1) v=B.CreateLoad(B.CreateIntToPtr(ea,p_t_i8));
		if (n==2) v=BSWAP16(B.CreateLoad(B.CreateIntToPtr(ea,p_t_i16)));
		if (n==4) v=BSWAP32(B.CreateLoad(B.CreateIntToPtr(ea,p_t_i32)));
		if (n==8) v=BSWAP64(B.CreateLoad(B.CreateIntToPtr(ea,p_t_i64)));
		if (n==16) v=BSWAP128(B.CreateLoad(B.CreateIntToPtr(ea,p_t_i128)));
		if (!v) xcept("RMEM bad n %d",n);
		if (debug_output&&debug_superverbose) {
			if (n<16) B.CreateCall(F_dump_addr,EXTZ(v));
		}
		return v;
	};
	auto WMEM = [&](Value*ea,int n,Value*v) -> Value* {
		if (debug_tests) {
			k_if(B.CreateICmpEQ(ea,ConstantInt::get(ea->getType(),0)),[&]() {
				mkerr("attempt to write to null address");
			},[&](){});
		}
		if (debug_output&&debug_superverbose) {
			B.CreateCall(F_dump_addr,ea);
			if (n<16) B.CreateCall(F_dump_addr,EXTZ(v));
		}
// 		k_if(B.CreateICmpEQ(ea,B.CreateLoad(g_debug_write_addr)),[&]() {
// 			mkdumpregs();
// 			B.CreateCall(F_dump_addr,ea);
// 			mkerr("!!WRITE!!");
// 		},[&](){});
		if (n==1) return B.CreateStore(v,B.CreateIntToPtr(ea,p_t_i8));
		if (n==2) return B.CreateStore(BSWAP16(v),B.CreateIntToPtr(ea,p_t_i16));
		if (n==4) return B.CreateStore(BSWAP32(v),B.CreateIntToPtr(ea,p_t_i32));
		if (n==8) return B.CreateStore(BSWAP64(v),B.CreateIntToPtr(ea,p_t_i64));
		if (n==16) return B.CreateStore(BSWAP128(v),B.CreateIntToPtr(ea,p_t_i128));
		xcept("WMEM bad n %d",n);
		return 0;
	};

	auto ROTL64 = [&](Value*a,Value*b) -> Value* {
		// this select is needed since shl 64 is not allowed
		return B.CreateSelect(B.CreateICmpEQ(b,ci64(0)),a,B.CreateOr(B.CreateShl(a,b),B.CreateLShr(a,B.CreateSub(ci64(64),b))));
	};
	auto ROTL32 = [&](Value*a,Value*b) -> Value* {
		a = OR(EXTZ(TRUNC(a,4)),B.CreateShl(EXTZ(TRUNC(a,4)),ci64(32)));
		return ROTL64(a,b);
	};
	auto MASK = [&](int s,int e) -> Value* {
		uint64_t v = 1LL<<(s&0x3f);
		while ((s&0x3f)!=(e&0x3f)) {
			s++;
			v |= 1LL<<(s&0x3f);
		}
		return ci64(v);
	};

	auto RMEMF = [&](Value*ea,int n) -> Value* {
		if (n==4) return B.CreateBitCast(RMEM(ea,n),t_float);
		else if (n==8) return B.CreateBitCast(RMEM(ea,n),t_double);
		else xcept("bad n in RMEMF");
		return 0;
	};
	auto WMEMF = [&](Value*ea,int n,Value*v) -> Value* {
		if ((n==4&&v->getType()!=t_float)||(n==8&&v->getType()!=t_double)) xcept("bad type in WMEMF");
		if (n==4) return WMEM(ea,n,B.CreateBitCast(v,t_i32));
		else if (n==8) return WMEM(ea,n,B.CreateBitCast(v,t_i64));
		else xcept("bad n in WMEMF");
		return 0;
	};

	auto DOUBLE = [&](Value*v) -> Value* {
		if (v->getType()==t_double) {
			return v;
		} else if (v->getType()==t_float) {
			return B.CreateFPExt(v,t_double);
		}
		else xcept("unknown type in DOUBLE");
		return 0;
	};
	auto SINGLE = [&](Value*v) -> Value* {
		if (v->getType()==t_double) {
			return B.CreateFPTrunc(v,t_float);
		} else if (v->getType()==t_float) {
			return v;
		}
		else xcept("unknown type in SINGLE");
		return 0;
	};
	auto FADD = [&](Value*a,Value*b) -> Value* {
		return B.CreateFAdd(a,b);
	};
	auto FSUB = [&](Value*a,Value*b) -> Value* {
		return B.CreateFSub(a,b);
	};
	auto FMUL = [&](Value*a,Value*b) -> Value* {
		return B.CreateFMul(a,b);
	};
	auto FDIV = [&](Value*a,Value*b) -> Value* {
		return B.CreateFDiv(a,b);
	};
	auto FSQRT = [&](Value*a) -> Value* {
// 		mkerr("FSQRT");
// 		return a;
		if (a->getType()==t_float) return B.CreateCall(F_llvm_sqrt32,a);
		else return B.CreateCall(F_llvm_sqrt64,a);
// 		if (a->getType()==t_float) return B.CreateCall(F_sqrt32,a);
// 		else return B.CreateCall(F_sqrt64,a);
	};
	auto FMADD = [&](Value*a,Value*b,Value*c) -> Value* {
		//mkerr("FMADD");
		//return a;
		//if (a->getType()==t_float) return B.CreateCall3(F_llvm_fma32,a,c,b);
		//else return B.CreateCall3(F_llvm_fma64,a,c,b);
		return FADD(FMUL(a,c),b);
	};
	auto FMSUB = [&](Value*a,Value*b,Value*c) -> Value* {
		return FMADD(a,FSUB(ConstantFP::getNegativeZero(b->getType()),b),c);
	};
	auto FNMADD = [&](Value*a,Value*b,Value*c) -> Value* {
		return FSUB(ConstantFP::getNegativeZero(a->getType()),FMADD(a,b,c));
	};
	auto FNMSUB = [&](Value*a,Value*b,Value*c) -> Value* {
		return FSUB(ConstantFP::getNegativeZero(a->getType()),FMSUB(a,b,c));
	};
	auto FMAX = [&](Value*a,Value*b) -> Value* {
		return B.CreateSelect(B.CreateFCmpOGE(a,b),a,b);
	};
	auto FMIN = [&](Value*a,Value*b) -> Value* {
		return B.CreateSelect(B.CreateFCmpOGE(a,b),b,a);
	};

	// MSB is bit 0
	auto getbits = [&](Value*v,int from,int to) -> Value* {
		int bits = to-from+1;
		int shift = v->getType()->getPrimitiveSizeInBits()-(to+1);
		if (shift) v = LSHRI(v,shift);
		return B.CreateTruncOrBitCast(v,get_int_type(bits));
	};
	auto concatbits = [&](Value*a,Value*b) -> Value* {
		int abits = a->getType()->getPrimitiveSizeInBits();
		int bbits = b->getType()->getPrimitiveSizeInBits();
		Type*t = get_int_type(abits+bbits);
		a = B.CreateZExtOrBitCast(a,t);
		b = B.CreateZExtOrBitCast(b,t);
		return OR(SHLI(a,bbits),b);
	};

	auto ConvertSPtoSXWsaturate = [&](Value*x,Value*y) -> Value* {

		BasicBlock*bb_entry = new_bb();
		B.CreateBr(bb_entry);
		switch_bb(bb_entry);

		x = CAST32(x);

		Value*sign = getbits(x,0,0);
		Value*exp = getbits(x,1,8);
		Value*frac = concatbits(getbits(x,9,31),ci8(0));

		BasicBlock*bb_post = new_bb();
		BasicBlock*bb_zero = new_bb();
		BasicBlock*bb_sat = new_bb();
		BasicBlock*bb0 = new_bb();
		B.CreateCondBr(AND(B.CreateICmpEQ(exp,ci8(255)),B.CreateICmpNE(frac,ConstantInt::get(frac->getType(),0))),bb_zero,bb0);

		switch_bb(bb_zero);
		B.CreateBr(bb_post);

		switch_bb(bb0);
		Value*real_exp = SUBI(ADD(B.CreateZExt(exp,t_i32),B.CreateSExt(y,t_i32)),127);
		BasicBlock*bb1 = new_bb();
		B.CreateCondBr(AND(B.CreateICmpEQ(exp,ci8(255)),B.CreateICmpEQ(frac,ConstantInt::get(frac->getType(),0))),bb_sat,bb1);
		switch_bb(bb1);
		BasicBlock*bb2 = new_bb();
		B.CreateCondBr(B.CreateICmpSGT(real_exp,ci32(30)),bb_sat,bb2);

		switch_bb(bb_sat);
		wrbit(r_vscr,vscr_sat,ci1(1));
		Value*satval = B.CreateSelect(sign,ci32(0x80000000),ci32(0x7fffffff));
		B.CreateBr(bb_post);

		switch_bb(bb2);
		BasicBlock*bb3 = new_bb();
		B.CreateCondBr(B.CreateICmpSLT(real_exp,ci32(0)),bb_zero,bb3);
		switch_bb(bb3);
		Value*significand = LSHR(concatbits(ci1(1),frac),SUB(ci32(31),real_exp));
		significand = B.CreateSelect(sign,NEG(significand),significand);
		bb3 = BB;
		B.CreateBr(bb_post);
		switch_bb(bb_post);
		PHINode*ret_phi = B.CreatePHI(t_i32,3);
		ret_phi->addIncoming(ci32(0),bb_zero);
		ret_phi->addIncoming(satval,bb_sat);
		ret_phi->addIncoming(significand,bb3);
		return CASTFP(ret_phi);
	};
	auto ConvertSPtoUXWsaturate = [&](Value*x,Value*y) -> Value* {

		BasicBlock*bb_entry = new_bb();
		B.CreateBr(bb_entry);
		switch_bb(bb_entry);

		x = CAST32(x);

		Value*sign = getbits(x,0,0);
		Value*exp = getbits(x,1,8);
		Value*frac = concatbits(getbits(x,9,31),ci8(0));

		BasicBlock*bb_post = new_bb();
		BasicBlock*bb_zero = new_bb();
		BasicBlock*bb_sat = new_bb();
		BasicBlock*bb0 = new_bb();
		B.CreateCondBr(AND(B.CreateICmpEQ(exp,ci8(255)),B.CreateICmpNE(frac,ConstantInt::get(frac->getType(),0))),bb_zero,bb0);

		switch_bb(bb_zero);
		B.CreateBr(bb_post);

		switch_bb(bb0);
		Value*real_exp = SUBI(ADD(B.CreateZExt(exp,t_i32),B.CreateSExt(y,t_i32)),127);
		BasicBlock*bb1 = new_bb();
		B.CreateCondBr(AND(B.CreateICmpEQ(exp,ci8(255)),B.CreateICmpEQ(frac,ConstantInt::get(frac->getType(),0))),bb_sat,bb1);
		switch_bb(bb1);
		BasicBlock*bb2 = new_bb();
		B.CreateCondBr(B.CreateICmpSGT(real_exp,ci32(31)),bb_sat,bb2);

		switch_bb(bb_sat);
		wrbit(r_vscr,vscr_sat,ci1(1));
		Value*satval = B.CreateSelect(sign,ci32(0x00000000),ci32(0xffffffff));
		B.CreateBr(bb_post);

		switch_bb(bb2);
		BasicBlock*bb3 = new_bb();
		BasicBlock*bb2_5 = new_bb();
		B.CreateCondBr(B.CreateICmpSLT(real_exp,ci32(0)),bb_zero,bb2_5);
		switch_bb(bb2_5);
		BasicBlock*bb_sat_zero = new_bb();
		B.CreateCondBr(sign,bb_sat_zero,bb3);

		switch_bb(bb_sat_zero);
		wrbit(r_vscr,vscr_sat,ci1(1));
		B.CreateBr(bb_zero);

		switch_bb(bb3);
		Value*significand = LSHR(concatbits(ci1(1),frac),SUB(ci32(31),real_exp));
		bb3 = BB;
		B.CreateBr(bb_post);
		switch_bb(bb_post);
		PHINode*ret_phi = B.CreatePHI(t_i32,3);
		ret_phi->addIncoming(ci32(0),bb_zero);
		ret_phi->addIncoming(satval,bb_sat);
		ret_phi->addIncoming(significand,bb3);
		return CASTFP(ret_phi);
	};

	auto ConvertSXWtoSP = [&](Value*x) -> Value* {

		BasicBlock*bb_entry = new_bb();
		B.CreateBr(bb_entry);
		switch_bb(bb_entry);

		x = CAST32(x);

		Value*sign = getbits(x,0,0);
		Value*exp = ci8(32+127);
		Value*frac = concatbits(sign,x);
		BasicBlock*bb_post = new_bb();
		BasicBlock*bb_zero = new_bb();
		BasicBlock*bb0 = new_bb();
		B.CreateCondBr(B.CreateICmpEQ(frac,ConstantInt::get(frac->getType(),0)),bb_zero,bb0);
		switch_bb(bb_zero);
		B.CreateBr(bb_post);
		switch_bb(bb0);
		frac = B.CreateSelect(sign,NEG(frac),frac);
		BasicBlock*bb_loop = new_bb();
		BasicBlock*bb1 = new_bb();
		BasicBlock*bb2 = new_bb();
		B.CreateBr(bb_loop);
		switch_bb(bb_loop);
		PHINode*phi_frac = B.CreatePHI(frac->getType(),2);
		PHINode*phi_exp = B.CreatePHI(exp->getType(),2);
		phi_frac->addIncoming(frac,bb0);
		phi_exp->addIncoming(exp,bb0);
		frac = phi_frac;
		exp = phi_exp;
		B.CreateCondBr(B.CreateICmpEQ(getbits(frac,0,0),ci1(0)),bb1,bb2);
		switch_bb(bb1);
		Value*tmp_frac = SHLI(frac,1);
		Value*tmp_exp = SUBI(exp,1);
		phi_frac->addIncoming(tmp_frac,BB);
		phi_exp->addIncoming(tmp_exp,BB);
		B.CreateBr(bb_loop);
		switch_bb(bb2);
		Value*lsb = getbits(frac,23,23);
		Value*gbit = getbits(frac,24,24);
		Value*xbit = B.CreateICmpNE(getbits(frac,25,32),ci8(0));
		Value*inc = OR(AND(lsb,gbit),AND(gbit,xbit));
		Value*tmp = getbits(frac,0,23);
		frac = ADD(tmp,B.CreateZExt(inc,t_i24));
		exp = B.CreateSelect(B.CreateICmpULT(frac,tmp),ADDI(exp,1),exp);
		Value*rv = concatbits(concatbits(sign,exp),getbits(frac,1,23));
		bb2 = BB;
		B.CreateBr(bb_post);
		switch_bb(bb_post);
		PHINode*ret_phi = B.CreatePHI(t_i32,2);
		ret_phi->addIncoming(ci32(0),bb_zero);
		ret_phi->addIncoming(rv,bb2);
		return CASTFP(ret_phi);
	};
	auto ConvertUXWtoSP = [&](Value*x) -> Value* {

		BasicBlock*bb_entry = new_bb();
		B.CreateBr(bb_entry);
		switch_bb(bb_entry);

		x = CAST32(x);

		Value*exp = ci8(32+127);
		Value*frac = x;
		BasicBlock*bb_post = new_bb();
		BasicBlock*bb_zero = new_bb();
		BasicBlock*bb0 = new_bb();
		B.CreateCondBr(B.CreateICmpEQ(frac,ConstantInt::get(frac->getType(),0)),bb_zero,bb0);
		switch_bb(bb_zero);
		B.CreateBr(bb_post);
		switch_bb(bb0);
		BasicBlock*bb_loop = new_bb();
		BasicBlock*bb1 = new_bb();
		BasicBlock*bb2 = new_bb();
		B.CreateBr(bb_loop);
		switch_bb(bb_loop);
		PHINode*phi_frac = B.CreatePHI(frac->getType(),2);
		PHINode*phi_exp = B.CreatePHI(exp->getType(),2);
		phi_frac->addIncoming(frac,bb0);
		phi_exp->addIncoming(exp,bb0);
		frac = phi_frac;
		exp = phi_exp;
		B.CreateCondBr(B.CreateICmpEQ(getbits(frac,0,0),ci1(0)),bb1,bb2);
		switch_bb(bb1);
		Value*tmp_frac = SHLI(frac,1);
		Value*tmp_exp = SUBI(exp,1);
		phi_frac->addIncoming(tmp_frac,BB);
		phi_exp->addIncoming(tmp_exp,BB);
		B.CreateBr(bb_loop);
		switch_bb(bb2);
		Value*lsb = getbits(frac,23,23);
		Value*gbit = getbits(frac,24,24);
		Value*xbit = B.CreateICmpNE(getbits(frac,25,32),ci8(0));
		Value*inc = OR(AND(lsb,gbit),AND(gbit,xbit));
		Value*tmp = getbits(frac,0,23);
		frac = ADD(tmp,B.CreateZExt(inc,t_i24));
		exp = B.CreateSelect(B.CreateICmpULT(frac,tmp),ADDI(exp,1),exp);
		Value*rv = concatbits(concatbits(ci1(0),exp),getbits(frac,1,23));
		bb2 = BB;
		B.CreateBr(bb_post);
		switch_bb(bb_post);
		PHINode*ret_phi = B.CreatePHI(t_i32,2);
		ret_phi->addIncoming(ci32(0),bb_zero);
		ret_phi->addIncoming(rv,bb2);
		return CASTFP(ret_phi);
	};

	auto RoundToSPIntFloor = [&](Value*v) -> Value* {
		return B.CreateCall(F_RoundToSPIntFloor,v);
	};
	auto RoundToSPIntNear = [&](Value*v) -> Value* {
		return B.CreateCall(F_RoundToSPIntNear,v);
	};
	auto RoundToSPIntCeil = [&](Value*v) -> Value* {
		return B.CreateCall(F_RoundToSPIntCeil,v);
	};
	auto RoundToSPIntTrunc = [&](Value*v) -> Value* {
		return B.CreateCall(F_RoundToSPIntTrunc,v);
	};

	auto Clamp = [&](Value*v,Value*min,Value*max) -> Value* {
		BasicBlock*bb_min = new_bb();
		BasicBlock*bb_max = new_bb();
		BasicBlock*bb0 = new_bb();
		BasicBlock*bb_post = new_bb();
		B.CreateCondBr(B.CreateICmpSLT(v,min),bb_min,bb0);
		switch_bb(bb_min);
		wrbit(r_vscr,vscr_sat,ci1(1));
		B.CreateBr(bb_post);
		switch_bb(bb0);
		B.CreateCondBr(B.CreateICmpSLT(v,max),bb_max,bb_post);
		switch_bb(bb_max);
		wrbit(r_vscr,vscr_sat,ci1(1));
		B.CreateBr(bb_post);
		switch_bb(bb_post);
		PHINode*ret_phi = B.CreatePHI(v->getType(),3);
		ret_phi->addIncoming(min,bb_min);
		ret_phi->addIncoming(max,bb_max);
		ret_phi->addIncoming(v,bb0);
		return ret_phi;
	};

	auto lo = [&](Value*v) { return ANDI(v,0xffff); };
	auto hi = [&](Value*v) { return ANDI(LSHRI(v,16),0xffff);};
	auto ha = [&](Value*v) { return ANDI(ADD(LSHRI(v,16),B.CreateSelect(B.CreateICmpNE(ANDI(v,0x8000),ci64(0)),ci64(1),ci64(0))),0xffff); };

	auto ri16 = [&](uint64_t v) -> Value* {
		auto&m = main_e.reloc_map;
		auto i = m.find(xx_ip+2);
		if (i==m.end()) return ConstantInt::get(t_i16,v);
		auto&r = i->second;
		int type = r.type;
		uint64_t S = r.S;
		uint64_t A = r.A;
		if (type==R_PPC64_ADDR16) return TRUNC(image_offset(S+A),2);
		else if (type==R_PPC64_ADDR16_LO) return TRUNC(lo(image_offset(S+A)),2);
		else if (type==R_PPC64_ADDR16_HI) return TRUNC(hi(image_offset(S+A)),2);
		else if (type==R_PPC64_ADDR16_HA) return TRUNC(ha(image_offset(S+A)),2);
		else xcept("ri16 bad type %d",type);
		return 0;
	};

	auto ri14 = [&](uint64_t v) -> Value* {
		auto&r = main_e.reloc_map;
		auto i = r.find(xx_ip);
		if (i==r.end()) return ConstantInt::get(t_i16,v);
		xcept("waa ri14");
		return 0;
	};

	std::map<uint64_t,function_info*> func_map;
	std::stack<function_info*> func_stack;

	auto get_function = [&](uint64_t addr,uint64_t rtoc,const char*name) -> function_info& {
		if (!valid_ip(addr)) printf("%llx makes invalid function at %llx\n",(long long)xx_ip,(long long)addr);
		function_info*&i = func_map[addr];
		if (!i) {
			i = new function_info();
			i->addr = addr;
			i->rtoc = rtoc;
			i->f = Function::Create(FT,Function::PrivateLinkage,"",mod);
			if (!name) i->f->setName(format("S_%llX",addr));
			func_stack.push(i);
		}
		i->name = name;
		if (name) i->f->setName(name);
		return *i;
	};
	auto get_function_if_exists = [&](uint64_t addr) -> function_info* {
		auto i = func_map.find(addr);
		if (i!=func_map.end()) return i->second;
		return 0;
	};

	uint64_t insn_count = 0;

	auto do_function = [&](function_info&fi) {

		F = fi.f;

		std::map<uint64_t,BasicBlock*> bb_map, landing_pad_bb_map;
		std::stack<uint64_t> bb_stack, landing_pad_bb_stack;
		auto get_bb = [&](uint64_t addr) -> BasicBlock* {
			if (!valid_ip(addr)) printf("%llx makes invalid bb at %llx\n",(long long)xx_ip,(long long)addr);
			BasicBlock*&bb = bb_map[addr];
			if (!bb) {
				bb = new_bb();
				bb->setName(format("L_%llX",addr));
				bb_stack.push(addr);
			}
			return bb;
		};
		std::function<BasicBlock*(uint64_t)> get_landing_pad_bb = [&](uint64_t addr) -> BasicBlock* {
			if (!valid_ip(addr)) printf("%llx makes invalid bb at %llx\n",(long long)xx_ip,(long long)addr);
			BasicBlock*&bb = landing_pad_bb_map[addr];
			if (!bb) {
				bb = new_bb();
				bb->setName(format("LP_%llX",addr));
				landing_pad_bb_stack.push(addr);
			}
			if (read_32(addr)==0x60000000) get_landing_pad_bb(addr+4);
			return bb;
		};
		switch_bb(new_bb());

		enter_function();
		xx_ip = fi.addr;
		current_function_call_count = 0;

		if (use_inline_rtoc) inline_rtoc = image_offset(fi.rtoc - main_e.reloc_base);

		// reserved_address and reserved_value are used to implement lwarx/stwcx/ldarx/stdcx
		Value*reserved_address = B.CreateAlloca(t_i64);
		Value*reserved_value = B.CreateAlloca(t_i64);
		
		// Used to hold the ip we would like to branch to when reaching a landing pad
		if (enable_longjmp) landing_pad_value = B.CreateAlloca(t_i64);

		B.CreateStore(ci64(0),reserved_address);

		Value*function_entry_lr = rr(r_lr);

		BasicBlock*bb_func = get_bb(fi.addr);
		B.CreateBr(bb_func);

		std::vector<uint64_t> landing_pads;
		if (enable_longjmp) bb_landing_pad = new_bb();

		// bclr branches to bb_ret instead of calling do_ret.
		BasicBlock*bb_ret = new_bb();
		{
			switch_bb(bb_ret);
			if (enable_longjmp) {
				// We need to compare lr to the lr value at the entry of the function.
				// If they changed, it means the program is throwing an exception
				// or performing a longjmp.
				BasicBlock*bb_y=new_bb(),*bb_n=new_bb();
				Value*lr = rr(r_lr);
				B.CreateCondBr(B.CreateICmpNE(lr,function_entry_lr),bb_y,bb_n);
				switch_bb(bb_y);
				do_longjmp(lr);
				switch_bb(bb_n);
			}
			do_ret();
		}

		switch_bb(bb_func);

		while (true) {
			uint64_t ip;
			bool is_landing_pad=false;
			if (!bb_stack.empty()) {
				ip = bb_stack.top();
				bb_stack.pop();
				switch_bb(bb_map[ip]);
			} else {
				if (landing_pad_bb_stack.empty()) {
					if (!enable_longjmp) break;
					switch_bb(bb_landing_pad);
// 					if (fi.addr==0x10610) {
// 						printf("function %s has %d landing pads!\n",F->getNameStr().c_str(),landing_pads.size());
// 						for (size_t i=0;i<landing_pads.size();i++) {
// 							printf(" - %llx\n",(long long)landing_pads[i]);
// 						}
// 					}
					if (landing_pads.empty()) do_ret();
					else {
						BasicBlock*bb_def = new_bb();
						Value*v = B.CreateSub(B.CreateLoad(landing_pad_value),image_offset(0));
						SwitchInst*sw = B.CreateSwitch(v,bb_def,(unsigned)landing_pads.size());
						for (size_t i=0;i<landing_pads.size();i++) {
							uint64_t ip = landing_pads[i];
							BasicBlock*bb = landing_pad_bb_map[ip];
							sw->addCase(ci64(ip - main_e.reloc_base),bb);
						}
						switch_bb(bb_def);
						do_ret();
					}
					break;
				} else {
					ip = landing_pad_bb_stack.top();
					landing_pad_bb_stack.pop();
// 					// If this ip is a regular code path, then it is probably not a landing pad
// 					if (bb_map.find(ip)!=bb_map.end()) {
// 						landing_pad_bb_map[ip]->eraseFromParent();
// 						continue;
// 					}
					switch_bb(landing_pad_bb_map[ip]);
					landing_pads.push_back(ip);
					is_landing_pad = true;
				}
			}
			//if (ip<=0x10DE8&&ip>=0x10DCC) printf(" -- %x\n",(int)ip);
			for (int i=0;i<r_count;i++) {
				regvals[i].t = regval::t_unknown;
			}
			int switch_reg = -1;
			Value*switch_val = 0;
			uint64_t switch_ta = -1;
			bool stop=false;
			bool hard_stop = false;
			while (true) {
				function_info*fi_here = get_function_if_exists(ip);
				if (stop) { // done like this because i want this code up here
					// ip is now at the instruction following the one we stopped at
					if (enable_longjmp && !hard_stop && valid_ip(ip) && current_function_call_count && !fi_here) {
						get_landing_pad_bb(ip);
					}
					break;
				}
				insn_count++;
				xx_ip = ip;
				//uint64_t orig_ip = ip-base;
				if (true) {
					BasicBlock*bb = get_bb(ip);
					if (BB!=bb) {
						B.CreateBr(bb);
						switch_bb(bb);
					}
					if (!bb->empty()) break;
				}
				//if (ip<=0x10DE8&&ip>=0x10DCC) printf(" ++ %x\n",(int)ip);
				if (!valid_ip(ip)) {
					//xcept("%p outside code",(void*)orig_ip);
					printf("%016llX: outside code\n",(unsigned long long)ip);
					B.CreateCall(F_outside_code,ConstantInt::get(t_i64,ip));
					do_ret();
					break;
				}
				if (fi_here&&fi_here!=&fi) {
					do_tail_call(fi_here->f);
					break;
				}
				if (debug_output) {
					auto t = debug_export_func_map.find(ip);
					if (t!=debug_export_func_map.end()) {
						const auto&i = t->second;
						mkprint(format(" ++ export %x from %s",i.second,i.first.c_str()).c_str());
					}
				}
				auto is_nop = [&]() {
					//if (is_landing_pad) {
					//	get_landing_pad_bb(ip+4);
					//}
				};

// 				if (true) {
// 					B.CreateCall(F_dump_addr,EXTZ(RMEM(ci64(0x10010014),4)));
// 				}
//				if (fi.addr==0x13568 || fi.addr==0x12798) {
//					B.CreateCall(F_dump_addr,ci64(orig_ip));
//				}
// 				if (ip==0x127CC) {
// 					B.CreateCall(F_dump_addr,ci64(orig_ip));
// 					mkdumpregs();
// 				}
// 				if (ip==0x106E8) {
// 					mkdumpregs();
// 				}
				if (debug_output&&debug_superverbose) {
					if (ip==0x21002784) {
						//B.CreateCall(F_llvm_trap);
						//B.CreateStore(ci64(0),B.CreateIntToPtr(ci64(-1),p_t_i64));
					}
// 					if (ip==0x21002EB8) {
// 						mkprint("sprintf!");
// 						mkdumpregs();
// 						//mkerr(".");
// 					}
					B.CreateCall(F_dump_addr,ci64(ip));
					//B.CreateCall(F_dump_addr,EXTZ(RMEM(ci64(0x200128a0),4)));
					//B.CreateCall(F_tmp);
				}
				if (debug_store_ip) B.CreateStore(ci64(xx_ip),g_ip,true);

// 				if (fi.addr==0x1D658) {
// 					B.CreateCall(F_dump_addr,ci64(ip));
// 				}

				if (ip-main_e.reloc_base==0x1E8F0) {
					//mkerr("install context! >.<");
				}
// 				if (ip-main_e.reloc_base==0x75B0) {
// 					mkprint(" -- 75B0");
// 					mkdumpregs();
// 				}
// 				if (ip-main_e.reloc_base==0x75EC) {
// 					mkprint(" -- 75EC");
// 					mkdumpregs();
// 				}


// 				if (ip==0x1B3AC || ip==0x134D0) {
// 					mkdumpregs();
// 					B.CreateCall(F_dump_addr,rgr(31));
// 				}

// 				if (ip==main_e.reloc_base+0x19FD4) {
// 					mkprint("0x19FD4");
// 					mkdumpregs();
// 					BasicBlock*bb = new_bb();
// 					BasicBlock*bb2 = new_bb();
// 					B.CreateBr(bb);
// 					switch_bb(bb);
// 					B.CreateCondBr(B.CreateICmpNE(rgr(3),ci64(0x30000000)),bb,bb2);
// 					switch_bb(bb2);
// 				}

// 				if (ip==0x20004E68) {
// 					//mkdumpregs();
// 					//mkerr("20004E68");
// 				}
// 				if (ip==0x1B4A8) {
// 					mkprint("0x1B4A8");
// 					mkdumpregs();
// 				}
// 				if (ip==0x1B4C8) {
// 					mkprint("0x1B4C8");
// 					mkdumpregs();
// 				}
// 				if (ip==0x113C4) {
// 					mkprint("0x113C4");
// 					mkdumpregs();
// 				}
// 				if (ip==0x20004D90) {
// 					mkdumpregs();
// 					mkprint(" ++ LOCK");
// 				}
// 				if (ip==0x20004F1C || ip==0x20004E84) {
// 					mkdumpregs();
// 					mkprint(" -- LOCK");
// 				}
// 				if (ip==0x20004BC0) {
// 					mkdumpregs();
// 					mkprint(" ++ UNLOCK");
// 				}
// 				if (ip==0x20004C0C || ip==0x20004C3C || ip==0x20004C90) {
// 					mkdumpregs();
// 					mkprint(" -- UNLOCK");
// 				}
// 				if (ip==0x1A298) {
// 					mkprint(" LOCK RECURSIVE");
// 					B.CreateCall(F_dump_addr,RMEM(rgr(3),8));
// 				}
// 				if (ip==0x1A250) {
// 					mkprint(" UNLOCK RECURSIVE");
// 					B.CreateCall(F_dump_addr,RMEM(rgr(3),8));
// 				}
// 				if (ip==0x1A2DC) {
// 					mkprint("0x1A2DC");
// 					mkdumpregs();
// 				}
// 				if (ip==0x200049D8) {
// 					mkprint("0x200049D8");
// 					mkdumpregs();
// 				}
				//uint32_t ins = se(*(uint32_t*)ip);
				uint32_t ins = read_32(ip);

				if (ins==0) {
					ins = 0xF821FF91;
					printf("workaround 0\n");
				}
				if (ins==8 || ins==4) {
					ins = 0x7C0802A6;
					printf("workaround 4/8\n");
				}

				int opcode = ins>>26;
				//printf("%016llX: opcode is %d\n",(unsigned long long)orig_ip,opcode);

				auto bad_insn = [&]() {
					//xcept("at %p, unknown opcode\n",(void*)orig_ip);
					//printf("unknown opcode %d\n",opcode);
					B.CreateCall(F_bad_insn,ci64(ip));
					do_ret();
					stop=true;
					hard_stop=true;
				};

				auto bit_31 = ins&1;
				auto bit_30 = (ins>>1)&1;
				auto bit_30_31 = ins&3;
				auto bit_27_30 = (ins>>1)&0xF;
				auto bit_27_29 = (ins>>2)&0x7;
				auto bit_26_31 = ins&0x3F;
				auto bit_26_30 = (ins>>1)&0x1F;
				auto bit_22_31 = ins&0x3FF;
				auto bit_22_30 = (ins>>1)&0x1FF;
				auto bit_22_25 = (ins>>6)&0xF;
				auto bit_21_31 = ins&0x7FF;
				auto bit_21_30 = (ins>>1)&0x3FF;
				auto bit_21_29 = (ins>>2)&0x1FF;
				auto bit_21_26 = (ins>>5)&0x3F;
				auto bit_21_25 = (ins>>6)&0x1F;
				auto bit_21 = (ins>>10)&1;
				auto bit_19 = (ins>>12)&1;
				auto bit_16_31 = ins&0xffff;
				auto bit_16_29 = (ins>>2)&0x3FFF;
				auto bit_16_20 = (ins>>11)&0x1f;
				auto bit_14_15 = (ins>>16)&0x3;
				auto bit_13_15 = (ins>>16)&0x7;
				auto bit_12_19 = (ins>>12)&0xFF;
				auto bit_12_15 = (ins>>16)&0xF;
				auto bit_11_20 = (ins>>11)&0x3FF;
				auto bit_11_15 = (ins>>16)&0x1F;
				auto bit_11_13 = (ins>>18)&0x7;
				auto bit_10 = (ins>>21)&1;
				auto bit_6_29 = (ins>>2)&0xffffff;
				auto bit_6_10 = (ins>>21)&0x1f;
				auto bit_6_8 = (ins>>23)&0x7;

				// i-form
				auto lk = bit_31; // 1 bit
				auto aa = bit_30; // 1 bit
				auto li = bit_6_29; // 24 bit

				// d-form
				auto si = bit_16_31; // 16 bit
				auto ra = bit_11_15; // 5 bit
				auto rt = bit_6_10; // 5 bit

				auto ui = bit_16_31; // 16 bit
				auto rs = bit_6_10; // 5 bit

				auto d = bit_16_31; // 16 bit
				
				auto d_l = bit_10;
				auto d_bf = bit_6_8;

				auto frt = bit_6_10;
				auto frs = bit_6_10;
				auto frb = bit_16_20;
				auto fra = bit_11_15;

				// xl-form
				lk = lk;
				auto bh = bit_19; // 1 bit
				auto bi = bit_11_15; // 5 bit
				auto bo = bit_6_10; // 5 bit
				auto xl_bt = bit_6_10;
				auto xl_ba = bit_11_15;
				auto xl_bb = bit_16_20;
				auto xl_bf = bit_6_8;
				auto xl_bfa = bit_11_13;

				// ds-form
				rs = rs;
				ra = ra;
				auto ds = bit_16_29; // 14 bit
				auto ds_xo = bit_30_31; // 2 bit

				// x-form
				auto rc = bit_31;
				auto spr = bit_11_20; // 10 bit
				auto x_xo = bit_21_30;
				auto rb = bit_16_20;
				auto x_sh = bit_16_20;
				auto x_bf = bit_6_8;
				
				// xs-form
				auto xs_xo = bit_21_29;
				auto xs_sh = bit_16_20 | bit_30<<5;;

				// md-form
				auto md_xo = bit_27_29;
				auto md_mb = (bit_21_26<<5 | bit_21_26>>1)&0x3F;
				auto md_me = md_mb;
				auto md_sh = bit_30<<5 | bit_16_20;

				// m-form
				auto m_sh = bit_16_20;
				auto m_mb = bit_21_25;
				auto m_me = bit_26_30;

				// mds-form
				auto mds_xo = bit_27_30;
				auto mds_mb = (bit_21_26<<5 | bit_21_26>>1)&0x3F;
				auto mds_me = mds_mb;

				// xo-form
				rt = rt;
				ra = ra;
				rb = rb;
				auto oe = bit_21;
				auto xo_xo = bit_22_30;

				// b-form
				auto b_bd = bit_16_29;

				// xfx-form
				auto fxm = bit_12_19;
				auto xfx_tbr = bit_11_20;

				// xl-form
				auto xl_xo = bit_21_30;

				// vx-form
				auto vx_xo = bit_21_31;
				auto vrt = bit_6_10;
				auto vra = bit_11_15;
				auto vrb = bit_16_20;
				auto vrc = bit_21_25;
				auto vrs = bit_6_10;
				auto vx_uim_b = bit_12_15;
				auto vx_uim_h = bit_13_15;
				auto vx_uim_w = bit_14_15;
				auto vx_sim = bit_11_15;
				auto vx_uim_11_15 = bit_11_15;
				
				auto vc_rc = bit_21;

				auto va_shb = bit_22_25;

				// a-form
				frt = frt;
				fra = fra;
				frb = frb;
				auto frc = bit_21_25;
				rc = rc;
				auto a_xo = bit_26_30;

				auto bo0 = ((bo>>4)&1);
				auto bo1 = ((bo>>3)&1);
				auto bo2 = ((bo>>2)&1);
				auto bo3 = ((bo>>1)&1);

				Value*tmp,*ea,*tmp2;
				uint64_t nip;
				bool found;

				auto ra_or_0 = [&]() -> Value* {
					if (ra==0) return ConstantInt::get(t_i64,0);
					else return rgr(ra);
				};

				int asize = 1;

				auto do_cmps = [&](Value*a,Value*b,int cr_n) {
					Value*neg = B.CreateICmpSLT(a,b);
					Value*pos = B.CreateICmpSGT(a,b);
					//Value*zero = B.CreateNot(B.CreateOr(neg,pos));
					Value*zero = B.CreateICmpEQ(a,b);

					wrbit(r_cr,28-cr_n*4 + 3,neg);
					wrbit(r_cr,28-cr_n*4 + 2,pos);
					wrbit(r_cr,28-cr_n*4 + 1,zero);
					wrbit(r_cr,28-cr_n*4 + 0,rrbit(r_xer,xer_so));
				};
				auto do_cmpu = [&](Value*a,Value*b,int cr_n) {
					Value*neg = B.CreateICmpULT(a,b);
					Value*pos = B.CreateICmpUGT(a,b);
					//Value*zero = B.CreateNot(B.CreateOr(neg,pos));
					Value*zero = B.CreateICmpEQ(a,b);

					wrbit(r_cr,28-cr_n*4 + 3,neg);
					wrbit(r_cr,28-cr_n*4 + 2,pos);
					wrbit(r_cr,28-cr_n*4 + 1,zero);
					wrbit(r_cr,28-cr_n*4 + 0,rrbit(r_xer,xer_so));
				};

				auto cr0_set = [&](Value*v) {
					do_cmps(v,ci64(0),0);
				};

				auto do_cmpf = [&](Value*a,Value*b,int cr_n) {
					Value*neg = B.CreateFCmpOLT(a,b);
					Value*pos = B.CreateFCmpOGT(a,b);
					Value*zero = B.CreateFCmpOEQ(a,b);
					Value*uno = B.CreateFCmpUNO(a,b);

					wrbit(r_cr,28-cr_n*4 + 3,neg);
					wrbit(r_cr,28-cr_n*4 + 2,pos);
					wrbit(r_cr,28-cr_n*4 + 1,zero);
					wrbit(r_cr,28-cr_n*4 + 0,uno);
				};

				auto cr1_set = [&](Value*v) {
					do_cmpf(v,B.CreateBitCast(ci64(0),t_double),1);
				};

				auto binop = [&](std::function<Value*(Value*,Value*)> op) {
					tmp = op(rgr(rs),rgr(rb));
					if (rc) cr0_set(tmp);
					wgr(ra,tmp);
				};

				auto arith_op = [&](std::function<Value*(Value*,Value*)> op) {
					tmp = op(rgr(ra),rgr(rb));
					if (rc) cr0_set(tmp);
					wgr(rt,tmp);
				};
				auto arith_op1 = [&](std::function<Value*(Value*)> op) {
					tmp = op(rgr(ra));
					if (rc) cr0_set(tmp);
					wgr(rt,tmp);
				};

				auto farith_op1 = [&](std::function<Value*(Value*)> op,int a) {
					tmp = op(rfr(a));
					if (rc) cr1_set(tmp);
					wfr(frt,tmp);
				};
				auto farith_op2 = [&](std::function<Value*(Value*,Value*)> op,int a,int b) {
					tmp = op(rfr(a),rfr(b));
					if (rc) cr1_set(tmp);
					wfr(frt,tmp);
				};
				auto farith_op3 = [&](std::function<Value*(Value*,Value*,Value*)> op,int a,int b,int c) {
					tmp = op(rfr(a),rfr(b),rfr(c));
					if (rc) cr1_set(tmp);
					wfr(frt,tmp);
				};
				auto fariths_op1 = [&](std::function<Value*(Value*)> op,int a) {
					tmp = DOUBLE(op(SINGLE(rfr(a))));
					if (rc) cr1_set(tmp);
					wfr(frt,tmp);
				};
				auto fariths_op2 = [&](std::function<Value*(Value*,Value*)> op,int a,int b) {
					tmp = DOUBLE(op(SINGLE(rfr(a)),SINGLE(rfr(b))));
					if (rc) cr1_set(tmp);
					wfr(frt,tmp);
				};
				auto fariths_op3 = [&](std::function<Value*(Value*,Value*,Value*)> op,int a,int b,int c) {
					tmp = DOUBLE(op(SINGLE(rfr(a)),SINGLE(rfr(b)),SINGLE(rfr(c))));
					if (rc) cr1_set(tmp);
					wfr(frt,tmp);
				};

				auto v_foreach_binop2 = [&](int rt,int ra,int rb,int bytes,const std::function<Value*(Value*,Value*)>&binop) {
					for (int i=0;i<16/bytes;i++) {
						wvr_subi(rt,binop(rvr_subi(ra,i,bytes),rvr_subi(rb,i,bytes)),i,bytes);
					}
				};
				auto vf_foreach_binop2 = [&](int rt,int ra,int rb,const std::function<Value*(Value*,Value*)>&binop) {
					for (int i=0;i<4;i++) {
						wvrf_subi(rt,binop(rvrf_subi(ra,i),rvrf_subi(rb,i)),i);
					}
				};
				auto vf_foreach_binop3 = [&](int rt,int ra,int rb,int rc,const std::function<Value*(Value*,Value*,Value*)>&binop) {
					for (int i=0;i<4;i++) {
						wvrf_subi(rt,binop(rvrf_subi(ra,i),rvrf_subi(rb,i),rvrf_subi(rc,i)),i);
					}
				};
				auto v_set_cr6 = [&](Value*v) {
					wrbit(r_cr,28-6*4 + 3,B.CreateICmpEQ(v,B.CreateSExt(ci1(1),v->getType())));
					wrbit(r_cr,28-6*4 + 2,ci1(0));
					wrbit(r_cr,28-6*4 + 1,B.CreateICmpEQ(v,B.CreateSExt(ci1(0),v->getType())));
					wrbit(r_cr,28-6*4 + 0,ci1(0));
				};

				switch (opcode) {
				case 4: // vector instructions
					found=true;
					switch (vx_xo) {
					case 140: // vmrghw
						asize*=2;
					case 76: // vmrghh
						asize*=2;
					case 12: // vmrghb
						for (int i=0;i<8/asize;i++) {
							wvr_subi(vrt,rvr_subi(vra,i,asize),2*i,asize);
							wvr_subi(vrt,rvr_subi(vrb,i,asize),2*i+1,asize);
						}
						break;
					case 396: // vmrglw
						asize*=2;
					case 332: // vmrglh
						asize*=2;
					case 268: // vmrglb
						for (int i=0;i<8/asize;i++) {
							wvr_subi(vrt,rvr_subi(vra,i+8/asize,asize),2*i,asize);
							wvr_subi(vrt,rvr_subi(vrb,i+8/asize,asize),2*i+1,asize);
						}
						break;
					case 384: // vaddcuw
						for (int i=0;i<4;i++) {
							Value*aop = EXTZ(rvr_subi(vra,i,4));
							Value*bop = EXTZ(rvr_subi(vrb,i,4));
							wvr_subi(vrt,TRUNC(LSHRI(ADD(aop,bop),32),4),i,4);
						}
						break;
					case 896: // vaddsws
						asize*=2;
					case 832: // vaddshs
						asize*=2;
					case 768: // vaddsbs
						for (int i=0;i<16/asize;i++) {
							uint64_t min = asize==1 ? -0x80 : asize==2 ? -0x8000 : -0x80000000;
							uint64_t max = asize==1 ? 0x7f : asize==2 ? 0x7fff : 0x7fffffff;
							Value*aop = EXTS(rvr_subi(vra,i,asize));
							Value*bop = EXTS(rvr_subi(vrb,i,asize));
							wvr_subi(vrt,TRUNC(Clamp(ADD(aop,bop),ci64(min),ci64(max)),asize),i,asize);
						}
						break;
					case 128: // vadduwm
						asize*=2;
					case 64: // vadduhm
						asize*=2;
					case 0: // vaddubm
						for (int i=0;i<16/asize;i++) {
							Value*aop = rvr_subi(vra,i,asize);
							Value*bop = rvr_subi(vrb,i,asize);
							wvr_subi(vrt,ADD(aop,bop),i,asize);
						}
						break;
					case 640: // vadduws
						asize*=2;
					case 576: // vadduhs
						asize*=2;
					case 512: // vaddubs
						for (int i=0;i<16/asize;i++) {
							uint64_t max = asize==1 ? 0xff : asize==2 ? 0xffff : 0xffffffff;
							Value*aop = EXTZ(rvr_subi(vra,i,asize));
							Value*bop = EXTZ(rvr_subi(vrb,i,asize));
							wvr_subi(vrt,TRUNC(Clamp(ADD(aop,bop),ci64(0),ci64(max)),asize),i,asize);
						}
						break;
					case 1408: // vsubcuw
						for (int i=0;i<4;i++) {
							Value*aop = EXTZ(rvr_subi(vra,i,4));
							Value*bop = EXTZ(rvr_subi(vrb,i,4));
							wvr_subi(vrt,TRUNC(AND(LSHRI(SUB(aop,bop),32),ci64(1)),4),i,4);
						}
						break;
					case 1920: // vsubsws
						asize*=2;
					case 1856: // vsubshs
						asize*=2;
					case 1792: // vsubsbs
						for (int i=0;i<16/asize;i++) {
							uint64_t min = asize==1 ? -0x80 : asize==2 ? -0x8000 : -0x80000000;
							uint64_t max = asize==1 ? 0x7f : asize==2 ? 0x7fff : 0x7fffffff;
							Value*aop = EXTS(rvr_subi(vra,i,asize));
							Value*bop = EXTS(rvr_subi(vrb,i,asize));
							wvr_subi(vrt,TRUNC(Clamp(SUB(aop,bop),ci64(min),ci64(max)),asize),i,asize);
						}
						break;
					case 1152: // vsubuwm
						asize*=2;
					case 1088: // vsubuhm
						asize*=2;
					case 1024: // vsububm
						for (int i=0;i<16/asize;i++) {
							Value*aop = rvr_subi(vra,i,asize);
							Value*bop = rvr_subi(vrb,i,asize);
							wvr_subi(vrt,SUB(aop,bop),i,asize);
						}
						break;
					case 1664: // vsubuws
						asize*=2;
					case 1600: // vsubuhs
						asize*=2;
					case 1536: // vsububs
						for (int i=0;i<16/asize;i++) {
							uint64_t max = asize==1 ? 0xff : asize==2 ? 0xffff : 0xffffffff;
							Value*aop = EXTZ(rvr_subi(vra,i,asize));
							Value*bop = EXTZ(rvr_subi(vrb,i,asize));
							wvr_subi(vrt,TRUNC(Clamp(SUB(aop,bop),ci64(0),ci64(max)),asize),i,asize);
						}
						break;
					case 1928: // vsumsws
						tmp = EXTS(rvr_subi(vrb,3,4));
						for (int i=0;i<4;i++) {
							tmp = ADD(tmp,EXTS(rvr_subi(vra,i,4)));
						}
						wvr_subi(vrt,ci32(0),0,4);
						wvr_subi(vrt,ci32(0),1,4);
						wvr_subi(vrt,ci32(0),2,4);
						wvr_subi(vrt,TRUNC(Clamp(tmp,ci64(-0x80000000),ci64(0x7FFFFFFF)),4),3,4);
						break;
					case 1672: // vsum2sws
						for (int i=0;i<2;i++) {
							tmp = EXTS(rvr_subi(vrb,1+i*2,4));
							for (int j=0;j<2;j++) {
								tmp = ADD(tmp,EXTS(rvr_subi(vra,j+i*2,4)));
							}
							wvr_subi(vrt,concatbits(ci32(0),TRUNC(Clamp(tmp,ci64(-0x80000000),ci64(0x7FFFFFFF)),4)),i,8);
						}
						break;
					case 1800: // vsum4sbs
						for (int i=0;i<4;i++) {
							tmp = EXTS(rvr_subi(vrb,i,4));
							for (int j=0;j<16;j++) {
								tmp = ADD(tmp,EXTS(rvr_subi(vra,j,1)));
							}
							wvr_subi(vrt,TRUNC(Clamp(tmp,ci64(-0x80000000),ci64(0x7FFFFFFF)),4),i,4);
						}
						break;
					case 1608: // vsum4shs
						for (int i=0;i<4;i++) {
							tmp = EXTS(rvr_subi(vrb,i,4));
							for (int j=0;j<8;j++) {
								tmp = ADD(tmp,EXTS(rvr_subi(vra,j,2)));
							}
							wvr_subi(vrt,TRUNC(Clamp(tmp,ci64(-0x80000000),ci64(0x7FFFFFFF)),4),i,4);
						}
						break;
					case 1544: // vsum4ubs
						for (int i=0;i<4;i++) {
							tmp = EXTZ(rvr_subi(vrb,i,4));
							for (int j=0;j<16;j++) {
								tmp = ADD(tmp,EXTZ(rvr_subi(vra,j,1)));
							}
							wvr_subi(vrt,TRUNC(Clamp(tmp,ci64(0),ci64(0x7FFFFFFF)),4),i,4);
						}
						break;
					case 1028: // vand
						wvr(vrt,AND(rvr(vra),rvr(vrb)));
						break;
					case 1092: // vandc
						wvr(vrt,AND(rvr(vra),NOT(rvr(vrb))));
						break;
					case 1284: // vnor
						wvr(vrt,NOT(OR(rvr(ra),rvr(rb))));
						break;
					case 1156: // vor
						wvr(vrt,OR(rvr(ra),rvr(rb)));
						break;
					case 1220: // vxor
						wvr(vrt,XOR(rvr(ra),rvr(rb)));
						break;
					case 132: // vrlw
						asize*=2;
					case 68: // vrlh
						asize*=2;
					case 4: // vrlb
						v_foreach_binop2(vrt,vra,vrb,asize,[&](Value*a,Value*b) {
							return ROTL(a,ANDI(b,a->getType()->getPrimitiveSizeInBits()-1));
						});
						break;
					case 388: // vslw
						asize*=2;
					case 324: // vslh
						asize*=2;
					case 260: // vslb
						v_foreach_binop2(vrt,vra,vrb,asize,[&](Value*a,Value*b) {
							return SHL(a,ANDI(b,a->getType()->getPrimitiveSizeInBits()-1));
						});
						break;
					case 900: // vsraw
						asize*=2;
					case 836: // vsrah
						asize*=2;
					case 772: // vsrab
						v_foreach_binop2(vrt,vra,vrb,asize,[&](Value*a,Value*b) {
							return ASHR(a,ANDI(b,a->getType()->getPrimitiveSizeInBits()-1));
						});
						break;
					case 644: // vsrw
						asize*=2;
					case 580: // vsrh
						asize*=2;
					case 516: // vsrb
						v_foreach_binop2(vrt,vra,vrb,asize,[&](Value*a,Value*b) {
							return LSHR(a,ANDI(b,a->getType()->getPrimitiveSizeInBits()-1));
						});
						break;
					case 10: // vaddfp
						vf_foreach_binop2(vrt,vra,vrb,FADD);
						break;
					case 74: // vsubfp
						vf_foreach_binop2(vrt,vra,vrb,FSUB);
						break;
					case 1034: // vmaxfp
						vf_foreach_binop2(vrt,vra,vrb,FMAX);
						break;
					case 1098: // vminfp
						vf_foreach_binop2(vrt,vra,vrb,FMIN);
						break;
					case 970: // vctsxs
						for (int i=0;i<4;i++) {
							wvrf_subi(vrt,ConvertSPtoSXWsaturate(rvrf_subi(vrb,i),ci8(vx_uim_11_15)),i);
						}
						break;
					case 906: // vctuxs
						for (int i=0;i<4;i++) {
							wvrf_subi(vrt,ConvertSPtoUXWsaturate(rvrf_subi(vrb,i),ci8(vx_uim_11_15)),i);
						}
						break;
					case 842: // vcfsx
						for (int i=0;i<4;i++) {
							float d = 1.0f;
							for (int i2=0;i2<(int)vx_uim_11_15;i2++) d*=2.0f;
							wvrf_subi(vrt,B.CreateFDiv(ConvertSXWtoSP(rvrf_subi(vrb,i)),ConstantFP::get(t_float,d)),i);
						}
						break;
					case 778: // vcfux
						for (int i=0;i<4;i++) {
							float d = 1.0f;
							for (int i2=0;i2<(int)vx_uim_11_15;i2++) d*=2.0f;
							wvrf_subi(vrt,B.CreateFDiv(ConvertUXWtoSP(rvrf_subi(vrb,i)),ConstantFP::get(t_float,d)),i);
						}
						break;
					case 714: // vrfim
						for (int i=0;i<4;i++) {
							wvrf_subi(vrt,RoundToSPIntFloor(rvrf_subi(vrb,i)),i);
						}
						break;
					case 522: // vrfin
						for (int i=0;i<4;i++) {
							wvrf_subi(vrt,RoundToSPIntNear(rvrf_subi(vrb,i)),i);
						}
						break;
					case 650: // vrfip
						for (int i=0;i<4;i++) {
							wvrf_subi(vrt,RoundToSPIntCeil(rvrf_subi(vrb,i)),i);
						}
						break;
					case 586: // vrfiz
						for (int i=0;i<4;i++) {
							wvrf_subi(vrt,RoundToSPIntTrunc(rvrf_subi(vrb,i)),i);
						}
						break;
					case 652: // vspltw
						asize*=2;
					case 588: // vsplth
						asize*=2;
					case 524: // vspltb
						tmp = rvr_subi(vrb,asize==1?vx_uim_b:asize==2?vx_uim_h:vx_uim_w,asize);
						for (int i=0;i<16/asize;i++) {
							wvr_subi(vrt,tmp,i,asize);
						}
						break;
					case 908: // vspltisw
						asize*=2;
					case 844: // vspltish
						asize*=2;
					case 780: // vspltisb
						tmp = ci5(vx_sim);
						if (asize==1) tmp = EXTS8(tmp);
						else if (asize==2) tmp = EXTS32(tmp);
						else tmp = EXTS(tmp);
						for (int i=0;i<16/asize;i++) {
							wvr_subi(vrt,tmp,i,asize);
						}
						break;
					case 452: // vsl
						tmp = ANDI(rvr(vrb),7);
						wvr(vrt,SHL(rvr(vra),tmp));
						break;
					case 1036: // vslo
						tmp = ANDI(LSHRI(rvr(vrb),3),0xf);
						wvr(vrt,SHL(rvr(vra),MULI(tmp,8)));
						break;
					case 708: // vsr
						tmp = ANDI(rvr(vrb),7);
						wvr(vrt,LSHR(rvr(vra),tmp));
						break;
					case 1100: // vsro
						tmp = ANDI(LSHRI(rvr(vrb),3),0xf);
						wvr(vrt,LSHR(rvr(vra),MULI(tmp,8)));
						break;
					default:
						found=false;
						break;
					}
					if (found) break;
					found = true;
					switch (bit_22_31) {
					case 134: // vcmpequw, vcmpequw.
						asize*=2;
					case 70: // vcmpequh, vcmpequh.
						asize*=2;
					case 6: // vcmpequb, vcmpequb.
						v_foreach_binop2(vrt,vra,vrb,asize,[&](Value*a,Value*b) {
							return B.CreateSExt(B.CreateICmpEQ(a,b),a->getType());
						});
						if (vc_rc) v_set_cr6(rvr(vrt));
						break;
					case 902: // vcmpgtsw, vcmpgtsw.
						asize*=2;
					case 838: // vcmpgtsh, vcmpgtsh.
						asize*=2;
					case 774: // vcmpgtsb, vcmpgtsb.
						v_foreach_binop2(vrt,vra,vrb,asize,[&](Value*a,Value*b) {
							return B.CreateSExt(B.CreateICmpSGT(a,b),a->getType());
						});
						if (vc_rc) v_set_cr6(rvr(vrt));
						break;
					case 646: // vcmpgtuw, vcmpgtuw.
						asize*=2;
					case 582: // vcmpgtuh, vcmpgtuh.
						asize*=2;
					case 518: // vcmpgtub, vcmpgtub.
						v_foreach_binop2(vrt,vra,vrb,asize,[&](Value*a,Value*b) {
							return B.CreateSExt(B.CreateICmpUGT(a,b),a->getType());
						});
						if (vc_rc) v_set_cr6(rvr(vrt));
						break;
					case 966: // vcmpbfp, vcmpbfp.
						vf_foreach_binop2(vrt,vra,vrb,[&](Value*a,Value*b) {
							return concatbits(concatbits(NOT(B.CreateFCmpOLE(a,b)),NOT(B.CreateFCmpOGE(a,b))),ci30(0));
						});
						if (vc_rc) {
							wrbit(r_cr,28-6*4 + 3,ci1(0));
							wrbit(r_cr,28-6*4 + 2,ci1(0));
							wrbit(r_cr,28-6*4 + 1,B.CreateICmpEQ(rvr(vrt),B.CreateSExt(ci1(0),t_i128)));
							wrbit(r_cr,28-6*4 + 0,ci1(0));
						}
						break;
					case 198: // vcmpeqfp, vcmpeqfp.
						vf_foreach_binop2(vrt,vra,vrb,[&](Value*a,Value*b) {
							return EXTS32(B.CreateFCmpOEQ(a,b));
						});
						if (vc_rc) v_set_cr6(rvr(vrt));
						break;
					case 454: // vcmpgefp, vcmpgefp.
						vf_foreach_binop2(vrt,vra,vrb,[&](Value*a,Value*b) {
							return EXTS32(B.CreateFCmpOGE(a,b));
						});
						if (vc_rc) v_set_cr6(rvr(vrt));
						break;
					case 710: // vcmpgtfp, vcmpgtfp.
						vf_foreach_binop2(vrt,vra,vrb,[&](Value*a,Value*b) {
							return EXTS32(B.CreateFCmpOGT(a,b));
						});
						if (vc_rc) v_set_cr6(rvr(vrt));
						break;
					default:
						found=false;
					}
					if (found) break;
					found = true;
					switch (bit_26_31) {
					case 43: // vperm
						tmp = concatbits(rvr(vra),rvr(vrb));
						for (int i=0;i<16;i++) {
							Value*b = getbits(rvr_subi(vrc,i,1),3,7);
							Value*shift = SUB(ci8(248),MULI(B.CreateZExt(b,t_i8),8));
							// This select is to work around a bug in llvm (0 shifts does not work for big integers)
							Value*v = B.CreateSelect(B.CreateICmpEQ(shift,ci8(0)),tmp,LSHR(tmp,EXTZ256(shift)));
							wvr_subi(vrt,TRUNC(v,1),i,1);
						}
						break;
					case 42: // vsel
						{
							Value*a = rvr(vra);
							Value*b = rvr(vrb);
							Value*c = rvr(vrc);
							wvr(vrt,OR(AND(c,b),AND(NOT(c),a)));
						}
						break;
					case 46: // vmaddfp
						vf_foreach_binop3(vrt,vra,vrb,vrc,FMADD);
						break;
					case 47: // vnmsubfp
						vf_foreach_binop3(vrt,vra,vrb,vrc,FNMSUB);
						break;
					case 44: // vsldoi
						tmp = concatbits(rvr(vra),rvr(vrb));
						wvr(vrt,TRUNC(LSHRI(tmp,128-8*va_shb),16));
						break;
					default:
						found=false;
					}
					if (found) break;
					bad_insn();
					break;
				case 7: // mulli
					wgr(rt,B.CreateMul(rgr(ra),EXTS(ri16(si))));
					break;
				case 11: // cmpi
					if (d_l==0) {
						tmp = EXTS(TRUNC(rgr(ra),4));
						tmp2 = EXTS(ri16(si));
					} else {
						tmp = rgr(ra);
						tmp2 = EXTS(ri16(si));
					}
					do_cmps(tmp,tmp2,d_bf);
					break;
				case 10: // cmpli
					if (d_l==0) {
						tmp = EXTZ(TRUNC(rgr(ra),4));
						tmp2 = EXTZ(ri16(si));
					} else {
						tmp = rgr(ra);
						tmp2 = EXTZ(ri16(si));
					}
					do_cmpu(tmp,tmp2,d_bf);
					break;
				case 8: // subfic
					wgr(rt,SUBFC(rgr(ra),EXTS(ri16(si))));
					break;
				case 12: // addic
					wgr(rt,ADDC(rgr(ra),EXTS(ri16(si))));
					break;
				case 13: // addic.
					tmp = ADDC(rgr(ra),EXTS(ri16(si)));
					cr0_set(tmp);
					wgr(rt,tmp);
					break;
				case 14: // addi
					wgr(rt,ADD(ra_or_0(),EXTS(ri16(si))));
					break;
				case 15: // addis
					wgr(rt,ADD(ra_or_0(),SHLI(EXTS(ri16(si)),16)));
					break;
				case 17: // sc
					if (bit_30==1) {
						if (debug_output) B.CreateCall(F_dump_addr,ci64(ip));
						if (debug_output) mkdumpregs();

						Value*n = rgr(11);
						B.CreateCall2(F_syscall,n,context_r);
						// SYS_EVENT_QUEUE_RECEIVE passes output through registers.
						// This is the ONLY syscall which does so, and it makes no sense, but we need to handle it.
						// TODO: This should be handled by F_syscall.
						k_if(B.CreateICmpEQ(n,ci64(0x82)),[&]() {
							// Just load the values the syscall wrote to memory.
							Value*src = rgr(4);
							wgr(4,RMEM(src,8));
							wgr(5,RMEM(ADDI(src,8),8));
							wgr(6,RMEM(ADDI(src,16),8));
							wgr(7,RMEM(ADDI(src,24),8));
						},[&](){});
					} else bad_insn();
					break;
				case 18: // b
					if (aa) nip = (((int32_t)li)<<8)>>6; // shifts to sign-extend (li is 24-bit), then shl 2
					else nip = ip + ((((int32_t)li)<<8)>>6);
					if (lk) {
						get_landing_pad_bb(ip+4);
						do_call(get_function(nip,fi.rtoc,0).f);
					} else {
						B.CreateBr(get_bb(nip));
						stop=true;
					}
					break;
				case 16: // bc
					if (!bo2) wr(r_ctr,B.CreateSub(rr(r_ctr),ConstantInt::get(t_i64,1)));
					Value*ctr_ok;
					Value*cond_ok;
					if (!bo2) ctr_ok = B.CreateXor(B.CreateICmpNE(rr(r_ctr),ConstantInt::get(t_i64,0)),ConstantInt::get(t_i1,bo3));
					else ctr_ok = ConstantInt::get(t_i1,1);
					if (!bo0) cond_ok = B.CreateICmpEQ(rrbit(r_cr,31-bi),ConstantInt::get(t_i1,bo1));
					else cond_ok = ConstantInt::get(t_i1,1);

					if (bo0&&bo2) {
						if (aa) nip = (((int32_t)b_bd)<<18)>>16; // shifts to sign-extend (bd is 14-bit), then shl 2
						else nip = ip + ((((int32_t)b_bd)<<18)>>16);
						if (lk) {
							do_call(get_function(nip,fi.rtoc,0).f);
						} else {
							B.CreateBr(get_bb(nip));
							stop=true;
						}
					} else {
						BasicBlock*b_true = BasicBlock::Create(context, "", F);
						BasicBlock*b_post = BasicBlock::Create(context, "", F);
						B.CreateCondBr(B.CreateAnd(ctr_ok,cond_ok),b_true,b_post);
						switch_bb(b_true);
						if (aa) nip = (((int32_t)b_bd)<<18)>>16; // shifts to sign-extend (bd is 14-bit), then shl 2
						else nip = ip + ((((int32_t)b_bd)<<18)>>16);
						if (lk) {
							do_call(get_function(nip,fi.rtoc,0).f);
						} else {
							B.CreateBr(get_bb(nip));
						}
						switch_bb(b_post);
					}
					break;
				case 19:
					switch (xl_xo) {
					case 16: // bclr

						if (lk) xcept("bclr with lk not supported");

						if (!bo2) wr(r_ctr,B.CreateSub(rr(r_ctr),ConstantInt::get(t_i64,1)));
						Value*ctr_ok;
						Value*cond_ok;
						if (!bo2) ctr_ok = B.CreateXor(B.CreateICmpNE(rr(r_ctr),ConstantInt::get(t_i64,0)),ConstantInt::get(t_i1,bo3));
						else ctr_ok = ConstantInt::get(t_i1,1);
						if (!bo0) cond_ok = B.CreateICmpEQ(rrbit(r_cr,31-bi),ConstantInt::get(t_i1,bo1));
						else cond_ok = ConstantInt::get(t_i1,1);


						if (bo0&&bo2) {
							B.CreateBr(bb_ret);
							stop=true;
						} else {
							BasicBlock*b_true = BasicBlock::Create(context, "", F);
							BasicBlock*b_post = BasicBlock::Create(context, "", F);
							B.CreateCondBr(B.CreateAnd(ctr_ok,cond_ok),b_true,b_post);
							switch_bb(b_true);
							B.CreateBr(bb_ret);
							switch_bb(b_post);
						}

						break;
					case 528: // bcctr
						
						//if (lk) xcept("bcctr with lk not supported");
						//if (lk) printf("bcctr with lk");

						//Value*fv;
						//fv = B.CreateIntToPtr(ANDI(rr(r_ctr),~3),p_FT);
						//fv = B.CreateIntToPtr(rr(r_ctr),p_FT);
						if (bo0) {
							//B.CreateCall(F_dump_addr,ANDI(rr(r_ctr),~3));

							//if (ip==0x4270C8) printf("a, switch_reg is %d, regvals[switch_reg].t is %d\n",(int)switch_reg,(int)regvals[switch_reg].t);

							// this isn't very good code to detect a switch, but i'll use it as long as it works
							//if (!lk && switch_reg!=-1 && regvals[switch_reg].t==regval::t_mem_reg_plus_reg ) {
								//regval&srv = regvals[switch_reg];
								//regval&trv = regvals[srv.r2];
								//if (ip==0x10DE8) printf("b\n");
								//if (trv.t==regval::t_mem_reg_offset && trv.r==2) {
							if (!lk && switch_ta!=-1 && !use_only_safe_switch_detection) {
								if (true) {
// 									uint64_t ta = fi.rtoc + trv.o;
// 									if (valid_data(ta)) ta = read_32(ta);
// 									else ta=-1;
									uint64_t ta = switch_ta;
									//if (ip==0x4270C8) printf("ta is %x + %x -> %x\n",(int)fi.rtoc,(int)trv.o,(int)ta);
									if (valid_ip(ta)) {
										// ta seems to always be equal to ip+4 at this point, could check for it..
										int32_t*p = (int32_t*)ta;
										uint32_t cnt = 0;
										uint32_t lowest_addend=~0;
										std::vector<BasicBlock*> targets;
										while (cnt*4<lowest_addend) {
											int32_t addend = read_32((uint32_t)(uint64_t)p);
											if (addend>0 && (uint32_t)addend<lowest_addend) lowest_addend=addend;
											uint64_t dst = addend + ta;
											if (!can_translate_addr(dst)) break;
											if (!valid_ip(dst)) break;
											targets.push_back(get_bb(dst));
											p++;
											cnt++;
										}
										printf("found switch with %d cases at %p\n",(int)cnt,(void*)ta);

										BasicBlock*def = new_bb();
										Value*v = LSHRI(switch_val,2); // divide by 4;
										SwitchInst*sw = B.CreateSwitch(v,def,cnt);
										for (size_t i=0;i<targets.size();i++) {
											sw->addCase(ci64(i),targets[i]);
										}
										switch_bb(def);
										mkerr("unreachable: switch error");
										do_ret();
										stop = true;
										hard_stop = true;
									} else {
										Value*ctr_val = rr(r_ctr);
										Value*fv = B.CreateIntToPtr(ctr_val,p_FT);
										if (lk) do_call(fv);
										else {
											do_tail_call(fv);
											stop=true;
										}
									}
								}
							} else {
								Value*ctr_val = rr(r_ctr);
								Value*fv = B.CreateIntToPtr(ctr_val,p_FT);
								if (lk) do_call(fv);
								else {
									if (enable_safe_switch_detection) {
										uint64_t ta = ip+4;
										int32_t*p = (int32_t*)ta;
										uint32_t cnt = 0;
										uint32_t lowest_addend=~0;
										std::map<uint64_t,BasicBlock*> targets;
										while (cnt*4<lowest_addend) {
											int32_t addend = read_32((uint32_t)(uint64_t)p);
											if (addend>0 && (uint32_t)addend<lowest_addend) lowest_addend=addend;
											uint64_t dst = addend + ta;
											if (!can_translate_addr(dst)) break;
											if (dst<lowest_addend+ta) break;
											if (!valid_ip(dst)) break;
											targets[dst] = get_bb(dst);
											p++;
											cnt++;
										}
										if (cnt) {
											printf("(safe) found switch with %d cases at %p\n",(int)cnt,(void*)ta);
											BasicBlock*def = new_bb();
											SwitchInst*sw = B.CreateSwitch(ctr_val,def,cnt);
											for (auto i=targets.begin();i!=targets.end();++i) {
												sw->addCase(ci64(i->first),i->second);
											}
											switch_bb(def);
										}// else printf("no switch at %p\n",(void*)ta);
										do_tail_call(fv);
										stop = true;
										hard_stop = true;
									} else {
										do_tail_call(fv);
										stop=true;
									}
								}
							}
						} else {
							Value*ctr_val = rr(r_ctr);
							Value*fv = B.CreateIntToPtr(ctr_val,p_FT);
							Value*cond_ok;
							cond_ok = B.CreateICmpEQ(rrbit(r_cr,31-bi),ConstantInt::get(t_i1,bo1));
							BasicBlock*b_true = BasicBlock::Create(context, "", F);
							BasicBlock*b_post = BasicBlock::Create(context, "", F);
							B.CreateCondBr(cond_ok,b_true,b_post);
							switch_bb(b_true);
							if (lk) do_call(fv);
							else {
								do_tail_call(fv);
								stop=true;
							}
							switch_bb(b_post);
						}
						break;
					case 150: // isync
						B.CreateCall5(F_llvm_memory_barrier,ci1(1),ci1(1),ci1(1),ci1(1),ci1(0));
						break;
					case 256: // crand
						wrbit(r_cr,31-xl_bt,AND(rrbit(r_cr,31-xl_ba),rrbit(r_cr,31-xl_bb)));
						break;
					case 225: // crnand
						wrbit(r_cr,31-xl_bt,NAND(rrbit(r_cr,31-xl_ba),rrbit(r_cr,31-xl_bb)));
						break;
					case 449: // cror
						wrbit(r_cr,31-xl_bt,OR(rrbit(r_cr,31-xl_ba),rrbit(r_cr,31-xl_bb)));
						break;
					case 193: // crxor
						wrbit(r_cr,31-xl_bt,XOR(rrbit(r_cr,31-xl_ba),rrbit(r_cr,31-xl_bb)));
						break;
					case 33: // crnor
						wrbit(r_cr,31-xl_bt,NOR(rrbit(r_cr,31-xl_ba),rrbit(r_cr,31-xl_bb)));
						break;
					case 289: // creqv
						wrbit(r_cr,31-xl_bt,EQV(rrbit(r_cr,31-xl_ba),rrbit(r_cr,31-xl_bb)));
						break;
					case 129: // crandc
						wrbit(r_cr,31-xl_bt,ANDC(rrbit(r_cr,31-xl_ba),rrbit(r_cr,31-xl_bb)));
						break;
					case 417: // crorc
						wrbit(r_cr,31-xl_bt,ORC(rrbit(r_cr,31-xl_ba),rrbit(r_cr,31-xl_bb)));
						break;
					case 0: // mcrf
						wrbit(r_cr,28-xl_bf*4+0,rrbit(r_cr,28-xl_bfa*4+0));
						wrbit(r_cr,28-xl_bf*4+1,rrbit(r_cr,28-xl_bfa*4+1));
						wrbit(r_cr,28-xl_bf*4+2,rrbit(r_cr,28-xl_bfa*4+2));
						wrbit(r_cr,28-xl_bf*4+3,rrbit(r_cr,28-xl_bfa*4+3));
						break;
					default:
						bad_insn();
						break;
					}
					break;
				case 24: // ori
					wgr(ra,B.CreateOr(rgr(rs),EXTZ(ri16(ui))));
					if (ui==0) is_nop();
					break;
				case 25: // oris
					wgr(ra,B.CreateOr(rgr(rs),B.CreateShl(EXTZ(ri16(ui)),16)));
					break;
				case 26: // xori
					wgr(ra,B.CreateXor(rgr(rs),EXTZ(ri16(ui))));
					break;
				case 27: // xoris
					wgr(ra,B.CreateXor(rgr(rs),B.CreateShl(EXTZ(ri16(ui)),16)));
					break;
				case 28: // andi
					tmp = B.CreateAnd(rgr(rs),EXTZ(ri16(ui)));
					cr0_set(tmp);
					wgr(ra,tmp);
					break;
				case 29: // andis
					tmp = B.CreateAnd(rgr(rs),B.CreateShl(EXTZ(ri16(ui)),16));
					cr0_set(tmp);
					wgr(ra,tmp);
					break;
				case 31: // 31
					found=true;
					switch (x_xo) {
						case 986: // extsw
							asize*=2;
						case 922: // extsh
							asize*=2;
						case 954: // extsb
							tmp = EXTS(TRUNC(rgr(rs),asize));
							if (rc) cr0_set(tmp);
							wgr(ra,tmp);
							break;
						case 339: // mfspr
							spr = (spr>>5 | spr<<5)&0x3FF;
							if (spr==1) wgr(rt,rr(r_xer));
							else if (spr==8) wgr(rt,rr(r_lr));
							else if (spr==9) wgr(rt,rr(r_ctr));
							else if (spr==256) wgr(rt,EXTZ(rr(r_vrsave)));
							else bad_insn();
							break;
						case 467: // mtspr
							spr = (spr>>5 | spr<<5)&0x3FF;
							if (spr==1) wr(r_xer,rgr(rt));
							else if (spr==8) wr(r_lr,rgr(rt));
							else if (spr==9) wr(r_ctr,rgr(rt));
							else if (spr==256) wr(r_vrsave,TRUNC(rgr(rt),4));
							else bad_insn();
							break;
						case 19: // mfcr
							wgr(rt,EXTZ(rr(r_cr)));
							break;
						case 144: { // mtcrf
							uint32_t mask = 0;
							for (int i=0;i<8;i++) {
								if (fxm&(1<<i)) {
									mask |= 0xf << (i*4);
								}
							}
							wr(r_cr,OR(ANDI(TRUNC(rgr(rs),4),mask),ANDI(rr(r_cr),~mask)));
							break;
						}
						case 28: // and
							binop(AND);
							break;
						case 444: // or
							binop(OR);
							break;
						case 316: // xor
							binop(XOR);
							break;
						case 476: // nand
							binop(NAND);
							break;
						case 124: // nor
							binop(NOR);
							break;
						case 284: // eqv
							binop(EQV);
							break;
						case 60: // andc
							binop(ANDC);
							break;
						case 412: // orc
							binop(ORC);
							break;
						case 21: // ldx
							asize*=2;
						case 23: // lwzx
							asize*=2;
						case 279: // lhzx
							asize*=2;
						case 87: // lbzx
							tmp = ra_or_0();
							tmp2 = rgr(rb);
							ea = ADD(tmp,tmp2);
							wgr(rt,EXTZ(RMEM(ea,asize)));
							//if (ip<=0x10DE8&&ip>=0x10DCC) printf(" -- %x -- ra is %d, rb is %d\n",(int)ip,ra,rb);
							//if (ip<=0x10DE8&&ip>=0x10DCC) printf("regvals[rb].t is %d\n",regvals[r_gpr+rb].t);
							if (ra && regvals[r_gpr+rb].t==regval::t_mem_reg_offset && regvals[r_gpr+rb].r==2) {
								regvals[r_gpr+ra].t = regval::t_mem_reg_plus_reg;
								regvals[r_gpr+ra].r = r_gpr+ra;
								regvals[r_gpr+ra].r2 = r_gpr+rb;
								switch_reg = r_gpr+ra;
								switch_val = tmp;
								
								uint64_t ta = fi.rtoc + regvals[r_gpr+rb].o;
								if (valid_data(ta)) switch_ta = read_32(ta);
							} else if (ra && regvals[r_gpr+ra].t==regval::t_mem_reg_offset && regvals[r_gpr+ra].r==2) {
								regvals[r_gpr+rb].t = regval::t_mem_reg_plus_reg;
								regvals[r_gpr+rb].r = r_gpr+rb;
								regvals[r_gpr+rb].r2 = r_gpr+ra;
								switch_reg = r_gpr+rb;
								switch_val = tmp2;

								uint64_t ta = fi.rtoc + regvals[r_gpr+ra].o;
								if (valid_data(ta)) switch_ta = read_32(ta);
							}
							break;
						case 53: // ldux
							asize*=2;
						case 55: // lwzux
							asize*=2;
						case 311: // lhzux
							asize*=2;
						case 119: // lbzux
							if (ra==0 || ra==rt) {bad_insn();break;}
							ea = ADD(rgr(ra),rgr(rb));
							wgr(rt,EXTZ(RMEM(ea,asize)));
							wgr(ra,ea);
							break;
						case 341: // lwax
							asize*=2;
						case 343: // lhax
							asize*=2;
							ea = ADD(ra_or_0(),rgr(rb));
							wgr(rt,EXTS(RMEM(ea,asize)));
							break;
						case 373: // lwaux
							asize*=2;
						case 375: // lhaux
							asize*=2;
							if (ra==0 || ra==rt) {bad_insn();break;}
							ea = ADD(rgr(ra),rgr(rb));
							wgr(rt,EXTS(RMEM(ea,asize)));
							wgr(ra,ea);
							break;
						case 149: // stdx
							asize*=2;
						case 151: // stwx
							asize*=2;
						case 407: // sthx
							asize*=2;
						case 215: // stbx
							ea = ADD(ra_or_0(),rgr(rb));
							WMEM(ea,asize,TRUNC(rgr(rs),asize));
							break;
						case 181: // stdux
							asize*=2;
						case 183: // stwux
							asize*=2;
						case 439: // sthux
							asize*=2;
						case 247: // stbux
							if (ra==0) {bad_insn();break;}
							ea = ADD(rgr(ra),rgr(rb));
							WMEM(ea,asize,TRUNC(rgr(rs),asize));
							wgr(ra,ea);
							break;
						case 790: // lhbrx
							ea = ADD(ra_or_0(),rgr(rb));
							wgr(rt,EXTZ(BSWAP16(RMEM(ea,2))));
							break;
						case 918: // sthbrx
							ea = ADD(ra_or_0(),rgr(rb));
							WMEM(ea,2,BSWAP16(TRUNC(rgr(rs),2)));
							break;
						case 534: // lwbrx
							ea = ADD(ra_or_0(),rgr(rb));
							wgr(rt,EXTZ(BSWAP32(RMEM(ea,4))));
							break;
						case 662: // stwbrx
							ea = ADD(ra_or_0(),rgr(rb));
							WMEM(ea,4,BSWAP32(TRUNC(rgr(rs),4)));
							break;
						case 0: // cmp
							if (d_l==0) {
								tmp = EXTS(TRUNC(rgr(ra),4));
								tmp2 = EXTS(TRUNC(rgr(rb),4));
							} else {
								tmp = rgr(ra);
								tmp2 = rgr(rb);
							}
							do_cmps(tmp,tmp2,d_bf);
							break;
						case 32: // cmpl
							if (d_l==0) {
								tmp = EXTZ(TRUNC(rgr(ra),4));
								tmp2 = EXTZ(TRUNC(rgr(rb),4));
							} else {
								tmp = rgr(ra);
								tmp2 = rgr(rb);
							}
							do_cmpu(tmp,tmp2,d_bf);
							break;
						case 20: // lwarx
							ea = ADD(ra_or_0(),rgr(rb));
							B.CreateStore(ea,reserved_address);
							tmp = EXTZ(RMEM(ea,4));
							B.CreateStore(tmp,reserved_value);
							wgr(rt,tmp);
							break;
						case 150: // stwcx.
							{
								ea = ADD(ra_or_0(),rgr(rb));
								BasicBlock*b_true = BasicBlock::Create(context, "", F);
								BasicBlock*b_false = BasicBlock::Create(context, "", F);
								BasicBlock*b_post = BasicBlock::Create(context, "", F);
								B.CreateCondBr(B.CreateICmpEQ(B.CreateLoad(reserved_address),ea),b_true,b_false);
								switch_bb(b_true);

								Value*ov = TRUNC(B.CreateLoad(reserved_value),4);
								Value*v = BSWAP32(B.CreateCall3(F_llvm_atomic_cmp_swap32,B.CreateIntToPtr(ea,p_t_i32),BSWAP32(ov),BSWAP32(TRUNC(rgr(rs),4))));

								wrbit(r_cr,28-0*4 + 3,ci1(0));
								wrbit(r_cr,28-0*4 + 2,ci1(0));
								wrbit(r_cr,28-0*4 + 1,B.CreateICmpEQ(v,ov));
								wrbit(r_cr,28-0*4 + 0,rrbit(r_xer,xer_so));
								B.CreateBr(b_post);
								switch_bb(b_false);
								wrbit(r_cr,28-0*4 + 3,ci1(0));
								wrbit(r_cr,28-0*4 + 2,ci1(0));
								wrbit(r_cr,28-0*4 + 1,ci1(0));
								wrbit(r_cr,28-0*4 + 0,rrbit(r_xer,xer_so));
								B.CreateBr(b_post);
								switch_bb(b_post);
								break;
							}
						case 84: // ldarx
							ea = ADD(ra_or_0(),rgr(rb));
							B.CreateStore(ea,reserved_address);
							tmp = RMEM(ea,8);
							B.CreateStore(tmp,reserved_value);
							//B.CreateCall(F_dump_addr,ea);
							//B.CreateCall(F_dump_addr,tmp);
							wgr(rt,tmp);
							break;
						case 214: // stdcx.
							{
								ea = ADD(ra_or_0(),rgr(rb));
								if (debug_output&&debug_superverbose) B.CreateCall(F_dump_addr,ea);
								BasicBlock*b_true = BasicBlock::Create(context, "", F);
								BasicBlock*b_false = BasicBlock::Create(context, "", F);
								BasicBlock*b_post = BasicBlock::Create(context, "", F);
								B.CreateCondBr(B.CreateICmpEQ(B.CreateLoad(reserved_address),ea),b_true,b_false);
								switch_bb(b_true);

								Value*ov = B.CreateLoad(reserved_value);
								Value*v = BSWAP64(B.CreateCall3(F_llvm_atomic_cmp_swap64,B.CreateIntToPtr(ea,p_t_i64),BSWAP64(ov),BSWAP64(rgr(rs))));

								if (debug_output&&debug_superverbose) B.CreateCall(F_dump_addr,ov);
								if (debug_output&&debug_superverbose) B.CreateCall(F_dump_addr,v);

								wrbit(r_cr,28-0*4 + 3,ci1(0));
								wrbit(r_cr,28-0*4 + 2,ci1(0));
								wrbit(r_cr,28-0*4 + 1,B.CreateICmpEQ(v,ov));
								wrbit(r_cr,28-0*4 + 0,rrbit(r_xer,xer_so));
								B.CreateBr(b_post);
								switch_bb(b_false);
								wrbit(r_cr,28-0*4 + 3,ci1(0));
								wrbit(r_cr,28-0*4 + 2,ci1(0));
								wrbit(r_cr,28-0*4 + 1,ci1(0));
								wrbit(r_cr,28-0*4 + 0,rrbit(r_xer,xer_so));
								B.CreateBr(b_post);
								switch_bb(b_post);
								break;
							}
						case 86: // dcbf // data cache block flush
						case 278: // dcbt // data cache block touch
							break;
						case 854: // eieio
						case 598: // sync
							B.CreateCall5(F_llvm_memory_barrier,ci1(1),ci1(1),ci1(1),ci1(1),ci1(0));
							break;
						case 371: // mftb
							wgr(rt,B.CreateCall(F_llvm_readcyclecounter));
							break;
						case 24: // slw, slw.
							binop(SLW);
							break;
						case 536: // srw, srw.
							binop(SRW);
							break;
						case 824: // srawi, srawi
							tmp = SRAW(rgr(rs),ci64(x_sh));
							if (rc) cr0_set(tmp);
							wgr(ra,tmp);
							break;
						case 792: // sraw, sraw.
							binop(SRAW);
							break;
						case 27: // sld, sld.
							binop(SLD);
							break;
						case 539: // srd, srd.
							binop(SRD);
							break;
						case 794: // srad, srad.
							binop(SRAD);
							break;
						case 7: // lvebx
							ea = ADD(ra_or_0(),rgr(rb));
							tmp = ANDI(ea,0xf);
							wvr(vrt,SHL(EXTZ128(RMEM(ea,1)),SUB(ci128(127,0),EXTZ128(MULI(tmp,8)))));
							break;
						case 39: // lvehx
							ea = ANDI(ADD(ra_or_0(),rgr(rb)),-2);
							tmp = ANDI(ea,0xf);
							wvr(vrt,SHL(EXTZ128(RMEM(ea,2)),SUB(ci128(127,0),EXTZ128(MULI(tmp,8)))));
							break;
						case 71: // lvewx
							ea = ANDI(ADD(ra_or_0(),rgr(rb)),-4);
							tmp = ANDI(ea,0xf);
							wvr(vrt,SHL(EXTZ128(RMEM(ea,4)),SUB(ci128(127,0),EXTZ128(MULI(tmp,8)))));
							break;
						case 103: // lvx
						case 359: // lvxl
							ea = ANDI(ADD(ra_or_0(),rgr(rb)),-16);
							wvr(vrt,RMEM(ea,16));
							break;
						case 135: // stvebx
							ea = ADD(ra_or_0(),rgr(rb));
							tmp = ANDI(ea,0xf);
							WMEM(ea,1,TRUNC(LSHR(rvr(vrs),EXTZ128(SUB(ci64(120),MULI(tmp,8)))),1));
							break;
						case 169: // stvehx
							ea = ANDI(ADD(ra_or_0(),rgr(rb)),-2);
							tmp = ANDI(ea,0xf);
							WMEM(ea,2,TRUNC(LSHR(rvr(vrs),EXTZ128(SUB(ci64(120),MULI(tmp,8)))),2));
							break;
						case 199: // stvewx
							ea = ANDI(ADD(ra_or_0(),rgr(rb)),-4);
							tmp = ANDI(ea,0xf);
							WMEM(ea,4,TRUNC(LSHR(rvr(vrs),EXTZ128(SUB(ci64(120),MULI(tmp,8)))),4));
							break;
						case 231: // stvx
						case 487: // stvxl
							ea = ANDI(ADD(ra_or_0(),rgr(rb)),-16);
							WMEM(ea,16,rvr(vrs));
							break;
						case 58: // cntlzd, cnlzd.
							tmp = B.CreateCall(F_llvm_ctlz64,rgr(rs));
							if (rc) cr0_set(tmp);
							wgr(ra,tmp);
							break;
						case 26: // cntlzw, cntlzw.
							tmp = EXTZ(B.CreateCall(F_llvm_ctlz32,TRUNC(rgr(rs),4)));
							if (rc) cr0_set(tmp);
							wgr(ra,tmp);
							break;
						case 599: // lfdx
							asize*=2;
						case 535: // lfsx
							asize*=4;
							ea = ADD(ra_or_0(),rgr(rb));
							wfr(frt,DOUBLE(RMEMF(ea,asize)));
							break;
						case 639: // lfdux
							asize*=2;
						case 567: // lfsux
							asize*=4;
							ea = ADD(rgr(ra),rgr(rb));
							wfr(frt,DOUBLE(RMEMF(ea,asize)));
							wgr(ra,ea);
							break;
						case 727: // stfdx
							asize*=2;
						case 663: // stfsx
							asize*=4;
							ea = ADD(ra_or_0(),rgr(rb));
							if (asize==4) WMEMF(ea,4,SINGLE(rfr(frs)));
							else WMEMF(ea,8,rfr(frs));
							break;
						case 759: // stfdux
							asize*=2;
						case 695: // stfsux
							asize*=4;
							ea = ADD(rgr(ra),rgr(rb));
							if (asize==4) WMEMF(ea,4,SINGLE(rfr(frs)));
							else WMEMF(ea,8,rfr(frs));
							wgr(ra,ea);
							break;
						case 983: // stfiwx
							ea = ADD(ra_or_0(),rgr(rb));
							WMEM(ea,4,TRUNC(B.CreateBitCast(rfr(frs),t_i64),4));
							break;
						case 6: // lvsl
							tmp = EXTZ256(SUB(ci64(0x10),ANDI(ADD(ra_or_0(),rgr(rb)),0xf)));
							tmp2 = ci256(0x18191A1B1C1D1E1F,0x1011121314151617,0x08090A0B0C0D0E0F,0x0001020304050607);
							wvr(vrt,TRUNC(LSHR(tmp2,tmp),16));
							break;
						case 38: // lvsr
							tmp = EXTZ256(ANDI(ADD(ra_or_0(),rgr(rb)),0xf));
							tmp2 = ci256(0x18191A1B1C1D1E1F,0x1011121314151617,0x08090A0B0C0D0E0F,0x0001020304050607);
							// This select is to work around bug in LLVM (0 shift does not work for big integers)
							tmp = B.CreateSelect(B.CreateICmpEQ(tmp,ci256(0,0,0,0)),tmp2,LSHR(tmp2,tmp));
							wvr(vrt,TRUNC(tmp,16));
							break;
						default:
							found=false;
							break;
					}
					if (found) break;
					found=true;
					switch (xs_xo) {
						case 413: // sradi, sradi
							tmp = SRAD(rgr(rs),ci64(xs_sh));
							if (rc) cr0_set(tmp);
							wgr(ra,tmp);
							break;
						default:
							found=false;
							break;
					}
					if (found) break;
					found=true;
					switch (xo_xo) {
						case 266: // add, add_, addo, addo_
							if (oe) arith_op(ADDO);
							else arith_op(ADD);
							break;
						case 40: // subf, subf_, subfc, subfc_
							if (oe) arith_op(SUBFO);
							else arith_op(SUBF);
							break;
						case 10: // addc, addc_, addco, addco_
							if (oe) arith_op(ADDCO);
							else arith_op(ADDC);
							break;
						case 8: // subfc, subfc_, subfco, subfco_
							if (oe) arith_op(SUBFCO);
							else arith_op(SUBFC);
							break;
						case 138: // adde, adde_, addeo, addeo_
							if (oe) arith_op(ADDEO);
							else arith_op(ADDE);
							break;
						case 136: // subfe, subfe_, subfeo, subfeo_
							if (oe) arith_op(SUBFEO);
							else arith_op(SUBFE);
							break;
						case 234: // addme, addme_, addmeo, addmeo_
							if (oe) arith_op1(ADDMEO);
							else arith_op1(ADDME);
							break;
						case 232: // subfme, subfme_, subfmeo, subfmeo_
							if (oe) arith_op1(SUBFMEO);
							else arith_op1(SUBFME);
							break;
						case 202: // addze, addze_, addzeo, addzeo_
							if (oe) arith_op1(ADDZEO);
							else arith_op1(ADDZE);
							break;
						case 200: // subfze, subfze_, subfzeo, subfzeo_
							if (oe) arith_op1(SUBFZEO);
							else arith_op1(SUBFZE);
							break;
						case 104: // neg, neg_, nego, nego_
							if (oe) arith_op1(NEGO);
							else arith_op1(NEG);
							break;
						case 75: // mulhw, mulhw.
							arith_op(MULHW);
							break;
						case 235: // mullw, mullw., mullwo, mullwo.
							if (oe) arith_op(MULLWO);
							else arith_op(MULLW);
							break;
						case 11: // mulhwu, mulhwu.
							arith_op(MULHWU);
							break;
						case 491: // divw, divw., divwo, divwo.
							if (oe) {
								arith_op(DIVW); // invalid: no support for divwo; undefined behaviour in case of divide by zero
								wrbit(r_xer,xer_ov,ci1(0));
							} else arith_op(DIVW);
							break;
						case 459: // divwu, divwu., divwuo, divwuo.
							if (oe) {
								arith_op(DIVWU);
								wrbit(r_xer,xer_ov,ci1(0));
							} else arith_op(DIVWU);
							break;
						case 233: // mulld, mulld., mulldo, mulldo.
							if (oe) arith_op(MULLDO);
							else arith_op(MULLD);
							break;
						case 73: // mulhd, mulhd.
							arith_op(MULHD);
							break;
						case 9: // mulhdu, mulhdu.
							arith_op(MULHDU);
							break;
						case 489: // divd, divd., divdo, divdo.
							if (oe) {
								arith_op(DIVD);
								wrbit(r_xer,xer_ov,ci1(0));
							} else arith_op(DIVD);
							break;
						case 457: // divdu, divdu., divduo, divduo.
							if (oe) {
								arith_op(DIVDU);
								wrbit(r_xer,xer_ov,ci1(0));
							} else arith_op(DIVDU);
							break;
						default:
							found=false;
							break;
					}
					if (found) break;
					bad_insn();
					break;
				case 58: // 58
					if (ds_xo==0) { // ld
						ea = ADD(ra_or_0(),EXTS(ri14(ds<<2)));;
						wgr(rt,RMEM(ea,8));
					} else if (ds_xo==1) { // ldu
						if (ra==0 || ra==rt) {bad_insn();break;}
						ea = ADD(rgr(ra),EXTS(ri14(ds<<2)));
						wgr(rt,RMEM(ea,8));
						wgr(ra,ea);
					} else if (ds_xo==2) { // lwa
						ea = ADD(ra_or_0(),EXTS(ri14(ds<<2)));
						wgr(rt,EXTS(RMEM(ea,4)));
					}
					else bad_insn();
					break;
				case 32: // lwz
					asize*=2;
				case 40: // lhz
					asize*=2;
				case 34: // lbz
					ea = ADD(ra_or_0(),EXTS(ri16(d)));;
					//B.CreateCall(F_dump_addr,ea);
					wgr(rt,EXTZ(RMEM(ea,asize)));
					if (ra) {
						regval&rv = regvals[r_gpr+rt];
						rv.t = regval::t_mem_reg_offset;
						rv.r = ra;
						rv.o = (int16_t)d;
						//if (ip<=0x10DE8&&ip>=0x10DCC) printf(" -- %x -- ra is %d, rt is %d\n",(int)ip,ra,rt);
					}
					break;
				case 33: // lwzu
					asize*=2;
				case 41: // lhzu
					asize*=2;
				case 35: // lbzu
					if (ra==0 || ra==rt) {bad_insn();break;}
					ea = ADD(rgr(ra),EXTS(ri16(d)));
					wgr(rt,EXTZ(RMEM(ea,asize)));
					wgr(ra,ea);
					break;
				case 42: // lha
					ea = ADD(ra_or_0(),EXTS(ri16(d)));
					wgr(rt,EXTS(RMEM(ea,2)));
					break;
				case 43: // lhau
					if (ra==0 || ra==rt) {bad_insn();break;}
					ea = ADD(rgr(ra),EXTS(ri16(d)));
					wgr(rt,EXTS(RMEM(ea,2)));
					wgr(ra,ea);
					break;
				case 36: // stw
					asize*=2;
				case 44: // sth
					asize*=2;
				case 38: // stb
					ea = ADD(ra_or_0(),EXTS(ri16(d)));
					WMEM(ea,asize,TRUNC(rgr(rs),asize));
					break;
				case 37: // stwu
					asize*=2;
				case 45: // sthu
					asize*=2;
				case 39: // stbu
					ea = ADD(ra_or_0(),EXTS(ri16(d)));
					WMEM(ea,asize,TRUNC(rgr(rs),asize));
					wgr(ra,ea);
					break;
				case 62: // 62
					if (bit_30_31==0) { // std
						ea = ADD(ra_or_0(),EXTS(ri14(ds<<2)));
						WMEM(ea,8,rgr(rs));
					} else { // stdu
						if (ra==0) {bad_insn();break;}
						ea = ADD(rgr(ra),EXTS(ri14(ds<<2)));
						WMEM(ea,8,rgr(rs));
						wgr(ra,ea);
					}
					break;
				case 30: // 30
					if (md_xo==0) { // rldicl
						//if (ip==0x103A4) xcept("ROTL64 %d %d",(int)rs,(int)md_sh);
						tmp = AND(ROTL64(rgr(rs),ci64(md_sh)),MASK(0,63-md_mb));
						if (rc) cr0_set(tmp);
						wgr(ra,tmp);
					} else if (md_xo==1) { // rldicr
						tmp = AND(ROTL64(rgr(rs),ci64(md_sh)),MASK(63-md_me,63));
						if (rc) cr0_set(tmp);
						wgr(ra,tmp);
					} else if (md_xo==2) { // rldic
						tmp = AND(ROTL64(rgr(rs),ci64(md_sh)),MASK(md_sh,63-md_mb));
						if (rc) cr0_set(tmp);
						wgr(ra,tmp);
					} else if (mds_xo==8) { // rldcl
						tmp = AND(ROTL64(rgr(rs),AND(rgr(rb),ci64(0x3F))),MASK(md_sh,63-md_mb));
						if (rc) cr0_set(tmp);
						wgr(ra,tmp);
					} else if (mds_xo==9) { // rldcr
						tmp = AND(ROTL64(rgr(rs),AND(rgr(rb),ci64(0x3F))),MASK(63-mds_me,63));
						if (rc) cr0_set(tmp);
						wgr(ra,tmp);
					} else if (md_xo==3) { // rldimi
						tmp = MASK(md_sh,63-md_mb);
						tmp = OR(AND(ROTL64(rgr(rs),ci64(md_sh)),tmp),AND(rgr(ra),NOT(tmp)));
						if (rc) cr0_set(tmp);
						wgr(ra,tmp);
					}
					else bad_insn();
					break;
				case 21: // rlwinm
					tmp = AND(ROTL32(rgr(rs),ci64(m_sh)),MASK(31-m_me,31-m_mb));
					if (rc) cr0_set(tmp);
					wgr(ra,tmp);
					break;
				case 23: // rlwnm
					tmp = AND(ROTL32(rgr(rs),AND(rgr(rb),ci64(0x3F))),MASK(31-m_me,31-m_mb));
					if (rc) cr0_set(tmp);
					wgr(ra,tmp);
					break;
				case 20: // rlwimi
					tmp = MASK(31-m_me,31-m_mb);
					tmp = OR(AND(ROTL32(rgr(rs),ci64(m_sh)),tmp),AND(rgr(ra),NOT(tmp)));
					if (rc) cr0_set(tmp);
					wgr(ra,tmp);
					break;
				case 50: // lfd
					asize*=2;
				case 48: // lfs
					asize*=4;
					ea = ADD(ra_or_0(),EXTS(ri16(d)));
					wfr(frt,DOUBLE(RMEMF(ea,asize)));
					break;
				case 51: // lfdu
					asize*=2;
				case 49: // lfsu
					asize*=4;
					ea = ADD(rgr(ra),EXTS(ri16(d)));
					wfr(frt,DOUBLE(RMEMF(ea,asize)));
					wgr(ra,ea);
					break;
				case 54: // stfd
					asize*=2;
				case 52: // stfs
					asize*=4;
					ea = ADD(ra_or_0(),EXTS(ri16(d)));
					if (asize==4) WMEMF(ea,4,SINGLE(rfr(frs)));
					else WMEMF(ea,8,rfr(frs));
					break;
				case 55: // stfdu
					asize*=2;
				case 53: // stfsu
					asize*=4;
					ea = ADD(ra_or_0(),EXTS(ri16(d)));
					if (asize==4) WMEMF(ea,4,SINGLE(rfr(frs)));
					else WMEMF(ea,8,rfr(frs));
					wgr(ra,ea);
					break;
				case 59:
					found=true; // invalid: no floating point arithmetic instructions set fpscr
					switch (a_xo) {
					case 21: // fadds, fadds.
						fariths_op2(FADD,fra,frb);
						break;
					case 20: // fsubs, fsubs.
						fariths_op2(FSUB,fra,frb);
						break;
					case 25: // fmuls, fmuls.
						fariths_op2(FMUL,fra,frc);
						break;
					case 18: // fdivs, fdivs.
						fariths_op2(FDIV,fra,frb);
						break;
					case 22: // fsqrts, fsqrts.
						fariths_op1(FSQRT,frb);
						break;
					case 29: // fmadds, fmadds.
						fariths_op3(FMADD,fra,frb,frc);
						break;
					case 28: // fmsubs, fmsubs.
						fariths_op3(FMSUB,fra,frb,frc);
						break;
					case 31: // fnmadds, fnmadds.
						fariths_op3(FNMADD,fra,frb,frc);
						break;
					case 30: // fnmsubs, fnmsubs.
						fariths_op3(FNMSUB,fra,frb,frc);
						break;
					default:
						found=false;
						break;
					}
					if (!found) bad_insn();
					break;
				case 63:
					found=true;
					switch (x_xo) {
						case 12: // frsp, frsp.
							tmp = SINGLE(rfr(frb)); // invalid: ingores rounding mode, does not set fpscr
							if (rc) cr1_set(tmp);
							wfr(frt,DOUBLE(tmp));
							break;
						case 815: // fctidz, fctidz. // these FPToSI instructions might set cr1 wrongly
						case 814: // fctid, fctid.
							tmp = B.CreateFPToSI(rfr(frb),t_i64); // invalid: can return undefined, ignores rounding mode, does not set fpscr
							if (rc) cr1_set(tmp);
							wfr(frt,B.CreateBitCast(tmp,t_double));
							break;
						case 15: // fctiwz, fctiwz.
						case 14: // fctiw, fctiw.
							tmp = B.CreateFPToSI(rfr(frb),t_i32); // invalid: can return undefined, ignores rounding mode, does not set fpscr
							if (rc) cr1_set(tmp);
							wfr(frt,B.CreateBitCast(EXTS(tmp),t_double));
							break;
						case 846: // fcfid, fcfid.
							tmp = B.CreateSIToFP(B.CreateBitCast(rfr(frb),t_i64),t_double); // invalid: does not set fpscr
							if (rc) cr1_set(tmp);
							wfr(frt,tmp);
							break;
						// todo: frin, frip, friz, frim (rounding instructions)
						case 72: // fmr, fmr.
							tmp = rfr(frb);
							if (rc) cr1_set(tmp);
							wfr(frt,tmp);
							break;
						case 40: // fneg, fneg.
							tmp = B.CreateBitCast(rfr(frb),t_i64);
							tmp = OR(AND(tmp,MASK(0,62)),AND(NOT(tmp),MASK(63,63)));
							tmp = B.CreateBitCast(tmp,t_double);
							if (rc) cr1_set(tmp);
							wfr(frt,tmp);
							break;
						case 264: // fabs, fabs.
							tmp = B.CreateBitCast(rfr(frb),t_i64);
							tmp = AND(tmp,MASK(0,62));
							tmp = B.CreateBitCast(tmp,t_double);
							if (rc) cr1_set(tmp);
							wfr(frt,tmp);
							break;
						case 136: // fnabs, fnabs.
							tmp = B.CreateBitCast(rfr(frb),t_i64);
							tmp = OR(AND(tmp,MASK(0,62)),MASK(63,63));
							tmp = B.CreateBitCast(tmp,t_double);
							if (rc) cr1_set(tmp);
							wfr(frt,tmp);
							break;
						case 0: // fcmpu
						case 32: // fcmpo
							do_cmpf(rfr(fra),rfr(frb),x_bf); // invalid: does not set fpscr (including fpcc)
							break;
						default:
							found=false;
							break;
					}
					if (found) break;
					found=true;
					switch (a_xo) {
						case 21: // fadd, fadd.
							farith_op2(FADD,fra,frb);
							break;
						case 20: // fsub, fsub.
							farith_op2(FSUB,fra,frb);
							break;
						case 25: // fmul, fmul.
							farith_op2(FMUL,fra,frc);
							break;
						case 18: // fdiv, fdiv.
							farith_op2(FDIV,fra,frb);
							break;
						case 22: // fsqrt, fsqrt.
							farith_op1(FSQRT,frb);
							break;
						case 29: // fmadd, fmadd.
							farith_op3(FMADD,fra,frb,frc);
							break;
						case 28: // fmsub, fmsub.
							farith_op3(FMSUB,fra,frb,frc);
							break;
						case 31: // fnmadd, fnmadd.
							farith_op3(FNMADD,fra,frb,frc);
							break;
						case 30: // fnmsub, fnmsub.
							farith_op3(FNMSUB,fra,frb,frc);
							break;
						case 23: // fsel, fsel.
							k_if(B.CreateFCmpOGE(rfr(fra),ConstantFP::get(t_double,0.0)),[&]() {
								tmp = rfr(frc);
								if (rc) cr1_set(tmp);
								wfr(frt,tmp);
							},[&]() {
								tmp = rfr(frb);
								if (rc) cr1_set(tmp);
								wfr(frt,tmp);
							});
							break;
						default:
							found=false;
							break;
					}
					if (!found) {
						bad_insn();
						stop=true;
					}
					break;
				default:
					bad_insn();
					stop=true;
				}
				ip += 4;
// 				if (!stop) {
// 					BasicBlock*bb = get_bb(ip);
// 					B.CreateBr(bb);
// 					switch_bb(bb);
// 				}
				//if (!stop) B.CreateBr(get_bb(ip));
				//break;
			}
		}
		//F->dump();
		verifyFunction(*F);
		leave_function();
	};
	//Function*I_entry = get_function(main_e.is_prx ? main_e.le_entry : se(*(uint32_t*)main_e.le_entry),"entry").f;
	Function*I_entry = 0;
	if (!main_e.is_prx) {
		I_entry = get_function(se(*(uint32_t*)main_e.le_entry),se(((uint32_t*)main_e.le_entry)[1]),"entry").f;
		I_entry->setLinkage(Function::ExternalLinkage);
	}

	std::map<uint64_t,Function*> opd_map;


	for (size_t i=0;i<got_list.size();i++) {
		uint32_t addr = se(*got_list[i]);
		uint32_t rtoc = se(got_list[i][1]);
 		opd_map[addr] = get_function(addr,rtoc,0).f;
		opd_map[addr]->setLinkage(Function::ExternalLinkage);
	}

	args.clear();
	args.push_back(t_i64); // stack_addr
	args.push_back(t_i64); // entry_addr
	args.push_back(t_i64); // tls_addr
	args.push_back(t_i64); // arg
	args.push_back(t_i64); // arg2
	Function*thread_entry = Function::Create(FunctionType::get(t_i64,args,false),Function::PrivateLinkage,"thread_entry",mod);
	{
		F = thread_entry;
		switch_bb(new_bb());
		context_r = B.CreateAlloca(t_i128,ConstantInt::get(t_i32,r_count));
		alloc_cur_regs();
		auto a = F->getArgumentList().begin();
		
		wgr(12,ci64(0)); // eh
		wr(r_fpscr,ci32(0));

		wgr(1,(Value*)a++); // stack_addr
		Value*e = (Value*)a++; // entry_addr
		wgr(13,(Value*)a++); // tls_addr
		wgr(3,(Value*)a++); // arg
		wgr(4,(Value*)a++); // arg

		wgr(2,EXTZ(RMEM(ADD(e,ci64(4)),4)));
		e = EXTZ(RMEM(e,4));

		do_call_no_longjmp(B.CreateIntToPtr(e,p_FT));
		B.CreateRet(rgr(3));

		verifyFunction(*F);
	}
	args.clear();
	args.push_back(t_i64); // stack_addr
	args.push_back(t_i64); // entry_addr
	args.push_back(t_i64); // tls_addr
	args.push_back(t_i64); // arg
	args.push_back(t_i64); // arg2
	Function*I_thread_entry = Function::Create(FunctionType::get(t_i64,args,false),Function::ExternalLinkage,"I_thread_entry",mod);
	{
		F = I_thread_entry;
		switch_bb(new_bb());

		//BasicBlock*bb = new_bb();
		auto a = F->getArgumentList().begin();
		Value*args[5];
		args[0] = (Value*)a++;
		args[1] = (Value*)a++;
		args[2] = (Value*)a++;
		args[3] = (Value*)a++;
		args[4] = (Value*)a++;
		Value*rv = B.CreateCall(thread_entry,ArrayRef<Value*>(args,args+5));
		//B.CreateInvoke(thread_entry,bb,bb,args,args+4);

		//switch_bb(bb);

		B.CreateRet(rv);
	}


	GlobalVariable*g_argv = new GlobalVariable(t_i64,false,Function::ExternalLinkage,ci64(0),"g_argv",false);
	mod->getGlobalList().push_back(g_argv);
	GlobalVariable*g_envv = new GlobalVariable(t_i64,false,Function::ExternalLinkage,ci64(0),"g_envv",false);
	mod->getGlobalList().push_back(g_envv);


	Function*I_libcall_wrapper = Function::Create(FT,Function::ExternalLinkage,"I_libcall_wrapper",mod);
	{
		F = I_libcall_wrapper;
		switch_bb(new_bb());
		enter_function();
		Value*rtoc = rr(r_gpr+2);
		B.CreateCall(F_libcall_print,rtoc);

		Value*real_addr = EXTZ(RMEM(ADDI(rtoc,8),4));

		wgr(2,EXTZ(RMEM(ADDI(real_addr,4),4)));

		do_tail_call(B.CreateIntToPtr(RMEM(real_addr,4),p_FT));

		leave_function();

		verifyFunction(*F);
	}

	Function*I_unresolved_import = Function::Create(FT,Function::ExternalLinkage,"I_unresolved_import",mod);
	{
		F = I_unresolved_import;
		switch_bb(new_bb());
		enter_function();
		B.CreateCall(F_unresolved_import,rr(r_gpr+2));
		
		do_ret();

		leave_function();

		verifyFunction(*F);
	}

	Function*I_fix_got = Function::Create(FunctionType::get(t_void,false),Function::ExternalLinkage,"I_fix_got",mod);
	{
		F = I_fix_got;
		switch_bb(new_bb());
		for (size_t i=0;i<got_list.size();i++) {
			uint32_t addr = se(*got_list[i]);
			uint32_t rtoc = se(got_list[i][1]);
			Function*f = get_function(addr,rtoc,0).f;
			opd_map[addr] = f;
			opd_map[addr]->setLinkage(Function::ExternalLinkage);

			B.CreateStore(BSWAP32(B.CreatePtrToInt(f,t_i32)),B.CreateBitCast(translate_addr_back((uint64_t)got_list[i]),p_t_i32));
		}
		B.CreateRetVoid();

		verifyFunction(*F);
	}

	args.clear();
	args.push_back(t_i64);   // argc
	args.push_back(p_t_i64); // argv
	args.push_back(t_i64);   // envc
	args.push_back(p_t_i64); // envv
	Function*I_main = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"_main",mod);
	if (!main_e.is_prx) {
		F = I_main;
		switch_bb(new_bb());

		context_r = B.CreateAlloca(t_i128,ConstantInt::get(t_i32,r_count));
		alloc_cur_regs();

		Value*malloc_pagesize = B.CreateAlloca(t_i64);
		Value*main_thread_id = B.CreateAlloca(t_i64);

		B.CreateCall(I_fix_got);
		Value*args[7];
		args[0] = B.CreateBitCast(I_thread_entry,p_t_i8);
		args[1] = main_e.process_params ? B.CreateBitCast(translate_addr_back((uint64_t)main_e.process_params),p_t_i8) : B.CreateBitCast(ci64(0),p_t_i8);;
		args[2] = main_e.process_prx_info ?  B.CreateBitCast(translate_addr_back((uint64_t)main_e.process_prx_info),p_t_i8) : B.CreateBitCast(ci64(0),p_t_i8);
		args[3] = B.CreateBitCast(I_unresolved_import,p_t_i8);
		args[4] = B.CreateBitCast(I_libcall_wrapper,p_t_i8);
		args[5] = malloc_pagesize;
		args[6] = main_thread_id;
		Value*entry = B.CreateCall(F_proc_init,ArrayRef<Value*>(args,args+7));

		wgr(1,B.CreateCall(F_alloc_main_stack));

		wr(r_fpscr,ci32(0));

		wr(r_lr,ci64(0));

		uint32_t*p = (uint32_t*)main_e.le_entry;
		printf(" entry point is %x, rtoc %x\n",(int)se(p[0]),(int)se(p[1]));

		// entry point should be in the got, so let's just confirm it is compiled
		if (!opd_map[se(p[0])]) xcept("entry point not compiled!");

		Value*elf_entry = translate_addr_back((uint64_t)p);

		entry = B.CreateSelect(B.CreateICmpNE(entry,ConstantPointerNull::get(p_t_i8)),B.CreatePtrToInt(entry,t_i64),elf_entry);

		Value*entry_f = RMEM(entry,4);
		Value*entry_rtoc = RMEM(ADDI(entry,4),4);

		wgr(2,EXTZ(entry_rtoc));

		auto a = F->getArgumentList().begin();

		Value*argc = (Value*)a++;
		Value*argv = (Value*)a++;
		Value*envc = (Value*)a++;
		Value*envv = (Value*)a++;

		// Strange order of parameters here.
		// argv and envv MUST be valid and probably null terminated.
		// The entrypoint converts argv and envv from an array of 64-bit pointers to 32-bit pointers IN PLACE
		// (ie. for (int i=0;i<=argc;i++) ((uint32_t*)argv)[i] = (uint32_t)((uint64_t*)argv)[i]; ).
		// Also remember that argv and envv themselves must fit in 32 bits
		wgr(3,argc); // argc
		wgr(4,B.CreatePtrToInt(argv,t_i64)); // argv
		wgr(5,B.CreatePtrToInt(envv,t_i64)); // envv
		wgr(6,envc); // envc

		// The following arguments are passed to sys_initialize_tls
		wgr(7,B.CreateLoad(main_thread_id)); // main thread id
		// These addresses are RVAs, but main_e is loaded at 0, so it's fine like this
		// (should probably change them to VAs, for consistency)
		wgr(8,ci64(main_e.tls_vaddr)); // initialized tls address
		wgr(9,ci64(main_e.tls_filesz)); // initialized tls size
		wgr(10,ci64(main_e.tls_memsz)); // total tls size

		// liblv2 only restores r3-r6 and r12 before calling the entry point, but i guess it's okay
		// since tls is already initialized at that point...

		wgr(11,elf_entry); // entry point address (only used if entry point is liblv2)

		wgr(12,B.CreateLoad(malloc_pagesize)); // dlmalloc default allocation granularity (mparams.granularity)

		wgr(13,ci64(0)); // unknown, only used if entry point is liblv2, stored in some global

		do_call_no_longjmp(B.CreateIntToPtr(entry_f,p_FT));

		B.CreateRetVoid();

		verifyFunction(*F);
	}	
	if (!main_e.is_prx) {
		Function*I_start = Function::Create(FunctionType::get(t_void,false),Function::ExternalLinkage,"start",mod);
		{
			F = I_start;
			switch_bb(new_bb());

			B.CreateCall(F_entry,B.CreateBitCast(I_main,p_t_i8));
			// never returns

			B.CreateRetVoid();

			verifyFunction(*F);
		}
	} else {
		auto&m = main_e.reloc_map;
		StructType*t_reloc = StructType::get(t_i64,t_i64,t_i64,t_i64,0);
		ArrayType*a_t_reloc = ArrayType::get(t_reloc,m.size()+1);
		std::vector<Constant*> v_reloc;
		std::vector<Constant*> tmp;
		for (auto i=m.begin();i!=m.end();++i) {
			uint64_t P = i->first - main_e.reloc_base;
			auto&r = i->second;
			int type = r.type;
			uint64_t S = r.S;
			uint64_t A = r.A;
			tmp.clear();
			tmp.push_back(ci64(P));
			tmp.push_back(ci64(type));
			tmp.push_back(ci64(S));
			tmp.push_back(ci64(A));
			v_reloc.push_back(ConstantStruct::get(t_reloc,tmp));
		}
		tmp.clear();
		tmp.push_back(ci64(0));
		tmp.push_back(ci64(0));
		tmp.push_back(ci64(0));
		tmp.push_back(ci64(0));
		v_reloc.push_back(ConstantStruct::get(t_reloc,tmp));

		GlobalVariable*g_reloc = new GlobalVariable(a_t_reloc,false,Function::ExternalLinkage,ConstantArray::get(a_t_reloc,v_reloc),"g_reloc",false);
		mod->getGlobalList().push_back(g_reloc);

		ArrayType*a_t_i64 = ArrayType::get(t_i64,g_image_section_list.size()*2+2);
		std::vector<Constant*> v_section_list;
		for (size_t i=0;i<g_image_section_list.size();i++) {
			auto&t = g_image_section_list[i];
			v_section_list.push_back(ci64(t.first));
			v_section_list.push_back(ci64(t.second));
		}
		v_section_list.push_back(ci64(0));
		v_section_list.push_back(ci64(0));

		GlobalVariable*g_section_list = new GlobalVariable(a_t_i64,false,Function::ExternalLinkage,ConstantArray::get(a_t_i64,v_section_list),"g_section_list",false);
		mod->getGlobalList().push_back(g_section_list);

		args.clear();
		args.push_back(p_t_i8); // hdllhandle
		args.push_back(t_i32); // dwreason
		args.push_back(p_t_i8); // lpreserved
		Function*I_dll_main = Function::Create(FunctionType::get(t_i64,args,false),Function::ExternalLinkage,"DllMain",mod);
		I_dll_main->setCallingConv(CallingConv::X86_StdCall);
		{
			F = I_dll_main;
			switch_bb(new_bb());

			auto a = F->getArgumentList().begin();
			Value*args[8];
			args[0] = (Value*)a++;
			args[1] = (Value*)a++;
			args[2] = (Value*)a++;
			args[3] = B.CreateBitCast(g_image,p_t_i8);
			args[4] = B.CreateBitCast(g_section_list,p_t_i8);
			args[5] = B.CreateBitCast(g_reloc,p_t_i8);
			args[6] = B.CreateBitCast(translate_addr_back((uint64_t)main_e.prx_mod_info),p_t_i8);
			args[7] = B.CreateBitCast(I_fix_got,p_t_i8);
			B.CreateRet(B.CreateCall(F_dll_main,ArrayRef<Value*>(args,args+8)));

			verifyFunction(*F);
		}
	}

	auto do_functions = [&]() {
		printf("compiling...\n");
		insn_count = 0;
		while (!func_stack.empty()) {
			function_info&fi = *func_stack.top();
			func_stack.pop();

			do_function(fi);
		}
		printf("%lld instructions processed, %lldMB\n",(long long)insn_count,(long long)(insn_count*4/1024/1024));
	};
	do_functions();

	//mod->dump();

	verifyModule(*mod);

	//JITExceptionHandling = true;
	
	FunctionPassManager FPM(mod);
	PassManager MPM;

	PassManagerBuilder pm_builder;
	pm_builder.OptLevel = 3;

	pm_builder.populateFunctionPassManager(FPM);
	pm_builder.populateModulePassManager(MPM);


	//FPM.add(createBasicAliasAnalysisPass());
// 	FPM.add(createPromoteMemoryToRegisterPass());
// 	FPM.add(createInstructionCombiningPass());
// 	FPM.add(createReassociatePass());
// 	FPM.add(createGVNPass());
// 	FPM.add(createCFGSimplificationPass());
// 
// 	FPM.add(createScalarReplAggregatesPass());
// 	FPM.add(createConstantPropagationPass());	
// 
// 	FPM.add(createDeadInstEliminationPass());
// 	FPM.add(createDeadStoreEliminationPass());
// 	FPM.add(createAggressiveDCEPass());
// 	FPM.add(createDeadCodeEliminationPass());
// 	FPM.add(createLICMPass());
// 	FPM.add(createBlockPlacementPass());
// 	FPM.add(createLCSSAPass());
// 	//FPM.add(createEarlyCSEPass());
// 	FPM.add(createGVNPass());
// 	FPM.add(createMemCpyOptPass());
// 	FPM.add(createLoopDeletionPass());
// 	FPM.add(createSimplifyLibCallsPass());
// 	FPM.add(createCorrelatedValuePropagationPass());
	//FPM.add(createInstructionSimplifierPass());

	//MPM.add(createInternalizePass(true));
	// 
	// 		FPM.add(createPartialInliningPass());
	// 		//FPM.add(createFunctionInliningPass());
	//FPM.add(createConstantMergePass());

	//MPM.add(createFunctionInliningPass());
	//MPM.add(createPartialInliningPass());

	//MPM.add(createScalarReplAggregatesPass());

	{
		printf("optimizing...\n");
		bool any_changes = true;
		int passes = 0;
		FPM.doInitialization();
		for (passes=0;any_changes && passes<1;passes++) {
			any_changes = false;
			for (auto i=mod->getFunctionList().begin();i!=mod->getFunctionList().end();++i) {
				Function*f = &*i;
				any_changes |= FPM.run(*f);
			}
			any_changes |= MPM.run(*mod);
		}
		FPM.doFinalization();
		//mod->dump();
		//I_entry->dump();

		printf("%d optimization passes\n",passes);

		{
			printf("dumping...\n");
			std::string errstr;
			raw_fd_ostream fd("out.s",errstr);
			if (errstr.size()) printf("failed to open: %s\n",errstr.c_str());
			mod->print(fd,0);
			printf("dumped to out.s\n");
		}

		{
			printf("generating code...\n");
			InitializeAllTargets();
			InitializeAllAsmPrinters();
			InitializeAllAsmParsers();

// 			Triple TheTriple(mod->getTargetTriple());
// 			if (TheTriple.getTriple().empty())
// 				TheTriple.setTriple(sys::getHostTriple());
// 			TheTriple.setOS(Triple::OSType::UnknownOS);

			// Use unknown os to get elf output. LLVM won't output exception info otherwise
			//Triple TheTriple("x86_64-pc-unknown");

			Triple TheTriple("x86_64-pc-win32");

			std::string err;
			const Target *TheTarget = TargetRegistry::lookupTarget(TheTriple.getTriple(), err);
			if (TheTarget == 0) {
				xcept("lookupTarget failed: %s",err.c_str());
			}
			
			std::string s;

			{
				raw_string_ostream os(s);
				formatted_raw_ostream FOS(os);

				std::auto_ptr<TargetMachine> 
					target(TheTarget->createTargetMachine(TheTriple.getTriple(),"",""));
				if (!target.get()) xcept("failed to allocate target machine");
				TargetMachine &Target = *target.get();
				
				PassManager PM;
				if (const TargetData *TD = Target.getTargetData())
					PM.add(new TargetData(*TD));
				else
					PM.add(new TargetData(mod));
				if (Target.addPassesToEmitFile(PM, FOS,TargetMachine::CGFT_ObjectFile,CodeGenOpt::Default)) xcept("addPassesToEmitFile failed");

				PM.run(*mod);

			}

			printf("emitted object file, %d bytes\n",(int)s.size());

// 			{
// 				printf("dumping...\n");
// 				FILE*f = fopen(out_fn.c_str(),"wb");
// 				if (!f) xcept("failed to open %s",out_fn.c_str());
// 				if (fwrite(s.c_str(),s.size(),1,f)!=1) xcept("failed to write to %s",out_fn.c_str());
// 				fclose(f);
// 				printf("dumped to %s\n",out_fn.c_str());
// 			}
			std::string s_data;
			out.no_reloc = !main_e.is_prx;
			out.make_obj_to_str(s_data);

			linker ld;
			
			ld.load_obj(s_data.c_str(),s_data.size(),out_data_fn.c_str());
			ld.load_obj(s.c_str(),s.size(),out_fn.c_str());;

			ld.out.is_dll = main_e.is_prx;
			ld.out.no_reloc = !main_e.is_prx;
			ld.entry_symbol = main_e.is_prx ? "DllMain@20" : "start";

			size_t p = out_fn.rfind(".");
			if (p!=std::string::npos) {
				std::string ext = out_fn.substr(p+1);
				ld.out_fn = out_fn.substr(0,p) + (main_e.is_prx?".dll":".exe");
			};

			ld.link();

			return 0;
		}
	}

// 	} catch (const char*str) {
// 		fprintf(stdout,"[exception] %s\n",str);
// 		return 1;
	} catch (const std::exception&e) {
		fprintf(stdout,"[std::exception] %s\n",e.what());
		return 1;
	}

	return 0;
}
