#include "types.h"
#include "maple_if.h"
#include "maple_helper.h"
#include "maple_devs.h"
#include "maple_cfg.h"
#include <time.h>

#include "deps/zlib/zlib.h"

const char* maple_sega_controller_name = "Dreamcast Controller";
const char* maple_sega_vmu_name = "Visual Memory";
const char* maple_sega_kbd_name = "Emulated Dreamcast Keyboard";
const char* maple_sega_mouse_name = "Emulated Dreamcast Mouse";
const char* maple_sega_dreameye_name_1 = "Dreamcast Camera Flash  Devic";
const char* maple_sega_dreameye_name_2 = "Dreamcast Camera Flash LDevic";
const char* maple_sega_mic_name = "MicDevice for Dreameye";
const char* maple_sega_purupuru_name = "Puru Puru Pack";

const char* maple_sega_brand = "Produced By or Under License From SEGA ENTERPRISES,LTD.";

enum MapleFunctionID
{
	MFID_0_Input       = 0x01000000, //DC Controller, Lightgun buttons, arcade stick .. stuff like that
	MFID_1_Storage     = 0x02000000, //VMU , VMS
	MFID_2_LCD         = 0x04000000, //VMU
	MFID_3_Clock       = 0x08000000, //VMU
	MFID_4_Mic         = 0x10000000, //DC Mic (, dreameye too ?)
	MFID_5_ARGun       = 0x20000000, //Artificial Retina gun ? seems like this one was never developed or smth -- I only remember of lightguns
	MFID_6_Keyboard    = 0x40000000, //DC Keyboard
	MFID_7_LightGun    = 0x80000000, //DC Lightgun
	MFID_8_Vibration   = 0x00010000, //Puru Puru Puur~~~
	MFID_9_Mouse       = 0x00020000, //DC Mouse
	MFID_10_StorageExt = 0x00040000, //Storage ? probably never used
	MFID_11_Camera     = 0x00080000, //DreamEye
};

enum MapleDeviceCommand
{
	MDC_DeviceRequest   = 0x01, //7 words.Note : Initialises device
	MDC_AllStatusReq    = 0x02, //7 words + device dependent ( seems to be 8 words)
	MDC_DeviceReset     = 0x03, //0 words
	MDC_DeviceKill      = 0x04, //0 words
	MDC_DeviceStatus    = 0x05, //Same as MDC_DeviceRequest ?
	MDC_DeviceAllStatus = 0x06, //Same as MDC_AllStatusReq ?

	//Various Functions
	MDCF_GetCondition   = 0x09, //FT
	MDCF_GetMediaInfo   = 0x0A, //FT,PT,3 pad
	MDCF_BlockRead      = 0x0B, //FT,PT,Phase,Block #
	MDCF_BlockWrite     = 0x0C, //FT,PT,Phase,Block #,data ...
	MDCF_GetLastError   = 0x0D, //FT,PT,Phase,Block #
	MDCF_SetCondition   = 0x0E, //FT,data ...
	MDCF_MICControl     = 0x0F, //FT,MIC data ...
	MDCF_ARGunControl   = 0x10, //FT,AR-Gun data ...
};

enum MapleDeviceRV
{
	MDRS_DeviceStatus    = 0x05, //28 words
	MDRS_DeviceStatusAll = 0x06, //28 words + device dependent data
	MDRS_DeviceReply     = 0x07, //0 words
	MDRS_DataTransfer    = 0x08, //FT,depends on the command

	MDRE_UnknownFunction = 0xFE, //0 words
	MDRE_UnknownCmd      = 0xFD, //0 words
	MDRE_TransminAgain   = 0xFC, //0 words
	MDRE_FileError       = 0xFB, //1 word, bitfield
	MDRE_LCDError        = 0xFA, //1 word, bitfield
	MDRE_ARGunError      = 0xF9, //1 word, bitfield
};



#define SWAP32(a) ((((a) & 0xff) << 24)  | (((a) & 0xff00) << 8) | (((a) >> 8) & 0xff00) | (((a) >> 24) & 0xff))

//fill in the info
void maple_device::Setup(u32 prt)
{
	maple_port = prt;
	bus_port = maple_GetPort(prt);
	bus_id = maple_GetBusId(prt);
	logical_port[0] = 'A' + bus_id;
	logical_port[1] = bus_port == 5 ? 'x' : '1' + bus_port;
	logical_port[2] = 0;
}
maple_device::~maple_device()
{
	if (config)
		delete config;
}

/*
	Base class with dma helpers and stuff
*/
struct maple_base: maple_device
{
	u8* dma_buffer_out;
	u32* dma_count_out;

	u8* dma_buffer_in;
	u32 dma_count_in;

	void w8(u8 data) { *((u8*)dma_buffer_out)=data;dma_buffer_out+=1;dma_count_out[0]+=1; }
	void w16(u16 data) { *((u16*)dma_buffer_out)=data;dma_buffer_out+=2;dma_count_out[0]+=2; }
	void w32(u32 data) { *(u32*)dma_buffer_out=data;dma_buffer_out+=4;dma_count_out[0]+=4; }

	void wptr(const void* src,u32 len)
	{
		u8* src8=(u8*)src;
		while(len--)
			w8(*src8++);
	}
	void wstr(const char* str,u32 len)
	{
		size_t ln=strlen(str);
		verify(len>=ln);
		len-=ln;
		while(ln--)
			w8(*str++);

		while(len--)
			w8(0x20);
	}
	
	u8 r8()	  { u8  rv=*((u8*)dma_buffer_in);dma_buffer_in+=1;dma_count_in-=1; return rv; }
	u16 r16() { u16 rv=*((u16*)dma_buffer_in);dma_buffer_in+=2;dma_count_in-=2; return rv; }
	u32 r32() { u32 rv=*(u32*)dma_buffer_in;dma_buffer_in+=4;dma_count_in-=4; return rv; }
	void rptr(const void* dst,u32 len)
	{
		u8* dst8=(u8*)dst;
		while(len--)
			*dst8++=r8();
	}
	u32 r_count() { return dma_count_in; }

	virtual u32 Dma(u32 Command,u32* buffer_in,u32 buffer_in_len,u32* buffer_out,u32& buffer_out_len)
	{
		dma_buffer_out=(u8*)buffer_out;
		dma_count_out=&buffer_out_len;

		dma_buffer_in=(u8*)buffer_in;
		dma_count_in=buffer_in_len;

		return dma(Command);
	}
	virtual u32 dma(u32 cmd)=0;
};

/*
	Sega Dreamcast Controller
	No error checking of any kind, but works just fine
*/
struct maple_sega_controller: maple_base
{
	virtual u32 dma(u32 cmd)
	{
		//printf("maple_sega_controller::dma Called 0x%X;Command %d\n",device_instance->port,Command);
		switch (cmd)
		{
		case MDC_DeviceRequest:
			//caps
			//4
			w32(MFID_0_Input);

			//struct data
			//3*4
			w32( 0xfe060f00);
			w32( 0);
			w32( 0);

			//1	area code
			w8(0xFF);

			//1	direction
			w8(0);
			
			//30
			wstr(maple_sega_controller_name,30);

			//60
			wstr(maple_sega_brand,60);

			//2
			w16(0x01AE); 

			//2
			w16(0x01F4);

			return MDRS_DeviceStatus;

			//controller condition
		case MDCF_GetCondition:
			{
				PlainJoystickState pjs;
				config->GetInput(&pjs);

				//caps
				//4
				w32(MFID_0_Input);

				//state data
				//2 key code
				w16(pjs.kcode);

				//triggers
				//1 R
				w8(pjs.trigger[PJTI_R]);
				//1 L
				w8(pjs.trigger[PJTI_L]);

				//joyx
				//1
				w8(pjs.joy[PJAI_X1]);
				//joyy
				//1
				w8(pjs.joy[PJAI_Y1]);

				//not used
				//1
				w8(0x80);
				//1
				w8(0x80);
			}

		return MDRS_DataTransfer;

		default:
			//printf("UNKOWN MAPLE COMMAND %d\n",cmd);
			return MDRE_UnknownFunction;
		}
	}	
};

