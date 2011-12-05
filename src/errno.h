

#undef EAGAIN     
#undef EINVAL     
#undef ENOSYS     
#undef ENOMEM     
#undef ESRCH      
#undef ENOENT     
#undef ENOEXEC    
#undef EDEADLK    
#undef EPERM      
#undef EBUSY      
#undef ETIMEDOUT  
#undef EABORT     
#undef EFAULT     
#undef ESTAT      
#undef EALIGN     
#undef EKRESOURCE 
#undef EISDIR     
#undef ECANCELED  
#undef EEXIST     
#undef EISCONN    
#undef ENOTCONN   
#undef EAUTHFAIL  
#undef ENOTMSELF  
#undef ESYSVER    

#undef EDOM       
#undef ERANGE     
#undef EILSEQ     
#undef EFPOS      
#undef EINTR      
#undef EFBIG      
#undef EMLINK     
#undef ENFILE     
#undef ENOSPC     
#undef ENOTTY     
#undef EPIPE      
#undef EROFS      
#undef ESPIPE     
#undef E2BIG      
#undef EACCES     
#undef EBADF      
#undef EIO        
#undef EMFILE     
#undef ENODEV     
#undef ENOTDIR    
#undef ENXIO      
#undef EXDEV      
#undef EBADMSG    
#undef EINPROGRESS 
#undef EMSGSIZE   
#undef ENAMETOOLONG 
#undef ENOLCK     
#undef ENOTEMPTY  
#undef ENOTSUP    
#undef EFSSPECIFIC 
#undef EOVERFLOW  
#undef ENOTMOUNTED 

enum {
	CELL_OK = 0,
	EAGAIN     = (int32_t)0x80010001,
	EINVAL     = (int32_t)0x80010002,
	ENOSYS     = (int32_t)0x80010003,
	ENOMEM     = (int32_t)0x80010004,
	ESRCH      = (int32_t)0x80010005,
	ENOENT     = (int32_t)0x80010006,
	ENOEXEC    = (int32_t)0x80010007,
	EDEADLK    = (int32_t)0x80010008,
	EPERM      = (int32_t)0x80010009,
	EBUSY      = (int32_t)0x8001000a,
	ETIMEDOUT  = (int32_t)0x8001000b,
	EABORT     = (int32_t)0x8001000c,
	EFAULT     = (int32_t)0x8001000d,
	ESTAT      = (int32_t)0x8001000f,
	EALIGN     = (int32_t)0x80010010,
	EKRESOURCE = (int32_t)0x80010011,
	EISDIR     = (int32_t)0x80010012,
	ECANCELED  = (int32_t)0x80010013,
	EEXIST     = (int32_t)0x80010014,
	EISCONN    = (int32_t)0x80010015,
	ENOTCONN   = (int32_t)0x80010016,
	EAUTHFAIL  = (int32_t)0x80010017,
	ENOTMSELF  = (int32_t)0x80010018,
	ESYSVER    = (int32_t)0x80010019,

	EDOM       = (int32_t)0x8001001b,
	ERANGE     = (int32_t)0x8001001c,
	EILSEQ     = (int32_t)0x8001001d,
	EFPOS      = (int32_t)0x8001001e,
	EINTR      = (int32_t)0x8001001f,
	EFBIG      = (int32_t)0x80010020,
	EMLINK     = (int32_t)0x80010021,
	ENFILE     = (int32_t)0x80010022,
	ENOSPC     = (int32_t)0x80010023,
	ENOTTY     = (int32_t)0x80010024,
	EPIPE      = (int32_t)0x80010025,
	EROFS      = (int32_t)0x80010026,
	ESPIPE     = (int32_t)0x80010027,
	E2BIG      = (int32_t)0x80010028,
	EACCES     = (int32_t)0x80010029,
	EBADF      = (int32_t)0x8001002a,
	EIO        = (int32_t)0x8001002b,
	EMFILE     = (int32_t)0x8001002c,
	ENODEV     = (int32_t)0x8001002d,
	ENOTDIR    = (int32_t)0x8001002e,
	ENXIO      = (int32_t)0x8001002f,
	EXDEV      = (int32_t)0x80010030,
	EBADMSG    = (int32_t)0x80010031,
	EINPROGRESS = (int32_t)0x80010032,
	EMSGSIZE   = (int32_t)0x80010033,
	ENAMETOOLONG = (int32_t)0x80010034,
	ENOLCK     = (int32_t)0x80010035,
	ENOTEMPTY  = (int32_t)0x80010036,
	ENOTSUP    = (int32_t)0x80010037,
	EFSSPECIFIC = (int32_t)0x80010038,
	EOVERFLOW  = (int32_t)0x80010039,
	ENOTMOUNTED = (int32_t)0x8001003a
};
