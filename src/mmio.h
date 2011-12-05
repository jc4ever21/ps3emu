
#include "udis86.h"

#pragma comment(lib,"libudis86.lib")

template<typename t>
void raw_spu_mmio_write(uint32_t offset,t val);
template<typename t>
t raw_spu_mmio_read(uint32_t offset);

namespace mmio {

	std::vector<uint32_t> addr_list; 
	boost::mutex addr_list_mutex;

//#define MMIO_DEBUG

	template<typename t>
	void mmio_w(t*addr,t val) {
		//dbgf("mmio write %#x to %p\n",val,addr);
		if ((uint64_t)addr>>28==0xe) {
			raw_spu_mmio_write<t>((uint64_t)addr - 0xe0000000,val);
		} else *addr = val;
	}
	template<typename t>
	uint64_t mmio_r(t*addr) {
		//dbgf("mmio read from %p\n",addr);
		if ((uint64_t)addr>>28==0xe) {
			return raw_spu_mmio_read<t>((uint64_t)addr - 0xe0000000);
		} else return *addr;
	}

	void*exec_mem_allocate(size_t size) {
		boost::unique_lock<boost::mutex> m(addr_list_mutex);
		if (addr_list.empty()) {
			uint32_t a = user_mem_mgr.allocate(0x10000,0x10000);
			if (!a) xcept("failed to allocate memory for hook");
			DWORD old_prot;
			VirtualProtect((void*)a,0x10000,PAGE_EXECUTE_READWRITE,&old_prot);
			for (uint32_t i=a;i<a+0x10000;i+=0x80) {
				addr_list.push_back(i);
			}
		}
		uint32_t r = addr_list.back();
		addr_list.pop_back();
		return (void*)r;
	}
	void exec_mem_setsize(void*ptr,size_t size) {

	}

	void**func_pointers;
	void init() {
		func_pointers = (void**)exec_mem_allocate(0x80);
		func_pointers[0] = &mmio_r<uint8_t>;
		func_pointers[1] = &mmio_r<uint16_t>;
		func_pointers[2] = &mmio_r<uint32_t>;
		func_pointers[3] = &mmio_r<uint64_t>;
		func_pointers[4] = &mmio_w<uint8_t>;
		func_pointers[5] = &mmio_w<uint16_t>;
		func_pointers[6] = &mmio_w<uint32_t>;
		func_pointers[7] = &mmio_w<uint64_t>;
	}

