

std::string host_root = config.get("file","root");
struct fn_remapper_t {
	std::vector<std::pair<std::string,std::string>> list;
	template<typename T> fn_remapper_t(T&&list_arg) {
		for (auto i=list_arg.begin();i!=list_arg.end();++i) {
			std::string&str = *i;
			std::string a, b;
			auto p = str.find('=');
			a = str.substr(0,p);
			if (p!=str.npos) b = str.substr(p+1);
			list.push_back(std::make_pair(a,b));
		}
	}
	bool remap(std::string&in,std::string&out) {
		for (auto i=list.begin();i!=list.end();++i) {
			std::string&a = i->first;
			std::string&b = i->second;
			if (in.size()<a.size()) continue;
			if (!memcmp(in.data(),a.data(),a.size())) {
				out = b + in.substr(a.size());
				return true;
			}
		}
		return false;
	}
} fn_remapper(config.get_all("file","remap"));

enum {
	file_open_read_only = 0,
	file_open_read_write = 2,
	file_open_write_only = 1,
	file_open_create = 0x40,
	file_open_exclusive = 0x80,
	file_open_truncate = 0x200,
	file_open_append = 0x400,
	file_open_mself = 0x1000,
};

#pragma pack(push,4)
struct file_stat {
	uint32_t st_mode;
	uint32_t st_uid, st_gid;
	uint64_t st_atime, st_mtime, st_ctime;
	uint64_t st_size;
	uint64_t st_blksize;
};

struct file_directory_entry {
	uint8_t type, namelen;
	char name[256];
};
#pragma pack(pop)

struct file_t {
	std::string orig_fn;
	std::string fq_fn;
	std::string host_fn;
	HANDLE fh;
	uint32_t open_flags;
	boost::filesystem::directory_iterator dir_it;
	file_t() : fh(0), open_flags(0) {}
	void set_fn(const char*fn) {
		orig_fn = fn;
		std::string s(fn);
		if (*fn!='/') s = "/" + s;
		boost::filesystem::path p(s);
		std::stack<std::string> ps;
		for (boost::filesystem::path::iterator i=++p.begin();i!=p.end();++i) {
			const boost::filesystem::path&s = *i;
			if (s=="..") {if (!ps.empty()) ps.pop();}
			else ps.push(s.string());
		}
		if (ps.empty()) {
			fq_fn="";
			host_fn="";
		} else {
			fq_fn = "";
			do {
				fq_fn = "/" + ps.top() + fq_fn;
				ps.pop();
			} while (!ps.empty());
			if (host_root.empty()) xcept("root is null!");
			if (!fn_remapper.remap(fq_fn,host_fn)) host_fn = host_root + fq_fn;
		}
	}
};

id_list_t<file_t,0x10> file_list;

#define get_file(fd) auto h = file_list.get(fd);\
	if (!h) return EBADF;\
	file_t*p_f = &*h;\
	HANDLE fh = p_f->fh;\
	if (!fh) return EBADF;
#define get_dir(fd) auto h = file_list.get(fd);\
	if (!h) return EBADF;\
	file_t*p_f = &*h;\
	boost::filesystem::directory_iterator&dir_it = p_f->dir_it;

int win32_fs_error(DWORD err) {
	dbgf("win32 error: %d\n",err);
	switch (err) {
	case ERROR_FILE_NOT_FOUND: return ENOENT;
	case ERROR_PATH_NOT_FOUND: return ENOENT;
	case ERROR_ALREADY_EXISTS: return EEXIST;
	default:
		return EFSSPECIFIC;
	}
}

