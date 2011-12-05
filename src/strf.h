
namespace strf {


	namespace strf_detail {
		struct format_buffer {
			char*str;
			size_t str_capacity;
			size_t str_size;
			format_buffer() : str(0), str_capacity(0), str_size(0) {}
			~format_buffer() {
				if (str) {free(str);str=0;str_capacity=0;}
			}
			const char*data() {
				return str;
			}
			const char*c_str() {
				if (str_size==0 || str[str_size-1]) {
					reserve(str_size+1);
					str[str_size++] = 0;
				}
				return str;
			}
			size_t size() {
				return str_size;
			}
			size_t capacity() {
				return str_capacity;
			}
			void reserve(size_t new_size) {
				if (str_capacity>=new_size) return;
				if (str_capacity==0) str_capacity=8;
				while (str_capacity<new_size) str_capacity *= 2;
				str = (char*)realloc(str,new_size);
				if (!str) throw std::bad_alloc();
			}
			void resize(size_t new_size) {
				reserve(new_size);
				str_size = new_size;
			}
		};
		struct format_buffer_ref {
			char*&str;
			size_t&str_capacity;
			size_t str_size;
			format_buffer_ref(char*&str,size_t&str_capacity) : str(str), str_capacity(str_capacity), str_size(0) {}
			const char*data() {
				return str;
			}
			const char*c_str() {
				if (str_size==0 || str[str_size-1]) {
					reserve(str_size+1);
					str[str_size++] = 0;
				}
				return str;
			}
			size_t size() {
				return str_size;
			}
			size_t capacity() {
				return str_capacity;
			}
			void reserve(size_t new_size) {
				if (str_capacity>=new_size) return;
				if (str_capacity==0) str_capacity=8;
				while (str_capacity<new_size) str_capacity *= 2;
				char*new_str = (char*)realloc(str,str_capacity);
				if (!new_str) throw std::bad_alloc();
				str = new_str;
			}
			void resize(size_t new_size) {
				reserve(new_size);
				str_size = new_size;
			}
			void free_str() {
				if (str) {
					free(str);
					str = 0;
					str_capacity = 0;
				}
			}
		};
		struct strf_exception: std::exception {
			const char*str;
			strf_exception(const char*str) : str(str) {}
			virtual const char*what() const throw() {
				return str;
			}
		};
		void bad(const char*str) {
			throw strf_exception(str);
		}
		struct descriptor {
			bool end;
			bool flag_left_justify, flag_sign, flag_space, flag_hash, flag_zero;
			unsigned int width, precision;
			char c;
			descriptor() : end(true) {}
		};
		template<typename T> struct unsigned_type;
		template<> struct unsigned_type<char> {typedef unsigned char type;};
		template<> struct unsigned_type<signed char> {typedef unsigned char type;};
		template<> struct unsigned_type<unsigned char> {typedef unsigned char type;};
		template<> struct unsigned_type<short> {typedef unsigned short type;};
		template<> struct unsigned_type<unsigned short> {typedef unsigned short type;};
		template<> struct unsigned_type<int> {typedef unsigned int type;};
		template<> struct unsigned_type<unsigned int> {typedef unsigned int type;};
		template<> struct unsigned_type<long> {typedef unsigned long type;};
		template<> struct unsigned_type<unsigned long> {typedef unsigned long type;};
		template<> struct unsigned_type<long long> {typedef unsigned long long type;};
		template<> struct unsigned_type<unsigned long long> {typedef unsigned long long type;};
		template<typename dst_T>
		struct builder {
			dst_T&dst;
			const char*fmt;
			size_t pos;
			descriptor desc;
			builder(dst_T&dst,const char*fmt) : dst(dst), fmt(fmt), pos(0) {}
			template<typename T> char*reserve_impl(T&dst,size_t n) {
				size_t size = dst.size();
				if (size<pos+n) {
					dst.resize(pos+n);
				}
				return (char*)dst.data();
			}
			char*reserve_impl(format_buffer&dst,size_t n) {
				size_t size = dst.capacity();
				if (size<pos+n) {
					dst.reserve(pos+n);
				}
				return (char*)dst.data();
			}
			char*reserve_impl(format_buffer_ref&dst,size_t n) {
				size_t size = dst.capacity();
				if (size<pos+n) {
					dst.reserve(pos+n);
				}
				return (char*)dst.data();
			}
			char*reserve(size_t n) {
				return reserve_impl(dst,n);
			}
			descriptor next() {
				descriptor r;
				const char*c = fmt;
				auto flush = [&]() {
					if (c==this->fmt) return;
					size_t n = c-this->fmt;
					char*str=this->reserve(n);
					memcpy(&str[this->pos],this->fmt,n);
					this->pos += n;
				};
				auto testflag = [&]() -> bool {
					switch (*c) {
					case '-': r.flag_left_justify = true; break;
					case '+': r.flag_sign = true; break;
					case ' ': r.flag_space = true; break;
					case '#': r.flag_hash = true; break;
					case '0': r.flag_zero = true; break;
					default: return false;
					}
					c++;
					return true;
				};
				auto num = [&](unsigned int&dst) {
					if (*c=='*') {
						dst = ~1;
						c++;
						return;
					}
					const char*e = c;
					unsigned int m = 1;
					if (*e>='0'&&*e<='9') e++;
					while (*e>='0'&&*e<='9') {e++;m*=10;};
					if (e==c) return;
					unsigned r = 0;
					for (;c!=e;c++) {
						r += (*c-'0')*m;
						m /= 10;
					}
					dst = r;
				};
				while (*c) {
					if (*c=='%') {
						flush();
						if (*++c=='%') {
							char*str=reserve(1);
							str[pos++] = '%';
							++c;
							fmt = c;
						} else {
							r.end = false;
							r.flag_left_justify = false;
							r.flag_sign = false;
							r.flag_space = false;
							r.flag_hash = false;
							r.flag_zero = false;
							while(testflag());
							r.width = ~0;
							r.precision = ~0;
							num(r.width);
							if (*c=='.') ++c, num(r.precision);
							if (!*c) bad("bad format string");
							r.c = *c++;
							fmt = c;
							return r;
						}
					}
					c++;
				}
				flush();
				r.end = true;
				return r;
			}
			template<typename T,int base,bool caps>
			void do_num(T v) {
				char buf[sizeof(v)*4];
				bool negative = v<0;
				char*c = &buf[sizeof(buf)];
				bool is_zero = v==0;
				if (is_zero) {
					if (desc.precision!=0) *--c = '0';
				} else {
					if (negative) v = 0-v;
					typename unsigned_type<T>::type&uv = (typename unsigned_type<T>::type&)v;
					while (uv) {
						char n = uv%base;
						uv /= base;
						char d;
						if (base>10 && n>9) d = n-10+(caps ? 'A' : 'a');
						else d = '0'+n;
						*--c = d;
					}
				}
				size_t len = &buf[sizeof(buf)]-c;
				const char*num = c;
				char prefix[4];
				c = &prefix[0];
				if (desc.flag_hash && !is_zero) {
					if (base==8) *c++ = '0';
					else if (base==16) {
						*c++ = '0';
						if (caps) *c++ = 'X';
						else *c++ = 'x';
					}
				}
				if (negative) {
					*c++ = '-';
				} else {
					if (desc.flag_sign) *c++ = '+';
					else if (desc.flag_space) *c++ = ' ';
				}
				size_t prefix_len = c-&prefix[0];
				size_t outlen = prefix_len + len;
				if (desc.precision!=-1 && desc.precision>len) outlen += (desc.precision-len);
				size_t numlen = outlen;
				if (desc.width!=-1 && desc.width>outlen) outlen = desc.width;
				char*str=reserve(outlen);
				if (!desc.flag_zero && !desc.flag_left_justify) {
					for (size_t i=0;i<outlen-numlen;i++) str[pos++] = ' ';
				}
				if (prefix_len) {
					memcpy(&str[pos],prefix,prefix_len);
					pos += prefix_len;
				}
				if (desc.flag_zero) {
					for (size_t i=0;i<outlen-numlen;i++) str[pos++] = '0';
				}
				if (desc.precision!=-1 && desc.precision>len) {
					for (size_t i=0;i<desc.precision-len;i++) str[pos++] = '0';
				}
				memcpy(&str[pos],num,len);
				pos += len;
				if (desc.flag_left_justify) {
					if (desc.flag_zero) bad("zero flag and left justify flag cannot be specified together");
					for (size_t i=0;i<outlen-numlen;i++) str[pos++] = ' ';
				}
			}
			void do_string(const char*s,size_t len) {
				if (desc.flag_zero || desc.flag_hash || desc.flag_sign || desc.flag_space) bad("bad flags for string");
				if (!s) {
					s = "(null)";
					len = 6;
				}
				size_t outlen = len;
				if (desc.precision!=-1 && outlen>desc.precision) outlen = desc.precision;
				if (outlen<len) len = outlen;
				if (desc.width!=-1 && desc.width>outlen) outlen = desc.width;
				char*str=reserve(outlen);
				if (!desc.flag_left_justify) {
					for (size_t i=0;i<outlen-len;i++) str[pos++] = ' ';
				}
				memcpy(&str[pos],s,len);
				pos += len;
				if (desc.flag_left_justify) {
					for (size_t i=0;i<outlen-len;i++) str[pos++] = ' ';
				}
			}

