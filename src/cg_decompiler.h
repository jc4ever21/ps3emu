
namespace vertex_program {
	std::string decompile(uint32_t*data) {

		dbgf("vertex program decompile %p\n",data);

		std::string body;
		int level = 1;
		auto stat = [&](std::string str) {
			for (int i=0;i<level;i++) body+='\t';
			body += str + ";\n";
		};
		boost::unordered_map<std::string,std::string> params;
		auto getparam = [&](std::string decl,std::string name) -> std::string {
			params[name] = decl;
			return name;
		};
		boost::unordered_map<std::string,std::string> tmps;
		int tmp_n = 0;
		auto gettmp = [&](std::string decl) -> std::string {
			std::string name = format("tmp%d",tmp_n++);
			tmps[name] = decl;
			return name;
		};
		auto func_str = [&]() -> std::string {
			std::string p, t;
			for (auto i=params.begin();i!=params.end();++i) {
				p += i->second;
				p += " ";
				p += i->first;
				p += ";\n";
			}
			for (auto i=tmps.begin();i!=tmps.end();++i) {
				t += "\t";
				t += i->second;
				t += " ";
				t += i->first;
				t += ";\n";
			}
			return format("#version 150\n%svoid main() {\n%s%s}\n",p.c_str(),t.c_str(),body.c_str());
		};
		boost::unordered_set<int> immediates;
		int errors = 0;
		int insn_count = 0;
		while (true) {

			uint32_t d0 = *data++;
			uint32_t d1 = *data++;
			uint32_t d2 = *data++;
			uint32_t d3 = *data++;

			dbgf(" -- %x %x %x %x\n",d0,d1,d2,d3);

			int cond_test_enable = d0>>13&1;
			d0 &= ~(1<<13);
			int exec_cond = d0>>10&7;
			d0 &= ~(7<<10);
			int cond_swizzle = d0>>2&0xff;
			d0 &= ~(0xff<<2);
			int abs0 = d0>>21&1;
			d0 &= ~(1<<21);
			int abs1 = d0>>22&1;
			d0 &= ~(1<<22);
			int abs2 = d0>>23&1;
			d0 &= ~(1<<23);

			int vec_result = d0>>30&1;
			d0 &= ~(1<<30);
			if (!vec_result) xcept("!vec_result");

			int set_cond1 = d0>>14&1;
			d0 &= ~(1<<14);
			int set_cond2 = d0>>29&1;
			d0 &= ~(1<<29);
			if (set_cond1) xcept("set_cond1"); // one of these is probably vec, the other sca
			if (set_cond2) xcept("set_cond2");
			int set_cond=0;

			int dst_tmp = d0>>15&0x3f;
			d0 &= ~(0x3f<<15);

			int addr_reg_select_1 = d0>>24&1;
			d0 &= ~(1<<24);
			if (addr_reg_select_1) xcept("addr_reg_select_1");
			int cond_reg_select_1 = d0>>25&1;
			d0 &= ~(1<<25);
			if (cond_reg_select_1) xcept("cond_reg_select_1");
			int saturate = d0>>26&1;
			d0 &= ~(1<<26);
			if (saturate) xcept("saturate");
			int index_input = d0>>27&1;
			d0 &= ~(1<<27);
			if (index_input) xcept("index_input");

			int opcode = d1>>22&0x1f;
			d1 &= ~(0x1f<<22);
			int const_src = d1>>12&0x1ff;
			d1 &= ~(0x1ff<<12);
			int input_src = d1>>8&0xf;
			d1 &= ~(0xf<<8);
			int src0 = (d1&0xff)<<9 | (d2>>23&0x1ff);
			d1 &= ~0xff;
			d2 &= ~(0x1ff<<23);
			int src1 = d2>>6&0x1ffff;
			d2 &= ~(0x1ffff<<6);
			int src2 = (d2&0x3f)<<11 | (d3>>21&0x7ff);
			d2 &= ~0x3f;
			d3 &= ~(0x7ff<<21);
			int writemask = d3>>13&0xf;
			d3 &= ~(0xf<<13);
			int dst = d3>>2&0x1f;
			d3 &= ~(0x1f<<2);

			int sca_dst_tmp = d3>>7&0x3f;
			d3 &= ~(0x3f<<7);

			int unk_d1_20 = d1>>20&1;
			d1 &= ~(1<<20);
			
			int last = d3&1;
			d3 &= ~1;

			if (d0) {outf("unknown bits set in d0 - %#x\n",d0);errors++;}
			if (d1) {outf("unknown bits set in d1 - %#x\n",d1);errors++;}
			if (d2) {outf("unknown bits set in d2 - %#x\n",d2);errors++;}
			if (d3) {outf("unknown bits set in d3 - %#x\n",d3);errors++;}

			auto dst_str = [&]() -> std::string {
				const char*table[] = {"gl_Position","col0","col1","bfc0","bfc1","fogc","gl_Pointsize",
					"tc0","tc1","tc2","tc3","tc4","tc5","tc6","tc7"};
				std::string v;
				switch (dst) {
				case 0: v="gl_Position";break;
				case 6: v="gl_PointSize";break;
				case 0x1f: xcept("todo temp dst");
				default:
					if (dst<15) {
						v=getparam("out vec4",table[dst]);
						break;
					}
					xcept("unknown dest %d",dst);
				}
				return v;
			};
			auto apply_writemask = [&](std::string s) -> std::string {
				if (writemask!=0xf) {
					s+='.';
					if (writemask&8) s+='x';
					if (writemask&4) s+='y';
					if (writemask&2) s+='z';
					if (writemask&1) s+='w';
				}
				return s;
			};
			auto tmp_src_str = [&](int n) -> std::string {
				return gettmp(format("tmp%u",n));
			};
			auto input_src_str = [&]() -> std::string {
				const char*table[] = {"in_pos","in_weight","in_normal","in_col0","in_col1","in_fogc","in_6","in_7",
					"in_tc0","in_tc1","in_tc2","in_tc3","in_tc4","in_tc5","in_tc6","in_tc7"};
				if (input_src<16) return getparam("in vec4",table[input_src]);
				else xcept("unknown input src %d",input_src);
			};
			auto const_src_str = [&]() -> std::string {
				return getparam("uniform vec4",format("vc%u",const_src));
			};
			auto src_str = [&](int n) -> std::string {
				int src = n==0?src0:n==1?src1:src2;
				int abs = n==0?abs0:n==1?abs1:abs2;
				int type = src&3;
				int tmp = src>>2&0x1f;
				int swz_w = src>>8&3;
				int swz_z = src>>10&3;
				int swz_y = src>>12&3;
				int swz_x = src>>14&3;
				int negate = src>>16&1;

				std::string r;
				switch (type) {
				case 1: r=tmp_src_str(tmp);break;
				case 2: r=input_src_str();break;
				case 3: r=const_src_str();break;
				default: xcept("unknown src type %d",type);
				}
				char field[4] = {'x','y','z','w'};
				std::string swiz;
				swiz+=field[swz_x];
				swiz+=field[swz_y];
				swiz+=field[swz_z];
				swiz+=field[swz_w];
				if (swiz!="xyzw") {
					r+='.';
					r+=swiz;
				}
				if (abs) xcept("src abs");
				if (negate) xcept("src negate");
				return r;
			};

			auto setdst = [&](std::string expr) {
				if (exec_cond==0) return;
				if (set_cond) getparam("vec4","cond");
				if (exec_cond!=7) {
					std::string tmp = gettmp("vec4");
					stat(tmp + " = " + expr);
					std::string cond = "cond";
					if (cond_swizzle!=(0|1<<2|2<<4|3<<6)) {
						char field[4] = {'x','y','z','w'};
						cond += ".";
						cond += field[cond_swizzle&3];
						cond += field[cond_swizzle>>2&3];
						cond += field[cond_swizzle>>4&3];
						cond += field[cond_swizzle>>6&3];
						cond = "(" + cond + ")";
					}
					std::string d = dst_str();
					auto stat2 = stat; // msvc bug workaround
					auto docond = [&](std::string field) {
						std::string s;
						switch (exec_cond) {
						case 1: s = "<"; break;
						case 2: s = "=="; break;
						case 3: s = "<="; break;
						case 4: s = ">"; break;
						case 5: s = "!="; break;
						case 6: s = ">="; break;
						default: xcept("unknown exec_cond %d",exec_cond);
						}
						s = "if ("+cond+"."+field+s+"0) ";
						if (set_cond) s+="{";
						s += d+"."+field+" = "+tmp+"."+field;
						if (set_cond) {
							s += "; "+cond+"."+field+" = "+tmp+"."+field+";}";
						}
						stat2(s);
					};
					if (writemask&1) docond("x");
					if (writemask&2) docond("y");
					if (writemask&4) docond("z");
					if (writemask&8) docond("w");
				} else {
					if (set_cond) {
						stat(apply_writemask("cond") + " = " + apply_writemask("("+expr+")"));
						expr = "cond";
					}
					stat(apply_writemask(dst_str()) + " = " + apply_writemask("("+expr+")"));
				}
			};

			auto cond_op = [&](std::string op) {
				std::string tmp = gettmp("vec4");
				std::string a = src_str(0);
				std::string b = src_str(1);
				stat(tmp + ".x = (("+a+").x"+op+"("+b+").x)?1:0");
				stat(tmp + ".y = (("+a+").y"+op+"("+b+").y)?1:0");
				stat(tmp + ".z = (("+a+").z"+op+"("+b+").z)?1:0");
				stat(tmp + ".w = (("+a+").w"+op+"("+b+").w)?1:0");
				setdst(tmp);
			};

			switch (opcode) {
			case 1: // mov
				setdst(src_str(0));
				break;
			case 7: // dp4
				setdst("(dot(" + src_str(0) + "," + src_str(1) + ").xxxx)");
				break;
			default:
				xcept("vp unknown opcode %d\n",opcode);
			}

			//dbgf(" so far -- \n%s\n",func_str().c_str());

			if (last) break;
			insn_count++;
		}
		//xcept("%s\n",func_str().c_str());
		if (errors) xcept("errors in vertex program");
		return func_str();

	}
}

