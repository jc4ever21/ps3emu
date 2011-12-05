

namespace ah_audio {

	enum {
		audio_ok = 0,
		err_not_implemented = (int32_t)0x8002b240,
		err_illegal_configuration = (int32_t)0x8002b241,
		err_illegal_parameter = (int32_t)0x8002b242,
		err_parameter_out_of_range = (int32_t)0x8002b243,
		err_device_not_found = (int32_t)0x8002b244,
		err_unsupported_audio_out = (int32_t)0x8002b245,
		err_unsupported_sound_mode = (int32_t)0x8002b246,
		err_condition_busy = (int32_t)0x8002b247
	};

	struct out_sound_mode {
		uint8_t type, channels, freqn;
		uint8_t reserved;
		uint32_t layout;
	};

	struct out_state {
		uint8_t state, encoder;
		uint8_t reserved[6];
		uint32_t down_mixer;
		out_sound_mode sound_mode;
	};

	struct out_config {
		uint8_t channels, encoder;
		uint8_t reserved[8];
		uint32_t down_mixer;
	};

	int32_t cellAudioOutGetNumberOfDevice(uint32_t audio_out) {
		dbgf("cellAudioOutGetNumberOfDevice, audio_out %d\n",audio_out);
		if (audio_out==0) return 1;
		else return 0;
	}

	int32_t cellAudioOutGetState(uint32_t audio_out,uint32_t device_index,out_state*state) {
		dbgf("cellAudioOutGetState, audio_out %d, device_index %d\n",audio_out,device_index);
		if (audio_out!=0 || device_index!=0) return err_device_not_found;
		memset(state,0,sizeof(*state));
		state->state = (uint8_t)0; // enabled
		state->encoder = (uint8_t)0; // lpcm
		state->down_mixer = se((uint32_t)0); // no downmixer
		state->sound_mode.type = (uint8_t)0; // lpcm
		state->sound_mode.channels = (uint8_t)8;
		state->sound_mode.freqn = (uint8_t)4; // 48khz
		state->sound_mode.layout = se((uint32_t)1); // 2-channel layout
		return audio_ok;
	}

	int32_t cellAudioOutSetCopyControl(uint32_t audio_out,uint32_t control) {
		dbgf("cellAudioOutSetCopyControl (doing nothing)\n");
		return audio_ok;
	}


	int32_t cellAudioOutGetSoundAvailability(uint32_t audio_out,uint32_t type,uint32_t freqn,uint32_t zero) {
		dbgf("cellAudioGetSoundAvailability, audio_out %d, type %d, freqn %d, zero %d\n",audio_out,type,freqn,zero);
		if (audio_out==0) {
			// lpcm, 48khz
			if (type==0 && freqn==4) return 8;
			else return 0;
		} else return 0;
	}

	void cellAudioOutGetDeviceInfo() {
		xcept("cellAudioOutGetDeviceInfo");
	}
	int32_t cellAudioOutConfigure(uint32_t audio_out,out_config*config,void*opt,uint32_t wait) {
		dbgf("cellAudioOutConfigure, audio_out %d, channels %d, encoder %d, down_mixer %d\n",audio_out,config->channels,config->encoder,config->down_mixer);
		if (audio_out!=0) return err_unsupported_audio_out;
		if (config->encoder!=0 || config->down_mixer!=0) return err_unsupported_sound_mode;
		return audio_ok;
	}
	void cellAudioOutGetConfiguration() {
		xcept("cellAudioOutGetConfiguration");
	}


	ah_reg(cellSysutil,
		cellAudioOutGetNumberOfDevice,
		cellAudioOutGetDeviceInfo,
		cellAudioOutGetState,
		cellAudioOutConfigure,
		cellAudioOutSetCopyControl,
		cellAudioOutGetSoundAvailability,
		cellAudioOutGetConfiguration

		// these are part of cellSysutilAvconfExt, not cellSysutil
		//cellAudioInSetDeviceMode,
		//cellAudioInRegisterDevice,
		//cellAudioInUnregisterDevice,
		//cellAudioInGetDeviceInfo,
		//cellAudioInGetAvailableDeviceInfo
		);
}

#include "Mmreg.h"

#include <dsound.h>
#include <DxErr.h>

#pragma comment(lib,"dsound.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"dxerr.lib")

