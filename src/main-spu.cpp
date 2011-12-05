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
//#include "win32_coff_object_writer.h"

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
//#include "common.h"
//#include "elf.h"
//#include "pe.h"
//#include "ar.h"
//#include "ld.h"

//#include "md5.h"

struct function_info {
	uint64_t addr;
	Function*f;
	void*compiled_function;
};

enum {
	r_count = 128,
};

struct spuc {
	bool debug_output ;
	bool debug_superverbose;
	bool debug_print_calls;
	bool debug_tests;
	char*image_data;
	void*fiber,*ret_fiber;
	uint32_t compile_offset;
	void*compiled_function;
	enum {req_compile,req_exit,req_sync};
	int request;
	bool is_raw_spu;
	std::map<uint64_t,function_info*> func_map;
	spuc(void*ls,bool is_raw_spu) : is_raw_spu(is_raw_spu) {
		image_data = (char*)ls;
		debug_output = false;
		debug_superverbose = true;
		debug_print_calls = true;
		debug_tests = true;
		init();
	}
	~spuc() {
		uninit();
	}
	void do_request(int req);
	void init();
	void uninit();
	void*compile(uint32_t function_offset);
	void sync();
	void(*call_lr)();
	void(*set_args)(uint64_t,uint64_t,uint64_t,uint64_t);
};

typedef void*spuc_handle;
__declspec(dllexport) spuc_handle spuc_open(void*ls,bool is_raw_spu) {
	return new spuc(ls,is_raw_spu);
}
__declspec(dllexport) void spuc_close(spuc_handle h) {
	spuc*s = (spuc*)h;
	delete s;
}
__declspec(dllexport) void spuc_reset(spuc_handle h) {
	spuc*s = (spuc*)h;
}
__declspec(dllexport) void*spuc_get_function(spuc_handle h,uint32_t offset) {
	spuc*s = (spuc*)h;
	return s->compile(offset);
}
__declspec(dllexport) void spuc_call(spuc_handle h,void*f) {
	spuc*s = (spuc*)h;
	((void(*)())f)();
	while (true) {
		s->call_lr();
	}
}
__declspec(dllexport) void spuc_sync(spuc_handle h) {
	spuc*s = (spuc*)h;
	s->sync();
}
__declspec(dllexport) void spuc_set_args(spuc_handle h,uint64_t arg1,uint64_t arg2,uint64_t arg3,uint64_t arg4) {
	spuc*s = (spuc*)h;
	s->set_args(arg1,arg2,arg3,arg4);
}

struct initer {
	initer() {
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
	}
} initer;
void spuc::do_request(int req) {
	request = req;
	ret_fiber = ConvertThreadToFiber(0);
	SwitchToFiber(fiber);
	ConvertFiberToThread();
}
void CALLBACK fiber_entry(void*param);
void spuc::init() {
	fiber = CreateFiber(0,&fiber_entry,this);
	do_request(0);
}
void spuc::uninit() {
	do_request(req_exit);
}
void*spuc::compile(uint32_t function_offset) {
	compile_offset = function_offset;
	auto i = func_map.find(function_offset);
	if (i!=func_map.end() && i->second->compiled_function) {
		printf("%#x already compiled at %p\n",(int)function_offset,i->second->compiled_function);
		return i->second->compiled_function;
	}
	do_request(req_compile);
	printf("compiled to %p\n",compiled_function);
	return compiled_function;
}
void spuc::sync() {
	do_request(req_sync);
}
void compile_func(spuc&s);
void CALLBACK fiber_entry(void*param) {
	spuc&s = *(spuc*)param;
	compile_func(s);
	SwitchToFiber(s.ret_fiber);
	xcept("unreachable: final SwitchToFiber returned");
}
void compile_func(spuc&s) {

	bool debug_output = s.debug_output;
	bool debug_superverbose = s.debug_superverbose;
	bool debug_print_calls = s.debug_print_calls;
	bool debug_tests = s.debug_tests;
	char*image_data = s.image_data;
	bool is_raw_spu = s.is_raw_spu;

	bool enable_ret_opt = true;

	LLVMContext context;
	Module*mod = new Module("spu module",context);

	uint64_t xx_ip = 0;

	auto read_32 = [&](uint64_t addr) -> uint32_t {
		return se(*(uint32_t*)&image_data[addr]);
	};

	IRBuilder<> B(context);

	Type*t_void = Type::getVoidTy(context);
	IntegerType*t_i1 = Type::getInt1Ty(context);
	IntegerType*t_i4 = Type::getIntNTy(context,4);
	IntegerType*t_i5 = Type::getIntNTy(context,5);
	IntegerType*t_i6 = Type::getIntNTy(context,6);
	IntegerType*t_i7 = Type::getIntNTy(context,7);
	IntegerType*t_i8 = Type::getInt8Ty(context);
	IntegerType*t_i10 = Type::getIntNTy(context,10);
	IntegerType*t_i14 = Type::getIntNTy(context,14);
	IntegerType*t_i16 = Type::getInt16Ty(context);
	IntegerType*t_i18 = Type::getIntNTy(context,18);
	IntegerType*t_i24 = Type::getIntNTy(context,24);
	IntegerType*t_i32 = Type::getInt32Ty(context);
	IntegerType*t_i33 = Type::getIntNTy(context,33);
	IntegerType*t_i64 = Type::getInt64Ty(context);
	IntegerType*t_i128 = Type::getIntNTy(context,128);
	IntegerType*t_i256 = Type::getIntNTy(context,256);
	IntegerType*t_i1024 = Type::getIntNTy(context,1024);

	Type*t_double = Type::getDoubleTy(context);
	Type*t_float = Type::getFloatTy(context);

	PointerType*p_t_i8 = Type::getInt8PtrTy(context);
	PointerType*p_t_i16 = Type::getInt16PtrTy(context);
	PointerType*p_t_i32 = Type::getInt32PtrTy(context);
	PointerType*p_t_i64 = Type::getInt64PtrTy(context);
	PointerType*p_t_i128 = Type::getIntNPtrTy(context,128);
	PointerType*p_t_i1024 = Type::getIntNPtrTy(context,1024);

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
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	Function*F_syscall = Function::Create(FunctionType::get(t_i64,args,false),Function::ExternalLinkage,"F_syscall",mod);

	args.clear();
	args.push_back(p_t_i8);
	args.push_back(p_t_i8);
	args.push_back(p_t_i8);
	args.push_back(p_t_i8);
	args.push_back(p_t_i8);
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
	Function*F_spu_dll_main = Function::Create(FunctionType::get(t_i64,args,false),Function::ExternalLinkage,"F_spu_dll_main",mod);

	args.clear();
	args.push_back(t_i64);
	Function*F_unresolved_import = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_unresolved_import",mod);
	args.clear();
	args.push_back(t_i64);
	Function*F_libcall_print = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_libcall_print",mod);

	args.clear();
	args.push_back(t_i32);
	Function*F_spu_halt = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_spu_halt",mod);

	args.clear();
	args.push_back(t_i32);
	args.push_back(t_i32);
	Function*F_spu_stop = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_spu_stop",mod);
	
	args.clear();
	args.push_back(t_i32);
	Function*F_spu_restart = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_spu_restart",mod);

	args.clear();
	args.push_back(t_i32);
	Function*F_spu_get_function = Function::Create(FunctionType::get(p_t_i8,args,false),Function::ExternalLinkage,"F_spu_get_function",mod);

	args.clear();
	args.push_back(t_i32);
	Function*F_spu_rdch = Function::Create(FunctionType::get(t_i32,args,false),Function::ExternalLinkage,"F_spu_rdch",mod);
	Function*F_spu_rdch_count = Function::Create(FunctionType::get(t_i32,args,false),Function::ExternalLinkage,"F_spu_rdch_count",mod);
	Function*F_spu_wrch_count = Function::Create(FunctionType::get(t_i32,args,false),Function::ExternalLinkage,"F_spu_wrch_count",mod);
	args.push_back(t_i32);
	Function*F_spu_wrch = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_spu_wrch",mod);

	args.clear();
	args.push_back(t_i32);
	Function*F_spu_x_get_ls_addr = Function::Create(FunctionType::get(t_i64,args,false),Function::ExternalLinkage,"F_spu_x_get_ls_addr",mod);

	args.clear();
	args.push_back(t_i32);
	args.push_back(t_i32);
	args.push_back(t_i32);
	Function*F_spu_x_write_snr = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_spu_x_write_snr",mod);

	args.clear();
	args.push_back(t_i32);
	Function*F_spu_wr_out_intr_mbox= Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"F_spu_wr_out_intr_mbox",mod);

	// 	args.clear();
	// 	args.push_back(p_t_i8);
	// 	args.push_back(p_t_i8);
	// 	args.push_back(t_i64);
	// 	args.push_back(t_i32);
	// 	args.push_back(t_i1);
	// 	Function*F_llvm_memmove = Function::Create(FunctionType::get(Type::getVoidTy(context),args,false),Function::ExternalLinkage,"llvm.memmove.p0i8.p0i8.i64",mod);