/*
	Sega Dreamcast Visual Memory Unit
	This is pretty much done (?)
*/


u8 vmu_default[] = {
		  0x78, 0x9c, 0xed, 0xdd, 0x5b, 0x88, 0x1c, 0x55, 0x1e, 0x07, 0xe0, 0xd3,
		  0x31, 0x6a, 0xf0, 0x3a, 0x0d, 0x0a, 0x5e, 0x18, 0x6a, 0x16, 0x02, 0x2e,
		  0xfb, 0x20, 0x68, 0x22, 0x2c, 0x28, 0x24, 0x21, 0x3d, 0xc2, 0x2e, 0xc6,
		  0x40, 0x26, 0xc3, 0x40, 0x2e, 0x90, 0x96, 0xec, 0xc8, 0x9a, 0xc4, 0x81,
		  0x6c, 0xa2, 0x20, 0x82, 0x11, 0xc1, 0x37, 0x41, 0x9f, 0x14, 0xf1, 0x61,
		  0x37, 0x64, 0xbc, 0x34, 0xb3, 0x17, 0xf0, 0xc1, 0x87, 0x0e, 0x9b, 0xb7,
		  0x55, 0x02, 0x3a, 0x19, 0x30, 0x8d, 0x0c, 0x1c, 0x82, 0xac, 0x17, 0x76,
		  0x59, 0x30, 0x77, 0xb5, 0x92, 0xce, 0xb1, 0xaa, 0x27, 0xc6, 0x98, 0x98,
		  0x68, 0xb2, 0x31, 0x15, 0x93, 0xef, 0x2b, 0x8a, 0x9a, 0xff, 0xef, 0xd4,
		  0xa9, 0x73, 0xe8, 0x3a, 0xf5, 0xd4, 0xc5, 0x74, 0x08, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd5, 0xfa, 0xf3, 0xdc, 0x2f, 0x47,
		  0x52, 0xfa, 0xae, 0xee, 0x7b, 0xfe, 0xf6, 0xaf, 0xe6, 0x0e, 0x84, 0xd0,
		  0x78, 0x20, 0x84, 0x4d, 0xa3, 0x95, 0x4d, 0x0b, 0x00, 0x7e, 0x31, 0xfe,
		  0xf8, 0xfb, 0x87, 0x6f, 0xa8, 0x7a, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0xc0, 0x95, 0xe1, 0x77, 0x9f, 0xfc, 0x69, 0xb2, 0x3c, 0xde, 0x7a, 0xed,
		  0x9c, 0xf0, 0xef, 0xa1, 0xe9, 0x63, 0x55, 0xcf, 0x07, 0x00, 0x00, 0x00,
		  0x00, 0x2e, 0x07, 0x0b, 0xcf, 0x92, 0x5d, 0x5f, 0x7b, 0x69, 0xf0, 0xc7,
		  0xce, 0xfd, 0x29, 0xd7, 0x2b, 0xc5, 0x3b, 0x47, 0x36, 0xbd, 0x58, 0xfb,
		  0xe7, 0x55, 0xc7, 0x8a, 0x6d, 0x7b, 0x2d, 0x84, 0x05, 0x0b, 0x6e, 0xfe,
		  0xed, 0x6d, 0x3f, 0x75, 0x92, 0x5c, 0x14, 0x0b, 0x65, 0x57, 0x5c, 0x56,
		  0xe5, 0xd8, 0xb2, 0x6a, 0xb3, 0x4b, 0x6d, 0x3e, 0xb2, 0x6a, 0xb2, 0x2a,
		  0xc7, 0x96, 0xb9, 0xef, 0xb2, 0xea, 0xb3, 0x2a, 0xc7, 0x96, 0xb9, 0xef,
		  0xb2, 0x8b, 0x9f, 0x5d, 0x6a, 0xf3, 0x91, 0x55, 0x93, 0x55, 0x39, 0xb6,
		  0xcc, 0x7d, 0x97, 0x55, 0x9f, 0x55, 0x39, 0xb6, 0xac, 0xda, 0xfb, 0xfe,
		  0x7f, 0x6a, 0x8c, 0x8d, 0x6d, 0x78, 0x64, 0x7d, 0x73, 0xed, 0x1f, 0x4e,
		  0x04, 0x1b, 0x9b, 0x8f, 0xac, 0xdf, 0xbc, 0x69, 0x6c, 0xe3, 0xb7, 0xc1,
		  0xd0, 0xba, 0xb1, 0x8d, 0x63, 0x8f, 0x37, 0x37, 0x9d, 0xe8, 0xb2, 0xbc,
		  0xa8, 0xd7, 0x7d, 0xef, 0x22, 0xbf, 0x2e, 0xf6, 0xeb, 0x8a, 0xbd, 0x16,
		  0x66, 0xfe, 0xd1, 0xd7, 0x96, 0x0d, 0xff, 0x69, 0xbc, 0xfc, 0xfc, 0xdb,
		  0xc5, 0x94, 0x9f, 0x5a, 0xfc, 0xdf, 0xa2, 0xe3, 0xb3, 0xb3, 0x26, 0xff,
		  0x75, 0xf2, 0xf9, 0x0b, 0x7f, 0x60, 0x22, 0xb2, 0xca, 0xd7, 0x02, 0xf0,
		  0x0b, 0xf3, 0x41, 0xd5, 0x13, 0xa0, 0x22, 0x7d, 0x67, 0x6a, 0x98, 0xf3,
		  0xa3, 0x5d, 0x6b, 0x17, 0x74, 0x22, 0xe7, 0xed, 0xb3, 0xbb, 0xa7, 0xef,
		  0x5f, 0x74, 0x7a, 0x9c, 0xae, 0x3f, 0x2d, 0xfa, 0xcd, 0x45, 0x98, 0x0d,
		  0x17, 0xdb, 0x19, 0x57, 0xf0, 0xec, 0x62, 0x9f, 0x75, 0xa2, 0x38, 0x96,
		  0xce, 0x55, 0xaf, 0xdf, 0xb9, 0x77, 0x3b, 0xa3, 0x2c, 0xec, 0x3d, 0xb7,
		  0x0e, 0x0b, 0xce, 0x7f, 0xf0, 0x67, 0x8a, 0xa9, 0x6f, 0x39, 0xfb, 0xe7,
		  0x76, 0xd3, 0x85, 0xf8, 0xf0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0xb8, 0x24, 0xa4, 0x6d, 0x31, 0xc6, 0x3d, 0xdd, 0x5d, 0x77, 0x64, 0xbb,
		  0xfa, 0xa7, 0xba, 0xf9, 0x93, 0x69, 0xe7, 0xea, 0x18, 0xf3, 0x38, 0x55,
		  0xef, 0xee, 0xea, 0xcf, 0x52, 0xeb, 0xe9, 0x94, 0x2f, 0x89, 0x71, 0xd5,
		  0x5f, 0x9b, 0x59, 0x77, 0xaa, 0x9e, 0x26, 0x27, 0x26, 0x77, 0x1c, 0xdd,
		  0xb6, 0x3a, 0x4e, 0x77, 0x9b, 0x59, 0x7c, 0x21, 0x4b, 0xf9, 0xe4, 0x91,
		  0x1d, 0x69, 0xe7, 0xb6, 0x46, 0xa3, 0xf1, 0xab, 0xb4, 0x7c, 0x38, 0x5b,
		  0x31, 0x91, 0x8e, 0xb4, 0x53, 0xda, 0xd9, 0x6a, 0xb5, 0x26, 0xf3, 0xd6,
		  0x44, 0x9a, 0x48, 0xa9, 0xdd, 0xde, 0x71, 0xf2, 0x97, 0xd2, 0x47, 0xda,
		  0xed, 0xf6, 0xf7, 0xcb, 0xd3, 0xdf, 0xdb, 0x98, 0xd3, 0x7b, 0x0f, 0xb4,
		  0x7c, 0xb5, 0x24, 0xbf, 0x2d, 0x84, 0xc7, 0x76, 0x7f, 0xfa, 0x79, 0x79,
		  0xac, 0xcd, 0x0a, 0x61, 0xd9, 0xa2, 0xc7, 0x9f, 0x68, 0x6e, 0x3c, 0xf9,
		  0xdc, 0x5b, 0x06, 0xeb, 0xb3, 0x9f, 0xdb, 0xfa, 0xca, 0xab, 0xff, 0xb8,
		  0x7a, 0xd1, 0xa3, 0xd7, 0x5c, 0x88, 0x8f, 0x63, 0xf6, 0x59, 0xda, 0x2e,
		  0xcb, 0xdf, 0xcc, 0x7b, 0x68, 0x69, 0x63, 0xe9, 0xd0, 0xd0, 0xc0, 0xc8,
		  0xa2, 0x65, 0x03, 0x3d, 0xcb, 0x06, 0x17, 0x2f, 0x5d, 0xd6, 0x18, 0x58,
		  0xfa, 0xe0, 0xc0, 0xa9, 0x2d, 0x33, 0x4e, 0xed, 0x3f, 0xf3, 0x0a, 0xd0,
		  0x6b, 0xb5, 0xb5, 0x4f, 0xfc, 0xf0, 0xf5, 0xbf, 0x4e, 0x07, 0xd3, 0x9b,
		  0xf9, 0xe8, 0xe1, 0xb5, 0x87, 0x87, 0x0e, 0xdd, 0x7b, 0xf0, 0xde, 0x03,
		  0xf7, 0x1c, 0x2c, 0xb6, 0x03, 0xd9, 0xde, 0x81, 0xfd, 0x7d, 0xfb, 0xc2,
		  0xbe, 0xb0, 0xb7, 0xdd, 0x7e, 0x67, 0xb8, 0xb0, 0xbc, 0xb1, 0x78, 0xde,
		  0xbc, 0xf9, 0x2b, 0xdb, 0xc7, 0xcb, 0xe1, 0xe1, 0xc1, 0x32, 0x29, 0x83,
		  0xcd, 0x2b, 0x8a, 0xe5, 0x55, 0x2e, 0xa9, 0x56, 0x6b, 0x7c, 0xde, 0x60,
		  0x7b, 0xfb, 0xf0, 0xbb, 0x1f, 0xd7, 0xeb, 0x33, 0xcb, 0x27, 0x1f, 0x9f,
		  0xbf, 0x72, 0xfb, 0xaa, 0xb4, 0xa1, 0x96, 0xba, 0x31, 0xee, 0x2e, 0x96,
		  0x71, 0x3e, 0x74, 0xff, 0xd1, 0x9d, 0xdd, 0x58, 0x2b, 0xca, 0x38, 0x51,
		  0xae, 0xeb, 0xf1, 0xfb, 0x52, 0x2b, 0x76, 0x7a, 0xf5, 0x92, 0x5e, 0x3d,
		  0x2f, 0xfd, 0x3d, 0x76, 0xea, 0x65, 0x3d, 0x5d, 0xec, 0x31, 0x9f, 0x9f,
		  0x26, 0xe2, 0x68, 0x56, 0xfe, 0xb9, 0xa7, 0x57, 0x37, 0xd2, 0x1b, 0x71,
		  0x4d, 0x51, 0x77, 0x46, 0xdf, 0x5f, 0x31, 0x53, 0x8f, 0xc7, 0x62, 0xad,
		  0x97, 0xe7, 0x7c, 0x78, 0xbc, 0xee, 0x34, 0xbb, 0xb1, 0xd3, 0x4c, 0x31,
		  0xae, 0xd8, 0x13, 0xf3, 0xc1, 0x34, 0x5e, 0x34, 0x75, 0x46, 0x6b, 0x33,
		  0x33, 0x28, 0xea, 0xad, 0xcd, 0x7a, 0xbd, 0x2f, 0xf4, 0x86, 0x98, 0x58,
		  0x9d, 0x8f, 0xa4, 0xad, 0x59, 0xfa, 0xa2, 0x5e, 0xcf, 0xca, 0xba, 0x68,
		  0x1f, 0x49, 0xaf, 0x17, 0x3d, 0x53, 0x3c, 0xae, 0xec, 0x5f, 0x1e, 0xa7,
		  0x5b, 0x6f, 0x95, 0x87, 0xd1, 0xff, 0x0d, 0x17, 0xf5, 0x9e, 0x38, 0x5a,
		  0x5c, 0xa3, 0x3b, 0x5a, 0x8c, 0x92, 0x95, 0xf5, 0xee, 0xce, 0x9a, 0x6c,
		  0x5f, 0x16, 0x9b, 0xb1, 0xd3, 0x97, 0xf5, 0xce, 0x7f, 0xbd, 0xd6, 0x9d,
		  0x2a, 0xea, 0x2f, 0x3a, 0xbb, 0x7a, 0xed, 0xf1, 0x70, 0xad, 0x78, 0x58,
		  0x3f, 0xaa, 0x67, 0x71, 0x2a, 0xf5, 0xea, 0xbf, 0xd5, 0xe3, 0x96, 0xfe,
		  0x4e, 0xbd, 0xbf, 0xd3, 0xcc, 0x7b, 0xf5, 0x87, 0xf5, 0xb8, 0xa6, 0xff,
		  0x2f, 0x7b, 0xfb, 0x3b, 0x59, 0x51, 0x17, 0xcf, 0x7b, 0x27, 0xef, 0x2f,
		  0x1e, 0xee, 0xe2, 0x79, 0x4f, 0xf9, 0xc8, 0xcf, 0xb9, 0xb6, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0xe0, 0x72, 0x73, 0x34, 0xd9, 0x2e, 0xef, 0x2d, 0x4f, 0xfb, 0xc2, 0xfe,
		  0x70, 0x20, 0x1c, 0x0c, 0x87, 0xc2, 0xe1, 0xf0, 0x65, 0xf8, 0x2a, 0x7c,
		  0x1d, 0xf2, 0x70, 0x24, 0x1c, 0x0d, 0x79, 0xd1, 0x36, 0x7c, 0x8a, 0x5a,
		  0xf9, 0xc3, 0x7a, 0x67, 0x59, 0x2f, 0xb7, 0xbf, 0x52, 0xbf, 0x2b, 0x84,
		  0x81, 0x13, 0xbf, 0x93, 0x98, 0x7a, 0xfb, 0xb1, 0x50, 0x0b, 0xdd, 0x70,
		  0x63, 0xd8, 0x1c, 0xde, 0x0b, 0x59, 0x91, 0x6c, 0xf9, 0xb9, 0x97, 0x2d,
		  0x17, 0xc8, 0x37, 0x7a, 0x6b, 0xe6, 0x6e
};