namespace audio {
	bool inited = false;
	bool stop=false;
	sync::busy_lock busy_lock;
	win32_thread audio_thread;

	enum {
		audio_ok = 0,
		err_already_initialized = (int32_t)0x80310701,
		err_audio_system = (int32_t)0x80310702,
		err_not_initialized = (int32_t)0x80310703,
		err_invalid_parameter = (int32_t)0x80310704,
		err_port_full = (int32_t)0x80310705,
		err_port_already_running = (int32_t)0x80310706,
		err_port_not_open = (int32_t)0x80310707,
		err_port_not_running = (int32_t)0x80310708,
		err_trans_event = (int32_t)0x80310709,
		err_port_open = (int32_t)0x8031070a,
		err_shared_memory = (int32_t)0x8031070b,
		err_mutex = (int32_t)0x8031070c,
		err_event_queue = (int32_t)0x8031070d,
		err_audio_system_not_found = (int32_t)0x8031070e,
		err_tag_not_found = (int32_t)0x8031070f
	};

	struct open_port_params {
		uint64_t channels, blocks, flags;
		float volume;
	};
	struct port_config {
		uint32_t next_read;
		uint32_t status;
		uint64_t channels, blocks;
		uint32_t port_size, port_addr;
	};

	struct port: boost::intrusive::list_base_hook<> {
		sync::busy_lock busy_lock;
		bool running;
		float*buffer;
		uint64_t*next_read;
		uint64_t this_read;
		int channels;
		int blocks;
		uint64_t flags;
		float volume;
		port() : running(false), buffer(0), next_read(0), this_read(0) {}
		~port() {
			if (buffer) mm_free(buffer);
		}
	};
	id_list_t<port,0> port_list;
	typedef id_list_t<port,0> port_handle;

	boost::intrusive::list<port> open_ports;

	ipc_event_queue_handle eqh;

	void dxerr(HRESULT hr,const char*str) {
		xcept("DirectX error in '%s': %s (%#x)",str,DXGetErrorString(hr),hr);
	}

	void audio_thread_entry() {

		SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_TIME_CRITICAL);

		IDirectSound8*ds;
		HRESULT hr = DirectSoundCreate8(0,&ds,0);
		if (FAILED(hr)) dxerr(hr,"DirectSoundCreate8");
		hr = ds->SetCooperativeLevel(gcm::get_window().get_handle(),DSSCL_PRIORITY);
		if (FAILED(hr)) dxerr(hr,"SetCooperativeLevel");
		WAVEFORMATEX wf;
		wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		wf.nChannels = 2;
		wf.nSamplesPerSec = 48000;
		wf.nAvgBytesPerSec = 48000*2*sizeof(float);
		wf.nBlockAlign = 2*sizeof(float);
		wf.wBitsPerSample = 32;
		wf.cbSize = 0;

		const int buffers = 24;
		
		DSBUFFERDESC dbd;
		memset(&dbd,0,sizeof(dbd));
		dbd.dwSize = sizeof(dbd);
		dbd.dwFlags = DSBCAPS_GETCURRENTPOSITION2|DSBCAPS_GLOBALFOCUS;
		dbd.lpwfxFormat = &wf;
		dbd.dwBufferBytes = 2*sizeof(float)*256*buffers;
		IDirectSoundBuffer*dsbuf;
		hr = ds->CreateSoundBuffer(&dbd,&dsbuf,0);
		if (FAILED(hr)) dxerr(hr,"CreateSoundBuffer");
		float*lock_ptr,*lock_ptr2;
		DWORD lock_size, lock_size2;
		hr = dsbuf->Lock(0,dbd.dwBufferBytes,(void**)&lock_ptr,&lock_size,(void**)&lock_ptr2,&lock_size2,0);
		if (FAILED(hr)) dxerr(hr,"Lock");
		memset(lock_ptr,0,dbd.dwBufferBytes);
		dsbuf->Unlock(lock_ptr,lock_size,lock_ptr2,lock_size2);

		dsbuf->Play(0,0,DSBPLAY_LOOPING);