	struct codegen {
		char*dst;
		template<typename t>
		void w(const t&v) {
			*((t*&)dst)++ = v;
		}
		struct modrm {
			int mod,reg,rm;
			int64_t imm;
			int scale,index,base;
			modrm() : mod(0), reg(0), rm(0), imm(0), scale(0), index(0), base(0) {}
		};
		struct rex {
			int w,r,x,b;
			rex() : w(0), r(0), x(0), b(0) {}
		};
		void emit_modrm(const modrm&m) {
			w<uint8_t>(m.mod<<6|m.reg<<3|m.rm);
			if (m.mod==3) return;
			if (m.rm==4) w<uint8_t>(m.scale<<6|m.index<<3|m.base);
			if (m.mod==1) {
				if ((uint8_t)m.imm!=m.imm) xcept("immediate value does not fit in 8 bits");
				w<uint8_t>(m.imm);
			} else if (m.mod==2 || (m.mod==0&&m.base==5)) {
				if ((uint32_t)m.imm!=m.imm) xcept("immediate value does not fit in 32 bits");
				w<uint32_t>(m.imm);
			}
			if (m.mod==0&&m.rm==5) {
				int64_t rel = m.imm - (uint64_t)dst;
				if ((int32_t)rel!=rel) xcept("relative address (%#x) does not fit in 32 bits",rel);
				w<uint32_t>(rel);
			}
		}
		void emit_rex(const rex&x) {
			w<uint8_t>(4<<4|x.w<<3|x.r<<2|x.x<<1|x.b);
		}
		void emit_modrm_insn(uint8_t opcode,int dst,int base,int index,int scale,int64_t imm) {
			rex x;
			modrm m;
			bool prefix_66=false,prefix_67=false;
			if (dst<UD_R_AL||dst>UD_R_R15) xcept("bad dst");
			if (dst<UD_R_AX) xcept("fixme: dst<UD_R_AX");
			if (dst<UD_R_EAX) prefix_66=true;
			else if (dst>=UD_R_RAX) x.w=1;
			dst = (dst-UD_R_AX)&15;
			if (dst<8) m.reg = dst;
			else {m.reg = dst-8; x.r=1;}
			if (scale==0) scale=1;
			if (base==UD_NONE&&index==UD_NONE) {
				if ((uint64_t)imm!=(uint32_t)imm) xcept("offset does not fit in 32 bits");
				m.mod = 0; m.rm = 5;
				m.imm = imm;
			} else {
				if ((int32_t)imm!=imm) xcept("offset does not fit in 32 bits");
				if (base!=UD_NONE) {
					if (base<UD_R_EAX||base>UD_R_R15) xcept("bad base");
					if (base<UD_R_RAX) prefix_67=true;
					base = (base-UD_R_AX)&15;
					if (base<8) m.rm = base;
					else {m.rm = dst-8; x.b=1;}
				}
				if (index==UD_NONE) {
					if (m.rm==4) {
						m.base = 4;
						m.index = 4;
						m.mod = 0;
						m.scale = 0;
					}
				} else {
					if (index<UD_R_EAX||index>UD_R_R15) xcept("bad base");
					if (index<UD_R_RAX) prefix_67=true;
					index = (index-UD_R_AX)&15;
					if (index<8) m.index = base;
					else {m.index = dst-8; x.x=1;}
					if (base==UD_NONE||scale>1||imm) {
						m.rm = 4;
						m.base = 5;
						m.mod = 0;
						m.imm = imm;
						if (scale==1) m.scale=0;
						else if (scale==2) m.scale=1;
						else if (scale==4) m.scale=2;
						else m.scale=3;
					}
				}
				if (imm) {
					if ((int8_t)imm==imm) m.mod=1;
					else m.mod=2;
					m.imm = imm;
				}
			}

			if (prefix_66) w<uint8_t>(0x66);
			if (prefix_67) w<uint8_t>(0x67);
			if (x.w|x.r|x.x|x.b) emit_rex(x);
			w(opcode);
			emit_modrm(m);
		}
		int emit_reg_insn(uint8_t opcode,int r) {
			rex x;
			int bits;
			if (r<UD_R_AX||r>UD_R_R15) xcept("bad reg");
			if (r<UD_R_EAX) bits=16;
			else if (r<UD_R_RAX) bits=32;
			else bits=64;
			r = (r-UD_R_AX)&15;
			if (r>=8) x.b=1;
			if (bits==16) {
				w<uint8_t>(0x66);
				if (x.w|x.r|x.x|x.b) emit_rex(x);
				w<uint8_t>(opcode+(r&7));
			} else if (bits==32) {
				if (x.w|x.r|x.x|x.b) emit_rex(x);
				w<uint8_t>(opcode+(r&7));
			} else {
				x.w = 1;
				emit_rex(x);
				w<uint8_t>(opcode+(r&7));
			}
			return bits;
		}
		void mk_mov_imm(int dst,uint64_t val) {
			int bits = emit_reg_insn(0xb8,dst);
			if (bits==16) {
				if ((uint16_t)val!=val) xcept("immediate does not fit in 16 bits");
				w<uint16_t>(val);
			} else if (bits==32) {
				if ((uint32_t)val!=val) xcept("immediate does not fit in 32 bits");
				w<uint32_t>(val);
			} else {
				w<uint64_t>(val);
			}
		}
		void mk_lea(int dst,int base,int index,int scale,uint64_t offset) {
			if (base==UD_NONE&&index==UD_NONE) {
				mk_mov_imm(dst,offset);
			} else emit_modrm_insn(0x8d,dst,base,index,scale,offset);
		}
		void mk_push_reg(int r) {
			emit_reg_insn(0x50,r);
		}
		void mk_pop_reg(int r) {
			emit_reg_insn(0x58,r);
		}
		void mk_jmp(uint64_t addr) {
			int64_t rel = addr - (uint64_t)dst - 5;
			if ((int32_t)rel==rel) {
				w<uint8_t>(0xe9);
				w<int32_t>(rel);
			} else {
				if ((uint32_t)addr==addr) {
					w<uint8_t>(0x66);
					w<uint8_t>(0xff);
					w<uint8_t>(0x25);
					w<uint32_t>(0);
					w<uint32_t>(addr);
				} else {
					w<uint8_t>(0xff);
					w<uint8_t>(0x25);
					w<uint32_t>(0);
					w<uint64_t>(addr);
				}
			}
		}
		int reg_bits(int r) {
			if (r<UD_R_AL||r>UD_R_R15) xcept("bad reg");
			if (r<UD_R_AX) return 8;
			if (r<UD_R_EAX) return 16;
			if (r<UD_R_RAX) return 32;
			return 64;
		}
		void mk_mov_reg(int dst,int src) {
			if (reg_bits(dst)!=reg_bits(src)) xcept("mov dst/src mismatching size");
			rex x;
			modrm m;
			m.mod = 3;
			bool prefix_66=false;
			uint8_t opcode = 0x8b;
			if (dst<UD_R_AL||dst>UD_R_R15) xcept("bad dst");
			if ((src>=UD_R_AH&&src<=UD_R_BH)!=(dst>=UD_R_AH&&dst<=UD_R_BH)) xcept("invalid instruction");
			if ((src>=UD_R_AH&&src<=UD_R_BH)&&(dst>=UD_R_AH&&dst<=UD_R_BH)) {
				src -= UD_R_AL;
				dst -= UD_R_AL;
				m.reg = dst;
				m.rm = src;
			} else {
				if (dst<UD_R_AX) {
					opcode = 0x8a;
					if (dst>=UD_R_SPL) dst = (dst-4-UD_R_AL)+UD_R_AX;
					else dst = (dst-UD_R_AL)+UD_R_AX;
				}
				else if (dst<UD_R_EAX) prefix_66=true;
				else if (dst>=UD_R_RAX) x.w=1;
				dst = (dst-UD_R_AX)&15;
				if (dst<8) m.reg = dst;
				else {m.reg = dst-8; x.r=1;}
				if (src<UD_R_AX) {
					if (src>=UD_R_SPL) src = (src-4-UD_R_AL)+UD_R_AX;
					else src = (src-UD_R_AL)+UD_R_AX;
				}
				src = (src-UD_R_AX)&15;
				if (src<8) m.rm = src;
				else {m.rm = src-8; x.b=1;}
			}
			if (x.w|x.r|x.x|x.b) emit_rex(x);
			w<uint8_t>(opcode);
			emit_modrm(m);
		}
		void mk_call_rel(uint64_t addr) {
			int64_t rel = addr - (uint64_t)dst - 5;
			if ((int32_t)rel==rel) {
				w<uint8_t>(0xe8);
				w<int32_t>(rel);
			} else xcept("call relative address does not fit in 32 bits!");
		}
		void mk_call_mem64_rel(uint64_t addr) {
			int64_t rel = addr - (uint64_t)dst - 6;
			if ((int32_t)rel==rel) {
				w<uint8_t>(0xff);
				w<uint8_t>(0x15);
				w<int32_t>(rel);
			} else xcept("call (mem) relative address does not fit in 32 bits!");
		}
		void mk_add_reg_imm(int r,int64_t imm) {
			rex x;
			modrm m;
			if (r<UD_R_RAX||r>UD_R_RDI) xcept("add_reg_imm unsupported reg"); // I'm lazy.
			m.mod = 3;
			m.reg = 0;
			m.rm = r-UD_R_RAX;
			x.w = 1;
			if ((int8_t)imm==imm) {
				emit_rex(x);
				w<uint8_t>(0x83);
				emit_modrm(m);
				w<int8_t>(imm);
			} else {
				if ((int32_t)imm!=imm) xcept("imm does not fit in 32 bits");
				emit_rex(x);
				w<uint8_t>(0x81);
				emit_modrm(m);
				w<int32_t>(imm);
			}
		}
		int reg_n(int r) {
			if (r<UD_R_AX) {
				if (r>=UD_R_SPL) r = (r-4-UD_R_AL)+UD_R_AX;
				else r = (r-UD_R_AL)+UD_R_AX;
			}
			return (r-UD_R_AX)&15;
		}
		void mk_mov_r_rm(int r,int base,int index,int scale,uint64_t offset) {
			if (base==UD_NONE&&index==UD_NONE) {
				mk_mov_imm(r,offset);
			} else {
				if (reg_bits(r)==8) emit_modrm_insn(0x8a,r,base,index,scale,offset);
				else emit_modrm_insn(0x8b,r,base,index,scale,offset);
			}
		}
	};