struct maple_sega_vmu: maple_base
{
	FILE* file;
	u8 flash_data[128*1024];
	u8 lcd_data[192];
	u8 lcd_data_decoded[VMU_SCREEN_WIDTH*VMU_SCREEN_HEIGHT];
	
	virtual bool maple_serialize(void **data, unsigned int *total_size)
	{
		LIBRETRO_SA(flash_data,128*1024);
		LIBRETRO_SA(lcd_data,192);
		LIBRETRO_SA(lcd_data_decoded,48*32);
		return true ;
	}
	virtual bool maple_unserialize(void **data, unsigned int *total_size)
	{
		LIBRETRO_USA(flash_data,128*1024);
		LIBRETRO_USA(lcd_data,192);
		LIBRETRO_USA(lcd_data_decoded,48*32);
		return true ;
	}
	virtual void OnSetup()
	{
		memset(flash_data,0,sizeof(flash_data));
		memset(lcd_data,0,sizeof(lcd_data));
		wchar tempy[512];
		sprintf(tempy,"vmu_save_%s.bin",logical_port);
		string apath=get_writable_data_path(tempy);

		vmu_screen_params[bus_id].vmu_lcd_screen = lcd_data_decoded ;

		uLongf dec_sz = sizeof(flash_data);
		printf("Initializing VMU data...\n");
		int rv=uncompress(flash_data, &dec_sz, vmu_default, sizeof(vmu_default));

		verify(rv == Z_OK);
		verify(dec_sz == sizeof(flash_data));

		file=fopen(apath.c_str(),"rb+");
		if (!file)
		{
			printf("Unable to open VMU save file \"%s\", creating new file\n",apath.c_str());
			file=fopen(apath.c_str(),"wb");
			if (file) {
				fwrite(flash_data, sizeof(flash_data), 1, file);
				fseek(file,0,SEEK_SET);
			} else {
				printf("Unable to create vmu\n");
			}
		}

		if (!file)
		{
			printf("Failed to create VMU save file \"%s\"\n",apath.c_str());
		}
		else
		{
			fread(flash_data,1,sizeof(flash_data),file);
		}

	}
	virtual ~maple_sega_vmu()
	{
		if (file) fclose(file);
	}
	virtual u32 dma(u32 cmd)
	{
		//printf("maple_sega_vmu::dma Called for port 0x%X, Command %d\n",device_instance->port,Command);
		switch (cmd)
		{
		case MDC_DeviceRequest:
			{
				//caps
				//4
				w32(MFID_1_Storage | MFID_2_LCD | MFID_3_Clock);

				//struct data
				//3*4
				w32( 0x403f7e7e); // for clock
				w32( 0x00100500); // for LCD
				w32( 0x00410f00); // for storage
				//1  area code
				w8(0xFF);
				//1  direction
				w8(0);
				//30
				wstr(maple_sega_vmu_name,30);

				//60
				wstr(maple_sega_brand,60);

				//2
				w16(0x007c);

				//2
				w16(0x0082);

				return MDRS_DeviceStatus;
			}

			//in[0] is function used
			//out[0] is function used
		case MDCF_GetMediaInfo:
			{
				u32 function=r32();
				switch(function)
				{
				case MFID_1_Storage:
					{
						w32(MFID_1_Storage);
						
						//total_size;
						w16(0xff);
						//partition_number;
						w16(0);
						//system_area_block;
						w16(0xFF);
						//fat_area_block;
						w16(0xfe);
						//number_fat_areas_block;
						w16(1);
						//file_info_block;
						w16(0xfd);
						//number_info_blocks;
						w16(0xd);
						//volume_icon;
						w8(0);
						//reserved1;
						w8(0);
						//save_area_block;
						w16(0xc8);
						//number_of_save_blocks;
						w16(0x1f);
						//reserverd0 (something for execution files?)
						w32(0);

						return MDRS_DataTransfer;//data transfer
					}
					break;

				case MFID_2_LCD:
					{
						u32 pt=r32();
						if (pt!=0)
						{
							printf("VMU: MDCF_GetMediaInfo -> bad input |%08X|, returning MDRE_UnknownCmd\n",pt);
							return MDRE_UnknownCmd;
						}
						else
						{
							w32(MFID_2_LCD);

							w8(47);             //X dots -1
							w8(31);             //Y dots -1
							w8(((1)<<4) | (0)); //1 Color, 0 contrast levels
							w8(0);              //Padding
							
							return MDRS_DataTransfer;
						}
					}
					break;

				default:
					printf("VMU: MDCF_GetMediaInfo -> Bad function used |%08X|, returning -2\n",function);
					return MDRE_UnknownFunction;//bad function
				}
			}
			break;

		case MDCF_BlockRead:
			{
				u32 function=r32();
				switch(function)
				{
				case MFID_1_Storage:
					{
						w32(MFID_1_Storage);
						u32 xo=r32();
						u32 Block = (SWAP32(xo))&0xffff;
						w32(xo);

						if (Block>255)
						{
							printf("Block read : %d\n",Block);
							printf("BLOCK READ ERROR\n");
							Block&=255;
						}
						wptr(flash_data+Block*512,512);

						return MDRS_DataTransfer;//data transfer
					}
					break;

				case MFID_2_LCD:
					{
						w32(MFID_2_LCD);
						w32(r32()); // mnn ?
						wptr(flash_data,192);
						
						return MDRS_DataTransfer;//data transfer
					}
					break;

				case MFID_3_Clock:
					{
						if (r32()!=0)
						{
							printf("VMU: Block read: MFID_3_Clock : invalid params \n");
							return MDRE_TransminAgain; //invalid params
						}
						else
						{
							w32(MFID_3_Clock);

							time_t now;
							time(&now);
							tm* timenow=localtime(&now);
							
							u8* timebuf=dma_buffer_out;

							w8((timenow->tm_year+1900)%256);
							w8((timenow->tm_year+1900)/256);

							w8(timenow->tm_mon+1);
							w8(timenow->tm_mday);

							w8(timenow->tm_hour);
							w8(timenow->tm_min);
							w8(timenow->tm_sec);
							w8(0);

							printf("VMU: CLOCK Read-> datetime is %04d/%02d/%02d ~ %02d:%02d:%02d!\n",timebuf[0]+timebuf[1]*256,timebuf[2],timebuf[3],timebuf[4],timebuf[5],timebuf[6]);

							return MDRS_DataTransfer;//transfer reply ...
						}
					}
					break;

				default:
					printf("VMU: cmd MDCF_BlockRead -> Bad function |%08X| used, returning -2\n",function);
					return MDRE_UnknownFunction;//bad function
				}
			}
			break;

		case MDCF_BlockWrite:
			{
				switch(r32())
				{
					case MFID_1_Storage:
					{
						u32 bph=r32();
						u32 Block = (SWAP32(bph))&0xffff;
						u32 Phase = ((SWAP32(bph))>>16)&0xff; 
						u32 write_adr=Block*512+Phase*(512/4);
						u32 write_len=r_count();
						rptr(&flash_data[write_adr],write_len);

						if (file)
						{
							fseek(file,write_adr,SEEK_SET);
							fwrite(&flash_data[write_adr],1,write_len,file);
							fflush(file);
						}
						else
						{
							printf("Failed to save VMU %s data\n",logical_port);
						}
						return MDRS_DeviceReply;//just ko
					}
					break;
					

					case MFID_2_LCD:
					{
						u32 wat=r32();
						rptr(lcd_data,192);
						
						u8 white=0xff,black=0x00;

						for(int y=0;y<32;++y)
						{
							u8* dst=lcd_data_decoded+y*48;
							u8* src=lcd_data+6*y+5;
							for(int x=0;x<6;++x)
							{
								u8 col=*src--;
								for(int l=0;l<8;l++)
								{
									*dst++=col&1?black:white;
									col>>=1;
								}
							}
						}
						config->SetImage(lcd_data_decoded);
#if !defined(__MACH__)
                  push_vmu_screen(lcd_data_decoded);
#endif
#if 0
						// Update LCD window
						if (!dev->lcd.visible)
						{
							dev->lcd.visible=true;
							ShowWindow(dev->lcd.handle,SHOW_OPENNOACTIVATE);
						}
						
							
							InvalidateRect(dev->lcd.handle,NULL,FALSE);
						}

						// Logitech G series stuff start
	#ifdef _HAS_LGLCD_
						{
							lgLcdBitmap160x43x1 bmp;
							bmp.hdr.Format = LGLCD_BMP_FORMAT_160x43x1;

							const u8 white=0x00;
							const u8 black=0xFF;

							//make it all black...
							memset(bmp.pixels,black,sizeof(bmp.pixels));

							//decode from the VMU
							for(int y=0;y<32;++y)
							{
								u8 *dst=bmp.pixels+5816+((-y)*(48+112)); //ugly way to make things look right :p
								u8 *src=dev->lcd.data+6*y+5;
								for(int x=0;x<48/8;++x)
								{
									u8 val=*src;
									for(int m=0;m<8;++m)
									{
										if(val&(1<<(m)))
											*dst++=black;
										else
											*dst++=white;
									}
									--src;
								}
							}

							//Set the damned bits
							res = lgLcdUpdateBitmap(openContext.device, &bmp.hdr, LGLCD_ASYNC_UPDATE(LGLCD_PRIORITY_NORMAL));
						}
	#endif
						//Logitech G series stuff end
#endif
						return  MDRS_DeviceReply;//just ko
					}
					break;

					case MFID_3_Clock:
					{
						if (r32()!=0 || r_count()!=8)
							return MDRE_TransminAgain;	//invalid params ...
						else
						{
							u8 timebuf[8];
							rptr(timebuf,8);
							printf("VMU: CLOCK Write-> datetime is %04d/%02d/%02d ~ %02d:%02d:%02d! Nothing set tho ...\n",timebuf[0]+timebuf[1]*256,timebuf[2],timebuf[3],timebuf[4],timebuf[5],timebuf[6]);
							return  MDRS_DeviceReply;//ok !
						}
					}
					break;

					default:
					{
						printf("VMU: command MDCF_BlockWrite -> Bad function used, returning MDRE_UnknownFunction\n");
						return  MDRE_UnknownFunction;//bad function
					}
				}
			}
			break;

		case MDCF_GetLastError:
			return MDRS_DeviceReply;//just ko

		case MDCF_SetCondition:
			{
				switch(r32())
				{
				case MFID_3_Clock:
					{
						u32 bp=r32();
						if (bp)
						{
							printf("BEEP : %08X\n",bp);
						}
						return  MDRS_DeviceReply;//just ko
					}
					break;

				default:
					{
						printf("VMU: command MDCF_SetCondition -> Bad function used, returning MDRE_UnknownFunction\n");
						return MDRE_UnknownFunction;//bad function
					}
					break;
				}
			}


		default:
			//printf("Unknown MAPLE COMMAND %d\n",cmd);
			return MDRE_UnknownCmd;
		}
	}	
};


