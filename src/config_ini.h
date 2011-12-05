

struct config_t {
	boost::unordered_multimap<std::string,std::unordered_multimap<std::string,std::string>> map;
	config_t(const char*fn) {
		load(fn);
	}
	void load(const char*fn) {
		FILE*f = fopen(fn,"rb");
		if (!f) xcept("failed to open %s for reading",fn);
		std::vector<char> buf;
		fseek(f,0,SEEK_END);
		buf.resize(ftell(f));
		fseek(f,0,SEEK_SET);
		if (!fread(&buf[0],buf.size(),1,f)) xcept("failed to read from %s",fn);
		fclose(f);
		const char*c = &buf[0];
		map.clear();
		std::string section_name;
		std::unordered_multimap<std::string,std::string> section_data;
		while (*c) {
			while (isspace(*c)) ++c;
			if (*c=='[') {
				const char*p = ++c;
				while (*c&&*c!=']') ++c;
				if (!section_data.empty()) {
					map.insert(std::make_pair(section_name,section_data));
				}
				section_name = std::string(p,c-p);
				section_data.clear();
				++c;
			} else {
				const char*p = c;
				while (*c&&*c!='=') ++c;
				if (!*c) break;
				std::string n = std::string(p,c-p);
				p = ++c;
				while (*c&&*c!='\r'&&*c!='\n') ++c;
				section_data.insert(std::make_pair(n,std::string(p,c-p)));
			}
		}
		if (!section_data.empty()) {
			map.insert(std::make_pair(section_name,section_data));
		}
	}
	std::string get(const char*section,const char*name) {
		auto i = map.find(section);
		if (i!=map.end()) {
			auto i2 = i->second.find(name);
			if (i2!=i->second.end()) {
				return i2->second;
			}
		}
		return std::string();
	}
	int get_int(const char*section,const char*name) {
		return atoi(get(section,name).c_str());
	}
	std::vector<std::string> get_all(const char*section,const char*name) {
		std::vector<std::string> r;
		auto i = map.find(section);
		if (i!=map.end()) {
			auto i2 = i->second.equal_range(name);
			for (auto i=i2.first;i!=i2.second;++i) {
				r.push_back(i->second);
			}
		}
		return r;
	}

} config("config.ini");