// 	args.clear();
// 	args.push_back(p_t_i8);
// 	args.push_back(p_t_i8);
// 	args.push_back(t_i64);
// 	args.push_back(t_i32);
// 	args.push_back(t_i1);
// 	Function*F_llvm_memcpy = Function::Create(FunctionType::get(Type::getVoidTy(context),args,false),Function::ExternalLinkage,"llvm.memcpy.p0i8.p0i8.i64",mod);

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
	tys.push_back(p_t_i8);
	tys.push_back(p_t_i8);
	tys.push_back(t_i64);
	Function*F_llvm_memcpy = Intrinsic::getDeclaration(mod,Intrinsic::memcpy,tys);
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
	tys.push_back(t_i8);
	Function*F_llvm_ctpop8 = Intrinsic::getDeclaration(mod,Intrinsic::ctpop,tys);

	tys.clear();
	tys.push_back(t_float);
	Function*F_llvm_sqrt32 = Intrinsic::getDeclaration(mod,Intrinsic::sqrt,tys);
	tys.clear();
	tys.push_back(t_double);
	Function*F_llvm_sqrt64 = Intrinsic::getDeclaration(mod,Intrinsic::sqrt,tys);


	args.clear();

	FunctionType*FT = FunctionType::get(t_void,args,false);
	PointerType*p_FT = PointerType::getUnqual(FT);


	// 	GlobalVariable*g_ip = new GlobalVariable(t_i64,false,GlobalVariable::ExternalLinkage,UndefValue::get(t_i64),"g_ip",false);
	// 	mod->getGlobalList().push_back(g_ip);

	// 	GlobalVariable*g_debug_write_addr = new GlobalVariable(t_i64,false,GlobalVariable::ExternalLinkage,ConstantInt::get(t_i64,0),"g_debug_write_addr",false);
	// 	mod->getGlobalList().push_back(g_debug_write_addr);

	const unsigned int ls_size = 1024*256;
	
	GlobalVariable*g_ls = 0;
	GlobalVariable*g_context = 0;
	GlobalVariable*g_channel = 0;

	auto r_name = [&](int n) -> std::string {
		if (n<r_count) return format("r[%d]",n);
		else return "invalid register";
	};

	Function*F = 0;
	BasicBlock*BB = 0;
	BasicBlock*BB_entry = 0;
	auto switch_bb = [&](BasicBlock*new_bb) -> BasicBlock* {
		B.SetInsertPoint(new_bb);
		BB = new_bb;
		return new_bb;
	};
	Value*ls = 0;
	Value*context_r = 0;
	Value*channel_r = 0;
	Value*cur_regs[r_count];
	auto alloc_cur_regs = [&]() {
		for (int i=0;i<r_count;i++) {
			cur_regs[i] = B.CreateConstGEP1_32(context_r,i,r_name(i));
		}
	};

	auto enter_function = [&]() {
		auto a = F->getArgumentList().begin();
		ls = B.CreateBitCast(g_ls,p_t_i8);
		context_r = B.CreateBitCast(g_context,p_t_i128);
		channel_r = B.CreateBitCast(g_channel,p_t_i128);
		alloc_cur_regs();
	};
	auto leave_function = [&]() {

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

	std::function<Value*(Value*,Value*)> AND,OR,LSHR,ASHR,SHL,MUL,ADD,SUB;
	std::function<Value*(Value*,uint64_t)> ANDI,ORI,LSHRI,ASHRI,SHLI,MULI,ADDI,SUBI;
	std::function<Value*(Value*,int)> TRUNC;
	std::function<Value*(Value*)> NOT;

	auto get_r = [&](int n) -> Value* {
		return cur_regs[n];
	};

	auto rr = [&](int n) -> Value* {
		return B.CreateLoad(get_r(n));
	};
	auto wr = [&](int n,Value*v) {
		B.CreateStore(v,get_r(n));
	};
	auto rr_sub = [&](int n,Value*subn,int bytes) -> Value* {
		Value*ov = rr(n);
		return TRUNC(LSHR(ov,SUB(ConstantInt::get(ov->getType(),ov->getType()->getPrimitiveSizeInBits()-bytes*8),MULI(subn,bytes*8))),bytes);
	};
	auto wr_sub = [&](int n,Value*v,Value*subn,int bytes) {
		uint64_t b_m;
		if (bytes==1) b_m=0xff;
		else if (bytes==2) b_m=0xffff;
		else if (bytes==4) b_m=0xffffffff;
		else xcept("wr_sub: bad bytes %d",bytes);
		Value*ov = rr(n);
		Value*shift_n = SUB(ConstantInt::get(ov->getType(),ov->getType()->getPrimitiveSizeInBits()-bytes*8),MULI(subn,bytes*8));
		Value*m = SHL(ConstantInt::get(ov->getType(),b_m),shift_n);
		wr(n,OR(AND(ov,NOT(m)),SHL(B.CreateZExt(v,ov->getType()),shift_n)));
	};

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

	auto CAST32 = [&](Value*v) -> Value* {
		return B.CreateBitCast(v,t_i32);
	};
	auto CAST64 = [&](Value*v) -> Value* {
		return B.CreateBitCast(v,t_i64);
	};
	auto CASTFP = [&](Value*v) -> Value* {
		return B.CreateBitCast(v,t_float);
	};
	auto CASTDFP = [&](Value*v) -> Value* {
		return B.CreateBitCast(v,t_double);
	};

	auto rrf_subi = [&](int n,int subn) -> Value* {
		return CASTFP(rr_subi(n,subn,4));
	};
	auto wrf_subi = [&](int n,Value*v,int subn) {
		wr_subi(n,CAST32(v),subn,4);
	};
	auto rrdf_subi = [&](int n,int subn) -> Value* {
		return CASTDFP(rr_subi(n,subn,8));
	};
	auto wrdf_subi = [&](int n,Value*v,int subn) {
		wr_subi(n,CAST64(v),subn,8);
	};

	auto ci1 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i1,v);
	};
	auto ci7 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i7,v);
	};
	auto ci8 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i8,v);
	};
	auto ci10 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i10,v);
	};
	auto ci14 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i14,v);
	};
	auto ci16 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i16,v);
	};
	auto ci18 = [&](uint64_t v) -> ConstantInt* {
		return ConstantInt::get(t_i18,v);
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

	uint32_t lslr_value = 0x0003FFFF; // 256k local storage

	auto do_call = [&](Value*v,int retaddr_reg) {
		//B.CreateCall(F_dump_addr,B.CreateLoad(B.CreateIntToPtr(ConstantInt::get(t_i64,0x10011380+0x10),p_t_i64)));
		if (debug_output&&debug_print_calls) B.CreateCall(F_stack_call,ConstantInt::get(t_i64,xx_ip));
		//if (debug_output&&debug_superverbose) B.CreateCall(F_stack_call,B.CreatePtrToInt(v,t_i64));

		wr_subi(retaddr_reg,ci32((xx_ip+4)&lslr_value&-4),0,4);
		wr_subi(retaddr_reg,ci32(0),1,4);
		wr_subi(retaddr_reg,ci32(0),2,4);
		wr_subi(retaddr_reg,ci32(0),3,4);

		B.CreateCall(v);
		if (debug_output&&debug_print_calls) B.CreateCall(F_stack_ret,ConstantInt::get(t_i64,xx_ip+4));
	};
	auto do_ret = [&]() {
		B.CreateRetVoid();
	};
	auto do_tail_call = [&](Value*v) {
		if (debug_output&&debug_print_calls) B.CreateCall(F_stack_call,ConstantInt::get(t_i64,xx_ip));
		CallInst*c = B.CreateCall(v);
		c->setTailCall();
		B.CreateRetVoid();
	};

	auto EXTZ16 = [&](Value*v) -> Value* {
		return B.CreateZExtOrBitCast(v,t_i16);
	};
	auto EXTZ32 = [&](Value*v) -> Value* {
		return B.CreateZExtOrBitCast(v,t_i32);
	};
	auto EXTZ33 = [&](Value*v) -> Value* {
		return B.CreateZExtOrBitCast(v,t_i33);
	};
	auto EXTZ64 = [&](Value*v) -> Value* {
		return B.CreateZExtOrBitCast(v,t_i64);
	};
	auto EXTZ128 = [&](Value*v) -> Value* {
		return B.CreateZExtOrBitCast(v,t_i128);
	};
	auto EXTZ256 = [&](Value*v) -> Value* {
		return B.CreateZExtOrBitCast(v,t_i256);
	};
	auto EXTS16 = [&](Value*v) -> Value* {
		return B.CreateSExtOrBitCast(v,t_i16);
	};
	auto EXTS32 = [&](Value*v) -> Value* {
		return B.CreateSExtOrBitCast(v,t_i32);
	};
	auto EXTS64 = [&](Value*v) -> Value* {
		return B.CreateSExtOrBitCast(v,t_i64);
	};
	auto EXTS128 = [&](Value*v) -> Value* {
		return B.CreateSExtOrBitCast(v,t_i128);
	};

	auto check_safe_shift = [&](Value*a,Value*b) {
		if (!debug_tests) return;
		int bits = a->getType()->getPrimitiveSizeInBits();
		k_if(B.CreateICmpUGE(b,ConstantInt::get(b->getType(),bits)),[&]() {
			mkerr("bad shift");
		},[&](){});
	};

	auto SHLZ = [&](Value*a,Value*b) -> Value* {
		if (a->getType()!=b->getType()) xcept("SHLZ with mismatching types (ip %llx)",(long long)xx_ip);
		return B.CreateSelect(B.CreateICmpUGE(b,ConstantInt::get(b->getType(),a->getType()->getPrimitiveSizeInBits())),ConstantInt::get(a->getType(),0),SHL(a,b));
	};
	auto LSHRZ = [&](Value*a,Value*b) -> Value* {
		if (a->getType()!=b->getType()) xcept("LSHRZ with mismatching types (ip %llx)",(long long)xx_ip);
		return B.CreateSelect(B.CreateICmpUGE(b,ConstantInt::get(b->getType(),a->getType()->getPrimitiveSizeInBits())),ConstantInt::get(a->getType(),0),LSHR(a,b));
	};
	auto ASHRZ = [&](Value*a,Value*b) -> Value* {
		if (a->getType()!=b->getType()) xcept("LSHRZ with mismatching types (ip %llx)",(long long)xx_ip);
		return B.CreateSelect(B.CreateICmpUGE(b,ConstantInt::get(b->getType(),a->getType()->getPrimitiveSizeInBits())),
			ASHRI(a,a->getType()->getPrimitiveSizeInBits()-1),ASHR(a,b));
	};

	auto ROTL = [&](Value*a,Value*b) -> Value* {
		if (a->getType()!=b->getType()) xcept("ROTL with mismatching types (ip %llx)",(long long)xx_ip);
		return B.CreateSelect(B.CreateICmpEQ(b,ConstantInt::get(b->getType(),0)),a,OR(SHL(a,b),B.CreateLShr(a,SUB(ConstantInt::get(b->getType(),a->getType()->getPrimitiveSizeInBits()),b))));
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
	SHLI = [&](Value*v,uint64_t n) -> Value* {
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
		xcept("TRUNC bad n %d",n);
		return 0;
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

	SUB = [&](Value*a,Value*b) -> Value* {
		return B.CreateSub(a,b);
	};
	SUBI = [&](Value*v,uint64_t n) -> Value* {
		return SUB(v,ConstantInt::get(v->getType(),n));
	};
	ADD = [&](Value*a,Value*b) -> Value* {
		return B.CreateAdd(a,b);
	};
	ADDI = [&](Value*v,uint64_t n) -> Value* {
		return ADD(v,ConstantInt::get(v->getType(),n));
	};

	auto NEG = [&](Value*a) -> Value* {
		return B.CreateAdd(B.CreateNot(a),ConstantInt::get(a->getType(),1));
	};

	MUL = [&](Value*a,Value*b) -> Value* {
		return B.CreateMul(a,b);
	};
	MULI = [&](Value*a,uint64_t n) -> Value* {
		return B.CreateMul(a,ConstantInt::get(a->getType(),n));
	};

	auto MPY = [&](Value*a,Value*b) -> Value* { // mpy is 16-bit multiply with 32-bit result
		return MUL(EXTS32(TRUNC(a,2)),EXTS32(TRUNC(b,2)));
	};
	auto MPYU = [&](Value*a,Value*b) -> Value* {
		return MUL(EXTZ32(TRUNC(a,2)),EXTZ32(TRUNC(b,2)));
	};
	auto MPYA = [&](Value*a,Value*b,Value*c) -> Value* {
		return ADD(MUL(EXTS32(TRUNC(a,2)),EXTS32(TRUNC(b,2))),c);
	};
	auto MPYH = [&](Value*a,Value*b) -> Value* { 
		return SHLI(MUL(LSHRI(a,16),EXTZ32(TRUNC(b,2))),16);
	};
	auto MPYS = [&](Value*a,Value*b) -> Value* {
		return ASHRI(MUL(EXTS32(TRUNC(a,2)),EXTS32(TRUNC(b,2))),16);
	};
	auto MPYHH = [&](Value*a,Value*b) -> Value* {
		return MUL(ASHRI(a,16),ASHRI(b,16));
	};
	auto MPYHHA = [&](Value*a,Value*b,Value*c) -> Value* {
		return ADD(MUL(ASHRI(a,16),ASHRI(b,16)),c);
	};
	auto MPYHHU = [&](Value*a,Value*b) -> Value* {
		return MUL(LSHRI(a,16),LSHRI(b,16));
	};
	auto MPYHHAU = [&](Value*a,Value*b,Value*c) -> Value* {
		return ADD(MUL(LSHRI(a,16),LSHRI(b,16)),c);
	};

	auto SUBF = [&](Value*a,Value*b) -> Value* { // note: subf is b - a, not a - b (implemented as ~a + b + 1)
		return B.CreateAdd(B.CreateAdd(B.CreateNot(a),b),ConstantInt::get(a->getType(),1));
	};
	auto NOTADD = [&](Value*a,Value*b) -> Value* { // used by sfx (~a + b)
		return B.CreateAdd(B.CreateNot(a),b);
	};

	auto CEQ = [&](Value*a,Value*b) -> Value* {
		return B.CreateSExtOrBitCast(B.CreateICmpEQ(a,b),a->getType());
	};
	auto CGT = [&](Value*a,Value*b) -> Value* {
		return B.CreateSExtOrBitCast(B.CreateICmpSGT(a,b),a->getType());
	};
	auto CLGT = [&](Value*a,Value*b) -> Value* {
		return B.CreateSExtOrBitCast(B.CreateICmpUGT(a,b),a->getType());
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
		if (shift<0) while(true);
		if (shift<0) xcept("bad getbits");
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

		Value*exp = ci8(31+127);
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
		Value*xbit = B.CreateICmpNE(getbits(frac,25,31),ci7(0));
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
		// 		B.CreateCall(F_dump_addr,rgr(2));
// 		if (debug_tests) {
// 			k_if(B.CreateICmpEQ(ea,ConstantInt::get(ea->getType(),0)),[&]() {
// 				mkerr("attempt to read from null address");
// 			},[&](){});
// 		}
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
// 		if (debug_output&&debug_superverbose) {
// 			if (n<16) B.CreateCall(F_dump_addr,EXTZ(v));
// 		}
		return v;
	};
	auto WMEM = [&](Value*ea,int n,Value*v) -> Value* {
// 		if (debug_tests) {
// 			k_if(B.CreateICmpEQ(ea,ConstantInt::get(ea->getType(),0)),[&]() {
// 				mkerr("attempt to write to null address");
// 			},[&](){});
// 		}
// 		if (debug_output&&debug_superverbose) {
// 			B.CreateCall(F_dump_addr,ea);
// 			if (n<16) B.CreateCall(F_dump_addr,EXTZ(v));
// 		}
		// 		k_if(B.CreateICmpEQ(ea,B.CreateLoad(g_debug_write_addr)),[&]() {
		// 			mkdumpregs();
		// 			B.CreateCall(F_dump_addr,ea);
		// 			mkerr("!!WRITE!!");
		// 		},[&](){});
		Value*r=0;
		//Value*iv;
		//if (use_context_r) iv = B.CreateCall2(F_llvm_invariant_start,ci64(-1),B.CreateBitCast(context_r,p_t_i8));
		if (n==1) r=B.CreateStore(v,B.CreateIntToPtr(ea,p_t_i8));
		if (n==2) r=B.CreateStore(BSWAP16(v),B.CreateIntToPtr(ea,p_t_i16));
		if (n==4) r=B.CreateStore(BSWAP32(v),B.CreateIntToPtr(ea,p_t_i32));
		if (n==8) r=B.CreateStore(BSWAP64(v),B.CreateIntToPtr(ea,p_t_i64));
		if (n==16) r=B.CreateStore(BSWAP128(v),B.CreateIntToPtr(ea,p_t_i128));
		if (!r) xcept("WMEM bad n %d",n);
		//if (use_context_r) B.CreateCall3(F_llvm_invariant_end,iv,ci64(-1),B.CreateBitCast(context_r,p_t_i8));
		return r;
	};

	auto lslr = [&]() -> Value* {
		return ci32(lslr_value);
	};
	auto get_ls = [&](Value*ea) {
		return B.CreatePtrToInt(B.CreateGEP(ls,EXTZ64(AND(ea,lslr()))),t_i64);
	};
	auto LSA = [&](Value*ea) -> Value* {
		return B.CreatePtrToInt(B.CreateGEP(ls,EXTZ64(ANDI(AND(ea,lslr()),0xFFFFFFF0))),t_i64);
	};

	std::map<int,unsigned int> channel_index_map;
	int channel_r_size = 0;

	auto addchd = [&](int ca,int size) {
		channel_index_map[ca] = channel_r_size;
		channel_r_size += size+1;
	};
	auto getchd = [&](int ca) -> Value* {
		return B.CreateConstGEP1_32(channel_r,channel_index_map[ca]+1);
	};
	auto getchdt = [&](int ca,Type*t) -> Value* {
		return B.CreateBitCast(B.CreateConstGEP1_32(channel_r,channel_index_map[ca]+1),t->getPointerTo());
	};
	auto getchc = [&](int ca) -> Value* {
		return B.CreateBitCast(B.CreateConstGEP1_32(channel_r,channel_index_map[ca]),p_t_i32);
	};

	addchd(0x10,1); // MFC_LSA
	addchd(0x11,1); // MFC_EAH
	addchd(0x12,1); // MFC_EAL
	addchd(0x13,1); // MFC_Size
	addchd(0x14,1); // MFC_TagID
	addchd(0x1b,1); // MFC_RdAtomicStat

	addchd(0x16,1); // MFC_WrTagMask
	addchd(0x17,1); // MFC_WrTagUpdate

	auto rdch = [&](int ca) -> Value* {
		switch (ca) {
			case 0x3: // SPU_RdSigNotify1
			case 0x4: // SPU_RdSigNotify2
			case 0x1d: // SPU_RdInMbox
				return B.CreateCall(F_spu_rdch,ci32(ca));
			case 0x1b: // MFC_RdAtomicStat
				return B.CreateLoad(getchdt(ca,t_i32));
			case 0xc: // MFC_RdTagMask
			case 0x18: // MFC_RdTagStat
				return B.CreateLoad(getchdt(0x16,t_i32));
		}
		mkerr("bad rdch");
		return ci32(0);
	};
	auto rchcnt = [&](int ca) -> Value* {
		switch (ca) {
			case 0x3: // SPU_RdSigNotify1
			case 0x4: // SPU_RdSigNotify2
			case 0x1d: // SPU_RdInMbox
				return B.CreateCall(F_spu_rdch_count,ci32(ca));
			case 0x1c: // SPU_WrOutMbox
				return B.CreateCall(F_spu_wrch_count,ci32(ca));
			case 0x10: // MFC_LSA
			case 0x11: // MFC_EAH
			case 0x12: // MFC_EAL
			case 0x13: // MFC_Size
			case 0x14: // MFC_TagID
				return ci32(1);
				break;
			case 0x1b: // MFC_RdAtomicStat
				return ci32(1);
				break;
			case 0x16: // MFC_WrTagMask
			case 0x17: // MFC_WrTagUpdate
				return ci32(1);
			case 0x1e: // SPU_WrOutIntrMbox
				if (is_raw_spu) return B.CreateCall(F_spu_wrch_count,ci32(ca));
				else return ci32(1);
				break;
		}
		return ci32(0);
	};
	std::function<void(Value*cmd)> MFC_Cmd;
	auto wrch = [&](int ca,Value*v) {
		Value*v_32 = TRUNC(LSHRI(v,96),4);
		switch (ca) {
			case 0x10: // MFC_LSA
			case 0x11: // MFC_EAH
			case 0x12: // MFC_EAL
			case 0x13: // MFC_Size
			case 0x14: // MFC_TagID
				B.CreateStore(v_32,getchdt(ca,t_i32));
				break;
			case 0x15: // MFC_Cmd
				MFC_Cmd(v_32);
				break;
			case 0x16: // MFC_WrTagMask
			case 0x17: // MFC_WrTagUpdate
				B.CreateStore(v_32,getchdt(ca,t_i32));
				break;
			case 0x1c: // SPU_WrOutMbox
				B.CreateCall2(F_spu_wrch,ci32(ca),v_32);
				break;
			case 0x1e: // SPU_WrOutIntrMbox
				B.CreateCall(F_spu_wr_out_intr_mbox,v_32);
				break;
			default:
				mkerr("bad wrch");
				break;
		}
	};

	addchd(0x100 + 0xd0,0x80); // getllar (data)
	addchd(0x100 + 0xb4,1); // putllc (address)

	MFC_Cmd = [&](Value*cmd) {
		Value*lsa = B.CreateLoad(getchdt(0x10,t_i32));
		Value*eah = B.CreateLoad(getchdt(0x11,t_i32));
		Value*eal = B.CreateLoad(getchdt(0x12,t_i32));
		Value*size = B.CreateLoad(getchdt(0x13,t_i32));
		Value*tag_id = B.CreateLoad(getchdt(0x14,t_i32));
		Value*ea = OR(EXTZ64(eal),SHLI(EXTZ64(eah),32));
		for (int i=0x10;i<=0x14;i++) B.CreateStore(ci32(0),getchdt(i,t_i32));

		if (debug_output&&debug_superverbose) {
			mkprint("MFC");
			B.CreateCall(F_dump_addr,EXTZ64(cmd));
			B.CreateCall(F_dump_addr,EXTZ64(lsa));
			B.CreateCall(F_dump_addr,EXTZ64(eah));
			B.CreateCall(F_dump_addr,EXTZ64(eal));
			B.CreateCall(F_dump_addr,EXTZ64(size));
			B.CreateCall(F_dump_addr,EXTZ64(tag_id));
			B.CreateCall(F_dump_addr,ea);
		}

		BasicBlock*bb_def = new_bb();
		BasicBlock*bb_post = new_bb();

		// This code checks if the address is in the spu thread resources region (0xf0000000-0x100000000).
		// If the address is in another SPU's LS, then update the address with the real address and fall-thru.
		// If it is the address of SNR1 or SNR2, then handle that for put and get commands with size 4.
		// Otherwise, throw an error.
		{
			BasicBlock*bb_mmio_ls = new_bb();
			BasicBlock*bb_mmio_ls_post = new_bb();
			BasicBlock*bb_mmio_ls_pre = BB;
			B.CreateCondBr(B.CreateAnd(B.CreateICmpUGE(ea,ci64(0xf0000000)),B.CreateICmpULT(ea,ci64(0x100000000))),bb_mmio_ls,bb_mmio_ls_post);
			switch_bb(bb_mmio_ls);
			Value*spu_num = TRUNC(B.CreateSub(B.CreateUDiv(ea,ci64(0x100000)),ci64(0xf00)),4);
			Value*addr = B.CreateAnd(ea,ci64(0xfffff));
			BasicBlock*bb_yes,*bb_no;
			Value*bb_yes_ea;
			k_if(B.CreateICmpULT(B.CreateAdd(addr,EXTZ64(size)),ci64(0x40000)),[&]() {
				bb_yes_ea = B.CreateAdd(B.CreateCall(F_spu_x_get_ls_addr,spu_num),addr);
				bb_yes = BB;
			},[&]() {
				BasicBlock*bb_y = new_bb(),*bb_n = new_bb();
				Value*is_snr2 = B.CreateICmpEQ(addr,ci64(0x5C00C));
				Value*is_snr1 = B.CreateICmpEQ(addr,ci64(0x5400C));
				Value*is_32bit = B.CreateICmpEQ(size,ci32(4));
				Value*is_put = B.CreateICmpEQ(cmd,ci32(0x20));
				B.CreateCondBr(AND(OR(is_snr1,is_snr2),AND(is_put,is_32bit)),bb_y,bb_n);
				switch_bb(bb_y);
				Value*v = RMEM(get_ls(lsa),4);
				B.CreateCall3(F_spu_x_write_snr,spu_num,EXTZ32(is_snr2),v);
				B.CreateBr(bb_post);
				
				switch_bb(bb_n);
				mkerr("bad spu thread mmio MFC access");
				bb_no = BB;
			});
			PHINode*mmio_ea = B.CreatePHI(t_i64,2);
			mmio_ea->addIncoming(bb_yes_ea,bb_yes);
			mmio_ea->addIncoming(UndefValue::get(t_i64),bb_no);
			BasicBlock*bb_mmio_ea = BB;
			B.CreateBr(bb_mmio_ls_post);

			switch_bb(bb_mmio_ls_post);
			PHINode*phi_ea = B.CreatePHI(t_i64,2);
			phi_ea->addIncoming(ea,bb_mmio_ls_pre);
			phi_ea->addIncoming(mmio_ea,bb_mmio_ea);
			ea = phi_ea;
		}

		SwitchInst*sw = B.CreateSwitch(cmd,bb_def);

		BasicBlock*last_bb;

		auto addc = [&](int n,const std::function<void()>&f) {
			last_bb = switch_bb(new_bb());
			sw->addCase(ci32(n),last_bb);
			f();
			B.CreateBr(bb_post);
		};
		auto addlast = [&](int n) {
			sw->addCase(ci32(n),last_bb);
		};

		addc(0x20,[&]() { // put
			B.CreateCall5(F_llvm_memcpy,B.CreateIntToPtr(ea,p_t_i8),B.CreateIntToPtr(get_ls(lsa),p_t_i8),EXTZ64(size),ci32(0),ci1(1));
		});
		addlast(0x30);

		addc(0x40,[&]() { // get
			B.CreateCall5(F_llvm_memcpy,B.CreateIntToPtr(get_ls(lsa),p_t_i8),B.CreateIntToPtr(ea,p_t_i8),EXTZ64(size),ci32(0),ci1(1));
		}); 
		
		addc(0xd0,[&](){ // getllar
			if (debug_tests) {
				k_if(B.CreateICmpNE(size,ci32(0x80)),[&]() {
					mkerr("getllar size is not 0x80!");
				},[&](){});
			}
			B.CreateStore(ea,getchdt(0x100+0xb4,t_i64));
			LoadInst*v = B.CreateLoad(B.CreateIntToPtr(ea,p_t_i1024),true);
			B.CreateStore(v,getchdt(0x100+0xd0,t_i1024));
			B.CreateStore(v,B.CreateIntToPtr(get_ls(lsa),p_t_i1024));
			B.CreateStore(ci32(4),getchdt(0x1b,t_i32)); // getllar completed
		});

		addc(0xb4,[&](){ // putllc
			if (debug_tests) {
				k_if(B.CreateICmpNE(size,ci32(0x80)),[&]() {
					mkerr("putllc size is not 0x80!");
				},[&](){});
			}
			k_if(B.CreateICmpNE(B.CreateLoad(getchdt(0x100+0xb4,t_i64)),ea),[&]() {
				B.CreateStore(ci32(1),getchdt(0x1b,t_i32)); // putllc failed
			},[&](){
				
				// This is how this instruction would ideally be implemented:
				// Value*ov = B.CreateLoad(getchdt(0x100+0xd0,t_i1024));
				// Value*nv = B.CreateLoad(B.CreateIntToPtr(get_ls(lsa),p_t_i1024));
				// Value*v = B.CreateCall3(F_llvm_atomic_cmp_swap1024,B.CreateIntToPtr(ea,p_t_i1024),ov,nv);

				// Line locks are emulated using compare & swap.
				// Unfortunately, this can be done with at most 64-bit values, so there is no correct way to implement putllc.
				// Instead, we implement it as compare & swap on each 64-bit value individually in the 128-byte cache line.
				// If more than one 64-bit value has changed, we error out.

				Value*a_ov = getchdt(0x100+0xd0,t_i64);
				Value*a_nv = B.CreateIntToPtr(get_ls(lsa),p_t_i64);
				
				BasicBlock*bb_post = new_bb();
				BasicBlock*tmp_bb = BB;
				switch_bb(bb_post);
				PHINode*fail = B.CreatePHI(t_i1,17);
				switch_bb(tmp_bb);

				Value*change_count = ci32(0);
				for (int i=0;i<1024/64;i++) {
					Value*ov = B.CreateLoad(B.CreateConstGEP1_32(a_ov,i));
					Value*nv = B.CreateLoad(B.CreateConstGEP1_32(a_nv,i));

					change_count = ADD(change_count,EXTZ32(B.CreateICmpNE(ov,nv)));
				}
				k_if(B.CreateICmpUGT(change_count,ci32(1)),[&]() {
					mkerr("change count > 1");
				},[&](){});
				
				for (int i=0;i<1024/64;i++) {
					Value*ov = B.CreateLoad(B.CreateConstGEP1_32(a_ov,i));
					Value*nv = B.CreateLoad(B.CreateConstGEP1_32(a_nv,i));
					BasicBlock*bb_yes = new_bb();
					BasicBlock*bb_no = new_bb();
					B.CreateCondBr(B.CreateICmpNE(ov,nv),bb_yes,bb_no);
					switch_bb(bb_yes);
					Value*v = B.CreateCall3(F_llvm_atomic_cmp_swap64,B.CreateIntToPtr(ADDI(ea,8*i),p_t_i64),ov,nv);

					fail->addIncoming(B.CreateICmpNE(v,ov),BB);

					B.CreateBr(bb_post);
					switch_bb(bb_no);
				}
				fail->addIncoming(ci1(0),BB);
				B.CreateBr(bb_post);
				switch_bb(bb_post);
				
				B.CreateStore(EXTZ32(fail),getchdt(0x1b,t_i32));
			});
		});


		switch_bb(bb_def);
		B.CreateCall(F_dump_addr,EXTZ64(cmd));
		mkerr("unknown MFC_Cmd");
		B.CreateBr(bb_post);
		switch_bb(bb_post);
	};

	std::map<uint64_t,function_info*>&func_map = s.func_map;
	std::stack<function_info*> func_stack;
	std::vector<function_info*> new_functions;

	auto get_function = [&](uint64_t addr) -> function_info& {
		//if (!valid_ip(addr)) printf("%llx makes invalid function at %llx\n",(long long)xx_ip,(long long)addr);
		function_info*&i = func_map[addr];
		if (!i) {
			i = new function_info();
			i->addr = addr;
			i->f = Function::Create(FT,Function::PrivateLinkage,"",mod);
			char buf[0x20];
			sprintf(buf,"S_%llX",(int)addr);
			i->f->setName(buf);
			i->compiled_function = 0;
			func_stack.push(i);
			new_functions.push_back(i);
		}
		return *i;
	};

	uint64_t insn_count = 0;

	auto hexbin = [&](uint64_t val) -> uint64_t {
		uint64_t s = 1;
		uint64_t r = 0;
		while (val) {
			if (val%2) r|=s;
			val/=2;
			s<<=4;
		}
		return r;
	};

	auto do_function = [&](function_info&fi) {

		F = fi.f;

		std::map<uint64_t,BasicBlock*> bb_map;
		std::stack<uint64_t> bb_stack;
		auto get_bb = [&](uint64_t addr) -> BasicBlock* {
			//if (!valid_ip(addr)) printf("%llx makes invalid bb at %llx\n",(long long)xx_ip,(long long)addr);
			BasicBlock*&bb = bb_map[addr];
			if (!bb) {
				bb = new_bb();
				bb->setName(format("L_%llX",addr).c_str());
				bb_stack.push(addr);
			}
			return bb;
		};
		switch_bb(new_bb());

		enter_function();

		Value*reserved_address = B.CreateAlloca(t_i64);
		Value*reserved_value = B.CreateAlloca(t_i64);

		B.CreateStore(ci64(0),reserved_address);

		B.CreateBr(get_bb(fi.addr));
		switch_bb(get_bb(fi.addr));

		while (!bb_stack.empty()) {
			uint64_t ip = bb_stack.top();
			bb_stack.pop();
			switch_bb(bb_map[ip]);
			int switch_reg = -1;
			Value*switch_val = 0;
			bool stop=false;
			while (!stop) {
				insn_count++;
				ip = ip&lslr_value;
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
// 				if (!valid_ip(ip)) {
// 					//xcept("%p outside code",(void*)orig_ip);
// 					printf("%016llX: outside code\n",(unsigned long long)ip);
// 					B.CreateCall(F_outside_code,ConstantInt::get(t_i64,ip));
// 					do_ret();
// 					break;
// 				}

				if (debug_output&&debug_superverbose) {
					B.CreateCall(F_dump_addr,ci64(ip));
				}
	
				uint32_t ins = read_32(ip);

				auto bad_insn = [&]() {
					B.CreateCall(F_bad_insn,ci64(ip));
				};

				auto bit_31 = ins&1;
				auto bit_30 = (ins>>1)&1;
				auto bit_30_31 = ins&3;
				auto bit_27_30 = (ins>>1)&0xF;
				auto bit_27_29 = (ins>>2)&0x7;
				auto bit_26_30 = (ins>>1)&0x1F;
				auto bit_25_31 = ins&0x7F;
				auto bit_22_30 = (ins>>1)&0x1FF;
				auto bit_21_31 = ins&0x7FF;
				auto bit_21_30 = (ins>>1)&0x3FF;
				auto bit_21_29 = (ins>>2)&0x1FF;
				auto bit_21_26 = (ins>>5)&0x3F;
				auto bit_21_25 = (ins>>6)&0x1F;
				auto bit_21 = (ins>>10)&1;
				auto bit_19 = (ins>>12)&1;
				auto bit_18_31 = ins&0x3fff;
				auto bit_18_24 = (ins>>7)&0x7f;
				auto bit_16_31 = ins&0xffff;
				auto bit_16_29 = (ins>>2)&0x3FFF;
				auto bit_16_20 = (ins>>11)&0x1f;
				auto bit_14_15 = (ins>>16)&0x3;
				auto bit_13_15 = (ins>>16)&0x7;
				auto bit_13 = (ins>>18)&0x1;
				auto bit_12_19 = (ins>>12)&0xFF;
				auto bit_12_15 = (ins>>16)&0xF;
				auto bit_12 = (ins>>19)&0x1;
				auto bit_11_20 = (ins>>11)&0x3FF;
				auto bit_11_17 = (ins>>14)&0x7F;
				auto bit_11_15 = (ins>>16)&0x1F;
				auto bit_11 = (ins>>20)&0x1;
				auto bit_10_17 = (ins>>14)&0xff;
				auto bit_10 = (ins>>21)&1;
				auto bit_9_24 = (ins>>7)&0xffff;
				auto bit_8_17 = (ins>>14)&0x3ff;
				auto bit_7_24 = (ins>>7)&0x3ffff;
				auto bit_6_29 = (ins>>2)&0xffffff;
				auto bit_6_10 = (ins>>21)&0x1f;
				auto bit_6_8 = (ins>>23)&0x7;
				auto bit_4_10 = (ins>>21)&0x7f;
				auto bit_0_10 = (ins>>21)&0x7ff;
				auto bit_0_9 = (ins>>22)&0x3ff;
				auto bit_0_8 = (ins>>23)&0x1ff;
				auto bit_0_7 = (ins>>24)&0xff;
				auto bit_0_6 = (ins>>25)&0x7f;
				auto bit_0_3 = (ins>>28)&0xf;

				auto rr_op = bit_0_10;
				auto rr_rb = bit_11_17;
				auto rr_ra = bit_18_24;
				auto rr_rt = bit_25_31;

				auto rrr_op = bit_0_3;
				auto rrr_rt = bit_4_10;
				auto rrr_rb = bit_11_17;
				auto rrr_ra = bit_18_24;
				auto rrr_rc = bit_25_31;

				auto ri7_op = bit_0_10;
				auto ri7_i7 = bit_11_17;
				auto ri7_ra = bit_18_24;
				auto ri7_rt = bit_25_31;

				auto ri8_op = bit_0_9;
				auto ri8_i8 = bit_10_17;
				auto ri8_ra = bit_18_24;
				auto ri8_rt = bit_25_31;

				auto ri10_op = bit_0_7;
				auto ri10_i10 = bit_8_17;
				auto ri10_ra = bit_18_24;
				auto ri10_rt = bit_25_31;
				
				auto ri16_op = bit_0_8;
				auto ri16_i16 = bit_9_24;
				auto ri16_rt = bit_25_31;

				auto ri18_op = bit_0_6;
				auto ri18_i18 = bit_7_24;
				auto ri18_rt = bit_25_31;

				auto foreach_store = [&](int rt,Value*v,int bytes) {
					for (int i=0;i<16/bytes;i++) {
						wr_subi(rt,v,i,bytes);
					}
				};
				auto foreach_binop2i = [&](int rt,int ra,Value*v,int bytes,const std::function<Value*(Value*,Value*)>&binop) {
					for (int i=0;i<16/bytes;i++) {
						wr_subi(rt,binop(rr_subi(ra,i,bytes),v),i,bytes);
					}
				};
				auto foreach_binop2 = [&](int rt,int ra,int rb,int bytes,const std::function<Value*(Value*,Value*)>&binop) {
					for (int i=0;i<16/bytes;i++) {
						wr_subi(rt,binop(rr_subi(ra,i,bytes),rr_subi(rb,i,bytes)),i,bytes);
					}
				};
				auto foreach_binop2x = [&](int rt,int ra,int rb,int bytes,const std::function<Value*(Value*,Value*)>&binop) {
					for (int i=0;i<16/bytes;i++) {
						Value*ov = rr_subi(rt,i,bytes);
						wr_subi(rt,ADD(binop(rr_subi(ra,i,bytes),rr_subi(rb,i,bytes)),AND(ov,ConstantInt::get(ov->getType(),1))),i,bytes);
					}
				};
				auto foreach_binop3 = [&](int rt,int ra,int rb,int rc,int bytes,const std::function<Value*(Value*,Value*,Value*)>&binop) {
					for (int i=0;i<16/bytes;i++) {
						wr_subi(rt,binop(rr_subi(ra,i,bytes),rr_subi(rb,i,bytes),rr_subi(rc,i,bytes)),i,bytes);
					}
				};

				auto f_foreach_binop2 = [&](int rt,int ra,int rb,const std::function<Value*(Value*,Value*)>&binop) {
					for (int i=0;i<4;i++) {
						wrf_subi(rt,binop(rrf_subi(ra,i),rrf_subi(rb,i)),i);
					}
				};
				auto df_foreach_binop2 = [&](int rt,int ra,int rb,const std::function<Value*(Value*,Value*)>&binop) {
					for (int i=0;i<2;i++) {
						wrdf_subi(rt,binop(rrdf_subi(ra,i),rrdf_subi(rb,i)),i);
					}
				};
				auto f_foreach_binop3 = [&](int rt,int ra,int rb,int rc,const std::function<Value*(Value*,Value*,Value*)>&binop) {
					for (int i=0;i<4;i++) {
						wrf_subi(rt,binop(rrf_subi(ra,i),rrf_subi(rb,i),rrf_subi(rc,i)),i);
					}
				};
				auto df_foreach_binop3 = [&](int rt,int ra,int rb,int rc,const std::function<Value*(Value*,Value*,Value*)>&binop) {
					for (int i=0;i<2;i++) {
						wrdf_subi(rt,binop(rrdf_subi(ra,i),rrdf_subi(rb,i),rrdf_subi(rc,i)),i);
					}
				};

				bool found = true;

				Value*tmp,*lsa;

				uint64_t nip;

				do {
					int ra,rb,rc,rt;
					uint64_t i7 = ri7_i7;
					uint64_t i8 = ri8_i8;
					uint64_t i10 = ri10_i10;
					uint64_t i16 = ri16_i16;
					uint64_t i18 = ri18_i18;

					rt = ri18_rt;
					switch (hexbin(ri18_op)) {
						case 0x0100001: // ila
							tmp = EXTZ32(ci18(i18));
							foreach_store(rt,tmp,4);
							break;
						case 0x0001000: // hbra
						case 0x0001001: // hbrr
							break;
						default:
							found = false;
					}
					if (found) break;
					found=true;
					rt = ri16_rt;
					switch (hexbin(ri16_op)) {
						case 0x001100001: // lqa
							lsa = LSA(EXTS32(ci18(i16<<2)));
							wr(rt,RMEM(lsa,16));
							break;
						case 0x001100111: // lqr
							lsa = LSA(ADD(EXTS32(ci18(i16<<2)),ci32(ip)));
							wr(rt,RMEM(lsa,16));
							break;
						case 0x001000001: // stqa
							lsa = LSA(EXTS32(ci18(i16<<2)));
							WMEM(lsa,16,rr(rt));
							break;
						case 0x001000111: // stqr
							lsa = LSA(ADD(EXTS32(ci18(i16<<2)),ci32(ip)));
							WMEM(lsa,16,rr(rt));
							break;
						case 0x010000011: // ilh
							foreach_store(rt,ci16(i16),2);
							break;
						case 0x010000010: // ilhu
							foreach_store(rt,ci32(i16<<16),4);
							break;
						case 0x010000001: // il
							foreach_store(rt,EXTS32(ci16(i16)),4);
							break;
						case 0x011000001: // iohl
							foreach_binop2i(rt,rt,EXTZ32(ci16(i16)),4,OR);
							break;
						case 0x001100101: // fsmbi
							uint8_t nbuf[16];
							for (int i=0;i<16;i++) {
								if (ri16_i16&(1<<i)) nbuf[i] = 0xff;
								else nbuf[i] = 0;
							}
							wr(rt,ConstantInt::get(t_i128,APInt(128,2,(uint64_t*)nbuf)));
							break;
						case 0x001100100: // br
							nip = (ip + (((int32_t)(int16_t)i16)<<2)) & lslr_value;
							B.CreateBr(get_bb(nip));
							stop=true;
							break;
						case 0x001100000: // bra
							nip = (((int32_t)(int16_t)i16)<<2) & lslr_value;
							B.CreateBr(get_bb(nip));
							stop=true;
							break;
						case 0x001100110: // brsl
							nip = (ip + (((int32_t)(int16_t)i16)<<2)) & lslr_value;
							do_call(get_function(nip).f,rt);
							break;
						case 0x001100010: // brasl
							nip = (((int32_t)(int16_t)i16)<<2) & lslr_value;
							do_call(get_function(nip).f,rt);
							break;
						case 0x001000010: // brnz
							BasicBlock*b_post;
							b_post = BasicBlock::Create(context, "", F);
							nip = (ip + (((int32_t)(int16_t)i16)<<2)) & lslr_value & 0xFFFFFFFC;
							B.CreateCondBr(B.CreateICmpNE(rr_subi(rt,0,4),ci32(0)),get_bb(nip),b_post);
							switch_bb(b_post);
							break;
						case 0x001000000: // brz
							b_post = BasicBlock::Create(context, "", F);
							nip = (ip + (((int32_t)(int16_t)i16)<<2)) & lslr_value & 0xFFFFFFFC;
							B.CreateCondBr(B.CreateICmpEQ(rr_subi(rt,0,4),ci32(0)),get_bb(nip),b_post);
							switch_bb(b_post);
							break;
						case 0x001000110: // brhnz
							b_post = BasicBlock::Create(context, "", F);
							nip = (ip + (((int32_t)(int16_t)i16)<<2)) & lslr_value & 0xFFFFFFFC;
							B.CreateCondBr(B.CreateICmpNE(rr_subi(rt,0,2),ci16(0)),get_bb(nip),b_post);
							switch_bb(b_post);
							break;
						case 0x001000100: // brhz
							b_post = BasicBlock::Create(context, "", F);
							nip = (ip + (((int32_t)(int16_t)i16)<<2)) & lslr_value & 0xFFFFFFFC;
							B.CreateCondBr(B.CreateICmpEQ(rr_subi(rt,0,2),ci16(0)),get_bb(nip),b_post);
							switch_bb(b_post);
							break;
						default:
							found = false;
					}
					if (found) break;
					found=true;
					rt = ri10_rt;
					ra = ri10_ra;
					switch (hexbin(ri10_op)) {
						case 0x00110100: // lqd
							lsa = LSA(ADD(EXTS32(ci14(i10<<4)),rr_subi(ra,0,4)));
							wr(rt,RMEM(lsa,16));
							break;
						case 0x00100100: // stqd
							lsa = LSA(ADD(EXTS32(ci14(i10<<4)),rr_subi(ra,0,4)));
							WMEM(lsa,16,rr(rt));
							break;
						case 0x00011101: // ahi
							foreach_binop2i(rt,ra,EXTS16(ci10(i10)),2,ADD);
							break;
						case 0x00011100: // ai
							foreach_binop2i(rt,ra,EXTS32(ci10(i10)),4,ADD);
							break;
						case 0x00001101: // sfhi
							foreach_binop2i(rt,ra,EXTS16(ci10(i10)),2,SUBF);
							break;
						case 0x00001100: // sfi
							foreach_binop2i(rt,ra,EXTS32(ci10(i10)),4,SUBF);
							break;
						case 0x01110100: // mpyi
							foreach_binop2i(rt,ra,EXTS32(ci10(i10)),4,MPY);
							break;
						case 0x01110101: // mpyui
							foreach_binop2i(rt,ra,EXTS32(ci10(i10)),4,MPYU);
							break;
						case 0x00010110: // andbi
							foreach_binop2i(rt,ra,ci8(i10),1,AND);
							break;
						case 0x00010101: // andhi
							foreach_binop2i(rt,ra,EXTS16(ci10(i10)),2,AND);
							break;
						case 0x00010100: // andi
							foreach_binop2i(rt,ra,EXTS32(ci10(i10)),4,AND);
							break;
						case 0x00000110: // orbi
							foreach_binop2i(rt,ra,ci8(i10),1,OR);
							break;
						case 0x00000101: // orhi
							foreach_binop2i(rt,ra,EXTS16(ci10(i10)),2,OR);
							break;
						case 0x00000100: // ori
							foreach_binop2i(rt,ra,EXTS32(ci10(i10)),4,OR);
							break;
						case 0x01000110: // xorbi
							foreach_binop2i(rt,ra,ci8(i10),1,XOR);
							break;
						case 0x01000101: // xorhi
							foreach_binop2i(rt,ra,EXTS16(ci10(i10)),2,XOR);
							break;
						case 0x01000100: // xori
							foreach_binop2i(rt,ra,EXTS32(ci10(i10)),4,XOR);
							break;
						case 0x01111111: // heqi
							k_if(B.CreateICmpEQ(rr_subi(ra,0,4),EXTS32(ci10(i10))),[&]() {
								//B.CreateCall(F_spu_halt,ci32(ip));
								mkerr("halt");
							},[&](){});
							break;
						case 0x01001111: // hgti
							k_if(B.CreateICmpSGT(rr_subi(ra,0,4),EXTS32(ci10(i10))),[&]() {
								//B.CreateCall(F_spu_halt,ci32(ip));
								mkerr("halt");
							},[&](){});
							break;
						case 0x01011111: // hlgti
							k_if(B.CreateICmpUGT(rr_subi(ra,0,4),EXTS32(ci10(i10))),[&]() {
								//B.CreateCall(F_spu_halt,ci32(ip));
								mkerr("halt");
							},[&](){});
							break;
						case 0x01111110: // ceqbi
							foreach_binop2i(rt,ra,ci8(i10),1,CEQ);
							break;
						case 0x01111101: // ceqhi
							foreach_binop2i(rt,ra,EXTS16(ci10(i10)),2,CEQ);
							break;
						case 0x01111100: // ceqi
							foreach_binop2i(rt,ra,EXTS32(ci10(i10)),4,CEQ);
							break;
						case 0x01001110: // cgtbi
							foreach_binop2i(rt,ra,ci8(i10),1,CGT);
							break;
						case 0x01001101: // cgthi
							foreach_binop2i(rt,ra,EXTS16(ci10(i10)),2,CGT);
							break;
						case 0x01001100: // cgti
							foreach_binop2i(rt,ra,EXTS32(ci10(i10)),4,CGT);
							break;
						case 0x01011110: // clgtbi
							foreach_binop2i(rt,ra,ci8(i10),1,CLGT);
							break;
						case 0x01011101: // clgthi
							foreach_binop2i(rt,ra,EXTS16(ci10(i10)),2,CLGT);
							break;
						case 0x01011100: // clgtqi
							foreach_binop2i(rt,ra,EXTS32(ci10(i10)),4,CLGT);
							break;
						default:
							found = false;
					}
					if (found) break;
					found=true;
					rt = ri8_rt;
					ra = ri8_ra;
					switch (hexbin(ri8_op)) {
						case 0x0111011010: // csflt
							for (int i=0;i<4;i++) {
								float d = 1.0f;
								for (int i2=0;i2<(int)(155-i8);i2++) d*=2.0f;
								wrf_subi(rt,B.CreateFDiv(ConvertSXWtoSP(rrf_subi(ra,i)),ConstantFP::get(t_float,d)),i);
							}
							break; 
						case 0x0111011000: // cflts
							for (int i=0;i<4;i++) {
								wrf_subi(rt,ConvertSPtoSXWsaturate(rrf_subi(ra,i),ci8(173-i8)),i);
							}
							break;
						case 0x0111011011: // cuflt
							for (int i=0;i<4;i++) {
								float d = 1.0f;
								for (int i2=0;i2<(int)(155-i8);i2++) d*=2.0f;
								wrf_subi(rt,B.CreateFDiv(ConvertUXWtoSP(rrf_subi(ra,i)),ConstantFP::get(t_float,d)),i);
							}
							break;
						case 0x0111011001: // cfltu
							for (int i=0;i<4;i++) {
								wrf_subi(rt,ConvertSPtoUXWsaturate(rrf_subi(ra,i),ci8(173-i8)),i);
							}
							break;
						default:
							found = false;
					}
					if (found) break;
					found=true;
					rt = ri7_rt;
					ra = ri7_ra;
					switch (hexbin(ri7_op)) {
						case 0x00111110100: // cbd
							tmp = ANDI(ADD(rr_subi(ra,0,4),EXTS32(ci7(i7))),0xF);
							tmp = EXTZ128(SUB(ci32(120),MULI(tmp,8)));
							wr(rt,OR(AND(ci128(0x18191A1B1C1D1E1F,0x1011121314151617),NOT(SHL(ci128(0xff,0),tmp))),SHL(ci128(0x03,0),tmp)));
							break;
						case 0x00111110101: // chd
							tmp = ANDI(ADD(rr_subi(ra,0,4),EXTS32(ci7(i7))),0xE);
							tmp = EXTZ128(SUB(ci32(112),MULI(tmp,8)));
							wr(rt,OR(AND(ci128(0x18191A1B1C1D1E1F,0x1011121314151617),NOT(SHL(ci128(0xffff,0),tmp))),SHL(ci128(0x0203,0),tmp)));
							break;
						case 0x00111110110: // cwd
							tmp = ANDI(ADD(rr_subi(ra,0,4),EXTS32(ci7(i7))),0xC);
							tmp = EXTZ128(SUB(ci32(96),MULI(tmp,8)));
							wr(rt,OR(AND(ci128(0x18191A1B1C1D1E1F,0x1011121314151617),NOT(SHL(ci128(0xffffffff,0),tmp))),SHL(ci128(0x00010203,0),tmp)));
							break;
						case 0x00111110111: // cdd
							tmp = ANDI(ADD(rr_subi(ra,0,4),EXTS32(ci7(i7))),0x8);
							tmp = EXTZ128(SUB(ci32(64),MULI(tmp,8)));
							wr(rt,OR(AND(ci128(0x18191A1B1C1D1E1F,0x1011121314151617),NOT(SHL(ci128(0xffffffffffffffff,0),tmp))),SHL(ci128(0x0001020304050607,0),tmp)));
							break;
						case 0x00001111111: // shlhi
							foreach_binop2i(rt,ra,ANDI(EXTS16(ci7(i7)),0x1f),2,SHLZ);
							break;
						case 0x00001111011: // shli
							foreach_binop2i(rt,ra,ANDI(EXTS32(ci7(i7)),0x3f),4,SHLZ);
							break;
						case 0x00111111011: // shlqbii
							wr(rt,SHL(rr(ra),EXTZ128(ANDI(ci7(i7),0x7))));
							break;
						case 0x00111111111: // shlqbyi
							wr(rt,SHLZ(rr(ra),ci128((i7&0x1f)*8,0)));
							break;
						case 0x00001111100: // rothi
							for (int i=0;i<8;i++) {
								wr_subi(rt,ROTL(rr_subi(ra,i,2),ANDI(EXTS16(ci7(i7)),0xf)),i,2);
							}
							break;
						case 0x00001111000: // roti
							for (int i=0;i<4;i++) {
								wr_subi(rt,ROTL(rr_subi(ra,i,4),ANDI(EXTS32(ci7(i7)),0x1f)),i,4);
							}
							break;
						case 0x00111111100: // rotqbyi
							wr(rt,ROTL(rr(ra),ci128((i7&0xf)*8,0)));
							break;
						case 0x00111111000: // rotqbii
							wr(rt,ROTL(rr(ra),EXTZ128(ANDI(ci7(i7),0x7))));
							break;
						case 0x00001111101: // rothmi
							for (int i=0;i<8;i++) {
								wr_subi(rt,LSHRZ(rr_subi(ra,i,2),ANDI(NEG(EXTS16(ci7(i7))),0x1f)),i,2);
							}
							break;
						case 0x00001111001: // rotmi
							for (int i=0;i<4;i++) {
								wr_subi(rt,LSHRZ(rr_subi(ra,i,4),ANDI(NEG(EXTS32(ci7(i7))),0x3f)),i,4);
							}
							break;
						case 0x00111111101: // rotqmbyi
							wr(rt,LSHRZ(rr(ra),EXTZ128(MULI(ANDI(NEG(ci7(i7)),0x1F),8))));
							break;
						case 0x00111111001: // rotqmbii
							wr(rt,LSHR(rr(ra),EXTZ128(ANDI(NEG(ci7(i7)),0x7))));
							break;
						case 0x00001111110: // rotmahi
							for (int i=0;i<8;i++) {
								wr_subi(rt,ASHRZ(rr_subi(ra,i,2),ANDI(NEG(EXTS16(ci7(i7))),0x1f)),i,2);
							}
							break;
						case 0x00001111010: // rotmai
							for (int i=0;i<4;i++) {
								wr_subi(rt,ASHRZ(rr_subi(ra,i,4),ANDI(NEG(EXTS32(ci7(i7))),0x3f)),i,4);
							}
							break;
						default:
							found = false;
					}
					if (found) break;
					found=true;
					ra = rr_ra;
					rb = rr_rb;
					rt = rr_rt;
					switch (hexbin(rr_op)) {
						case 0x00111000100: // lqx
							lsa = LSA(ADD(rr_subi(ra,0,4),rr_subi(rb,0,4)));
							wr(rt,RMEM(lsa,16));
							break;
						case 0x00101000100: // stqx
							lsa = LSA(ADD(rr_subi(ra,0,4),rr_subi(rb,0,4)));
							WMEM(lsa,16,rr(rt));
							break;
						case 0x00111010100: // cbx
							tmp = ANDI(ADD(rr_subi(ra,0,4),rr_subi(rb,0,4)),0xF);
							tmp = EXTZ128(SUB(ci32(120),MULI(tmp,8)));
							wr(rt,OR(AND(ci128(0x18191A1B1C1D1E1F,0x1011121314151617),NOT(SHL(ci128(0xff,0),tmp))),SHL(ci128(0x03,0),tmp)));
							break;
						case 0x00111010101: // chx
							tmp = ANDI(ADD(rr_subi(ra,0,4),rr_subi(rb,0,4)),0xE);
							tmp = EXTZ128(SUB(ci32(112),MULI(tmp,8)));
							wr(rt,OR(AND(ci128(0x18191A1B1C1D1E1F,0x1011121314151617),NOT(SHL(ci128(0xffff,0),tmp))),SHL(ci128(0x0203,0),tmp)));
							break;
						case 0x00111010110: // cwx
							tmp = ANDI(ADD(rr_subi(ra,0,4),rr_subi(rb,0,4)),0xC);
							tmp = EXTZ128(SUB(ci32(96),MULI(tmp,8)));
							wr(rt,OR(AND(ci128(0x18191A1B1C1D1E1F,0x1011121314151617),NOT(SHL(ci128(0xffffffff,0),tmp))),SHL(ci128(0x00010203,0),tmp)));
							break;
						case 0x00111010111: // cdx
							tmp = ANDI(ADD(rr_subi(ra,0,4),rr_subi(rb,0,4)),0x8);
							tmp = EXTZ128(SUB(ci32(64),MULI(tmp,8)));
							wr(rt,OR(AND(ci128(0x18191A1B1C1D1E1F,0x1011121314151617),NOT(SHL(ci128(0xffffffffffffffff,0),tmp))),SHL(ci128(0x0001020304050607,0),tmp)));
							break;
						case 0x00011001000: // ah
							foreach_binop2(rt,ra,rb,2,ADD);
							break;
						case 0x00011000000: // a
							foreach_binop2(rt,ra,rb,4,ADD);
							break;
						case 0x00001001000: // sfh
							foreach_binop2(rt,ra,rb,2,SUBF);
							break;
						case 0x00001000000: // sf
							foreach_binop2(rt,ra,rb,4,SUBF);
							break;
						case 0x01101000000: // addx
							foreach_binop2x(rt,ra,rb,4,ADD);
							break;
						case 0x00011000010: // cg
							for (int i=0;i<4;i++) {
								tmp = ADD(EXTZ33(rr_subi(ra,i,4)),EXTZ33(rr_subi(ra,i,4)));
								tmp = TRUNC(LSHRI(tmp,32),4);
								wr_subi(rt,tmp,i,4);
							}
							break;
						case 0x01101000010: // cgx
							for (int i=0;i<4;i++) {
								Value*ov = rr_subi(rt,i,4);
								tmp = ADD(ADD(EXTZ33(rr_subi(ra,i,4)),EXTZ33(rr_subi(ra,i,4))),EXTZ33(AND(ov,ConstantInt::get(ov->getType(),1))));
								tmp = TRUNC(LSHRI(tmp,32),4);
								wr_subi(rt,tmp,i,4);
							}
							break;
						case 0x01101000001: // sfx
							foreach_binop2x(rt,ra,rb,4,NOTADD);
							break;
						case 0x00001000010: // bg
							for (int i=0;i<4;i++) {
								tmp = B.CreateSelect(B.CreateICmpUGE(rr_subi(rb,i,4),rr_subi(ra,i,4)),ci32(1),ci32(0));
								wr_subi(rt,tmp,i,4);
							}
							break;
						case 0x01101000011: // bgx
							for (int i=0;i<4;i++) {
								Value*ge = B.CreateSelect(B.CreateICmpUGE(rr_subi(rb,i,4),rr_subi(ra,i,4)),ci32(1),ci32(0));
								Value*gt = B.CreateSelect(B.CreateICmpUGT(rr_subi(rb,i,4),rr_subi(ra,i,4)),ci32(1),ci32(0));
								tmp = B.CreateSelect(B.CreateTrunc(rr_subi(rt,i,4),t_i1),ge,gt);
								wr_subi(rt,tmp,i,4);
							}
							break;
						case 0x01111000100: // mpy
							foreach_binop2(rt,ra,rb,4,MPY);
							break;
						case 0x01111001100: // mpyu
							foreach_binop2(rt,ra,rb,4,MPYU);
							break;
						case 0x0111100010: // mpyh
							foreach_binop2(rt,ra,rb,4,MPYH);
							break;
						case 0x01111000111: // mpys
							foreach_binop2(rt,ra,rb,4,MPYS);
							break;
						case 0x01111000110: // mpyhh
							foreach_binop2(rt,ra,rb,4,MPYHH);
							break;
						case 0x01101000110: // mpyhha
							foreach_binop3(rt,ra,rb,rt,4,MPYHHA);
							break;
						case 0x01111001110: // mpyhhu
							foreach_binop2(rt,ra,rb,4,MPYHHU);
							break;
						case 0x01101001110: // mpyhhau
							foreach_binop3(rt,ra,rb,rt,4,MPYHHAU);
							break;
						case 0x01010100101: // clz
							for (int i=0;i<4;i++) {
								wr_subi(rt,B.CreateCall(F_llvm_ctlz32,rr_subi(ra,i,4)),i,4);
							}
							break;
						case 0x01010110100: // cntb
							for (int i=0;i<16;i++) {
								wr_subi(rt,B.CreateCall(F_llvm_ctpop8,rr_subi(ra,i,1)),i,1);
							}
							break;
						case 0x00110110110: // fsmb
							tmp = rr_subi(ra,1,2);
							for (int i=0;i<16;i++) {
								wr_subi(rt,B.CreateSelect(B.CreateTrunc(LSHRI(tmp,15-i),t_i1),ci8(0xff),ci8(0)),i,1);
							}
							break;
						case 0x00110110101: // fsmh
							tmp = rr_subi(ra,3,1);
							for (int i=0;i<8;i++) {
								wr_subi(rt,B.CreateSelect(B.CreateTrunc(LSHRI(tmp,7-i),t_i1),ci16(0xffff),ci16(0)),i,2);
							}
							break;
						case 0x00110110100: // fsm
							tmp = B.CreateTrunc(rr_subi(ra,3,1),t_i4);
							for (int i=0;i<4;i++) {
								wr_subi(rt,B.CreateSelect(B.CreateTrunc(LSHRI(tmp,3-i),t_i1),ci32(0xffffffff),ci32(0)),i,4);
							}
							break;
						case 0x00110110010: // gbb
							tmp = ci32(0);
							for (int i=0;i<16;i++) {
								tmp = OR(tmp,SHLI(EXTZ32(B.CreateTrunc(rr_subi(ra,i,1),t_i1)),15-i));
							}
							wr_subi(rt,tmp,0,4);
							wr_subi(rt,ci32(0),1,4);
							wr_subi(rt,ci32(0),2,4);
							wr_subi(rt,ci32(0),3,4);
							break;
						case 0x00110110001: // gbh
							tmp = ci32(0);
							for (int i=0;i<8;i++) {
								tmp = OR(tmp,SHLI(EXTZ32(B.CreateTrunc(rr_subi(ra,i,2),t_i1)),7-i));
							}
							wr_subi(rt,tmp,0,4);
							wr_subi(rt,ci32(0),1,4);
							wr_subi(rt,ci32(0),2,4);
							wr_subi(rt,ci32(0),3,4);
							break;
						case 0x00110110000: // gb
							tmp = ci32(0);
							for (int i=0;i<4;i++) {
								tmp = OR(tmp,SHLI(EXTZ32(B.CreateTrunc(rr_subi(ra,i,4),t_i1)),3-i));
							}
							wr_subi(rt,tmp,0,4);
							wr_subi(rt,ci32(0),1,4);
							wr_subi(rt,ci32(0),2,4);
							wr_subi(rt,ci32(0),3,4);
							break;
						case 0x00011010011: // avgb
							for (int i=0;i<15;i++) {
								wr_subi(rt,TRUNC(SHLI(ADD(EXTZ16(rr_subi(ra,i,1)),EXTZ16(rr_subi(rb,i,1))),1),1),i,1);
							}
							break;
						case 0x00001010011: // absdb
							for (int i=0;i<15;i++) {
								Value*a = rr_subi(ra,i,1);
								Value*b = rr_subi(rb,i,1);
								wr_subi(rt,B.CreateSelect(B.CreateICmpUGT(b,a),SUB(b,a),SUB(a,b)),i,1);
							}
							break;
						case 0x01001010011: // sumb
							for (int i=0;i<4;i++) {
								wr_subi(rt,ADD(ADD(ADD(EXTZ16(rr_subi(rb,i*4,1)),EXTZ16(rr_subi(rb,i*4+1,1))),EXTZ16(rr_subi(rb,i*4+2,1))),EXTZ16(rr_subi(rb,i*4+3,1))),i*2,2);
								wr_subi(rt,ADD(ADD(ADD(EXTZ16(rr_subi(ra,i*4,1)),EXTZ16(rr_subi(ra,i*4+1,1))),EXTZ16(rr_subi(ra,i*4+2,1))),EXTZ16(rr_subi(ra,i*4+3,1))),i*2+1,2);
							}
							break;
						case 0x01010110110: // xsbh
							for (int i=0;i<8;i++) {
								wr_subi(rt,EXTS16(rr_subi(ra,i*2+1,1)),i,2);
							}
							break;
						case 0x01010101110: // xshw
							for (int i=0;i<4;i++) {
								wr_subi(rt,EXTS32(rr_subi(ra,i*2+1,2)),i,4);
							}
							break;
						case 0x01010100110: // xswd
							for (int i=0;i<2;i++) {
								wr_subi(rt,EXTS64(rr_subi(ra,i*2+1,4)),i,8);
							}
							break;
						case 0x00011000001: // and
							wr(rt,AND(rr(ra),rr(rb)));
							break;
						case 0x01011000001: // andc
							wr(rt,ANDC(rr(ra),rr(rb)));
							break;
						case 0x00001000001: // or
							wr(rt,OR(rr(ra),rr(rb)));
							break;
						case 0x01011001001: // orc
							wr(rt,ORC(rr(ra),rr(rb)));
							break;
						case 0x00111110000: // orx
							wr_subi(rt,OR(OR(OR(rr_subi(ra,0,4),rr_subi(ra,1,4)),rr_subi(ra,2,4)),rr_subi(ra,3,4)),0,4);
							wr_subi(rt,ci32(0),1,4);
							wr_subi(rt,ci32(0),2,4);
							wr_subi(rt,ci32(0),3,4);
							break;
						case 0x01001000001: // xor
							wr(rt,XOR(rr(ra),rr(rb)));
							break;
						case 0x00011001001: // nand
							wr(rt,NAND(rr(ra),rr(rb)));
							break;
						case 0x00001001001: // nor
							wr(rt,NOR(rr(ra),rr(rb)));
							break;
						case 0x01001001001: // eqv
							wr(rt,EQV(rr(ra),rr(rb)));
							break;
						case 0x00001011111: // shlh
							for (int i=0;i<8;i++) {
								wr_subi(rt,SHLZ(rr_subi(ra,i,2),ANDI(rr_subi(rb,i,2),0x1f)),i,2);
							}
							break;
						case 0x00001011011: // shl
							for (int i=0;i<4;i++) {
								wr_subi(rt,SHLZ(rr_subi(ra,i,4),ANDI(rr_subi(rb,i,4),0x3f)),i,4);
							}
							break;
						case 0x00111011011: // shlqbi
							wr(rt,SHL(rr(ra),EXTZ128(ANDI(rr_subi(rb,0,4),0x7))));
							break;
						case 0x00111011111: // shlqby
							wr(rt,SHLZ(rr(ra),EXTZ128(MULI(ANDI(rr_subi(rb,0,4),0x1F),8))));
							break;
						case 0x00111001111: // shlqbybi
							wr(rt,SHLZ(rr(ra),EXTZ128(MULI(ANDI(LSHRI(rr_subi(rb,0,4),3),0x1F),8))));
							break;
						case 0x00001011100: // roth
							for (int i=0;i<8;i++) {
								wr_subi(rt,ROTL(rr_subi(ra,i,2),ANDI(rr_subi(rb,i,2),0xf)),i,2);
							}
							break;
						case 0x00001011000: // rot
							for (int i=0;i<4;i++) {
								wr_subi(rt,ROTL(rr_subi(ra,i,4),ANDI(rr_subi(rb,i,4),0x1f)),i,4);
							}
							break;
						case 0x00111011100: // rotqby
							wr(rt,ROTL(rr(ra),EXTZ128(MULI(ANDI(rr_subi(rb,0,4),0xF),8))));
							break;
						case 0x00111001100: // rotqbybi
							wr(rt,ROTL(rr(ra),EXTZ128(MULI(ANDI(LSHRI(rr_subi(rb,0,4),3),0xF),8))));
							break;
						case 0x00111011000: // rotqbi
							wr(rt,ROTL(rr(ra),EXTZ128(ANDI(rr_subi(rb,0,4),0x7))));
							break;
						case 0x00001011101: // rothm
							for (int i=0;i<8;i++) {
								wr_subi(rt,LSHRZ(rr_subi(ra,i,2),ANDI(NEG(rr_subi(rb,i,2)),0x1f)),i,2);
							}
							break;
						case 0x00001011001: // rotm
							for (int i=0;i<4;i++) {
								wr_subi(rt,LSHRZ(rr_subi(ra,i,4),ANDI(NEG(rr_subi(rb,i,4)),0x3f)),i,4);
							}
							break;
						case 0x00111011101: // rotqmby
							wr(rt,LSHRZ(rr(ra),EXTZ128(MULI(ANDI(NEG(rr_subi(rb,0,4)),0x1F),8))));
							break;
						case 0x00111001101: // rotqbybi
							wr(rt,LSHRZ(rr(ra),EXTZ128(MULI(ANDI(NEG(LSHRI(rr_subi(rb,0,4),3)),0x1f),8))));
							break;
						case 0x00111011001: // rotqmbi
							wr(rt,LSHR(rr(ra),EXTZ128(ANDI(NEG(rr_subi(rb,0,4)),0x7))));
							break;
						case 0x00001011110: // rotmah
							for (int i=0;i<8;i++) {
								wr_subi(rt,ASHRZ(rr_subi(ra,i,2),ANDI(NEG(rr_subi(rb,i,2)),0x1f)),i,2);
							}
							break;
						case 0x00001011010: // rotma
							for (int i=0;i<4;i++) {
								wr_subi(rt,ASHRZ(rr_subi(ra,i,4),ANDI(NEG(rr_subi(rb,i,4)),0x3f)),i,4);
							}
							break;
						case 0x01111011000: // heq
							k_if(B.CreateICmpEQ(rr_subi(ra,0,4),rr_subi(rb,0,4)),[&]() {
								mkerr("heq halt");
								//B.CreateCall(F_spu_halt,ci32(ip));
							},[&](){});
							break;
						case 0x01001011000: // hgt
							k_if(B.CreateICmpSGT(rr_subi(ra,0,4),rr_subi(rb,0,4)),[&]() {
								mkerr("hgt halt");
								//B.CreateCall(F_spu_halt,ci32(ip));
							},[&](){});
							break;
						case 0x01011011000: // hlgt
							k_if(B.CreateICmpUGT(rr_subi(ra,0,4),rr_subi(rb,0,4)),[&]() {
								mkerr("hlgt halt");
								//B.CreateCall(F_spu_halt,ci32(ip));
							},[&](){});
							break;
						case 0x01111010000: // ceqb
							foreach_binop2(rt,ra,rb,1,CEQ);
							break;
						case 0x01111001000: // ceqh
							foreach_binop2(rt,ra,rb,2,CEQ);
							break;
						case 0x01111000000: // ceq
							foreach_binop2(rt,ra,rb,4,CEQ);
							break;
						case 0x01001010000: // cgtb
							foreach_binop2(rt,ra,rb,1,CGT);
							break;
						case 0x01001001000: // cgth
							foreach_binop2(rt,ra,rb,2,CGT);
							break;
						case 0x01001000000: // cgt
							foreach_binop2(rt,ra,rb,4,CGT);
							break;
						case 0x01011010000: // clgtb
							foreach_binop2(rt,ra,rb,1,CLGT);
							break;
						case 0x01011001000: // clgth
							foreach_binop2(rt,ra,rb,2,CLGT);
							break;
						case 0x01011000000: // clgtq
							foreach_binop2(rt,ra,rb,4,CLGT);
							break;
						case 0x01011000100: // fa
							f_foreach_binop2(rt,ra,rb,FADD);
							break;
						case 0x01011001100: // dfa
							df_foreach_binop2(rt,ra,rb,FADD);
							break;
						case 0x01011000101: // fs
							f_foreach_binop2(rt,ra,rb,FSUB);
							break;
						case 0x01011001101: // dfs
							df_foreach_binop2(rt,ra,rb,FSUB);
							break;
						case 0x01011000110: // fm
							f_foreach_binop2(rt,ra,rb,FMUL);
							break;
						case 0x01011001110: // dfm
							df_foreach_binop2(rt,ra,rb,FMUL);
							break;
						case 0x01101011100: // dfma
							df_foreach_binop3(rt,ra,rb,rt,FMADD);
							break;
						case 0x01101011110: // dfnms
							df_foreach_binop3(rt,ra,rb,rt,FNMSUB);
							break;
						case 0x01101011101: // dfms
							df_foreach_binop3(rt,ra,rb,rt,FMSUB);
							break;
						case 0x01101011111: // dfnma
							df_foreach_binop3(rt,ra,rb,rt,FNMADD);
							break;
						case 0x00110111000: // frest
							for (int i=0;i<4;i++) {
								wrf_subi(rt,FDIV(ConstantFP::get(t_float,1.0),rrf_subi(ra,i)),i);
							}
							break;
						case 0x00110111001: // frsqest
							mkerr("frsqest");
							break;
						case 0x01111010100: // fi
							for (int i=0;i<4;i++) {
								wrf_subi(rt,rrf_subi(ra,i),i);
							}
							break;
						case 0x01110111001: // frds
							for (int i=0;i<2;i++) {
								wrf_subi(rt,SINGLE(rrdf_subi(ra,i)),i*2);
								wr_subi(rt,ci32(0),i*2+1,4);
							}
							break;
						case 0x01110111000: // fesd
							for (int i=0;i<2;i++) {
								wrdf_subi(rt,DOUBLE(rrf_subi(ra,i*2)),i);
							}
							break;
						default:
							found = false;
					}
					if (found) break;
					found=true;
					ra = rrr_ra;
					rb = rrr_rb;
					rc = rrr_rc;
					rt = rrr_rt;
					switch (hexbin(rrr_op)) {
						case 0x1100: // mpya
							foreach_binop3(rt,ra,rb,rc,4,MPYA);
							break;
						case 0x1000: { // selb
							Value*a = rr(ra);
							Value*b = rr(rb);
							Value*c = rr(rc);
							wr(rt,OR(AND(c,b),AND(NOT(c),a)));
							break;
						}
						case 0x1011: // shufb
							tmp = OR(SHLI(EXTZ256(rr(ra)),128),EXTZ256(rr(rb)));

							for (int i=0;i<16;i++) {
								Value*b = rr_subi(rc,i,1);
								Value*b_01 = LSHRI(b,6);
								Value*b_02 = LSHRI(b,5);
								Value*c = B.CreateSelect(B.CreateICmpEQ(b_01,ci8(2)),ci8(0),
									B.CreateSelect(B.CreateICmpEQ(b_02,ci8(6)),ci8(0xff),
									B.CreateSelect(B.CreateICmpEQ(b_02,ci8(7)),ci8(0x80),

									// this select is to work around a bug in llvm (0 shifts do not work for big integers)
									B.CreateSelect(B.CreateICmpEQ(b,ci8(0x1f)),TRUNC(tmp,1),
									TRUNC(LSHR(tmp,EXTZ256(SUB(ci8(248),MULI(ANDI(b,0x1f),8)))),1)))));
								wr_subi(rt,c,i,1);
							}
							break;
						case 0x1110: // fma
							f_foreach_binop3(rt,ra,rb,rc,FMADD);
							break;
						case 0x1101: // fnms
							f_foreach_binop3(rt,ra,rb,rc,FNMSUB);
							break;
						case 0x1111: // fms
							f_foreach_binop3(rt,ra,rb,rc,FMSUB);
						default:
							found = false;
					}
					if (found) break;
					found=true;
					ra = rr_ra;
					rt = rr_rt;
					auto C = bit_11;
					auto D = bit_12;
					auto E = bit_13;
					auto stop_and_signal_type = bit_18_31;
					auto ca = bit_18_24;
					switch (hexbin(rr_op)) {
						case 0x00110101000: // bi
							if (ra==0 && enable_ret_opt) {
								do_ret();
								stop=true;
							} else {
								Value*f = B.CreateCall(F_spu_get_function,ANDI(AND(rr_subi(rt,0,4),lslr()),-4));
								do_tail_call(B.CreateBitCast(f,p_FT));
								stop=true;
							}
							break;
						case 0x00110101010: // iret
							mkerr("iret");
							do_ret();
							stop=true;
							break;
						case 0x00110101011: // bisled
							mkerr("bisled");
							break;
						case 0x00110101001: // bisl
							{
								Value*f = B.CreateCall(F_spu_get_function,ANDI(AND(rr_subi(ra,0,4),lslr()),-4));
								do_call(B.CreateBitCast(f,p_FT),rt);
							}
							break;
						case 0x00100101000: // biz
							mkerr("biz");
							break;
						case 0x00100101001: // binz
							tmp = B.CreateICmpNE(rr_subi(rt,0,4),ci32(0));
							if (ra==0 && enable_ret_opt) {
								BasicBlock*bb_yes=new_bb(),*bb_no=new_bb();
								B.CreateCondBr(tmp,bb_yes,bb_no);
								switch_bb(bb_yes);
								do_ret();
								switch_bb(bb_no);
							} else {
								mkerr("binz");
							}
							break;
							break;
						case 0x00100101010: // bihz
							mkerr("bihz");
							break;
						case 0x00100101011: // bihnz
							tmp = B.CreateICmpNE(rr_subi(rt,1,2),ci16(0));
							if (ra==0 && enable_ret_opt) {
								BasicBlock*bb_yes=new_bb(),*bb_no=new_bb();
								B.CreateCondBr(tmp,bb_yes,bb_no);
								switch_bb(bb_yes);
								do_ret();
								switch_bb(bb_no);
							} else {
								mkerr("bihnz");
							}
							break;
						case 0x00000000000: // stop
							B.CreateCall2(F_spu_stop,ci32(stop_and_signal_type),ci32((ip+4)&lslr_value));
							if (is_raw_spu) {
								mkerr("F_spu_stop returned");
								stop=true;
								do_ret();
							}
							break;
						case 0x00101000000: // stopd
							mkerr("stopd");
							break;
						case 0x00000000001: // lnop
						case 0x01000000001: // nop
							break;
						case 0x00000000010: // sync
							B.CreateCall(F_spu_restart,ci32((ip+4)&lslr_value));
							mkerr("F_spu_restart returned");
							stop = true;
							do_ret();
							break;
						case 0x00000000011: // dsync
							B.CreateCall5(F_llvm_memory_barrier,ci1(1),ci1(1),ci1(1),ci1(1),ci1(0));
							break;
						case 0x00000001100: // mfspr
							mkerr("mfspr");
							break;
						case 0x00100001100: // mtspr
							mkerr("mtspr");
							break;
						case 0x00110101100: // hbr
							break;
						case 0x00000001101: // rdch
							tmp = rdch(ca);
							if (tmp->getType()==t_i128) wr(rt,tmp);
							else if (tmp->getType()==t_i32) {
								wr_subi(rt,tmp,0,4);
								wr_subi(rt,ci32(0),1,4);
								wr_subi(rt,ci32(0),2,4);
								wr_subi(rt,ci32(0),3,4);
							} else xcept("rdch returned unknown type");
							break;
						case 0x00000001111: // rchcnt
							tmp = rchcnt(ca);
							wr_subi(rt,tmp,0,4);
							wr_subi(rt,ci32(0),1,4);
							wr_subi(rt,ci32(0),2,4);
							wr_subi(rt,ci32(0),3,4);
							break;
						case 0x00100001101: // wrch
							wrch(ca,rr(rt));
							break;
						default:
							found = false;
					}
					if (found) break;

					bad_insn();
					stop=true;
					do_ret();
				} while (false);
				ip += 4;
			}
		}
// 			{
// 				printf("dumping...\n");
// 				std::string errstr;
// 				raw_fd_ostream fd("out.s",errstr);
// 				if (errstr.size()) printf("failed to open: %s\n",errstr.c_str());
// 				F->print(fd,0);
// 				printf("dumped to out.s\n");
// 			}
		//F->dump();
		verifyFunction(*F);
		leave_function();
	};

	ArrayType*t_g_ls = ArrayType::get(t_i8,ls_size);
	g_ls = new GlobalVariable(t_g_ls,false,GlobalVariable::ExternalLinkage,0,"g_ls");
	mod->getGlobalList().push_back(g_ls);
	ArrayType*t_g_context = ArrayType::get(t_i128,r_count);
	g_context = new GlobalVariable(t_g_context,false,GlobalVariable::ExternalLinkage,ConstantAggregateZero::get(t_g_context),"g_context");
	mod->getGlobalList().push_back(g_context);
	ArrayType*t_g_channel = ArrayType::get(t_i128,channel_r_size);
	g_channel = new GlobalVariable(t_g_channel,false,GlobalVariable::ExternalLinkage,ConstantAggregateZero::get(t_g_channel),"g_channel");
	mod->getGlobalList().push_back(g_channel);


	//ExecutionEngine*EE = EngineBuilder(mod).setEngineKind(EngineKind::JIT).setOptLevel(CodeGenOpt::Aggressive).create();
	ExecutionEngine*EE = EngineBuilder(mod).setEngineKind(EngineKind::JIT).create();
	//ExecutionEngine*EE = EngineBuilder(mod).setEngineKind(EngineKind::JIT).setOptLevel(CodeGenOpt::None).create();

	EE->addGlobalMapping(g_ls,s.image_data);

	FunctionPassManager FPM(mod);
	//PassManager MPM;

	TargetData*TD = new TargetData(*EE->getTargetData());

	FPM.add(TD);

	PassManagerBuilder pm_builder;
	pm_builder.OptLevel = 3;

	pm_builder.populateFunctionPassManager(FPM);
	//pm_builder.populateModulePassManager(MPM);

	FPM.add(createPromoteMemoryToRegisterPass());
	FPM.add(createInstructionCombiningPass());
	FPM.add(createReassociatePass());
	FPM.add(createGVNPass());
	FPM.add(createCFGSimplificationPass());

	FPM.add(createScalarReplAggregatesPass());
	FPM.add(createConstantPropagationPass());	

	FPM.add(createDeadInstEliminationPass());
	FPM.add(createDeadStoreEliminationPass());
	FPM.add(createAggressiveDCEPass());
	FPM.add(createDeadCodeEliminationPass());
	FPM.add(createLICMPass());
	FPM.add(createBlockPlacementPass());
	FPM.add(createLCSSAPass());
	//FPM.add(createEarlyCSEPass());
	FPM.add(createGVNPass());
	FPM.add(createMemCpyOptPass());
	FPM.add(createLoopDeletionPass());
	FPM.add(createSimplifyLibCallsPass());
	FPM.add(createCorrelatedValuePropagationPass());
	FPM.add(createSinkingPass());
	//FPM.add(createInstructionSimplifierPass());

	FPM.doInitialization();

	//JITExceptionHandling = true;

	args.clear();
	Function*I_call_lr = Function::Create(FunctionType::get(t_void,false),Function::ExternalLinkage,"I_call_lr",mod);
	{
		F = I_call_lr;
		switch_bb(new_bb());
		enter_function();

		Value*f = B.CreateCall(F_spu_get_function,rr_subi(0,0,4));		
		do_tail_call(B.CreateBitCast(f,p_FT));

		leave_function();
		verifyFunction(*F);
	}
	FPM.run(*I_call_lr);
	s.call_lr = (void(*)())EE->getPointerToFunction(I_call_lr);

	args.clear();
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	args.push_back(t_i64);
	Function*I_set_args = Function::Create(FunctionType::get(t_void,args,false),Function::ExternalLinkage,"I_set_args",mod);
	{
		F = I_set_args;
		switch_bb(new_bb());
		enter_function();

		auto a = F->getArgumentList().begin();
		Value*arg1 = (Value*)a++;
		Value*arg2 = (Value*)a++;
		Value*arg3 = (Value*)a++;
		Value*arg4 = (Value*)a++;
		wr_subi(3,arg1,0,8);
		wr_subi(3,ci64(0),1,8);
		wr_subi(4,arg2,0,8);
		wr_subi(4,ci64(0),1,8);
		wr_subi(5,arg3,0,8);
		wr_subi(5,ci64(0),1,8);
		wr_subi(6,arg4,0,8);
		wr_subi(6,ci64(0),1,8);

		do_ret();

		leave_function();
		verifyFunction(*F);
	}
	FPM.run(*I_set_args);
	(void*&)s.set_args = EE->getPointerToFunction(I_set_args);

	while (true) {
		SwitchToFiber(s.ret_fiber);
		if (s.request==spuc::req_exit) break;
		if (s.request==spuc::req_sync) {
			printf(" -- SPU -- sync\n");
			for (auto i=func_map.begin();i!=func_map.end();++i) {
				Function*f = i->second->f;
				EE->freeMachineCodeForFunction(f);
			}
			for (auto i=func_map.begin();i!=func_map.end();++i) {
				Function*f = i->second->f;
				//f->removeFromParent();
				//f->eraseFromParent(); // fixme: This should be called, but causes a debug
				                        //        assertion on free (memory is modified
				                        //        after it is freed).
				delete i->second;
			}
			func_map.clear();
// 			{
// 				printf("dumping...\n");
// 				std::string errstr;
// 				raw_fd_ostream fd("out.s",errstr);
// 				if (errstr.size()) printf("failed to open: %s\n",errstr.c_str());
// 				mod->print(fd,0);
// 				printf("dumped to out.s\n");
// 			}
			continue;
		}
		if (s.request!=spuc::req_compile) xcept("bad s.request");
		printf(" -- SPU -- compiling function at %#x\n",(int)s.compile_offset);
		function_info&fi = get_function(s.compile_offset);
		fi.f->setLinkage(Function::ExternalLinkage);

		auto do_functions = [&]() {
			printf("compiling...\n");
			insn_count = 0;
			while (!func_stack.empty()) {
				function_info&fi = *func_stack.top();
				func_stack.pop();

				do_function(fi);
			}
			printf("%lld instructions processed, %lldK\n",(long long)insn_count,(long long)(insn_count*4/1024));
		};
		do_functions();

		printf("%d new functions\n",(int)new_functions.size());
		printf("optimizing...\n");

		for (auto i=new_functions.begin();i!=new_functions.end();++i) {
			Function*f = (*i)->f;
			FPM.run(*f);
		}

		new_functions.clear();

		// We cannot run module passes, since they delete unreferenced data and
		// functions that we may need in the future.
		//MPM.run(*mod);

// 		{
// 			printf("dumping...\n");
// 			std::string errstr;
// 			raw_fd_ostream fd("out.s",errstr);
// 			if (errstr.size()) printf("failed to open: %s\n",errstr.c_str());
// 			mod->print(fd,0);
// 			printf("dumped to out.s\n");
// 		}

		printf("generating code...\n");
		void*f = EE->getPointerToFunction(fi.f);
		printf("done\n");
		fi.compiled_function = f;
		s.compiled_function = f;
	}

	FPM.doFinalization();

	delete EE;


}