struct maple_microphone: maple_base
{
	u8 micdata[SIZE_OF_MIC_DATA];

	virtual bool maple_serialize(void **data, unsigned int *total_size)
	{
		LIBRETRO_SA(micdata,SIZE_OF_MIC_DATA);
		return true ;
	}
	virtual bool maple_unserialize(void **data, unsigned int *total_size)
	{
		LIBRETRO_USA(micdata,SIZE_OF_MIC_DATA);
		return true ;
	}
	virtual void OnSetup()
	{
		memset(micdata,0,sizeof(micdata));
	}

	virtual u32 dma(u32 cmd)
	{
		//printf("maple_microphone::dma Called 0x%X;Command %d\n",this->maple_port,cmd);
		//LOGD("maple_microphone::dma Called 0x%X;Command %d\n",this->maple_port,cmd);

		switch (cmd)
		{
		case MDC_DeviceRequest:
			printf("maple_microphone::dma MDC_DeviceRequest");
			//this was copied from the controller case with just the id and name replaced!

			//caps
			//4
			w32(MFID_4_Mic);

			//struct data
			//3*4
			w32( 0xfe060f00);
			w32( 0);
			w32( 0);

			//1	area code
			w8(0xFF);

			//1	direction
			w8(0);
			
			//30
			wstr(maple_sega_mic_name,30);

			//60
			wstr(maple_sega_brand,60);

			//2
			w16(0x01AE); 

			//2
			w16(0x01F4);

			return MDRS_DeviceStatus;

		case MDCF_GetCondition:
			{
				printf("maple_microphone::dma MDCF_GetCondition");
				//this was copied from the controller case with just the id replaced!

				//PlainJoystickState pjs;
				//config->GetInput(&pjs);
				//caps
				//4
				w32(MFID_4_Mic);

				//state data
				//2 key code
				//w16(pjs.kcode);

				//triggers
				//1 R
				//w8(pjs.trigger[PJTI_R]);
				//1 L
				//w8(pjs.trigger[PJTI_L]);

				//joyx
				//1
				//w8(pjs.joy[PJAI_X1]);
				//joyy
				//1
				//w8(pjs.joy[PJAI_Y1]);

				//not used
				//1
				w8(0x80);
				//1
				w8(0x80);
			}

			return MDRS_DataTransfer;

		case MDC_DeviceReset:
			//uhhh do nothing?
			printf("maple_microphone::dma MDC_DeviceReset");
			return MDRS_DeviceReply;

		case MDCF_MICControl:
		{
			//LOGD("maple_microphone::dma handling MDCF_MICControl %d\n",cmd);
			//MONEY
			u32 function=r32();
			//LOGD("maple_microphone::dma MDCF_MICControl function (1st word) %#010x\n", function);
			//LOGD("maple_microphone::dma MDCF_MICControl words: %d\n", dma_count_in);

			switch(function)
			{
			case MFID_4_Mic:
			{
				//MAGIC HERE
				//http://dcemulation.org/phpBB/viewtopic.php?f=34&t=69600
				// <3 <3 BlueCrab <3 <3
				/*
				2nd word               What it does:
				0x0000??03          Sets the amplifier gain, ?? can be from 00 to 1F
									0x0f = default
				0x00008002          Enables recording
				0x00000001          Returns sampled data while recording is enabled
									While not enabled, returns status of the mic.
				0x00000002          Disables recording
				 *
				 */
				u32 secondword=r32();
				//LOGD("maple_microphone::dma MDCF_MICControl subcommand (2nd word) %#010x\n", subcommand);

				u32 subcommand = secondword & 0xFF; //just get last byte for now, deal with params later

				//LOGD("maple_microphone::dma MDCF_MICControl (3rd word) %#010x\n", r32());
				//LOGD("maple_microphone::dma MDCF_MICControl (4th word) %#010x\n", r32());
				switch(subcommand)
				{
				case 0x01:
				{
					//printf("maple_microphone::dma MDCF_MICControl someone wants some data! (2nd word) %#010x\n", secondword);

					w32(MFID_4_Mic);

					//from what i can tell this is up to spec but results in transmit again
					//w32(secondword);

					//32 bit header
					w8(0x04);//status (just the bit for recording)
					w8(0x0f);//gain (default)
					w8(0);//exp ?
					if(get_mic_data(micdata)){
						w8(240);//ct (240 samples)
						wptr(micdata, SIZE_OF_MIC_DATA);
					}else
					{
						w8(0);
					}

					return MDRS_DataTransfer;
				}
				case 0x02:
					printf("maple_microphone::dma MDCF_MICControl toggle recording %#010x\n",secondword);
					return MDRS_DeviceReply;
				case 0x03:
					printf("maple_microphone::dma MDCF_MICControl set gain %#010x\n",secondword);
					return MDRS_DeviceReply;
				case MDRE_TransminAgain:
					printf("maple_microphone::dma MDCF_MICControl MDRE_TransminAgain");
					//apparently this doesnt matter
					//wptr(micdata, SIZE_OF_MIC_DATA);
					return MDRS_DeviceReply;//MDRS_DataTransfer;
				default:
					printf("maple_microphone::dma UNHANDLED secondword %#010x\n",secondword);
					break;
				}
			}
			default:
				printf("maple_microphone::dma UNHANDLED function %#010x\n",function);
				break;
			}
		}

		default:
			printf("maple_microphone::dma UNHANDLED MAPLE COMMAND %d\n",cmd);
			return MDRE_UnknownFunction;
		}
	}	
};