		int next_buffer = buffers/2;
		int64_t last_write = sys_time_get_system_time();
		uint64_t counter=0;
		while (!atomic_read(&stop)) {

			DWORD play_cursor,write_cursor;
			hr = dsbuf->GetCurrentPosition(&play_cursor,&write_cursor);
			if (FAILED(hr)) dxerr(hr,"GetCurrentPosition");
			
			//outf("(write_cursor-play_cursor)%%dbd.dwBufferBytes is %d\n",(int)((write_cursor-play_cursor)%dbd.dwBufferBytes));

			DWORD block_offset = 2*sizeof(float)*256*next_buffer;
			DWORD block_size = 2*sizeof(float)*256;
			if (write_cursor<play_cursor?next_buffer==buffers-1 || next_buffer==0:block_offset<write_cursor&&block_offset+block_size>play_cursor) {
				win32_thread::sleep(1);
				continue;
			}
			// One buffer lasts 5+1/3 ms, but we don't need to stay perfectly in sync with that.
			// The only reason we wait here is because DirectSound usually feeds with larger
			// buffers than that, so we give applications some time to fill the buffer
			// before we move on.
			int64_t now = sys_time_get_system_time();
			if (now-last_write>5333) last_write = now-5333;
			while (now-last_write<5333) {
				win32_thread::sleep(1);
				now = sys_time_get_system_time();
			}
			last_write += 5333;

			hr = dsbuf->Lock(block_offset,block_size,(void**)&lock_ptr,&lock_size,(void**)&lock_ptr2,&lock_size2,0);
			if (FAILED(hr)) dxerr(hr,"Lock");
			float*dst = lock_ptr;
			memset(dst,0,2*sizeof(float)*256);
			
			next_buffer = (next_buffer+1)%buffers;

			{
				sync::busy_locker l(busy_lock);
				int src_count = 0;
				for (auto i=open_ports.begin();i!=open_ports.end();++i) {
					port&p = *i;
					sync::busy_locker l(p.busy_lock);
					if (!p.running) continue;

					uint64_t this_read = (p.this_read++)%p.blocks;
					src_count++;
					float*src = &p.buffer[p.channels*256*this_read];
					for (int i=0;i<256;i++) {
						float*c = &src[p.channels*i];
						for (int j=0;j<p.channels;j++) {
							float s = se(c[j]);
							if (j==0||j==1) {
								float&d = dst[i*2+j];
								float v = d + s*p.volume;
								if (v>1.0f) v = 1.0f;
								else if (v<-1.0f) v = -1.0f;
								d = v;
							}
							(uint32_t&)c[j] = 0;
						}
					}

					uint64_t next_read = (this_read)%p.blocks;
					*p.next_read = se((uint64_t)next_read);
				}
			}
			dsbuf->Unlock(lock_ptr,lock_size,lock_ptr2,lock_size2);
			if (eqh) {
				event_t d;
				d.source = 0;
				d.data1 = se(counter++);
				d.data2 = 0;
				d.data3 = 0;
				eqh->send(d);
				dbgf(" ++ send\n");
			}
		}
		ds->Release();
	}

	int cellAudioInit() {
		dbgf("cellAudioInit\n");
		sync::busy_locker l(busy_lock);
		if (inited) return err_already_initialized;
		audio_thread = win32_thread(&audio_thread_entry);
		return audio_ok;
	}
	int cellAudioQuit() {
		dbgf("cellAudioQuit\n");
		sync::busy_locker l(busy_lock);
		if (!inited) return err_not_initialized;
		stop=true;
		audio_thread.join();
		stop=false;
		return audio_ok;
	}
	int cellAudioPortOpen(open_port_params*params,uint32_t*port_n) {
		// Audio ports are actually limited to 4, but I see no reason as of yet
		// to impose this limit.
		auto h = port_list.get_new();
		if (!h) return err_port_open;
		port&p = *h;
		p.channels = se(params->channels);
		p.blocks = se(params->blocks);
		p.flags = se(params->flags);
		if (p.flags&0x1000) p.volume = se(params->volume);
		else p.volume = 1.0f;
		p.buffer = (float*)mm_alloc(sizeof(float)*256*p.channels*p.blocks + 8);
		if (!p.buffer) return err_port_open;
		p.next_read = (uint64_t*)(p.buffer+256*p.channels*p.blocks);
		*p.next_read = 0;
		*port_n = se((uint32_t)h.id());
		h.keep();
		sync::busy_locker l(busy_lock);
		open_ports.push_back(p);
		return audio_ok;
	}
	int cellAudioPortStart(uint32_t port_n) {
		auto h = port_list.get(port_n);
		if (!h) return err_port_not_open;
		port&p = *h;
		sync::busy_locker l(p.busy_lock);
		if (p.running) return err_port_already_running;
		p.running = true;
		return audio_ok;
	}
	int cellAudioPortStop(uint32_t port_n) {
		auto h = port_list.get(port_n);
		if (!h) return err_port_not_open;
		port&p = *h;
		sync::busy_locker l(p.busy_lock);
		if (!p.running) return err_port_not_running;
		p.running = false;
		return audio_ok;
	}
	int cellAudioGetPortConfig(uint32_t port_n,port_config*config) {
		auto h = port_list.get(port_n);
		if (!h) {
			memset(config,0,sizeof(*config));
			config->status = se((uint32_t)0x1010); // closed
			return audio_ok;
		}
		port&p = *h;
		sync::busy_locker l(p.busy_lock);
		//memset(portConfig,0,sizeof(*portConfig));
		config->next_read = se((uint32_t)p.next_read);
		if (p.running) config->status = se((uint32_t)2); // running
		else config->status = se((uint32_t)1); // ready
		config->channels = se((uint64_t)p.channels);
		config->blocks = se((uint64_t)p.blocks);
		config->port_size = se((uint32_t)(sizeof(float)*256*p.channels*p.blocks));
		config->port_addr = se((uint32_t)p.buffer);
		return audio_ok;
	}
	int cellAudioGetPortBlockTag(uint32_t port_n,uint64_t block_n,uint64_t*tag) {
		*tag = se((uint64_t)block_n+1000);
		return audio_ok;
	}
	int cellAudioGetPortTimestamp(uint32_t port_n,uint64_t tag,uint64_t*timestamp) {
		xcept("cellAudioGetPortTimestamp port %d, tag %d",port_n,tag);
	}
	int cellAudioSetPortLevel(uint32_t port_n,float level) {
		xcept("cellAudioSetPortLevel");
	}
	int cellAudioPortClose(uint32_t port_n) {
		auto h = port_list.get(port_n);
		if (!h) return err_port_not_open;
		port&p = *h;
		sync::busy_locker l(p.busy_lock);
		open_ports.erase(open_ports.iterator_to(p));
		h.kill();
		return audio_ok;
	}

	int cellAudioCreateNotifyEventQueue(uint32_t*id,uint64_t*ipc_key) {
		xcept("cellAudioCreateNotifyEventQueue");
	}
	int cellAudioSetNotifyEventQueue(uint64_t ipc_key) {
		sync::busy_locker l(busy_lock);
		auto h = ipc_event_queue->get_existing(ipc_key);
		if (!h) return ESRCH;
		eqh = h;
		return audio_ok;
	}
	int cellAudioRemoveNotifyEventQueue(uint64_t key) {
		xcept("cellAudioRemoveNotifyEventQueue");
	}

	int cellAudioAddData(uint32_t port_num,float*src,uint32_t samples,float volume) {
		auto h = port_list.get(port_num);
		if (!h) return err_port_not_open;
		port&p = *h;
		sync::busy_locker l(p.busy_lock);
		float*dst = &p.buffer[p.channels*256*((se(*p.next_read)+1)%p.blocks)];
		for (uint32_t i=0;i<samples;i++) {
			for (int c=0;c<p.channels;c++) {
				float v = se(*dst) + se(*src)*volume;
				if (v>1.0f) v = 1.0f;
				else if (v<-1.0f) v = -1.0f;
				*dst = se(v);
				++src;
				++dst;
			}
		}
		return audio_ok;
	}
	int cellAudioAdd2chData() {
		xcept("cellAudioAdd2chData");
	}
	int cellAudioMiscSetAccessoryVolume() {
		xcept("cellAudioMiscSetAccessoryVolume");
	}

	ah_reg(cellAudio,
		cellAudioInit,
		cellAudioQuit,
		cellAudioPortOpen,
		cellAudioPortStart,
		cellAudioPortStop,
		cellAudioGetPortConfig,
		cellAudioGetPortBlockTag,
		cellAudioGetPortTimestamp,
		cellAudioSetPortLevel,
		cellAudioPortClose,
		//cellAudioCreateNotifyEventQueue,
		cellAudioSetNotifyEventQueue,
		cellAudioRemoveNotifyEventQueue,
		cellAudioAddData,
		cellAudioAdd2chData,
		cellAudioMiscSetAccessoryVolume
		);

}