namespace shader_program {


	std::pair<std::string,boost::unordered_set<int>> decompile(uint32_t*data) {

		dbgf("shader program decompile %p\n",data);

		std::string body;
		int level = 1;
		auto stat = [&](std::string str) {
			for (int i=0;i<level;i++) body+='\t';
			body += str + ";\n";
		};
		boost::unordered_map<std::string,std::string> params;
		auto getparam = [&](std::string decl,std::string name) -> std::string {
			params[name] = decl;
			return name;
		};
		boost::unordered_map<std::string,std::string> tmps;
		int tmp_n = 0;
		auto gettmp = [&](std::string decl) -> std::string {
			std::string name = format("tmp%d",tmp_n++);
			tmps[name] = decl;
			return name;
		};
		auto func_str = [&]() -> std::string {
			std::string p, t;
			for (auto i=params.begin();i!=params.end();++i) {
				p += i->second;
				p += " ";
				p += i->first;
				p += ";\n";
			}
			for (auto i=tmps.begin();i!=tmps.end();++i) {
				t += "\t";
				t += i->second;
				t += " ";
				t += i->first;
				t += ";\n";
			}
			return format("#version 150\n%svoid main() {\n%s%s}\n",p.c_str(),t.c_str(),body.c_str());
		};
		boost::unordered_set<int> immediates;
		int errors = 0;
		int insn_count = 0;
		while (true) {

			uint32_t d0 = se(*data++);
			uint32_t d1 = se(*data++);
			uint32_t d2 = se(*data++);
			uint32_t d3 = se(*data++);

			d0 = d0<<16 | d0>>16;
			d1 = d1<<16 | d1>>16;
			d2 = d2<<16 | d2>>16;
			d3 = d3<<16 | d3>>16;

			dbgf(" -- %x %x %x %x\n",d0,d1,d2,d3);

			int last = d0&1;
			d0 &= ~1;
			int writemask = d0>>9&0xf;
			d0 &= ~(0xf<<9);
			int opcode = d0>>24&0x3f;
			d0 &= ~(0x3f<<24);
			int dst = d0>>1&0x3f;
			d0 &= ~(0x3f<<1);
			int input_src = d0>>13&0xf;
			d0 &= ~(0xf<<13);
			int dst_fp16 = d0>>7&1;
			d0 &= ~(1<<7);
			int precision = d0>>22&3;
			d0 &= ~(3<<22);
			int set_cond = d0>>8&1;
			d0 &= ~(1<<8);
			int tex_unit = d0>>17&0x1f;
			d0 &= ~(0x1f<<17);
			int use_index_reg = d0>>30&1;
			d0 &= ~(1<<30);

			if (use_index_reg) dbgf("use index reg? >.<\n");

			dbgf("opcode %x, dst %x, src_reg %x, precision %x\n",opcode,dst,input_src,precision);
			dbgf("d0 is now %x\n",d0);

			int src0 = d1&0x3FFFF;
			d1 &= ~0x3FFFF;
			int src1 = d2&0x3FFFF;
			d2 &= ~0x3FFFF;
			int src2 = d3&0x3FFFF;
			d3 &= ~0x3FFFF;

			int exec_cond = d1>>18&7;
			d1 &= ~(7<<18);
			int cond_swizzle = d1>>21&0xff;
			d1 &= ~(0xff<<21);
			int src0_abs = d1>>29&1;
			d1 &= ~(1<<29);

			int src1_abs = d2>>18&1;
			d2 &= ~(1<<18);
			int scale = d2>>28&7;
			d2 &= ~(7<<28);

			int src2_abs = d3>>18&1;
			d3 &= ~(1<<18);
			int addr_reg_disp = d3>>19&0x7FF;
			d3 &= ~(0x7FF<<19);

			if (d0) {outf("unknown bits set in d0 - %#x\n",d0);errors++;}
			if (d1) {outf("unknown bits set in d1 - %#x\n",d1);errors++;}
			if (d2) {outf("unknown bits set in d2 - %#x\n",d2);errors++;}
			if (d3) {outf("unknown bits set in d3 - %#x\n",d3);errors++;}

			auto getreg = [&](int n) -> std::string {
				if (n==0) return getparam("out vec4",format("r%d",n));
				else return getparam("vec4",format("r%d",n));
			};
			auto apply_writemask = [&](std::string s) -> std::string {
				if (writemask!=0xf) {
					s+='.';
					if (writemask&1) s+='x';
					if (writemask&2) s+='y';
					if (writemask&4) s+='z';
					if (writemask&8) s+='w';
				}
				return s;
			};
			auto input_src_str = [&]() -> std::string {
				const char*table[] = {"gl_Position","col0","col1","fogc",
					"tc0","tc1","tc2","tc3","tc4","tc5","tc6","tc7"};
				if (input_src<12) return getparam("in vec4",table[input_src]);
				else xcept("unknown input src %d",input_src);
			};
			bool has_imm=false;
			auto input_immediate = [&]() -> std::string {
				if (!has_imm) {
					data += 4;
					has_imm = true;
					immediates.insert(insn_count+1);
				}
				return getparam("uniform vec4",format("imm%d",insn_count+1));
			};
			auto src_str = [&](int n) -> std::string {
				int src = n==0?src0:n==1?src1:src2;
				int abs = n==0?src0_abs:n==1?src1_abs:src2_abs;
				int type = src&3;
				int tmp_reg = src>>2&0x3f;
				int fp16 = src>>8&1;
				int swz_x = src>>9&3;
				int swz_y = src>>11&3;
				int swz_z = src>>13&3;
				int swz_w = src>>15&3;
				int negate = src>>17&1;
				std::string r;
				switch (type) {
				case 0: r=getreg(tmp_reg);break;
				case 1: r=input_src_str();break;
				case 2: r=input_immediate();break;
				default: xcept("unknown src type %d",type);
				}
				char field[4] = {'x','y','z','w'};
				std::string swiz;
				swiz+=field[swz_x];
				swiz+=field[swz_y];
				swiz+=field[swz_z];
				swiz+=field[swz_w];
				if (swiz!="xyzw") {
					r+='.';
					r+=swiz;
				}
				if (abs) xcept("src abs");
				if (negate) xcept("src negate");
				return r;
			};
			auto tex_str = [&]() -> std::string {
				return getparam("uniform sampler2D",format("tex%u",tex_unit));
			};

			auto setdst = [&](std::string expr) {
				switch (scale) {
				case 0:break;
				case 1: expr = "(" + expr + ")*2";break;
				case 2: expr = "(" + expr + ")*4";break;
				case 3: expr = "(" + expr + ")*8";break;
				case 5: expr = "(" + expr + ")/2";break;
				case 6: expr = "(" + expr + ")/4";break;
				case 7: expr = "(" + expr + ")/8";break;
				default: xcept("unknown scale %d",scale);
				}
				if (exec_cond==0) return;
				if (set_cond) getparam("vec4","cond");
				if (exec_cond!=7) {
					std::string tmp = gettmp("vec4");
					stat(tmp + " = " + expr);
					std::string cond = "cond";
					if (cond_swizzle!=(0|1<<2|2<<4|3<<6)) {
						char field[4] = {'x','y','z','w'};
						cond += ".";
						cond += field[cond_swizzle&3];
						cond += field[cond_swizzle>>2&3];
						cond += field[cond_swizzle>>4&3];
						cond += field[cond_swizzle>>6&3];
						cond = "(" + cond + ")";
					}
					std::string d = getreg(dst);
					auto stat2 = stat; // msvc bug workaround
					auto docond = [&](std::string field) {
						std::string s;
						switch (exec_cond) {
						case 1: s = "<"; break;
						case 2: s = "=="; break;
						case 3: s = "<="; break;
						case 4: s = ">"; break;
						case 5: s = "!="; break;
						case 6: s = ">="; break;
						default: xcept("unknown exec_cond %d",exec_cond);
						}
						s = "if ("+cond+"."+field+s+"0) ";
						if (set_cond) s+="{";
						s += d+"."+field+" = "+tmp+"."+field;
						if (set_cond) {
							s += "; "+cond+"."+field+" = "+tmp+"."+field+";}";
						}
						stat2(s);
					};
					if (writemask&1) docond("x");
					if (writemask&2) docond("y");
					if (writemask&4) docond("z");
					if (writemask&8) docond("w");
				} else {
					if (set_cond) {
						stat(apply_writemask("cond") + " = " + apply_writemask("("+expr+")"));
						expr = "cond";
					}
					stat(apply_writemask(getreg(dst)) + " = " + apply_writemask("("+expr+")"));
				}
			};

			auto cond_op = [&](std::string op) {
				std::string tmp = gettmp("vec4");
				std::string a = src_str(0);
				std::string b = src_str(1);
				stat(tmp + ".x = (("+a+").x"+op+"("+b+").x)?1:0");
				stat(tmp + ".y = (("+a+").y"+op+"("+b+").y)?1:0");
				stat(tmp + ".z = (("+a+").z"+op+"("+b+").z)?1:0");
				stat(tmp + ".w = (("+a+").w"+op+"("+b+").w)?1:0");
				setdst(tmp);
			};

			switch (opcode) {
			case 1: // mov
				setdst(src_str(0));
				break;
			case 2: // mul
				setdst(src_str(0) + " * " + src_str(1));
				break;
			case 3: // add
				setdst(src_str(0) + " + " + src_str(1));
				break;
			case 6: // dp4
				setdst("(dot(" + src_str(0) + "," + src_str(1) + ").xxxx)");
				break;
			case 8: // min
				setdst("min(" + src_str(0) + "," + src_str(1) + ")");
				break;
			case 0xc: // sle
				cond_op("<=");
				break;
			case 0x17: // tex
				setdst("texture(" + tex_str() + ",(" + src_str(0) + ").xy)");
				break;
			default: xcept("fp unknown opcode %#x",opcode);
			}

			//dbgf(" so far -- \n%s\n",func_str().c_str());

			if (last) break;
			insn_count++;
			if (has_imm) insn_count++;
		}
		if (errors) xcept("errors in shader program");
		return std::make_pair(func_str(),immediates);
	}


	int get_size(uint32_t*data) {
		int r = 0;
		while (true) {

			uint32_t d0 = se(*data++);
			uint32_t d1 = se(*data++);
			uint32_t d2 = se(*data++);
			uint32_t d3 = se(*data++);

			d0 = d0<<16 | d0>>16;
			d1 = d1<<16 | d1>>16;
			d2 = d2<<16 | d2>>16;
			d3 = d3<<16 | d3>>16;

			r += 4;
			if (d0&1) break;
		}
		return r;
	}

}