struct maple_sega_purupuru : maple_base
{
   u16 AST, AST_ms;
   u32 VIBSET;

   virtual bool maple_serialize(void **data, unsigned int *total_size)
   {
      LIBRETRO_S(AST);
      LIBRETRO_S(AST_ms);
      LIBRETRO_S(VIBSET);
      return true ;
   }
   virtual bool maple_unserialize(void **data, unsigned int *total_size)
   {
      LIBRETRO_US(AST);
      LIBRETRO_US(AST_ms);
      LIBRETRO_US(VIBSET);
      return true ;
   }
   virtual u32 dma(u32 cmd)
   {
      switch (cmd)
      {
         case MDC_DeviceRequest:
            //caps
            //4
            w32(MFID_8_Vibration);

            //struct data
            //3*4
            w32(0x00000101);
            w32(0);
            w32(0);

            //1	area code
            w8(0xFF);

            //1	direction
            w8(0);

            //30
            wstr(maple_sega_purupuru_name, 30);

            //60
            wstr(maple_sega_brand, 60);

            //2
            w16(0x00C8);

            //2
            w16(0x0640);

            return MDRS_DeviceStatus;

            //get last vibration
         case MDCF_GetCondition:

            w32(MFID_8_Vibration);

            w32(VIBSET);

            return MDRS_DataTransfer;

         case MDCF_GetMediaInfo:

            w32(MFID_8_Vibration);

            // PuruPuru vib specs
            w32(0x3B07E010);

            return MDRS_DataTransfer;

         case MDCF_BlockRead:

            w32(MFID_8_Vibration);
            w32(0);

            w16(2);
            w16(AST);

            return MDRS_DataTransfer;

         case MDCF_BlockWrite:

            //Auto-stop time
            AST = dma_buffer_in[10];
            AST_ms = AST * 250 + 250;

            return MDRS_DeviceReply;

         case MDCF_SetCondition:

            VIBSET = *(u32*)&dma_buffer_in[4];
            //Do the rumble thing!
            config->SetVibration(VIBSET);

            return MDRS_DeviceReply;

         default:
            //printf("UNKOWN MAPLE COMMAND %d\n",cmd);
            return MDRE_UnknownFunction;
      }
   }
};