	void*mmio_hook(void*ptr,int required_bytes) {
		ud u;
		ud_init(&u);
		ud_set_mode(&u,64);
		ud_set_input_buffer(&u,(uint8_t*)ptr,0x100);
		ud_set_pc(&u,(uint64_t)ptr);

		char*hook_entry = (char*)exec_mem_allocate(0x80);
#ifdef MMIO_DEBUG
		dbgf("hook_entry is %p\n",hook_entry);
#endif

		codegen cg;
		cg.dst = (char*)hook_entry;

		int volatile_regs[] = {UD_R_RAX,UD_R_RCX,UD_R_RDX,UD_R_R8,UD_R_R9,UD_R_R10,UD_R_R11};

		bool nojmp = false;
		size_t size = 0;
		while (size<required_bytes) {
			unsigned int isize = ud_decode(&u);
			if (size==0) {
				const char*asm_str = 0;
#ifdef MMIO_DEBUG
				dbgf("mmio hook -- \n");
				ud_translate_intel(&u);
				asm_str = ud_insn_asm(&u);
				dbgf(" %p :: %-16s  %s\n",(void*)ud_insn_off(&u),ud_insn_hex(&u),asm_str);
#endif

				void*addr = (void*)ud_insn_off(&u);

				for (int i=0;i<sizeof(volatile_regs)/sizeof(volatile_regs[0]);i++) {
					int r = volatile_regs[i];
					cg.mk_push_reg(r);
				}
				
				auto&op0 = u.operand[0];
				auto&op1 = u.operand[1];
				switch (u.mnemonic) {
				case UD_Imov:
				case UD_Imovzx:
					break;
				default:
					if (!asm_str) {
						ud_translate_intel(&u);
						asm_str = ud_insn_asm(&u);
					}
					xcept("%p :: %-16s  %s -- instruction not supported for mmio",addr,ud_insn_hex(&u),asm_str);
				}
				bool is_read = op1.type==UD_OP_MEM;
				auto&reg = is_read ? op0 : op1;
				auto&mem = is_read ? op1 : op0;
				if (reg.type!=UD_OP_REG) xcept("bad reg type");
				if (mem.type!=UD_OP_MEM) xcept("bad mem type");
				int val_reg = reg.base;
				int addr_reg = UD_R_RCX;
				int reg2=0;
				int reg_size = cg.reg_bits(val_reg);
				int mem_size = mem.size;
				switch (mem_size) {
				case 8: reg2=UD_R_DL;break;
				case 16: reg2=UD_R_DX;break;
				case 32: reg2=UD_R_EDX;break;
				case 64: reg2=UD_R_RDX;break;
				}
				cg.mk_lea(addr_reg,mem.base,mem.index,mem.scale,
					mem.offset==8?mem.lval.sbyte:
					mem.offset==16?mem.lval.sword:
					mem.offset==32?mem.lval.sdword:
					mem.lval.sqword);
				if (!is_read && reg2!=val_reg) {
					if (cg.reg_n(addr_reg)==cg.reg_n(val_reg)) {
						// The order we pushed the registers in leaves rcx at [rsp+8*5]
						cg.mk_mov_r_rm(reg2,UD_R_RSP,0,0,8*5);
					} else cg.mk_mov_reg(reg2,val_reg);
				}
				if (is_read) {
					void*faddr=0;
					switch (mem_size) {
					case 8: faddr = &func_pointers[0];break;
					case 16: faddr = &func_pointers[1];break;
					case 32: faddr = &func_pointers[2];break;
					case 64: faddr = &func_pointers[3];break;
					}
					// Microsoft calling convention.
					// We must make room for the arguments (minimum 4, hence 32
					// bytes) in the stack, even tho they're passed in registers.
					// The stack must also be 16-byte aligned (which it
					// will be after call pushes RIP).
					cg.mk_add_reg_imm(UD_R_RSP,-32);
					cg.mk_call_mem64_rel((uint64_t)faddr);
					cg.mk_add_reg_imm(UD_R_RSP,32);
				} else {
					void*faddr=0;
					switch (mem_size) {
					case 8: faddr = &func_pointers[4];break;
					case 16: faddr = &func_pointers[5];break;
					case 32: faddr = &func_pointers[6];break;
					case 64: faddr = &func_pointers[7];break;
					}
					cg.mk_add_reg_imm(UD_R_RSP,-32);
					cg.mk_call_mem64_rel((uint64_t)faddr);
					cg.mk_add_reg_imm(UD_R_RSP,32);
				}

				for (int i=sizeof(volatile_regs)/sizeof(volatile_regs[0]);i>0;i--) {
					int r = volatile_regs[i-1];
					if (r==UD_R_RAX && is_read) {
						if (val_reg==UD_R_RAX||val_reg==UD_R_EAX) {
							cg.mk_add_reg_imm(UD_R_RSP,8);
						} else if (val_reg==UD_R_AX||val_reg==UD_R_AH||val_reg==UD_R_AL) {
							// Insert code to eg. xchg rax, [rsp]; mov al/ah/ax, [rsp]; add rsp, 8.
							// This is because we must leave the most significant bits untouched.
							xcept("dst rax fixme");
						} else {
							cg.mk_mov_reg(val_reg,reg2-2);
							cg.mk_pop_reg(r);
						}
					} else cg.mk_pop_reg(r);
				}
			} else {
#ifdef MMIO_DEBUG
				ud_translate_intel(&u);
				dbgf(" %p :: %-16s  %s\n",(void*)ud_insn_off(&u),ud_insn_hex(&u),ud_insn_asm(&u));
#endif
				auto&op0 = u.operand[0];
				auto&op1 = u.operand[1];
				if (op0.type==UD_OP_JIMM) {
					uint8_t*p = (uint8_t*)u.insn_offset;
					// Skip prefixes, since we must remove them anyways.
					// (and only 0x66 has an effect anyways)
					while (*p==0x67 || *p==0x66 || (*p&~0xf)==0x40) {p++;isize--;}
					uint8_t opcode = *p;
					uint64_t addr = (uint64_t)(u.pc + (op0.size==8?op0.lval.sbyte:op1.size==16?op0.lval.sword:op0.size==32?op0.lval.sdword:op0.lval.sqword));
					// If the destination is moved too, don't do anything
					if (addr<(uint64_t)hook_entry||addr>(uint64_t)hook_entry+5) {
						if (op0.size==64) {
							xcept("64-bit relative branches do not exist!");
						} else if (op0.size==32) {
							// Just copy everything but the offset for 32-bit branches
							memcpy(cg.dst,p,isize-4);
							cg.dst+=isize-4;
						} else if (op0.size==16) {
							// 16-bit relative branches have the same opcode as 32-bit ones,
							// but with the 0x66 prefix. We already removed the prefix.
							memcpy(cg.dst,p,isize-2);
							cg.dst+=isize-2;
						} else {
							// Replace jmp short (0xeb) with jmp near (0xe9)
							if (opcode==0xeb) *((uint8_t*&)cg.dst)++ = 0xe9;
							// jcc short is [0x70,0x80)
							else if (opcode>=0x70&&opcode<0x80) {
								// jcc long is 0x0f [0x80,0x90)
								*((uint8_t*&)cg.dst)++ = 0x0f;
								*((uint8_t*&)cg.dst)++ = opcode+0x10;
							} else xcept("unknown 8-bit relative branch");
						}
						int64_t offset = addr - (uint64_t)(cg.dst+4);
						if ((uint64_t)(cg.dst+4)+(int32_t)offset!=addr) xcept("cannot move RIP-dependant instruction; offset (%#x) does not fit in 32 bits",offset);
						*((int32_t*&)cg.dst)++ = offset;
					} else {
						memcpy(cg.dst,(void*)u.insn_offset,isize);
						cg.dst += isize;
					}
				} else if ((op0.type==UD_OP_MEM&&op0.base==UD_R_RIP) || (op1.type==UD_OP_MEM&&op1.base==UD_R_RIP)) {
					// Instruction has modrm byte with RIP-relative addressing. Since the offset is always
					// the last 4 bytes, updating this is trivial.
					uint8_t*p = (uint8_t*)u.insn_offset;
					memcpy(cg.dst,p,isize-4);
					uint64_t dst_addr = (u.pc+(int32_t&)p[isize-4]);
					int64_t offset = dst_addr - (uint64_t)(cg.dst+isize);
					if ((uint64_t)(cg.dst+isize)+(int32_t)offset!=dst_addr) xcept("cannot move RIP-dependant instruction; offset (%#x) does not fit in 32 bits",offset);
					(int32_t&)*(cg.dst+isize-4) = offset;
					cg.dst += isize;
				} else {
					memcpy(cg.dst,(void*)u.insn_offset,isize);
					cg.dst += isize;
				}
			}
			size += isize;
			if (u.mnemonic==UD_Ijmp||u.mnemonic==UD_Iret||u.mnemonic==UD_Iiretd||u.mnemonic==UD_Iiretq||u.mnemonic==UD_Iiretw||u.mnemonic==UD_Iretf) {
				if (size<required_bytes) {
					uint8_t*p = (uint8_t*)u.pc;
					while (*p==0||*p==0xcc) {++size;++p;}
					xcept("not enough space for hook at %p; function end at %p",ptr,p);
				} else nojmp=true;
			}
		}

		if (!nojmp) cg.mk_jmp((uint64_t)ptr + size);

#ifdef MMIO_DEBUG
		{
			char*p = hook_entry;
			ud_set_input_buffer(&u,(uint8_t*)p,0x100);
			ud_set_pc(&u,(uint64_t)p);
			dbgf("hook code -- \n");
			while (p<cg.dst) {
				p += ud_decode(&u);
				ud_translate_intel(&u);
				dbgf(" %p :: %-16s  %s\n",(void*)ud_insn_off(&u),ud_insn_hex(&u),ud_insn_asm(&u));
			}
		}
#endif

		exec_mem_setsize(hook_entry,cg.dst-hook_entry);

		return hook_entry;

// 		DWORD old_prot;
// 		VirtualProtect(ptr,5,PAGE_EXECUTE_READWRITE,&old_prot);
// 
// 		char*p = (char*)ptr;
//  		int64_t rel = hook_entry-(p+5);
//  		if ((int32_t)rel!=rel) xcept("relative jump does not fit in 32 bits");
// 		while (true) {
// 			uint64_t v = atomic_read((uint64_t*)p);
// 			uint64_t nv = v&~0xffffffffff;
// 			nv |= 0xe9 | (uint64_t)((uint32_t)rel)<<8;
// 			if (atomic_cas((uint64_t*)p,v,nv)==v) break;
// 		}
// 
// #ifdef MMIO_DEBUG
// 		{
// 			char*p = (char*)ptr;
// 			ud_set_input_buffer(&u,(uint8_t*)p,0x100);
// 			ud_set_pc(&u,(uint64_t)p);
// 			dbgf("new instr -- \n");
// 
// 			p += ud_decode(&u);
// 			ud_translate_intel(&u);
// 			dbgf(" %p :: %-16s  %s\n",(void*)ud_insn_off(&u),ud_insn_hex(&u),ud_insn_asm(&u));
// 		}
// #endif
// 
// 		FlushInstructionCache(GetCurrentProcess(),hook_entry,cg.dst-hook_entry);
// 		FlushInstructionCache(GetCurrentProcess(),ptr,5);
	}