int sys_fs_open(const char*path,uint32_t flags,int32_t*fd,uint32_t mode,void*arg,uint64_t arg_size) {
	dbgf("sys_fs_open %s, flags %x, fd %p, mode %x, arg %p, arg_size %x\n",path,flags,fd,mode,arg,arg_size);

	auto h = file_list.get_new();
	if (!h) return EMFILE;
	int id = h.id();
	file_t&f=*h;

	f.set_fn(path);
	dbgf("fully qualified file name: %s\n",f.fq_fn.c_str());
	dbgf("host file name: %s\n",f.host_fn.c_str());

	f.open_flags = flags;

	if (flags&file_open_mself && flags!=file_open_mself && flags!=(file_open_read_only|file_open_mself)) flags&=~file_open_mself;

	DWORD cf = 0;
	DWORD af = 0;

	if ((flags&3)==file_open_read_only) af = GENERIC_READ;
	else if ((flags&3)==file_open_write_only) af = GENERIC_WRITE;
	else if ((flags&3)==file_open_read_write) af = GENERIC_READ|GENERIC_WRITE;
	else return EINVAL;

	flags&=~3;

	if (flags&file_open_create) {
		if (flags&file_open_exclusive) cf = CREATE_NEW;
		else cf = CREATE_ALWAYS;
		flags&=~(file_open_create|file_open_exclusive);
	} else cf = OPEN_EXISTING;

	bool truncate = (flags&file_open_truncate)!=0;
	flags&=~file_open_truncate;
	//bool append = (flags&file_open_append)!=0;
	//flags&=~file_open_append;

	if (flags) xcept("sys_fs_open: unknown left-over flags %#x\n",flags);

	HANDLE fh = CreateFileA(f.host_fn.c_str(),af,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,0,cf,0,0);
	if (fh==INVALID_HANDLE_VALUE) return win32_fs_error(GetLastError());
	f.fh = fh;

	if (truncate) SetEndOfFile(fh);
	
	*fd = se((int32_t)id);

	dbgf("returning id %d\n",id);
	h.keep();
	return CELL_OK;
}

const unsigned int file_read_write_block_size = 1024*1024*64;