char EEPROM[0x100];
bool EEPROM_loaded = false;

_NaomiState State;


enum NAOMI_KEYS
{
	NAOMI_SERVICE_KEY_1 = 1 << 0,
	NAOMI_TEST_KEY_1 = 1 << 1,
	NAOMI_SERVICE_KEY_2 = 1 << 2,
	NAOMI_TEST_KEY_2 = 1 << 3,

	NAOMI_START_KEY = 1 << 4,

	NAOMI_UP_KEY = 1 << 5,
	NAOMI_DOWN_KEY = 1 << 6,
	NAOMI_LEFT_KEY = 1 << 7,
	NAOMI_RIGHT_KEY = 1 << 8,

	NAOMI_BTN0_KEY = 1 << 9,
	NAOMI_BTN1_KEY = 1 << 10,
	NAOMI_BTN2_KEY = 1 << 11,
	NAOMI_BTN3_KEY = 1 << 12,
	NAOMI_BTN4_KEY = 1 << 13,
	NAOMI_BTN5_KEY = 1 << 14,
	NAOMI_COIN_KEY = 1 << 15,
};


void printState(u32 cmd, u32* buffer_in, u32 buffer_in_len)
{
	printf("Command : 0x%X", cmd);
	if (buffer_in_len>0)
		printf(",Data : %d bytes\n", buffer_in_len);
	else
		printf("\n");
	buffer_in_len >>= 2;
	while (buffer_in_len-->0)
	{
		printf("%08X ", *buffer_in++);
		if (buffer_in_len == 0)
			printf("\n");
	}
}

extern u16 kcode[4];
extern s8 joyx[4],joyy[4];
extern char eeprom_file[PATH_MAX];