	boost::shared_mutex lock;
	boost::unordered_map<uint64_t,void*> hook_map;

	LONG CALLBACK filter(EXCEPTION_POINTERS*ep) { 
		uint64_t iaddr = (uint64_t)ep->ExceptionRecord->ExceptionAddress;
		if (ep->ExceptionRecord->ExceptionCode==EXCEPTION_BREAKPOINT) {
			boost::shared_lock<boost::shared_mutex> l(lock);
			auto i = hook_map.find(iaddr);
			if (i==hook_map.end()) return EXCEPTION_CONTINUE_SEARCH;
			ep->ContextRecord->Rip = (DWORD64)i->second;
			return EXCEPTION_CONTINUE_EXECUTION;
		}
		if (ep->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;

		ULONG_PTR addr = ep->ExceptionRecord->ExceptionInformation[1];
		if (addr>>28==0xe) {
			dbgf("AV at %p, address %p\n",(void*)iaddr,(void*)addr);
			void*h = mmio_hook((void*)iaddr,1);
			{
				boost::unique_lock<boost::shared_mutex> l(lock);
				hook_map[iaddr] = h;
			}
			*(uint8_t*)iaddr = 0xcc;
			FlushInstructionCache(GetCurrentProcess(),(void*)iaddr,1);
			ep->ContextRecord->Rip = (DWORD64)h;
			return EXCEPTION_CONTINUE_EXECUTION;
		}
		else return EXCEPTION_CONTINUE_SEARCH;
	}
}