			template<typename T>
			unsigned int to_uint(T&&v) {
				bad("argument can not be converted to unsigned int");
			}
			unsigned int to_uint(char v) {return v;}
			unsigned int to_uint(unsigned char v) {return v;}
			unsigned int to_uint(short v) {return v;}
			unsigned int to_uint(unsigned short v) {return v;}
			unsigned int to_uint(int v) {return v;}
			unsigned int to_uint(unsigned int v) {return v;}
			unsigned int to_uint(long v) {return v;}
			unsigned int to_uint(unsigned long v) {return v;}
			template<int base,bool caps,typename T>
			void do_signed_int(T&&v) {
				bad("argument is not numeric");
			}
			template<int base,bool caps> void do_signed_int(bool v) {do_num<int,base,caps>(v?1:0);}
			template<int base,bool caps> void do_signed_int(char v) {do_num<signed char,base,caps>(v);}
			template<int base,bool caps> void do_signed_int(signed char v) {do_num<signed char,base,caps>(v);}
			template<int base,bool caps> void do_signed_int(unsigned char v) {do_num<signed char,base,caps>(v);}
			template<int base,bool caps> void do_signed_int(short v) {do_num<short,base,caps>(v);}
			template<int base,bool caps> void do_signed_int(unsigned short v) {do_num<short,base,caps>(v);}
			template<int base,bool caps> void do_signed_int(int v) {do_num<int,base,caps>(v);}
			template<int base,bool caps> void do_signed_int(unsigned int v) {do_num<int,base,caps>(v);}
			template<int base,bool caps> void do_signed_int(long v) {do_num<long,base,caps>(v);}
			template<int base,bool caps> void do_signed_int(unsigned long v) {do_num<long,base,caps>(v);}
			template<int base,bool caps> void do_signed_int(long long v) {do_num<long long,base,caps>(v);}
			template<int base,bool caps> void do_signed_int(unsigned long long v) {do_num<long long,base,caps>(v);}
			template<int base,bool caps,typename T>
			void do_unsigned_int(T&&v) {
				bad("argument is not numeric");
			}
			template<int base,bool caps> void do_unsigned_int(bool v) {do_num<unsigned int,base,caps>(v?1:0);}
			template<int base,bool caps> void do_unsigned_int(char v) {do_num<unsigned char,base,caps>(v);}
			template<int base,bool caps> void do_unsigned_int(signed char v) {do_num<unsigned char,base,caps>(v);}
			template<int base,bool caps> void do_unsigned_int(unsigned char v) {do_num<unsigned char,base,caps>(v);}
			template<int base,bool caps> void do_unsigned_int(short v) {do_num<unsigned short,base,caps>(v);}
			template<int base,bool caps> void do_unsigned_int(unsigned short v) {do_num<unsigned short,base,caps>(v);}
			template<int base,bool caps> void do_unsigned_int(int v) {do_num<unsigned int,base,caps>(v);}
			template<int base,bool caps> void do_unsigned_int(unsigned int v) {do_num<unsigned int,base,caps>(v);}
			template<int base,bool caps> void do_unsigned_int(long v) {do_num<unsigned long,base,caps>(v);}
			template<int base,bool caps> void do_unsigned_int(unsigned long v) {do_num<unsigned long,base,caps>(v);}
			template<int base,bool caps> void do_unsigned_int(long long v) {do_num<unsigned long long,base,caps>(v);}
			template<int base,bool caps> void do_unsigned_int(unsigned long long v) {do_num<unsigned long long,base,caps>(v);}
			template<typename T>
			void do_string(T&&v) {
				bad("argument is not a string");
			}
			void do_string(char*str) {do_string(str,str?strlen(str):0);}
			void do_string(const char*str) {do_string(str,str?strlen(str):0);}
			void do_string(std::string&&s) {do_string(s.c_str(),s.size());}
			void do_string(const std::string&&s) {do_string(s.c_str(),s.size());}
			void do_string(std::string&s) {do_string(s.c_str(),s.size());}
			void do_string(const std::string&s) {do_string(s.c_str(),s.size());}
			template<typename T>
			void do_pointer(T&&v) {
				bad("argument is not a pointer");
			}
			void do_pointer(void*v) {
				desc.flag_hash = true;
				do_unsigned_int<16,false>((uintptr_t)v);
			}
			template<typename T> void do_pointer(T*v) {
				do_pointer((void*)v);
			}
			template<typename T>
			void do_float(T&&v) {
				bad("argument is not floating-point");
			}
			void do_float(double v) {
				char fstr[0x10];
				char*c = &fstr[0];
				*c++ = '%';
				if (desc.flag_hash) *c++='#';
				if (desc.flag_left_justify) *c++='-';
				if (desc.flag_sign) *c++='+';
				if (desc.flag_space) *c++=' ';
				if (desc.flag_zero) *c++='0';
				if (desc.width!=-1) *c++='*';
				if (desc.precision!=-1) {*c++='.';*c++='*';}
				*c++ = desc.c;
				*c=0;
				size_t len = 0x100;
				if (desc.precision!=-1) len += desc.precision;
				if (desc.width!=-1 && desc.width>=len) len += desc.width;
				char*str = reserve(len);
				str += pos;
				memset(str,0,len);
				if (desc.width!=-1&&desc.precision!=-1) sprintf(str,fstr,(int)desc.width,(int)desc.precision,v);
				else if (desc.width!=-1) sprintf(str,fstr,(int)desc.width,v);
				else if (desc.precision!=-1) sprintf(str,fstr,(int)desc.precision,v);
				else sprintf(str,fstr,v);
				pos += strlen(str);
			}
			void do_float(float v) {
				do_float((double)v);
			}
			template<typename T>
			void do_char(T&&v) {
				bad("argument is not convertible to a character");
			}
			void do_char(char c) {
				if (desc.flag_zero || desc.flag_hash || desc.flag_sign || desc.flag_space) bad("bad flags for character");
				size_t outlen = 1;
				if (desc.width!=-1 && desc.width>outlen) outlen = desc.width;
				char*str=reserve(outlen);
				if (!desc.flag_left_justify) {
					for (size_t i=0;i<outlen-1;i++) str[pos++] = ' ';
				}
				str[pos++] = c;
				if (desc.flag_left_justify) {
					for (size_t i=0;i<outlen-1;i++) str[pos++] = ' ';
				}
			}
			void do_char(signed char c) {do_char((char)c);}
			void do_char(unsigned char c) {do_char((char)c);}
			void do_char(short c) {do_char((char)c);}
			void do_char(unsigned short c) {do_char((char)c);}
			void do_char(int c) {do_char((char)c);}
			void do_char(unsigned int c) {do_char((char)c);}
			void do_char(long c) {do_char((char)c);}
			void do_char(unsigned long c) {do_char((char)c);}
			void do_char(long long c) {do_char((char)c);}
			void do_char(unsigned long long c) {do_char((char)c);}
			template<typename T>
			void advance(T&&v) {
				if (desc.end) desc = next();
				if (desc.end) bad("too many arguments for the specified format string");
				if (desc.width==~1) {
					desc.width = to_uint(std::forward<T>(v));
					return;
				}
				if (desc.precision==~1) {
					desc.precision = to_uint(std::forward<T>(v));
					return;
				}
				switch (desc.c) {
				case 'd':
				case 'i':
					do_signed_int<10,false>(std::forward<T>(v));
					break;
				case 'u':
					do_unsigned_int<10,false>(std::forward<T>(v));
					break;
				case 'x':
					do_unsigned_int<16,false>(std::forward<T>(v));
					break;
				case 'X':
					do_unsigned_int<16,true>(std::forward<T>(v));
					break;
				case 'o':
					do_unsigned_int<8,false>(std::forward<T>(v));
					break;
				case 's':
					do_string(std::forward<T>(v));
					break;
				case 'c':
					do_char(std::forward<T>(v));
					break;
				case 'p':
					do_pointer(std::forward<T>(v));
					break;
				case 'e':
				case 'E':
				case 'f':
				case 'g':
				case 'G':
					do_float(std::forward<T>(v));
					break;
				default: bad("unrecognized specifier in format string");
				}
				desc.end = true;
			}
			const char* finish() {
				if (!desc.end) bad("too few arguments for the specified format string");
				auto f = next();
				if (!f.end) bad("too few arguments for the specified format string");
				dst.resize(pos);
				return dst.c_str();
			}
		};
	}