/*
Sega Dreamcast Controller
No error checking of any kind, but works just fine
*/
struct maple_naomi_jamma : maple_sega_controller
{
	virtual u32 dma(u32 cmd)
	{
		u32* buffer_in = (u32*)dma_buffer_in;
		u32* buffer_out = (u32*)dma_buffer_out;
		
		u8* buffer_in_b = dma_buffer_in;
		u8* buffer_out_b = dma_buffer_out;

		u32& buffer_out_len = *dma_count_out;
		u32 buffer_in_len = dma_count_in;

		switch (cmd)
		{
		case 0x86:
		{
			u32 subcode = *(u8*)buffer_in;
			//printf("Naomi 0x86 : %x\n",SubCode);
			switch (subcode)
			{
			case 0x15:
			case 0x33:
			{
				PlainJoystickState pjs;
				config->GetInput(&pjs);

				buffer_out[0] = 0xffffffff;
				buffer_out[1] = 0xffffffff;
				u32 keycode1 = ~kcode[0];
				u32 keycode2 = ~kcode[1];
				u32 keycode3 = ~kcode[2];
				u32 keycode4 = ~kcode[3];

				if (keycode1&NAOMI_SERVICE_KEY_2)		//Service
					buffer_out[0] &= ~(1 << 0x1b);

				if (keycode1&NAOMI_TEST_KEY_2)		//Test
					buffer_out[0] &= ~(1 << 0x1a);

				if (State.Mode == 0 && subcode != 0x33)	//Get Caps
				{
					buffer_out_b[0x11 + 1] = 0x8E;	//Valid data check
					buffer_out_b[0x11 + 2] = 0x01;
					buffer_out_b[0x11 + 3] = 0x00;
					buffer_out_b[0x11 + 4] = 0xFF;
					buffer_out_b[0x11 + 5] = 0xE0;
					buffer_out_b[0x11 + 8] = 0x01;

					switch (State.Cmd)
					{
						//Reset, in : 2 bytes, out : 0
					case 0xF0:
						break;

						//Find nodes?
						//In addressing Slave address, in : 2 bytes, out : 1
					case 0xF1:
					{
						buffer_out_len = 4 * 4;
					}
					break;

					//Speed Change, in : 2 bytes, out : 0
					case 0xF2:
						break;

						//Name
						//"In the I / O ID" "Reading each slave ID data"
						//"NAMCO LTD.; I / O PCB-1000; ver1.0; for domestic only, no analog input"
						//in : 1 byte, out : max 102
					case 0x10:
					{
						static char ID1[102] = "nullDC Team; I/O Plugin-1; ver0.2; for nullDC or other emus";
						buffer_out_b[0x8 + 0x10] = (u8)strlen(ID1) + 3;
						for (int i = 0; ID1[i] != 0; ++i)
						{
							buffer_out_b[0x8 + 0x13 + i] = ID1[i];
						}
					}
					break;

					//CMD Version
					//REV in command|Format command to read the (revision)|One|Two 
					//in : 1 byte, out : 2 bytes
					case 0x11:
					{
						buffer_out_b[0x8 + 0x13] = 0x13;
					}
					break;

					//JVS Version
					//In JV REV|JAMMA VIDEO standard reading (revision)|One|Two 
					//in : 1 byte, out : 2 bytes
					case 0x12:
					{
						buffer_out_b[0x8 + 0x13] = 0x30;
					}
					break;

					//COM Version
					//VER in the communication system|Read a communication system compliant version of |One|Two
					//in : 1 byte, out : 2 bytes
					case 0x13:
					{
						buffer_out_b[0x8 + 0x13] = 0x10;
					}
					break;

					//Features
					//Check in feature |Each features a slave to read |One |6 to
					//in : 1 byte, out : 6 + (?)
					case 0x14:
					{
						unsigned char *FeatPtr = buffer_out_b + 0x8 + 0x13;
						buffer_out_b[0x8 + 0x9 + 0x3] = 0x0;
						buffer_out_b[0x8 + 0x9 + 0x9] = 0x1;
#define ADDFEAT(Feature,Count1,Count2,Count3)	*FeatPtr++=Feature; *FeatPtr++=Count1; *FeatPtr++=Count2; *FeatPtr++=Count3;
						if(settings.mapping.JammaSetup)
						{
							// 4 players with no axis
							if(settings.mapping.JammaSetup == 1)
							{
								ADDFEAT(1, 4, 12, 0);		//Feat 1=Digital Inputs.  4 Players. 12 bits
								ADDFEAT(2, 4, 0, 0);		//Feat 2=Coin inputs. 4 Inputs
								ADDFEAT(3, 0, 0, 0);		//Feat 3=Analog. 0 Chans
							}
						}
						else
						{
							// 2 players with 2 axis each
							ADDFEAT(1, 2, 12, 0);		//Feat 1=Digital Inputs.  2 Players. 12 bits
							ADDFEAT(2, 2, 0, 0);		//Feat 2=Coin inputs. 2 Inputs
							ADDFEAT(3, 4, 0, 0);		//Feat 3=Analog. 4 Chans
						}
						ADDFEAT(0, 0, 0, 0);		//End of list
					}
					break;

					//Coin counters
					case 0x21:
						break;

					default:
						printf("unknown CAP %X\n", State.Cmd);
						return 0;
					}
					buffer_out_len = 4 * 4;
				}
				else if (State.Mode == 1 || State.Mode == 2 || subcode == 0x33)	//Get Data
				{
					unsigned char glbl = 0x00;
					unsigned char p1_1 = 0x00;
					unsigned char p1_2 = 0x00;
					unsigned char p2_1 = 0x00;
					unsigned char p2_2 = 0x00;
					unsigned char p3_1 = 0x00;
					unsigned char p3_2 = 0x00;
					unsigned char p4_1 = 0x00;
					unsigned char p4_2 = 0x00;
					unsigned short analog_p1_x = 0x8000;
					unsigned short analog_p1_y = 0x8000;
					unsigned short analog_p2_x = 0x8000;
					unsigned short analog_p2_y = 0x8000;
					static unsigned char LastKey[256];
					static unsigned short coin1 = 0x0000;
					static unsigned short coin2 = 0x0000;
					static unsigned short coin3 = 0x0000;
					static unsigned short coin4 = 0x0000;
					unsigned char Key[256] = { 0 };

					analog_p1_x = (joyx[bus_id*2+0]*256)+32768;
					analog_p1_y = (joyy[bus_id*2+0]*256)+32768;
					analog_p2_x = (joyx[bus_id*2+1]*256)+32768;
					analog_p2_y = (joyy[bus_id*2+1]*256)+32768;

#if 0
#ifdef _WIN32
               GetKeyboardState(Key);
#endif
#endif
					if (keycode1&NAOMI_SERVICE_KEY_1)			//Service ?
						glbl |= 0x80;
					if (keycode1&NAOMI_TEST_KEY_1)			//Test
						p1_1 |= 0x40;
					if (keycode1&NAOMI_START_KEY)			//start ?
						p1_1 |= 0x80;
					if (keycode1&NAOMI_UP_KEY)			//up
						p1_1 |= 0x20;
					if (keycode1&NAOMI_DOWN_KEY)		//down
						p1_1 |= 0x10;
					if (keycode1&NAOMI_LEFT_KEY)		//left
						p1_1 |= 0x08;
					if (keycode1&NAOMI_RIGHT_KEY)		//right
						p1_1 |= 0x04;
					if (keycode1&NAOMI_BTN0_KEY)			//btn1
						p1_1 |= 0x02;
					if (keycode1&NAOMI_BTN1_KEY)			//btn2
						p1_1 |= 0x01;
					if (keycode1&NAOMI_BTN2_KEY)			//btn3
						p1_2 |= 0x80;
					if (keycode1&NAOMI_BTN3_KEY)			//btn4
						p1_2 |= 0x40;
					if (keycode1&NAOMI_BTN4_KEY)			//btn5
						p1_2 |= 0x20;
					if (keycode1&NAOMI_BTN5_KEY)			//btn6
						p1_2 |= 0x10;

					if (keycode2&NAOMI_TEST_KEY_1)			//Test
						p2_1 |= 0x40;
					if (keycode2&NAOMI_START_KEY)			//start ?
						p2_1 |= 0x80;
					if (keycode2&NAOMI_UP_KEY)			//up
						p2_1 |= 0x20;
					if (keycode2&NAOMI_DOWN_KEY)		//down
						p2_1 |= 0x10;
					if (keycode2&NAOMI_LEFT_KEY)		//left
						p2_1 |= 0x08;
					if (keycode2&NAOMI_RIGHT_KEY)		//right
						p2_1 |= 0x04;
					if (keycode2&NAOMI_BTN0_KEY)			//btn1
						p2_1 |= 0x02;
					if (keycode2&NAOMI_BTN1_KEY)			//btn2
						p2_1 |= 0x01;
					if (keycode2&NAOMI_BTN2_KEY)			//btn3
						p2_2 |= 0x80;
					if (keycode2&NAOMI_BTN3_KEY)			//btn4
						p2_2 |= 0x40;
					if (keycode2&NAOMI_BTN4_KEY)			//btn5
						p2_2 |= 0x20;
					if (keycode2&NAOMI_BTN5_KEY)			//btn6
						p2_2 |= 0x10;

					if (keycode3&NAOMI_TEST_KEY_1)			//Test
						p3_1 |= 0x40;
					if (keycode3&NAOMI_START_KEY)			//start ?
						p3_1 |= 0x80;
					if (keycode3&NAOMI_UP_KEY)			//up
						p3_1 |= 0x20;
					if (keycode3&NAOMI_DOWN_KEY)		//down
						p3_1 |= 0x10;
					if (keycode3&NAOMI_LEFT_KEY)		//left
						p3_1 |= 0x08;
					if (keycode3&NAOMI_RIGHT_KEY)		//right
						p3_1 |= 0x04;
					if (keycode3&NAOMI_BTN0_KEY)			//btn1
						p3_1 |= 0x02;
					if (keycode3&NAOMI_BTN1_KEY)			//btn2
						p3_1 |= 0x01;
					if (keycode3&NAOMI_BTN2_KEY)			//btn3
						p3_2 |= 0x80;
					if (keycode3&NAOMI_BTN3_KEY)			//btn4
						p3_2 |= 0x40;
					if (keycode3&NAOMI_BTN4_KEY)			//btn5
						p3_2 |= 0x20;
					if (keycode3&NAOMI_BTN5_KEY)			//btn6
						p3_2 |= 0x10;

					if (keycode4&NAOMI_TEST_KEY_1)			//Test
						p4_1 |= 0x40;
					if (keycode4&NAOMI_START_KEY)			//start ?
						p4_1 |= 0x80;
					if (keycode4&NAOMI_UP_KEY)			//up
						p4_1 |= 0x20;
					if (keycode4&NAOMI_DOWN_KEY)		//down
						p4_1 |= 0x10;
					if (keycode4&NAOMI_LEFT_KEY)		//left
						p4_1 |= 0x08;
					if (keycode4&NAOMI_RIGHT_KEY)		//right
						p4_1 |= 0x04;
					if (keycode4&NAOMI_BTN0_KEY)			//btn1
						p4_1 |= 0x02;
					if (keycode4&NAOMI_BTN1_KEY)			//btn2
						p4_1 |= 0x01;
					if (keycode4&NAOMI_BTN2_KEY)			//btn3
						p4_2 |= 0x80;
					if (keycode4&NAOMI_BTN3_KEY)			//btn4
						p4_2 |= 0x40;
					if (keycode4&NAOMI_BTN4_KEY)			//btn5
						p4_2 |= 0x20;
					if (keycode4&NAOMI_BTN5_KEY)			//btn6
						p4_2 |= 0x10;

					static bool old_coin1 = false;
					static bool old_coin2 = false;
					static bool old_coin3 = false;
					static bool old_coin4 = false;

					if ((old_coin1 == false) && (keycode1&NAOMI_COIN_KEY))
						coin1++;
					old_coin1 = (keycode1&NAOMI_COIN_KEY) ? true : false;

					if ((old_coin2 == false) && (keycode2&NAOMI_COIN_KEY))
						coin2++;
					old_coin2 = (keycode2&NAOMI_COIN_KEY) ? true : false;

					if ((old_coin3 == false) && (keycode3&NAOMI_COIN_KEY))
						coin3++;
					old_coin3 = (keycode3&NAOMI_COIN_KEY) ? true : false;

					if ((old_coin4 == false) && (keycode4&NAOMI_COIN_KEY))
						coin4++;
					old_coin4 = (keycode4&NAOMI_COIN_KEY) ? true : false;

					buffer_out_b[0x11 + 0] = 0x00;
					buffer_out_b[0x11 + 1] = 0x8E;	//Valid data check
					buffer_out_b[0x11 + 2] = 0x01;
					buffer_out_b[0x11 + 3] = 0x00;
					buffer_out_b[0x11 + 4] = 0xFF;
					buffer_out_b[0x11 + 5] = 0xE0;
					buffer_out_b[0x11 + 8] = 0x01;

					//memset(OutData+8+0x11,0x00,0x100);

					buffer_out_b[8 + 0x12 + 0] = 1;
					buffer_out_b[8 + 0x12 + 1] = glbl;
					if(settings.mapping.JammaSetup)
					{
						// 4 players with no analog stick setup
						if(settings.mapping.JammaSetup == 1)
						{
							buffer_out_b[8 + 0x12 + 2] = p1_1;
							buffer_out_b[8 + 0x12 + 3] = p1_2;
							buffer_out_b[8 + 0x12 + 4] = p2_1;
							buffer_out_b[8 + 0x12 + 5] = p2_2;
							buffer_out_b[8 + 0x12 + 6] = p3_1;
							buffer_out_b[8 + 0x12 + 7] = p3_2;
							buffer_out_b[8 + 0x12 + 8] = p4_1;
							buffer_out_b[8 + 0x12 + 9] = p4_2;
							buffer_out_b[8 + 0x12 + 10] = 1;
							buffer_out_b[8 + 0x12 + 11] = coin1 >> 8;
							buffer_out_b[8 + 0x12 + 12] = coin1 & 0xff;
							buffer_out_b[8 + 0x12 + 13] = coin2 >> 8;
							buffer_out_b[8 + 0x12 + 14] = coin2 & 0xff;
							buffer_out_b[8 + 0x12 + 15] = coin3 >> 8;
							buffer_out_b[8 + 0x12 + 16] = coin3 & 0xff;
							buffer_out_b[8 + 0x12 + 17] = coin4 >> 8;
							buffer_out_b[8 + 0x12 + 18] = coin4 & 0xff;
							buffer_out_b[8 + 0x12 + 19] = 1;
							buffer_out_b[8 + 0x12 + 20] = 0x00;
						}
					}
					else
					{
						// 2 players with analog sticks
						buffer_out_b[8 + 0x12 + 2] = p1_1;
						buffer_out_b[8 + 0x12 + 3] = p1_2;
						buffer_out_b[8 + 0x12 + 4] = p2_1;
						buffer_out_b[8 + 0x12 + 5] = p2_2;
						buffer_out_b[8 + 0x12 + 6] = 1;
						buffer_out_b[8 + 0x12 + 7] = coin1 >> 8;
						buffer_out_b[8 + 0x12 + 8] = coin1 & 0xff;
						buffer_out_b[8 + 0x12 + 9] = coin2 >> 8;
						buffer_out_b[8 + 0x12 + 10] = coin2 & 0xff;
						buffer_out_b[8 + 0x12 + 11] = 1;
						buffer_out_b[8 + 0x12 + 12] = analog_p1_y >> 8;
						buffer_out_b[8 + 0x12 + 13] = analog_p1_y;
						buffer_out_b[8 + 0x12 + 14] = analog_p1_x >> 8;
						buffer_out_b[8 + 0x12 + 15] = analog_p1_x;
						buffer_out_b[8 + 0x12 + 16] = analog_p2_y >> 8;
						buffer_out_b[8 + 0x12 + 17] = analog_p2_y;
						buffer_out_b[8 + 0x12 + 18] = analog_p2_x >> 8;
						buffer_out_b[8 + 0x12 + 19] = analog_p2_x;
						buffer_out_b[8 + 0x12 + 20] = 0x00;
					}

					memcpy(LastKey, Key, sizeof(Key));

					if (State.Mode == 1)
					{
						buffer_out_b[0x11 + 0x7] = 19;
						buffer_out_b[0x11 + 0x4] = 19 + 5;
					}
					else
					{
						buffer_out_b[0x11 + 0x7] = 17;
						buffer_out_b[0x11 + 0x4] = 17 - 1;
					}

					//OutLen=8+0x11+16;
					buffer_out_len = 8 + 0x12 + 22;
				}
				/*ID.Keys=0xFFFFFFFF;
				if(GetKeyState(VK_F1)&0x8000)		//Service
				ID.Keys&=~(1<<0x1b);
				if(GetKeyState(VK_F2)&0x8000)		//Test
				ID.Keys&=~(1<<0x1a);
				memcpy(OutData,&ID,sizeof(ID));
				OutData[0x12]=0x8E;
				OutLen=sizeof(ID);
				*/
			}
			return 8;

			case 0x17:	//Select Subdevice
			{
				State.Mode = 0;
				State.Cmd = buffer_in_b[8];
				State.Node = buffer_in_b[9];
				buffer_out_len = 0;
			}
			return (7);

			case 0x27:	//Transfer request
			{
				State.Mode = 1;
				State.Cmd = buffer_in_b[8];
				State.Node = buffer_in_b[9];
				buffer_out_len = 0;
			}
			return (7);
			case 0x21:		//Transfer request with repeat
			{
				State.Mode = 2;
				State.Cmd = buffer_in_b[8];
				State.Node = buffer_in_b[9];
				buffer_out_len = 0;
			}
			return (7);

			case 0x0B:	//EEPROM write
			{
				int address = buffer_in_b[1];
				int size = buffer_in_b[2];
				//printf("EEprom write %08X %08X\n",address,size);
				//printState(Command,buffer_in,buffer_in_len);
				memcpy(EEPROM + address, buffer_in_b + 4, size);

				FILE* f = fopen(eeprom_file, "wb");
				if (f)
				{
					fwrite(EEPROM, 1, 0x80, f);
					fclose(f);
				}
			}
			return (7);
			case 0x3:	//EEPROM read
			{
				if (!EEPROM_loaded)
				{
					EEPROM_loaded = true;
					FILE* f = fopen(eeprom_file, "rb");
					if (f)
					{
						fread(EEPROM, 1, 0x80, f);
						fclose(f);
					}
				}
				//printf("EEprom READ ?\n");
				int address = buffer_in_b[1];
				//printState(Command,buffer_in,buffer_in_len);
				memcpy(buffer_out, EEPROM + address, 0x80);
				buffer_out_len = 0x80;
			}
			return 8;
			//IF I return all FF, then board runs in low res
			case 0x31:
			{
				buffer_out[0] = 0xffffffff;
				buffer_out[1] = 0xffffffff;
			}
			return (8);

			//case 0x3:
			//	break;

			//case 0x1:
			//	break;
			default:
				printf("Unknown 0x86 : SubCommand 0x%X - State: Cmd 0x%X Mode :  0x%X Node : 0x%X\n", subcode, State.Cmd, State.Mode, State.Node);
				printState(cmd, buffer_in, buffer_in_len);
			}

			return 8;//MAPLE_RESPONSE_DATATRF
		}
		break;
		case 0x82:
		{
			const char *ID = "315-6149    COPYRIGHT SEGA E\x83\x00\x20\x05NTERPRISES CO,LTD.  ";
			memset(buffer_out_b, 0x20, 256);
			memcpy(buffer_out_b, ID, 0x38 - 4);
			buffer_out_len = 256;
			return (0x83);
		}
		
		case 1:
		case 9:
			return maple_sega_controller::dma(cmd);


		default:
			printf("unknown MAPLE Frame\n");
			//printState(Command, buffer_in, buffer_in_len);
			break;
		}
		return MDRE_UnknownFunction;
	}
};
maple_device* maple_Create(MapleDeviceType type)
{
	maple_device* rv=0;
	switch(type)
	{
	case MDT_SegaController:
		rv=new maple_sega_controller();
		break;

	case MDT_Microphone:
		rv=new maple_microphone();
		break;

	case MDT_SegaVMU:
		rv = new maple_sega_vmu();
		break;

   case MDT_PurupuruPack:
      rv = new maple_sega_purupuru();
      break;

	case MDT_NaomiJamma:
		rv = new maple_naomi_jamma();
		break;

	default:
		return 0;
	}

	return rv;
}
