#ifndef __AR_H__
#define __AR_H__


class ar {
public:
	std::vector<char> data;
	struct file {
		std::string name;
		unsigned int name_index;
		const char*data;
		unsigned int size;
		file() : name(), data(0), size(0), name_index(-1) {}
	};
	std::vector<file> files;
	file filenames_file;
	file symtab_file;
	void load(const char*buf,size_t size) {
		if (memcmp(buf,"!<arch>\n",8)) xcept("bad ar magic");
		const char*end=buf+size;
		buf+=8;
		while (buf<end) {
			file f;
			const char*name=buf;
			for (int i=0;i<16;i++) {if (*buf=='/') break;++buf;}
			bool is_symtab = false;
			if (buf==name) {
				if (buf[1]=='/') f.name="/";
				else if (buf[1]==' ') is_symtab=true;
				else f.name_index = atoi(buf+1);
			} else f.name.assign(name,buf-name);
			buf = name + 48; // skip right to file size, nothing else matters to us
			f.size = atoi(buf);
			buf += 12; // don't even care about validating the magic
			f.data = buf;
			buf += f.size;
			if (is_symtab) symtab_file=f;
			else if (f.name=="/") filenames_file=f;
			else files.push_back(f);
			//printf("load file '%s', %d bytes\n",f.name.c_str(),f.size);
			if ((size_t)buf & 1) buf++;
		}
		for (size_t i=0;i<files.size();i++) {
			if (files[i].name_index!=-1) {
				file&f=files[i];
				if (f.name_index>filenames_file.size) xcept("ar: bad name index %d",f.name_index);
				const char*o=filenames_file.data + f.name_index;
				const char*c=o;
				while (o<filenames_file.data+filenames_file.size && *o!='/') ++o;
				f.name.assign(c,o-c);
			}
		}
	}
	void load_file(const char*fn) {
		FILE*f = fopen(fn,"rb");
		if (!f) xcept("failed to open %s for reading",fn);
		fseek(f,0,SEEK_END);
		long fs = ftell(f);
		fseek(f,0,SEEK_SET);
		data.resize(fs);
		if (fread(&data[0],fs,1,f)!=1) xcept("failed to read from %s",fn);
		fclose(f);

		load(&data[0],data.size());
	}
};


#endif