	typedef strf_detail::format_buffer format_buffer;
	typedef strf_detail::format_buffer_ref format_buffer_ref;

	template<typename dst_T>
	const char*sformat(dst_T&dst,const char*fmt) {
		strf_detail::builder<dst_T> b(dst,fmt);
		return b.finish();
	}

	template<typename dst_T,typename A1>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4,typename A5>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		b.advance(std::forward<A5>(a5));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		b.advance(std::forward<A5>(a5));
		b.advance(std::forward<A6>(a6));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		b.advance(std::forward<A5>(a5));
		b.advance(std::forward<A6>(a6));
		b.advance(std::forward<A7>(a7));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		b.advance(std::forward<A5>(a5));
		b.advance(std::forward<A6>(a6));
		b.advance(std::forward<A7>(a7));
		b.advance(std::forward<A8>(a8));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		b.advance(std::forward<A5>(a5));
		b.advance(std::forward<A6>(a6));
		b.advance(std::forward<A7>(a7));
		b.advance(std::forward<A8>(a8));
		b.advance(std::forward<A9>(a9));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		b.advance(std::forward<A5>(a5));
		b.advance(std::forward<A6>(a6));
		b.advance(std::forward<A7>(a7));
		b.advance(std::forward<A8>(a8));
		b.advance(std::forward<A9>(a9));
		b.advance(std::forward<A10>(a10));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		b.advance(std::forward<A5>(a5));
		b.advance(std::forward<A6>(a6));
		b.advance(std::forward<A7>(a7));
		b.advance(std::forward<A8>(a8));
		b.advance(std::forward<A9>(a9));
		b.advance(std::forward<A10>(a10));
		b.advance(std::forward<A11>(a11));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		b.advance(std::forward<A5>(a5));
		b.advance(std::forward<A6>(a6));
		b.advance(std::forward<A7>(a7));
		b.advance(std::forward<A8>(a8));
		b.advance(std::forward<A9>(a9));
		b.advance(std::forward<A10>(a10));
		b.advance(std::forward<A11>(a11));
		b.advance(std::forward<A12>(a12));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		b.advance(std::forward<A5>(a5));
		b.advance(std::forward<A6>(a6));
		b.advance(std::forward<A7>(a7));
		b.advance(std::forward<A8>(a8));
		b.advance(std::forward<A9>(a9));
		b.advance(std::forward<A10>(a10));
		b.advance(std::forward<A11>(a11));
		b.advance(std::forward<A12>(a12));
		b.advance(std::forward<A13>(a13));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13,typename A14>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13,A14&&a14) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		b.advance(std::forward<A5>(a5));
		b.advance(std::forward<A6>(a6));
		b.advance(std::forward<A7>(a7));
		b.advance(std::forward<A8>(a8));
		b.advance(std::forward<A9>(a9));
		b.advance(std::forward<A10>(a10));
		b.advance(std::forward<A11>(a11));
		b.advance(std::forward<A11>(a12));
		b.advance(std::forward<A13>(a13));
		b.advance(std::forward<A14>(a14));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13,typename A14,typename A15>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13,A14&&a14,A15&&a15) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		b.advance(std::forward<A5>(a5));
		b.advance(std::forward<A6>(a6));
		b.advance(std::forward<A7>(a7));
		b.advance(std::forward<A8>(a8));
		b.advance(std::forward<A9>(a9));
		b.advance(std::forward<A10>(a10));
		b.advance(std::forward<A11>(a11));
		b.advance(std::forward<A12>(a12));
		b.advance(std::forward<A13>(a13));
		b.advance(std::forward<A14>(a14));
		b.advance(std::forward<A15>(a15));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13,typename A14,typename A15,typename A16>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13,A14&&a14,A15&&a15,A16&&a16) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		b.advance(std::forward<A5>(a5));
		b.advance(std::forward<A6>(a6));
		b.advance(std::forward<A7>(a7));
		b.advance(std::forward<A8>(a8));
		b.advance(std::forward<A9>(a9));
		b.advance(std::forward<A10>(a10));
		b.advance(std::forward<A11>(a11));
		b.advance(std::forward<A12>(a12));
		b.advance(std::forward<A13>(a13));
		b.advance(std::forward<A14>(a14));
		b.advance(std::forward<A15>(a15));
		b.advance(std::forward<A16>(a16));
		return b.finish();
	}
	template<typename dst_T,typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13,typename A14,typename A15,typename A16,typename A17>
	const char*sformat(dst_T&dst,const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13,A14&&a14,A15&&a15,A16&&a16,A17&&a17) {
		strf_detail::builder<dst_T> b(dst,fmt);
		b.advance(std::forward<A1>(a1));
		b.advance(std::forward<A2>(a2));
		b.advance(std::forward<A3>(a3));
		b.advance(std::forward<A4>(a4));
		b.advance(std::forward<A5>(a5));
		b.advance(std::forward<A6>(a6));
		b.advance(std::forward<A7>(a7));
		b.advance(std::forward<A8>(a8));
		b.advance(std::forward<A9>(a9));
		b.advance(std::forward<A10>(a10));
		b.advance(std::forward<A11>(a11));
		b.advance(std::forward<A12>(a12));
		b.advance(std::forward<A13>(a13));
		b.advance(std::forward<A14>(a14));
		b.advance(std::forward<A15>(a15));
		b.advance(std::forward<A16>(a16));
		b.advance(std::forward<A17>(a17));
		return b.finish();
	}

