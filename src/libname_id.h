
uint32_t libname_id(const char*name) {

	// SHA-1 with a salt

	std::vector<uint8_t> buf;

	size_t len = strlen(name);
	buf.resize(len+0x10+1);
	memcpy(&buf[0],name,len);
	memcpy(&buf[len],"\x67\x59\x65\x99\x04\x25\x04\x90\x56\x64\x27\x49\x94\x89\x74\x1a",0x10);
	len+=0x10;
	size_t msglen = len*8;
	buf[len++] = 0x80;
	buf.resize((buf.size()-1-56)+(64-(buf.size()-1-56)%64)+64);
	buf[buf.size()-1] = msglen;
	buf[buf.size()-2] = msglen>>8;
	buf[buf.size()-3] = msglen>>16;
	buf[buf.size()-4] = msglen>>24;
	buf[buf.size()-5] = msglen>>32;
	buf[buf.size()-6] = msglen>>40;
	buf[buf.size()-7] = msglen>>48;
	buf[buf.size()-8] = msglen>>56;

	uint32_t h0 = 0x67452301;
	uint32_t h1 = 0xEFCDAB89;
	uint32_t h2 = 0x98BADCFE;
	uint32_t h3 = 0x10325476;
	uint32_t h4 = 0xC3D2E1F0;

	auto rotl = [&](uint32_t v,int n) {
		return v<<n | v>>(32-n);
	};
	uint32_t w[80];
	size_t bufsize = buf.size()/4;
	uint32_t*dd = (uint32_t*)&buf[0];
	for (uint32_t*cd=dd;cd!=dd+bufsize;cd+=16) {

		uint32_t f, k;
		int i = 0;

		uint32_t a = h0;
		uint32_t b = h1;
		uint32_t c = h2;
		uint32_t d = h3;
		uint32_t e = h4;

#define x(n,x)\
	for (;i<(n);i++) { \
		uint32_t temp = (x) + rotl(a,5) + f + e + k; \
		e = d; \
		d = c; \
		c = rotl(b,30); \
		b = a; \
		a = temp; \
	}

#define w0 se(cd[i])
#define w1 rotl(w[i-3]^w[i-8]^w[i-14]^w[i-16],1)
		x(16,(f = (b&c)|(~b&d), k = 0x5A827999, w[i] = w0));
		x(20,(f = (b&c)|(~b&d), k = 0x5A827999, w[i] = w1));
		x(40,(f = b^c^d, k = 0x6ED9EBA1, w[i] = w1));
		x(60,(f = (b&c)|(b&d)|(c&d), k = 0x8F1BBCDC, w[i] = w1));
		x(80,(f = b^c^d, k = 0xCA62C1D6, w[i] = w1));
#undef x

		h0 += a;
		h1 += b;
		h2 += c;
		h3 += d;
		h4 += e;
	}

	return se(h0);

}

struct libid_mapper {
	std::vector<char> data;
	boost::unordered_map<std::string,boost::unordered_map<uint32_t,const char*>> map;
	libid_mapper(const char*fn) {
		FILE*f = fopen(fn,"rb");
		if (!f) return;
		fseek(f,0,SEEK_END);
		long fs = ftell(f);
		fseek(f,0,SEEK_SET);
		data.resize(fs+1);
		fread(&data[0],fs,1,f);
		fclose(f);
		char*c = &data[0],*e = &data[data.size()-1];
		while (c<e) {
			const char*l = c;
			while (c<e && !isspace(*c)) c++;
			if (c==e) break;
			*c++=0;
			while (c<e && isspace(*c)) c++;
			if (c==e) break;
			char*nc;
			uint32_t id = strtoul(c,&nc,0);
			if (nc==c) break;
			c = nc;
			while (c<e && isspace(*c)) c++;
			if (c==e) break;
			const char*n = c;
			while (c<e && *c!='\r'&&*c!='\n') c++;
			*c++=0;
			map[l][id] = n;
			while (c<e && isspace(*c)) c++;
		}
	}
	const char*get(const char*libname,uint32_t id) {
		auto i = map.find(libname);
		if (i==map.end()) return "?";
		auto i2 = i->second.find(id);
		if (i2==i->second.end()) return "?";
		return i2->second;
	}
} libid_mapper("libnames.txt");
const char*libid_name(const char*libname,uint32_t id) {
	return libid_mapper.get(libname,id);
}