int sys_fs_read(int32_t fd,uint8_t*buf,uint64_t size,uint64_t*read) {
	dbgf("fs read fd %d, buf %p, size %d, read %p\n",fd,buf,size,read);
	get_file(fd);
	if (!buf) return EFAULT;
	uint64_t read_n = 0;
	while (size) {
		DWORD dwread = 0;
		if (!ReadFile(fh,buf,std::min<DWORD>(size,file_read_write_block_size),&dwread,0)) {
			DWORD err = GetLastError();
			if (err==ERROR_HANDLE_EOF) dwread=0;
			else return win32_fs_error(err);
		}
		if (!dwread) break;
		size -= dwread;
		buf += dwread;
		read_n += dwread;
	}
	if (read) *read = se((uint64_t)read_n);
	return CELL_OK;
}
int sys_fs_write(int32_t fd,const uint8_t*buf,uint64_t size,uint64_t*written) {
	dbgf("fs write %d, buf %p, size %d, written %p\n",fd,buf,size,written);
	get_file(fd);
	uint64_t write_n = 0;
	while (size) {
		DWORD dwwritten = 0;
		if (!WriteFile(fh,buf,std::min<DWORD>(size,file_read_write_block_size),&dwwritten,0)) return win32_fs_error(GetLastError());
		size -= dwwritten;
		buf += dwwritten;
		write_n += dwwritten;
	}
	if (written) *written = se((uint64_t)write_n);
	return CELL_OK;
}
int sys_fs_close(int32_t fd) {
	dbgf("fs close %d\n",fd);
	get_file(fd);
	CloseHandle(fh);
	p_f->fh = 0;
	h.kill();
	return CELL_OK;
}
int sys_fs_open_dir(const char*path,int32_t*fd) {
	dbgf("open dir %s, fd %p\n",path,fd);
	file_t f;

	f.set_fn(path);
	dbgf("fully qualified file name: %s\n",f.fq_fn.c_str());
	dbgf("host file name: %s\n",f.host_fn.c_str());

	boost::system::error_code err;
	f.dir_it = boost::filesystem::directory_iterator(f.host_fn.c_str(),err);
	if (err) {
		dbgf("open dir error %s\n",err.message().c_str());
		return ENOENT;
	}

	auto h = file_list.get_new();
	if (!h) return EMFILE;
	int id = h.id();
	*h = f;

	*fd = se((int32_t)id);

	dbgf("returning id %d\n",id);
	h.keep();
	return CELL_OK;
}
int sys_fs_read_dir(int32_t fd,file_directory_entry*entry,uint64_t*read) {
	dbgf("read dir %d, entry %p, read %p\n",fd,entry,read);
	get_dir(fd);
	if (dir_it==boost::filesystem::directory_iterator()) {
		*read = 0;
		return CELL_OK;
	}
	boost::system::error_code err;
	boost::filesystem::directory_entry e = *dir_it;
	boost::filesystem::file_status s = e.status(err);
	if (err) {
		dbgf("read dir file_status error %s\n",err.message().c_str());
		return EIO;
	}
	size_t namelen;
	std::string fn = e.path().filename().string();
	namelen = fn.length();
	if (namelen>255) namelen = 255;
	entry->namelen = (uint8_t)namelen;
	// 0=unknown, 1=directory, 2=file, 3=symlink
	switch (s.type()) {
		case boost::filesystem::regular_file:
			entry->type = (uint8_t)2;
			break;
		case boost::filesystem::directory_file:
			entry->type = (uint8_t)1;
			break;
		default:
			entry->type = (uint8_t)0;
	}
	memcpy(entry->name,fn.c_str(),namelen+1);
	*read = 1;
	dir_it.increment(err);
	if (err) {
		dbgf("read dir directory_iterator error %s\n",err.message().c_str());
		return EIO;
	}
	return CELL_OK;
}
int sys_fs_close_dir(int32_t fd) {
	dbgf("close dir %d\n",fd);
	get_dir(fd);
	h.kill();
	return CELL_OK;
}
#include <sys/stat.h>
int sys_fs_stat(const char*path,file_stat*buf) {
	dbgf("stat %s, buf %p\n",path,buf);
	file_t f;

	f.set_fn(path);
	dbgf("fully qualified file name: %s\n",f.fq_fn.c_str());
	dbgf("host file name: %s\n",f.host_fn.c_str());
	path = f.host_fn.c_str();

	struct stat s;
	if (stat(path,&s)) {
		dbgf("stat failed\n");
		return ENOENT;
	}
	buf->st_mode = se((int32_t)s.st_mode);
	buf->st_uid = -1;
	buf->st_gid = -1;
	buf->st_atime = se((int64_t)s.st_atime);
	buf->st_mtime = se((int64_t)s.st_mtime);
	buf->st_ctime = se((int64_t)s.st_ctime);
	buf->st_size = se((uint64_t)s.st_size);
	buf->st_blksize = 0x1000;
	
	return CELL_OK;
}
int sys_fs_fstat(int32_t fd,file_stat*stat) {
	get_dir(fd);
	return sys_fs_stat(p_f->orig_fn.c_str(),stat);
}
int sys_fs_mkdir(const char*path,uint32_t mode) {
	dbgf("mkdir %s, mode %d\n",path,mode);
	file_t f;
	f.set_fn(path);
	dbgf("fully qualified file name: %s\n",f.fq_fn.c_str());
	dbgf("host file name: %s\n",f.host_fn.c_str());
	path = f.host_fn.c_str();
	if (!CreateDirectoryA(path,0)) return win32_fs_error(GetLastError());
	return CELL_OK;
}
int sys_fs_rename(const char*path,const char*newpath) {
	xcept("rename");
	return ENOSYS;
}
int sys_fs_rmdir(const char*path) {
	xcept("rmdir");
	return ENOSYS;
}
int sys_fs_unlink(const char*path) {
	xcept("unlink");
	return ENOSYS;
}
int sys_fs_lseek64(int32_t fd,int64_t offset,uint32_t origin,uint64_t*pos) {
	dbgf("fs lseek fd %d, offset %d, origin %d, pos %p\n",fd,offset,origin,pos);
	get_file(fd);
	int m = origin==0 ? FILE_BEGIN : origin==1 ? FILE_CURRENT : origin==2 ? FILE_END : -1;
	if (m==-1) return EINVAL;
	uint64_t pos_tmp;
	if (!SetFilePointerEx(fh,(LARGE_INTEGER&)offset,(LARGE_INTEGER*)&pos_tmp,m)) return win32_fs_error(GetLastError());
	if (pos) *pos = se((uint64_t)pos_tmp);
	return CELL_OK;
}
int sys_fs_fsync(int32_t fd) {
	get_file(fd);
	if (!FlushFileBuffers(fh)) return win32_fs_error(GetLastError());
	return CELL_OK;
}
int sys_fs_truncate(const char*path,uint64_t size) {
	xcept("truncate");
	return ENOSYS;
}
int sys_fs_ftruncate(int32_t fd,uint64_t size) {
	xcept("ftruncate");
	return ENOSYS;
}
int sys_fs_utime(const char*path,void*t) {
	xcept("utime");
	return ENOSYS;
}
int sys_fs_chmod(const char*path,uint32_t mode) {
	dbgf("chmod %s mode %x\n",path,mode);
	dbgf("returning ENOTSUP\n");
	return ENOTSUP;
}
int sys_fs_link(const char*oldpath,const char*newpath) {
	xcept("link");
	return ENOSYS;
}

#undef get_dir
#undef get_file