#define STRF_FUNC(retval,name,pre,post) \
	retval name(const char*fmt) {pre strf::format(fmt); post}\
	template<typename A1>\
	retval name(const char*fmt,A1&&a1) {pre strf::format(fmt,std::forward<A1>(a1)); post}\
	template<typename A1,typename A2>\
	retval name(const char*fmt,A1&&a1,A2&&a2) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2)); post}\
	template<typename A1,typename A2,typename A3>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3)); post}\
	template<typename A1,typename A2,typename A3,typename A4>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11),std::forward<A12>(a12)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11),std::forward<A12>(a12),std::forward<A13>(a13)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13,typename A14>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13,A14&&a14) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11),std::forward<A12>(a12),std::forward<A13>(a13),std::forward<A14>(a14)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13,typename A14,typename A15>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13,A14&&a14,A15&&a15) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11),std::forward<A12>(a12),std::forward<A13>(a13),std::forward<A14>(a14),std::forward<A15>(a15)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13,typename A14,typename A15,typename A16>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13,A14&&a14,A15&&a15,A16&&a16) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11),std::forward<A12>(a12),std::forward<A13>(a13),std::forward<A14>(a14),std::forward<A15>(a15),std::forward<A16>(a16)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13,typename A14,typename A15,typename A16,typename A17>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13,A14&&a14,A15&&a15,A16&&a16,A17&&a17) {pre strf::format(fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11),std::forward<A12>(a12),std::forward<A13>(a13),std::forward<A14>(a14),std::forward<A15>(a15),std::forward<A16>(a16),std::forward<A17>(a17)); post}

