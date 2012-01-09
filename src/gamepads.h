
namespace xinput {

#include <xinput.h>
#pragma comment(lib,"xinput.lib")

	struct xinput_pad_driver;

	xinput_pad_driver*con[4];

	struct xinput_pad_driver: pad_driver {
		bool dead;
		int xinput_n, pad_n;
		DWORD last_packet_number;
		xinput_pad_driver() : dead(false), last_packet_number(~0) {
			vendor_id = 0x045e; // Microsoft
			product_id = 0;
		}
		virtual ~xinput_pad_driver() {}
		virtual pad_capabilities get_capabilities() {
			pad_capabilities r;
			memset(&r,0,sizeof(r));
			return r;
		}
		virtual bool has_press() {return false;}
		virtual void set_press(bool) {}
		virtual bool has_sensor() {return false;}
		virtual void set_sensor(bool) {}
		
		virtual pad_data get_data() {
			pad_data r;
			memset(&r,0,sizeof(r));
			XINPUT_STATE st;
			memset(&st,0,sizeof(st));
			if (XInputGetState(xinput_n,&st)) {dead=true;return r;}
			if (st.dwPacketNumber==last_packet_number) return r;
			last_packet_number = st.dwPacketNumber;
			r.len = 8;
			r.ctrl_up = st.Gamepad.wButtons&XINPUT_GAMEPAD_DPAD_UP?1:0;
			r.ctrl_down = st.Gamepad.wButtons&XINPUT_GAMEPAD_DPAD_DOWN?1:0;
			r.ctrl_left = st.Gamepad.wButtons&XINPUT_GAMEPAD_DPAD_LEFT?1:0;
			r.ctrl_right = st.Gamepad.wButtons&XINPUT_GAMEPAD_DPAD_RIGHT?1:0;
			r.ctrl_start = st.Gamepad.wButtons&XINPUT_GAMEPAD_START?1:0;
			r.ctrl_select = st.Gamepad.wButtons&XINPUT_GAMEPAD_BACK?1:0;
			r.ctrl_l3 = st.Gamepad.wButtons&XINPUT_GAMEPAD_LEFT_THUMB?1:0;
			r.ctrl_r3 = st.Gamepad.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB?1:0;
			r.ctrl_l1 = st.Gamepad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER?1:0;
			r.ctrl_r1 = st.Gamepad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER?1:0;
			r.ctrl_cross = st.Gamepad.wButtons&XINPUT_GAMEPAD_A?1:0;
			r.ctrl_circle = st.Gamepad.wButtons&XINPUT_GAMEPAD_B?1:0;
			r.ctrl_square = st.Gamepad.wButtons&XINPUT_GAMEPAD_X?1:0;
			r.ctrl_triangle = st.Gamepad.wButtons&XINPUT_GAMEPAD_Y?1:0;
			r.ctrl_l2 = st.Gamepad.bLeftTrigger>=127;
			r.ctrl_r2 = st.Gamepad.bRightTrigger>=127;
			r.left_stick_x = st.Gamepad.sThumbLX/0x100+0x80;
			r.left_stick_y = st.Gamepad.sThumbLY/0x100+0x80;
			r.right_stick_x = st.Gamepad.sThumbRX/0x100+0x80;
			r.right_stick_y = st.Gamepad.sThumbRY/0x100+0x80;
			return r;
		}
		virtual void clear() {
			last_packet_number = ~0;
		}
	};



	void update() {
		for (int i=0;i<4;i++) {
			if (!con[i]) {
				XINPUT_STATE st;
				memset(&st,0,sizeof(st));
				if (XInputGetState(i,&st)==0 && can_add_pad()) {
					con[i] = new xinput_pad_driver();
					con[i]->xinput_n = i;
					con[i]->pad_n = add_pad(con[i]);
					dbgf("xinput device connected; xinput_n %d, pad_n %d\n",con[i]->xinput_n,con[i]->pad_n);
				}
			} else if (con[i]->dead) {
				dbgf("xinput device disconnected; xinput_n %d, pad_n %d\n",con[i]->xinput_n,con[i]->pad_n);
				remove_pad(con[i]->pad_n);
				con[i] = 0;
			}

		}
	}

}


void update_gamepads() {

	xinput::update();

}