#define STRSF_FUNC(retval,name,pre,dst,post) \
	retval name(const char*fmt) {pre strf::sformat(dst,fmt); post}\
	template<typename A1>\
	retval name(const char*fmt,A1&&a1) {pre strf::sformat(dst,fmt,std::forward<A1>(a1)); post}\
	template<typename A1,typename A2>\
	retval name(const char*fmt,A1&&a1,A2&&a2) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2)); post}\
	template<typename A1,typename A2,typename A3>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3)); post}\
	template<typename A1,typename A2,typename A3,typename A4>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11),std::forward<A12>(a12)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11),std::forward<A12>(a12),std::forward<A13>(a13)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13,typename A14>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13,A14&&a14) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11),std::forward<A12>(a12),std::forward<A13>(a13),std::forward<A14>(a14)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13,typename A14,typename A15>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13,A14&&a14,A15&&a15) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11),std::forward<A12>(a12),std::forward<A13>(a13),std::forward<A14>(a14),std::forward<A15>(a15)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13,typename A14,typename A15,typename A16>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13,A14&&a14,A15&&a15,A16&&a16) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11),std::forward<A12>(a12),std::forward<A13>(a13),std::forward<A14>(a14),std::forward<A15>(a15),std::forward<A16>(a16)); post}\
	template<typename A1,typename A2,typename A3,typename A4,typename A5,typename A6,typename A7,typename A8,typename A9,typename A10,typename A11,typename A12,typename A13,typename A14,typename A15,typename A16,typename A17>\
	retval name(const char*fmt,A1&&a1,A2&&a2,A3&&a3,A4&&a4,A5&&a5,A6&&a6,A7&&a7,A8&&a8,A9&&a9,A10&&a10,A11&&a11,A12&&a12,A13&&a13,A14&&a14,A15&&a15,A16&&a16,A17&&a17) {pre strf::sformat(dst,fmt,std::forward<A1>(a1),std::forward<A2>(a2),std::forward<A3>(a3),std::forward<A4>(a4),std::forward<A5>(a5),std::forward<A6>(a6),std::forward<A7>(a7),std::forward<A8>(a8),std::forward<A9>(a9),std::forward<A10>(a10),std::forward<A11>(a11),std::forward<A12>(a12),std::forward<A13>(a13),std::forward<A14>(a14),std::forward<A15>(a15),std::forward<A16>(a16),std::forward<A17>(a17)); post}

#define STRF_TLS_BUF(name) namespace name { TLS char*str;TLS size_t size; }
#define STRF_GET_TLS_BUF(name) strf::format_buffer_ref(name::str,name::size)

	namespace strf_detail {
		STRF_TLS_BUF(format_buf);
		STRF_TLS_BUF(outf_buf);
	}
	STRSF_FUNC(const char*,format,auto buf=STRF_GET_TLS_BUF(strf_detail::format_buf);return ,buf,);
	STRSF_FUNC(void,outf,auto buf=STRF_GET_TLS_BUF(strf_detail::outf_buf);,buf,size_t written = fwrite(buf.data(),1,buf.size(),stdout););

	void strf_free_tls_buffers() {
		STRF_GET_TLS_BUF(strf_detail::format_buf).free_str();
		STRF_GET_TLS_BUF(strf_detail::outf_buf).free_str();
	}

}

