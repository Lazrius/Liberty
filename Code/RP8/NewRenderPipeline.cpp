#include "RenderPipeline.h"
#include "VertexBufferManager.h"
#include "RPDraw.h"
#include "RPIndexBuffer.h"
#include "RPVertexBuffer.h"
#include "GammaControl.h"
#include "RPTexture.h"
#include "RPInternal.h"
#include "CachedMatrix.h"
#include "CachedViewport.h"
#include "CachedTexture.h"
#include "CachedGeometry.h"

#include <Tfuncs.h>
#include <Matrix4.h>
#include <IProfileParser_Utility.h>
#include <d3d8.h>
#include <Dump.h>
#include <3dMath.h>
#include <FLHook_st6.h>
#include <FVF.h>


static void Transform2D3D(D3DMATRIX& dst, const Transform& src);
static void D3D2Transform(const D3DMATRIX& src, Transform& dest);
static void Matrix2D3D(D3DMATRIX& dst, const Matrix4& src);
static void D3D2Matrix(const D3DMATRIX& src, Matrix4& dest);
static U32 GetPrimCount(D3DPRIMITIVETYPE type, U32 indexCount);

#define CHECK_STARTUP() \
if (direct3d_adapter == -1) \
{ \
	GENERAL_ERROR(TEMPSTR("%s() called outside of startup/shutdown", __FUNCTION__)); \
	return GR_GENERIC; \
}

#define CHECK_DEVICE_LIFETIME() \
if (direct3d_device == NULL) \
{ \
	GENERAL_ERROR(TEMPSTR("%s() called outside of device lifetime", __FUNCTION__)); \
	return GR_GENERIC; \
}

#define pf_to_d3d_table data_6D6FFA8
_extern D3DFORMAT pf_to_d3d_table[] =
{
	D3DFMT_UNKNOWN,
	D3DFMT_P8,
	D3DFMT_R8G8B8,
	D3DFMT_R5G6B5,
	D3DFMT_X1R5G5B5,
	D3DFMT_A4R4G4B4,
	D3DFMT_A1R5G5B5,
	D3DFMT_A8R8G8B8,
	D3DFMT_X8R8G8B8,
	D3DFMT_UNKNOWN,
	D3DFMT_DXT1,
	D3DFMT_DXT2,
	D3DFMT_DXT3,
	D3DFMT_DXT4,
	D3DFMT_DXT5,
	D3DFORMAT('POAD'),
	D3DFORMAT('TOAD'),
	D3DFORMAT('AAAD'),
	D3DFORMAT('LAAD'),
	D3DFORMAT('1AAD'),
	D3DFORMAT('4AAD'),
	D3DFORMAT('8AAD'),
};
static_assert(_countof(pf_to_d3d_table) == 22);

#define get_pf_to_d3d_table sub_6D5CC20
_extern D3DFORMAT* get_pf_to_d3d_table()
{
	return pf_to_d3d_table;
}

#define pf_to_d3d sub_6D5CBB0
_extern D3DFORMAT pf_to_d3d(PFenum pfenum)
{
	D3DFORMAT* pf_to_d3d_table = get_pf_to_d3d_table();

	if (pfenum >= PF_MAX_VALUE)
	{
		GENERAL_ERROR(TEMPSTR("pf_to_d3d: unknown pfenum passed (%x)", static_cast<U32>(pfenum)));
		return D3DFMT_UNKNOWN;
	}

	D3DFORMAT format = pf_to_d3d_table[pfenum];
	return format;
}

#define d3d_to_pf sub_6D5CC30
_extern PFenum d3d_to_pf(D3DFORMAT d3d_format)
{
	D3DFORMAT* pf_to_d3d_table = get_pf_to_d3d_table();
	for (U32 index = 0; index < PF_MAX_VALUE; index++)
	{
		if (pf_to_d3d_table[index] == d3d_format)
		{
			PFenum result = static_cast<PFenum>(index);
			return result;
		}
	}
	return PF_UNKNOWN;
}

#define pf_to_pixel_format_table data_6D70000
_extern PixelFormat pf_to_pixel_format_table[];
#define pf_to_pixel_format sub_6D5CC70
_extern PixelFormat* __cdecl pf_to_pixel_format(PFenum pixel_format)
{
	if (pixel_format <= 14)
		return &pf_to_pixel_format_table[pixel_format];
	else
		return pf_to_pixel_format_table;
}

#define validate_depth_stencil_format sub_6D159A6
_extern bool validate_depth_stencil_format(LPDIRECT3D8 direct3d, D3DFORMAT depth_stencil_format, D3DFORMAT adapter_format, D3DFORMAT render_target_format)
{
	HRESULT check_device_format_result = direct3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, adapter_format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, depth_stencil_format);
	HRESULT check_depth_stencil_match_result = direct3d->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, adapter_format, render_target_format, depth_stencil_format);
	bool result = SUCCEEDED(check_device_format_result) && SUCCEEDED(check_depth_stencil_match_result);
	return result;
}

#define dword_6D64040 data_6D64040
_extern DWORD dword_6D64040[];
#define sub_6D159FF sub_6D159FF
_extern D3DFORMAT __cdecl sub_6D159FF(
	LPDIRECT3D8 direct3d,
	D3DFORMAT adapter_format,
	D3DFORMAT render_target_format,
	DWORD* a4,
	DWORD* a5)
{
	int v7; // [esp+8h] [ebp-18h]
	int v8; // [esp+Ch] [ebp-14h]
	int v9; // [esp+10h] [ebp-10h]
	int v10; // [esp+14h] [ebp-Ch]
	DWORD* v11; // [esp+18h] [ebp-8h]
	DWORD* i; // [esp+1Ch] [ebp-4h]

	v9 = 100;
	v10 = 100;
	v11 = 0;
	for (i = dword_6D64040; *i; i += 3)
	{
		v7 = *a4 - i[1];
		v8 = *a5 - i[2];
		if (v8 >= 0
			&& v7 >= 0
			&& (!v11 || v8 <= v9)
			&& validate_depth_stencil_format(direct3d, (D3DFORMAT)*i, adapter_format, render_target_format)
			&& (!v11 || v8 < v9 || v7 < v10))
		{
			v11 = i;
			v9 = v8;
			v10 = v7;
		}
	}
	if (!v11)
		return D3DFMT_UNKNOWN;
	*a4 = v11[1];
	*a5 = v11[2];
	D3DFORMAT result = (D3DFORMAT)*v11;
	return result;
}

#define update_device_capabilities sub_6D020AB
_extern void __thiscall update_device_capabilities(/*RenderPipeline*/void* _this, D3DCAPS8* direct3d_caps)
{
	// nuked
	debug_point;
}

class NewRenderPipeline;

_extern int __cdecl sub_6D15B0D(int a1);
_extern int __cdecl sub_6D15B45(int a1);
_extern void __thiscall sub_6D0376F(NewRenderPipeline* _this);
_extern void __thiscall sub_6D038BA(NewRenderPipeline* _this);
_extern void __thiscall sub_6D03C94(NewRenderPipeline* _this);
_extern void __thiscall sub_6D047DF(NewRenderPipeline* _this);
_extern void __thiscall sub_6D2CE6A(void* _this);

TRAMPOLINE(IRenderPipeline8B*, __thiscall, DirectX8_Ctor, _sub_6D01143, IRenderPipeline8B* _this);
TRAMPOLINE(IRenderPipeline8B*, __thiscall, DirectX8_Dtor, _sub_6D01689, IRenderPipeline8B* _this);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_get_buffers, _sub_6D098F3, IRenderPipeline8B* _this, U32* adapter, RPBUFFERSINFO* out_buffersinfo);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_destroy_buffers, _sub_6D09982, IRenderPipeline8B* _this);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_clear_buffers, _sub_6D09B4C, IRenderPipeline8B* _this, U32 rp_clear_flags, RECT* viewport_sub_rect);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_swap_buffers, _sub_6D09D28, IRenderPipeline8B* _this);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_lock_buffer, _sub_6D09F96, IRenderPipeline8B* _this, RPLOCKDATA* lockData);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_unlock_buffer, _sub_6D0A6C1, IRenderPipeline8B* _this);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_get_buffer_interface, _sub_6D0A7EC, IRenderPipeline8B* _this, const char* iid, void** out_iif);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_get_device_stats, _sub_6D0A863, IRenderPipeline8B* _this, RPDEVICESTATS* stat);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_set_window, _sub_6D0AD7D, IRenderPipeline8B* _this, HWND hwnd, int x, int y, int w, int h);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_get_window, _sub_6D0AE23, IRenderPipeline8B* _this, HWND* out_hwnd, int* out_x, int* out_y, int* out_w, int* out_h);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_destroy_light, _sub_6D0CF9C, IRenderPipeline8B* _this, IRP_LIGHTHANDLE handle);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_get_light, _sub_6D0D044, IRenderPipeline8B* _this, IRP_LIGHTHANDLE handle, D3DLIGHT8* out_light_values);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_set_light_enable, _sub_6D0D0D7, IRenderPipeline8B* _this, IRP_LIGHTHANDLE handle, U32 enable);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_get_light_enable, _sub_6D0D157, IRenderPipeline8B* _this, IRP_LIGHTHANDLE handle, U32* out_enable);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_set_texture_level_data, _sub_6D0EA78, IRenderPipeline8B* _this, IRP_TEXTUREHANDLE htexture, U32 subsurface, int src_width, int src_height, int src_stride, const PFenum* src_format, const void* src_pixel, const void* src_alpha, const RGB* src_palette);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_reset_render_states_to_defaults, _sub_6D0F9E4, IRenderPipeline8B* _this);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_add_light, _sub_6D0CCB2, IRenderPipeline8B* _this, IRP_LIGHTHANDLE handle);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_remove_light, _sub_6D0CD32, IRenderPipeline8B* _this, IRP_LIGHTHANDLE handle);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_update_light, _sub_6D0CDDB, IRenderPipeline8B* _this, IRP_LIGHTHANDLE handle);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_set_world_n, _sub_6D0AF55, IRenderPipeline8B* _this, UNKNOWN a2, Transform* transform);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_VertexBufferManager_UnknownC, _sub_6D11354, IVertexBufferManager* _this, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_VertexBufferManager_Unknown10, _sub_6D1135D, IVertexBufferManager* _this);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_acquire_vertex_buffer, _sub_6D114EA, IVertexBufferManager* _this, UNKNOWN vertex_format, U32 num_verts, VertexBufferAcquire* out_result);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_release_vertex_buffer, _sub_6D11877, IVertexBufferManager* _this, VertexBufferAcquire* vbacquire);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_VertexBufferManager_Unknown1C, _sub_6D118BC, IVertexBufferManager* _this);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_VertexBufferManager_Unknown20, _sub_6D114C5, IVertexBufferManager* _this, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_draw_indexed_primitive2, _sub_6D111E1, IRPDraw* _this, D3DPRIMITIVETYPE type, U32 min_index, U32 num_verts, U32 start_index, U32 count);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_create_index_buffer, _sub_6D12D4E, IRPIndexBuffer* _this, U32 count, IRP_INDEXBUFFERHANDLE* out_ibhandle, BYTE flags);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_destroy_index_buffer, _sub_6D13002, IRPIndexBuffer* _this, IRP_INDEXBUFFERHANDLE ibhandle);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_create_ib, _sub_6D131B2, IRPIndexBuffer* _this, IRP_INDEXBUFFERHANDLE ibhandle, U32 count);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_copy_indices, _sub_6D13847, IRPIndexBuffer* _this, IRP_INDEXBUFFERHANDLE ibhandle, U32* offset, void* data, U32 count);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_lock_ib, _sub_6D134EC, IRPIndexBuffer* _this, IRP_INDEXBUFFERHANDLE ibhandle, U32* offset, void** locked_data, U32 count);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_unlock_ib, _sub_6D13794, IRPIndexBuffer* _this, IRP_INDEXBUFFERHANDLE ibhandle);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_select_ib, _sub_6D13BCE, IRPIndexBuffer* _this, IRP_INDEXBUFFERHANDLE ibhandle, UNKNOWN a3, UNKNOWN a4, UNKNOWN a5);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_get_ib_count, _sub_6D13D23, IRPIndexBuffer* _this, IRP_INDEXBUFFERHANDLE ibhandle, U32* out_count);
TRAMPOLINE(BOOL32, __stdcall, DirectX8_is_ib_valid, _sub_6D13B69, IRPIndexBuffer* _this, IRP_INDEXBUFFERHANDLE ibhandle);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_create_vb, _sub_6D118C8, IRPVertexBuffer* _this, UNKNOWN format, U32 count, IRP_VERTEXBUFFERHANDLE* out_vbhandle, BYTE flags);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_destroy_vb, _sub_6D11DB3, IRPVertexBuffer* _this, IRP_VERTEXBUFFERHANDLE vbhandle);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_ressize_vb, _sub_6D11F78, IRPVertexBuffer* _this, IRP_VERTEXBUFFERHANDLE vbhandle, UNKNOWN format, U32 count);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_copy_vertices, _sub_6D1228C, IRPVertexBuffer* _this, IRP_VERTEXBUFFERHANDLE vbhandle, U32* offset, UNKNOWN* a4, UNKNOWN a5, UNKNOWN a6);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_lock_vb, _sub_6D126BB, IRPVertexBuffer* _this, IRP_VERTEXBUFFERHANDLE vbhandle, U32* offset, void** locked_data, U32 count);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_unlock_vb, _sub_6D12A8B, IRPVertexBuffer* _this, IRP_VERTEXBUFFERHANDLE vbhandle);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_RPVertexBuffer_Unknown24, _sub_6D12B30, IRPVertexBuffer* _this, UNKNOWN);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_select_vb, _sub_6D12B9D, IRPVertexBuffer* _this, IRP_VERTEXBUFFERHANDLE vbhandle);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_get_vb_count, _sub_6D12CDC, IRPVertexBuffer* _this, IRP_VERTEXBUFFERHANDLE vbhandle, UNKNOWN* vertex_format, U32* num_verts);
TRAMPOLINE(BOOL32, __stdcall, DirectX8_is_vb_valid, _sub_6D12B3C, IRPVertexBuffer* _this, IRP_VERTEXBUFFERHANDLE vbhandle);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_set_gamma_function, _sub_6D153E6, IGammaControl* _this, IGC_COMPONENT which, float display_gamma, float bias, float slope, float black_offset);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_set_gamma_ramp, _sub_6D1560C, IGammaControl* _this, IGC_COMPONENT igc_component, U16* ramp);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_get_gamma_ramp, _sub_6D15757, IGammaControl* _this, IGC_COMPONENT igc_component, U16* out_ramp);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_set_calibration_enable, _sub_6D1535D, IGammaControl* _this, bool enabled);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_get_calibration_enable, _sub_6D153A4, IGammaControl* _this);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_print_screen, _sub_6D13D70, IRPTexture* _this, IFileSystem* pFileSystem, const char* filepath);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_load_texture, _sub_6D148D4, IRPTexture* _this, UNKNOWN* a2_interface, const char* filepath, IRP_TEXTUREHANDLE* out_texture);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_load_surface_from_file, _sub_6D1455E, IRPTexture* _this, UNKNOWN* a2_interface, UNKNOWN a3, UNKNOWN a4, UNKNOWN a5);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_RPTexture_Unknown18, _sub_6D152E4, IRPTexture* _this, UNKNOWN a2, UNKNOWN a3, UNKNOWN* a4);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_load_cubemap, _sub_6D14DD7, IRPTexture* _this, UNKNOWN* a2_interface, const char* filepath, IRP_TEXTUREHANDLE* out_texture);
TRAMPOLINE(GENRESULT, __stdcall, DirectX8_Initialize, _sub_6D01C68, IAggregateComponent* _this);

#define CLSID_NewRenderPipeline "NewRenderPipeline"

struct CACHED_PIPELINE_STATE
{
	DWORD value;
	DWORD unknown4;
	DWORD valid;
};

class NewRenderPipeline : IRenderPipeline8B, IVertexBufferManager, IRPDraw, IRPIndexBuffer, IRPVertexBuffer, IGammaControl, IRPTexture, IAggregateComponent
{
public:
	IProfileParser* profile_parser;
	char profile_name[128];
	char profile_nameB[128];
	DWORD direct3d_behavior_flags;
	DWORD unknown128;
	LPDIRECT3D8 direct3d;
	IDirect3DDevice8* direct3d_device;
	IDirect3DSurface8* direct3d_surface;
	CACHED_GEOMETRY curr_hw_geometry;
	RPIndexBufferInternal scratchIB;
	DWORD unknown168;
	DWORD unknown16C;
	DWORD unknown170;
	DWORD unknown174;
	DWORD unknown178;
	DWORD unknown17C;
	DWORD unknown180;
	BYTE unknown184_is_locked;
	BYTE unknown185;
	BYTE unknown186;
	BYTE unknown187;
	DWORD unknown188;
	DWORD unknown18C;
	DWORD unknown190;
	DWORD unknown194;
	st6::vector<RPDEVICEINFO> unknown198;
	DWORD direct3d_adapter;
	D3DDEVTYPE direct3d_device_type;
	char configuration_database_file[128];
	DWORD unknown230;
	PFenum pf_unknown234;
	RPBUFFERSINFO buffers_info;
	HWND hwnd;
	DWORD window_x;
	DWORD window_y;
	DWORD window_width;
	DWORD window_height;
	HWND window;
	DWORD unknown278;
	DWORD unknown27C;
	DWORD unknown280;
	DWORD unknown284;
	DWORD unknown288;
	st6::map<U32, RPSTATEINFO> pipeline_state_info;
	st6::map<U32, RPSTATEINFO> render_state_info;
	DWORD unknown2B4;
	DWORD unknown2B8;
	DWORD unknown2BC;
	DWORD unknown2C0;
	DWORD unknown2C4;
	DWORD unknown2C8;
	DWORD unknown2CC;
	DWORD unknown2D0;
	DWORD unknown2D4;
	DWORD unknown2D8;
	DWORD unknown2DC;
	DWORD unknown2E0;
	DWORD unknown2E4;
	DWORD unknown2E8;
	DWORD unknown2EC;
	DWORD unknown2F0;
	DWORD unknown2F4;
	DWORD unknown2F8;
	DWORD unknown2FC;
	DWORD unknown300;
	DWORD unknown304;
	DWORD unknown308;
	DWORD unknown30C;
	DWORD unknown310;
	DWORD unknown314;
	DWORD unknown318;
	DWORD unknown31C;
	DWORD unknown320;
	DWORD unknown324;
	DWORD unknown328;
	DWORD unknown32C;
	DWORD unknown330;
	DWORD unknown334;
	DWORD unknown338;
	DWORD unknown33C;
	DWORD unknown340;
	DWORD unknown344;
	DWORD unknown348;
	DWORD unknown34C;
	DWORD unknown350;
	CACHED_PIPELINE_STATE pipeline_states[14];
	U32 device_abilities[20];
	DWORD unknown44C;
	DWORD unknown450;
	DWORD unknown454;
	DWORD unknown458;
	DWORD unknown45C;
	DWORD unknown460;
	DWORD unknown464;
	DWORD unknown468;
	DWORD unknown46C;
	DWORD unknown470;
	DWORD unknown474;
	DWORD unknown478;
	DWORD unknown47C;
	DWORD unknown480;
	DWORD unknown484;
	DWORD unknown488;
	DWORD unknown48C;
	DWORD unknown490;
	DWORD unknown494;
	DWORD unknown498;
	DWORD unknown49C;
	DWORD unknown4A0;
	DWORD unknown4A4;
	DWORD unknown4A8;
	DWORD unknown4AC;
	DWORD unknown4B0;
	DWORD unknown4B4;
	DWORD unknown4B8;
	DWORD unknown4BC;
	DWORD unknown4C0;
	DWORD unknown4C4;
	DWORD unknown4C8;
	DWORD unknown4CC;
	DWORD unknown4D0;
	DWORD unknown4D4;
	DWORD unknown4D8;
	DWORD unknown4DC;
	DWORD unknown4E0;
	DWORD unknown4E4;
	DWORD unknown4E8;
	DWORD unknown4EC;
	DWORD unknown4F0;
	DWORD unknown4F4;
	DWORD unknown4F8;
	DWORD unknown4FC;
	DWORD unknown500;
	DWORD unknown504;
	DWORD unknown508;
	DWORD unknown50C;
	DWORD unknown510;
	DWORD unknown514;
	DWORD unknown518;
	DWORD unknown51C;
	DWORD unknown520;
	DWORD unknown524;
	DWORD unknown528;
	DWORD unknown52C;
	DWORD unknown530;
	DWORD unknown534;
	DWORD unknown538;
	DWORD unknown53C;
	DWORD unknown540;
	DWORD unknown544;
	DWORD unknown548;
	DWORD unknown54C;
	DWORD unknown550;
	DWORD unknown554;
	DWORD unknown558;
	DWORD unknown55C;
	DWORD unknown560;
	DWORD unknown564;
	DWORD unknown568;
	DWORD unknown56C;
	DWORD unknown570;
	DWORD unknown574;
	DWORD unknown578;
	DWORD unknown57C;
	DWORD unknown580;
	DWORD unknown584;
	DWORD unknown588;
	DWORD unknown58C;
	DWORD unknown590;
	DWORD unknown594;
	DWORD unknown598;
	DWORD unknown59C;
	DWORD unknown5A0;
	DWORD unknown5A4;
	DWORD unknown5A8;
	DWORD unknown5AC;
	DWORD unknown5B0;
	DWORD unknown5B4;
	DWORD unknown5B8;
	DWORD unknown5BC;
	DWORD unknown5C0;
	DWORD unknown5C4;
	DWORD unknown5C8;
	DWORD unknown5CC;
	DWORD unknown5D0;
	DWORD unknown5D4;
	DWORD unknown5D8;
	DWORD unknown5DC;
	DWORD unknown5E0;
	DWORD unknown5E4;
	DWORD unknown5E8;
	DWORD unknown5EC;
	DWORD unknown5F0;
	DWORD unknown5F4;
	DWORD unknown5F8;
	DWORD unknown5FC;
	DWORD unknown600;
	DWORD unknown604;
	DWORD unknown608;
	DWORD unknown60C;
	DWORD unknown610;
	DWORD unknown614;
	DWORD unknown618;
	DWORD unknown61C;
	DWORD unknown620;
	DWORD unknown624;
	DWORD unknown628;
	DWORD unknown62C;
	DWORD unknown630;
	DWORD unknown634;
	DWORD unknown638;
	DWORD unknown63C;
	DWORD unknown640;
	DWORD unknown644;
	DWORD unknown648;
	DWORD unknown64C;
	DWORD unknown650;
	DWORD unknown654;
	DWORD unknown658;
	DWORD unknown65C;
	DWORD unknown660;
	DWORD unknown664;
	DWORD unknown668;
	DWORD unknown66C;
	DWORD unknown670;
	DWORD unknown674;
	DWORD unknown678;
	DWORD unknown67C;
	DWORD unknown680;
	DWORD unknown684;
	DWORD unknown688;
	DWORD unknown68C;
	DWORD unknown690;
	DWORD unknown694;
	DWORD unknown698;
	DWORD unknown69C;
	DWORD unknown6A0;
	DWORD unknown6A4;
	DWORD unknown6A8;
	DWORD unknown6AC;
	DWORD unknown6B0;
	DWORD unknown6B4;
	DWORD unknown6B8;
	DWORD unknown6BC;
	DWORD unknown6C0;
	DWORD unknown6C4;
	DWORD unknown6C8;
	DWORD unknown6CC;
	DWORD unknown6D0;
	DWORD unknown6D4;
	DWORD unknown6D8;
	DWORD unknown6DC;
	DWORD unknown6E0;
	DWORD unknown6E4;
	DWORD unknown6E8;
	DWORD unknown6EC;
	DWORD unknown6F0;
	DWORD unknown6F4;
	DWORD unknown6F8;
	DWORD unknown6FC;
	DWORD unknown700;
	DWORD unknown704;
	DWORD unknown708;
	DWORD unknown70C;
	DWORD unknown710;
	DWORD unknown714;
	DWORD unknown718;
	DWORD unknown71C;
	DWORD unknown720;
	DWORD unknown724;
	DWORD unknown728;
	DWORD unknown72C;
	DWORD unknown730;
	DWORD unknown734;
	DWORD unknown738;
	DWORD unknown73C;
	DWORD unknown740;
	DWORD unknown744;
	DWORD unknown748;
	DWORD unknown74C;
	DWORD unknown750;
	DWORD unknown754;
	DWORD unknown758;
	DWORD unknown75C;
	DWORD unknown760;
	DWORD unknown764;
	DWORD unknown768;
	DWORD unknown76C;
	DWORD unknown770;
	DWORD unknown774;
	DWORD unknown778;
	DWORD unknown77C;
	DWORD unknown780;
	DWORD unknown784;
	DWORD unknown788;
	DWORD unknown78C;
	DWORD unknown790;
	DWORD unknown794;
	DWORD unknown798;
	DWORD unknown79C;
	DWORD unknown7A0;
	DWORD unknown7A4;
	DWORD unknown7A8;
	DWORD unknown7AC;
	DWORD unknown7B0;
	DWORD unknown7B4;
	DWORD unknown7B8;
	DWORD unknown7BC;
	DWORD unknown7C0;
	DWORD unknown7C4;
	DWORD unknown7C8;
	DWORD unknown7CC;
	DWORD unknown7D0;
	DWORD unknown7D4;
	DWORD unknown7D8;
	DWORD unknown7DC;
	DWORD unknown7E0;
	DWORD unknown7E4;
	DWORD unknown7E8;
	DWORD unknown7EC;
	DWORD unknown7F0;
	DWORD unknown7F4;
	DWORD unknown7F8;
	DWORD unknown7FC;
	DWORD unknown800;
	DWORD unknown804;
	DWORD unknown808;
	DWORD unknown80C;
	DWORD unknown810;
	DWORD unknown814;
	DWORD unknown818;
	DWORD unknown81C;
	DWORD unknown820;
	DWORD unknown824;
	DWORD unknown828;
	DWORD unknown82C;
	DWORD unknown830;
	DWORD unknown834;
	DWORD unknown838;
	DWORD unknown83C;
	DWORD unknown840;
	DWORD unknown844;
	DWORD unknown848;
	DWORD unknown84C;
	DWORD unknown850;
	DWORD unknown854;
	DWORD unknown858;
	DWORD unknown85C;
	DWORD unknown860;
	DWORD unknown864;
	DWORD unknown868;
	DWORD unknown86C;
	DWORD unknown870;
	DWORD unknown874;
	DWORD unknown878;
	DWORD unknown87C;
	DWORD unknown880;
	DWORD unknown884;
	DWORD unknown888;
	DWORD unknown88C;
	DWORD unknown890;
	DWORD unknown894;
	DWORD unknown898;
	DWORD unknown89C;
	DWORD unknown8A0;
	DWORD unknown8A4;
	DWORD unknown8A8;
	DWORD unknown8AC;
	DWORD unknown8B0;
	DWORD unknown8B4;
	DWORD unknown8B8;
	DWORD unknown8BC;
	DWORD unknown8C0;
	DWORD unknown8C4;
	DWORD unknown8C8;
	DWORD unknown8CC;
	DWORD unknown8D0;
	DWORD unknown8D4;
	DWORD unknown8D8;
	DWORD unknown8DC;
	DWORD unknown8E0;
	DWORD unknown8E4;
	DWORD unknown8E8;
	DWORD unknown8EC;
	DWORD unknown8F0;
	DWORD unknown8F4;
	DWORD unknown8F8;
	DWORD unknown8FC;
	DWORD unknown900;
	DWORD unknown904;
	DWORD unknown908;
	DWORD unknown90C;
	DWORD unknown910;
	DWORD unknown914;
	DWORD unknown918;
	DWORD unknown91C;
	DWORD unknown920;
	DWORD unknown924;
	DWORD unknown928;
	DWORD unknown92C;
	DWORD unknown930;
	DWORD unknown934;
	DWORD unknown938;
	DWORD unknown93C;
	DWORD unknown940;
	DWORD unknown944;
	DWORD unknown948;
	DWORD unknown94C;
	DWORD unknown950;
	DWORD unknown954;
	DWORD unknown958;
	DWORD unknown95C;
	DWORD unknown960;
	DWORD unknown964;
	DWORD unknown968;
	DWORD unknown96C;
	DWORD unknown970;
	DWORD unknown974;
	DWORD unknown978;
	DWORD unknown97C;
	DWORD unknown980;
	DWORD unknown984;
	DWORD unknown988;
	DWORD unknown98C;
	DWORD unknown990;
	DWORD unknown994;
	DWORD unknown998;
	DWORD unknown99C;
	DWORD unknown9A0;
	DWORD unknown9A4;
	DWORD unknown9A8;
	DWORD unknown9AC;
	DWORD unknown9B0;
	DWORD unknown9B4;
	DWORD unknown9B8;
	DWORD unknown9BC;
	DWORD unknown9C0;
	DWORD unknown9C4;
	DWORD unknown9C8;
	DWORD unknown9CC;
	DWORD unknown9D0;
	DWORD unknown9D4;
	DWORD unknown9D8;
	DWORD unknown9DC;
	DWORD unknown9E0;
	DWORD unknown9E4;
	DWORD unknown9E8;
	DWORD unknown9EC;
	DWORD unknown9F0;
	DWORD unknown9F4;
	DWORD unknown9F8;
	DWORD unknown9FC;
	DWORD unknownA00;
	DWORD unknownA04;
	DWORD unknownA08;
	DWORD unknownA0C;
	DWORD unknownA10;
	DWORD unknownA14;
	DWORD unknownA18;
	DWORD unknownA1C;
	DWORD unknownA20;
	DWORD unknownA24;
	DWORD unknownA28;
	DWORD unknownA2C;
	DWORD unknownA30;
	DWORD unknownA34;
	DWORD unknownA38;
	DWORD unknownA3C;
	DWORD unknownA40;
	DWORD unknownA44;
	DWORD unknownA48;
	DWORD unknownA4C;
	DWORD unknownA50;
	DWORD unknownA54;
	DWORD unknownA58;
	DWORD unknownA5C;
	DWORD unknownA60;
	DWORD unknownA64;
	DWORD unknownA68;
	DWORD unknownA6C;
	DWORD unknownA70;
	DWORD unknownA74;
	DWORD unknownA78;
	DWORD unknownA7C;
	DWORD unknownA80;
	DWORD unknownA84;
	DWORD unknownA88;
	DWORD unknownA8C;
	DWORD unknownA90;
	DWORD unknownA94;
	DWORD unknownA98;
	DWORD unknownA9C;
	DWORD unknownAA0;
	DWORD unknownAA4;
	DWORD unknownAA8;
	DWORD unknownAAC;
	DWORD unknownAB0;
	DWORD unknownAB4;
	DWORD unknownAB8;
	DWORD unknownABC;
	DWORD unknownAC0;
	DWORD unknownAC4;
	DWORD unknownAC8;
	DWORD unknownACC;
	DWORD unknownAD0;
	DWORD unknownAD4;
	DWORD unknownAD8;
	DWORD unknownADC;
	DWORD unknownAE0;
	DWORD unknownAE4;
	DWORD unknownAE8;
	DWORD unknownAEC;
	DWORD unknownAF0;
	DWORD unknownAF4;
	DWORD unknownAF8;
	DWORD unknownAFC;
	DWORD unknownB00;
	DWORD unknownB04;
	DWORD unknownB08;
	DWORD unknownB0C;
	DWORD unknownB10;
	DWORD unknownB14;
	DWORD unknownB18;
	DWORD unknownB1C;
	DWORD unknownB20;
	DWORD unknownB24;
	DWORD unknownB28;
	DWORD unknownB2C;
	DWORD unknownB30;
	DWORD unknownB34;
	DWORD unknownB38;
	DWORD unknownB3C;
	DWORD unknownB40;
	DWORD unknownB44;
	DWORD unknownB48;
	DWORD unknownB4C;
	DWORD unknownB50;
	DWORD unknownB54;
	DWORD unknownB58;
	DWORD unknownB5C;
	DWORD unknownB60;
	DWORD unknownB64;
	DWORD unknownB68;
	DWORD unknownB6C;
	DWORD unknownB70;
	DWORD unknownB74;
	DWORD unknownB78;
	DWORD unknownB7C;
	DWORD unknownB80;
	DWORD unknownB84;
	DWORD unknownB88;
	DWORD unknownB8C;
	DWORD unknownB90;
	DWORD unknownB94;
	DWORD unknownB98;
	DWORD unknownB9C;
	DWORD unknownBA0;
	DWORD unknownBA4;
	DWORD unknownBA8;
	DWORD unknownBAC;
	DWORD unknownBB0;
	DWORD unknownBB4;
	DWORD unknownBB8;
	DWORD unknownBBC;
	DWORD unknownBC0;
	DWORD unknownBC4;
	DWORD unknownBC8;
	DWORD unknownBCC;
	DWORD unknownBD0;
	DWORD unknownBD4;
	DWORD unknownBD8;
	DWORD unknownBDC;
	DWORD unknownBE0;
	DWORD unknownBE4;
	DWORD unknownBE8;
	DWORD unknownBEC;
	DWORD unknownBF0;
	DWORD unknownBF4;
	DWORD unknownBF8;
	DWORD unknownBFC;
	DWORD unknownC00;
	DWORD unknownC04;
	DWORD unknownC08;
	DWORD unknownC0C;
	DWORD unknownC10;
	DWORD unknownC14;
	DWORD unknownC18;
	DWORD unknownC1C;
	DWORD unknownC20;
	DWORD unknownC24;
	DWORD unknownC28;
	DWORD unknownC2C;
	DWORD unknownC30;
	DWORD unknownC34;
	DWORD unknownC38;
	DWORD unknownC3C;
	DWORD unknownC40;
	DWORD unknownC44;
	DWORD unknownC48;
	DWORD unknownC4C;
	DWORD unknownC50;
	DWORD unknownC54;
	DWORD unknownC58;
	DWORD unknownC5C;
	DWORD unknownC60;
	DWORD unknownC64;
	DWORD unknownC68;
	DWORD unknownC6C;
	DWORD unknownC70;
	DWORD unknownC74;
	DWORD unknownC78;
	DWORD unknownC7C;
	DWORD unknownC80;
	DWORD unknownC84;
	DWORD unknownC88;
	DWORD unknownC8C;
	DWORD unknownC90;
	DWORD unknownC94;
	DWORD unknownC98;
	DWORD unknownC9C;
	DWORD unknownCA0;
	DWORD unknownCA4;
	DWORD unknownCA8;
	DWORD unknownCAC;
	DWORD unknownCB0;
	DWORD unknownCB4;
	DWORD unknownCB8;
	DWORD unknownCBC;
	DWORD unknownCC0;
	DWORD unknownCC4;
	DWORD unknownCC8;
	DWORD unknownCCC;
	DWORD unknownCD0;
	DWORD unknownCD4;
	DWORD unknownCD8;
	DWORD unknownCDC;
	DWORD unknownCE0;
	DWORD unknownCE4;
	DWORD unknownCE8;
	DWORD unknownCEC;
	DWORD unknownCF0;
	DWORD unknownCF4;
	DWORD unknownCF8;
	DWORD unknownCFC;
	DWORD unknownD00;
	DWORD unknownD04;
	DWORD unknownD08;
	DWORD unknownD0C;
	DWORD unknownD10;
	DWORD unknownD14;
	DWORD unknownD18;
	DWORD unknownD1C;
	DWORD unknownD20;
	DWORD unknownD24;
	DWORD unknownD28;
	DWORD unknownD2C;
	DWORD unknownD30;
	DWORD unknownD34;
	DWORD unknownD38;
	DWORD unknownD3C;
	DWORD unknownD40;
	DWORD unknownD44;
	DWORD unknownD48;
	DWORD unknownD4C;
	DWORD unknownD50;
	DWORD unknownD54;
	DWORD unknownD58;
	DWORD unknownD5C;
	DWORD unknownD60;
	DWORD unknownD64;
	DWORD unknownD68;
	DWORD unknownD6C;
	DWORD unknownD70;
	DWORD unknownD74;
	DWORD unknownD78;
	DWORD unknownD7C;
	DWORD unknownD80;
	DWORD unknownD84;
	DWORD unknownD88;
	DWORD unknownD8C;
	DWORD unknownD90;
	DWORD unknownD94;
	DWORD unknownD98;
	DWORD unknownD9C;
	DWORD unknownDA0;
	DWORD unknownDA4;
	DWORD unknownDA8;
	DWORD unknownDAC;
	DWORD unknownDB0;
	DWORD unknownDB4;
	DWORD unknownDB8;
	DWORD unknownDBC;
	DWORD unknownDC0;
	DWORD unknownDC4;
	DWORD unknownDC8;
	DWORD unknownDCC;
	DWORD unknownDD0;
	DWORD unknownDD4;
	DWORD unknownDD8;
	DWORD unknownDDC;
	DWORD unknownDE0;
	DWORD unknownDE4;
	DWORD unknownDE8;
	DWORD unknownDEC;
	DWORD unknownDF0;
	DWORD unknownDF4;
	DWORD unknownDF8;
	DWORD unknownDFC;
	DWORD unknownE00;
	DWORD unknownE04;
	DWORD unknownE08;
	DWORD unknownE0C;
	DWORD unknownE10;
	DWORD unknownE14;
	DWORD unknownE18;
	DWORD unknownE1C;
	DWORD unknownE20;
	DWORD unknownE24;
	DWORD unknownE28;
	DWORD unknownE2C;
	DWORD unknownE30;
	DWORD unknownE34;
	DWORD unknownE38;
	DWORD unknownE3C;
	DWORD unknownE40;
	DWORD unknownE44;
	DWORD unknownE48;
	DWORD unknownE4C;
	DWORD unknownE50;
	DWORD unknownE54;
	DWORD unknownE58;
	DWORD unknownE5C;
	DWORD unknownE60;
	DWORD unknownE64;
	DWORD unknownE68;
	DWORD unknownE6C;
	DWORD unknownE70;
	DWORD unknownE74;
	DWORD unknownE78;
	DWORD unknownE7C;
	DWORD unknownE80;
	DWORD unknownE84;
	DWORD unknownE88;
	DWORD unknownE8C;
	DWORD unknownE90;
	DWORD unknownE94;
	DWORD unknownE98;
	DWORD unknownE9C;
	DWORD unknownEA0;
	DWORD unknownEA4;
	DWORD unknownEA8;
	DWORD unknownEAC;
	DWORD unknownEB0;
	DWORD unknownEB4;
	DWORD unknownEB8;
	DWORD unknownEBC;
	DWORD unknownEC0;
	DWORD unknownEC4;
	DWORD unknownEC8;
	DWORD unknownECC;
	DWORD unknownED0;
	DWORD unknownED4;
	DWORD unknownED8;
	DWORD unknownEDC;
	DWORD unknownEE0;
	DWORD unknownEE4;
	DWORD unknownEE8;
	DWORD unknownEEC;
	DWORD unknownEF0;
	DWORD unknownEF4;
	DWORD unknownEF8;
	DWORD unknownEFC;
	DWORD unknownF00;
	DWORD unknownF04;
	DWORD unknownF08;
	DWORD unknownF0C;
	DWORD unknownF10;
	DWORD unknownF14;
	DWORD unknownF18;
	DWORD unknownF1C;
	DWORD unknownF20;
	DWORD unknownF24;
	DWORD unknownF28;
	DWORD unknownF2C;
	DWORD unknownF30;
	DWORD unknownF34;
	DWORD unknownF38;
	DWORD unknownF3C;
	DWORD unknownF40;
	DWORD unknownF44;
	DWORD unknownF48;
	DWORD unknownF4C;
	DWORD unknownF50;
	DWORD unknownF54;
	DWORD unknownF58;
	DWORD unknownF5C;
	DWORD unknownF60;
	DWORD unknownF64;
	DWORD unknownF68;
	DWORD unknownF6C;
	DWORD unknownF70;
	DWORD unknownF74;
	DWORD unknownF78;
	DWORD unknownF7C;
	DWORD unknownF80;
	DWORD unknownF84;
	DWORD unknownF88;
	DWORD unknownF8C;
	DWORD unknownF90;
	DWORD unknownF94;
	DWORD unknownF98;
	DWORD unknownF9C;
	DWORD unknownFA0;
	DWORD unknownFA4;
	DWORD unknownFA8;
	DWORD unknownFAC;
	DWORD unknownFB0;
	DWORD unknownFB4;
	DWORD unknownFB8;
	DWORD unknownFBC;
	DWORD unknownFC0;
	DWORD unknownFC4;
	DWORD unknownFC8;
	DWORD unknownFCC;
	DWORD unknownFD0;
	DWORD unknownFD4;
	DWORD unknownFD8;
	DWORD unknownFDC;
	DWORD unknownFE0;
	DWORD unknownFE4;
	DWORD unknownFE8;
	DWORD unknownFEC;
	DWORD unknownFF0;
	DWORD unknownFF4;
	DWORD unknownFF8;
	DWORD unknownFFC;
	DWORD unknown1000;
	DWORD unknown1004;
	DWORD unknown1008;
	DWORD unknown100C;
	DWORD unknown1010;
	DWORD unknown1014;
	DWORD unknown1018;
	DWORD unknown101C;
	DWORD unknown1020;
	DWORD unknown1024;
	DWORD unknown1028;
	DWORD unknown102C;
	DWORD unknown1030;
	DWORD unknown1034;
	DWORD unknown1038;
	DWORD unknown103C;
	DWORD unknown1040;
	DWORD unknown1044;
	DWORD unknown1048;
	DWORD unknown104C;
	DWORD unknown1050;
	DWORD unknown1054;
	DWORD unknown1058;
	DWORD unknown105C;
	DWORD unknown1060;
	DWORD unknown1064;
	DWORD unknown1068;
	DWORD unknown106C;
	DWORD unknown1070;
	DWORD unknown1074;
	DWORD unknown1078;
	DWORD unknown107C;
	DWORD unknown1080;
	DWORD unknown1084;
	DWORD unknown1088;
	DWORD unknown108C;
	DWORD unknown1090;
	DWORD unknown1094;
	DWORD unknown1098;
	DWORD unknown109C;
	DWORD unknown10A0;
	DWORD unknown10A4;
	DWORD unknown10A8;
	DWORD unknown10AC;
	DWORD unknown10B0;
	DWORD unknown10B4;
	DWORD unknown10B8;
	DWORD unknown10BC;
	DWORD unknown10C0;
	DWORD unknown10C4;
	DWORD unknown10C8;
	DWORD unknown10CC;
	DWORD unknown10D0;
	DWORD unknown10D4;
	DWORD unknown10D8;
	DWORD unknown10DC;
	DWORD unknown10E0;
	DWORD unknown10E4;
	DWORD unknown10E8;
	DWORD unknown10EC;
	DWORD unknown10F0;
	DWORD unknown10F4;
	DWORD unknown10F8;
	DWORD unknown10FC;
	DWORD unknown1100;
	DWORD unknown1104;
	DWORD unknown1108;
	DWORD unknown110C;
	DWORD unknown1110;
	DWORD unknown1114;
	DWORD unknown1118;
	DWORD unknown111C;
	DWORD unknown1120;
	DWORD unknown1124;
	DWORD unknown1128;
	DWORD unknown112C;
	DWORD unknown1130;
	DWORD unknown1134;
	DWORD unknown1138;
	DWORD unknown113C;
	DWORD unknown1140;
	DWORD unknown1144;
	DWORD unknown1148;
	DWORD unknown114C;
	DWORD unknown1150;
	DWORD unknown1154;
	DWORD unknown1158;
	DWORD unknown115C;
	DWORD unknown1160;
	DWORD unknown1164;
	DWORD unknown1168;
	DWORD unknown116C;
	DWORD unknown1170;
	DWORD unknown1174;
	DWORD unknown1178;
	DWORD unknown117C;
	DWORD unknown1180;
	DWORD unknown1184;
	DWORD unknown1188;
	DWORD unknown118C;
	DWORD unknown1190;
	DWORD unknown1194;
	DWORD unknown1198;
	DWORD unknown119C;
	DWORD unknown11A0;
	DWORD unknown11A4;
	DWORD unknown11A8;
	DWORD unknown11AC;
	DWORD unknown11B0;
	DWORD unknown11B4;
	DWORD unknown11B8;
	DWORD unknown11BC;
	DWORD unknown11C0;
	DWORD unknown11C4;
	DWORD unknown11C8;
	DWORD unknown11CC;
	DWORD unknown11D0;
	DWORD unknown11D4;
	DWORD unknown11D8;
	DWORD unknown11DC;
	DWORD unknown11E0;
	DWORD unknown11E4;
	DWORD unknown11E8;
	DWORD unknown11EC;
	DWORD unknown11F0;
	DWORD unknown11F4;
	DWORD unknown11F8;
	DWORD unknown11FC;
	DWORD unknown1200;
	DWORD unknown1204;
	DWORD unknown1208;
	DWORD unknown120C;
	DWORD unknown1210;
	DWORD unknown1214;
	DWORD unknown1218;
	DWORD unknown121C;
	DWORD unknown1220;
	DWORD unknown1224;
	DWORD unknown1228;
	DWORD unknown122C;
	DWORD unknown1230;
	DWORD unknown1234;
	DWORD unknown1238;
	DWORD unknown123C;
	DWORD unknown1240;
	DWORD unknown1244;
	DWORD unknown1248;
	DWORD unknown124C;
	DWORD unknown1250;
	DWORD unknown1254;
	DWORD unknown1258;
	DWORD unknown125C;
	DWORD unknown1260;
	DWORD unknown1264;
	DWORD unknown1268;
	DWORD unknown126C;
	DWORD unknown1270;
	DWORD unknown1274;
	DWORD unknown1278;
	DWORD unknown127C;
	DWORD unknown1280;
	DWORD unknown1284;
	DWORD unknown1288;
	DWORD unknown128C;
	DWORD unknown1290;
	DWORD unknown1294;
	DWORD unknown1298;
	DWORD unknown129C;
	DWORD unknown12A0;
	DWORD unknown12A4;
	DWORD unknown12A8;
	DWORD unknown12AC;
	DWORD unknown12B0;
	DWORD unknown12B4;
	DWORD unknown12B8;
	DWORD unknown12BC;
	DWORD unknown12C0;
	DWORD unknown12C4;
	DWORD unknown12C8;
	DWORD unknown12CC;
	DWORD unknown12D0;
	DWORD unknown12D4;
	DWORD unknown12D8;
	DWORD unknown12DC;
	DWORD unknown12E0;
	DWORD unknown12E4;
	DWORD unknown12E8;
	DWORD unknown12EC;
	DWORD unknown12F0;
	DWORD unknown12F4;
	DWORD unknown12F8;
	DWORD unknown12FC;
	DWORD unknown1300;
	DWORD unknown1304;
	DWORD unknown1308;
	DWORD unknown130C;
	DWORD unknown1310;
	DWORD unknown1314;
	DWORD unknown1318;
	DWORD unknown131C;
	DWORD unknown1320;
	DWORD unknown1324;
	DWORD unknown1328;
	DWORD unknown132C;
	DWORD unknown1330;
	DWORD unknown1334;
	DWORD unknown1338;
	DWORD unknown133C;
	DWORD unknown1340;
	DWORD unknown1344;
	DWORD unknown1348;
	DWORD unknown134C;
	DWORD unknown1350;
	DWORD unknown1354;
	DWORD unknown1358;
	DWORD unknown135C;
	DWORD unknown1360;
	DWORD unknown1364;
	DWORD unknown1368;
	DWORD unknown136C;
	DWORD unknown1370;
	DWORD unknown1374;
	DWORD unknown1378;
	DWORD unknown137C;
	DWORD unknown1380;
	DWORD unknown1384;
	DWORD unknown1388;
	DWORD unknown138C;
	DWORD unknown1390;
	DWORD unknown1394;
	DWORD unknown1398;
	DWORD unknown139C;
	DWORD unknown13A0;
	DWORD unknown13A4;
	DWORD unknown13A8;
	DWORD unknown13AC;
	DWORD unknown13B0;
	DWORD unknown13B4;
	DWORD unknown13B8;
	DWORD unknown13BC;
	DWORD unknown13C0;
	DWORD unknown13C4;
	DWORD unknown13C8;
	DWORD unknown13CC;
	DWORD unknown13D0;
	DWORD unknown13D4;
	DWORD unknown13D8;
	DWORD unknown13DC;
	DWORD unknown13E0;
	DWORD unknown13E4;
	DWORD unknown13E8;
	DWORD unknown13EC;
	DWORD unknown13F0;
	DWORD unknown13F4;
	DWORD unknown13F8;
	DWORD unknown13FC;
	DWORD unknown1400;
	DWORD unknown1404;
	DWORD unknown1408;
	DWORD unknown140C;
	DWORD unknown1410;
	DWORD unknown1414;
	DWORD unknown1418;
	DWORD unknown141C;
	DWORD unknown1420;
	DWORD unknown1424;
	DWORD unknown1428;
	DWORD unknown142C;
	DWORD unknown1430;
	DWORD unknown1434;
	DWORD unknown1438;
	DWORD unknown143C;
	DWORD unknown1440;
	DWORD unknown1444;
	DWORD unknown1448;
	DWORD unknown144C;
	DWORD unknown1450;
	DWORD unknown1454;
	DWORD unknown1458;
	DWORD unknown145C;
	DWORD unknown1460;
	DWORD unknown1464;
	DWORD unknown1468;
	DWORD unknown146C;
	DWORD unknown1470;
	DWORD unknown1474;
	DWORD unknown1478;
	DWORD unknown147C;
	DWORD unknown1480;
	DWORD unknown1484;
	DWORD unknown1488;
	DWORD unknown148C;
	DWORD unknown1490;
	DWORD unknown1494;
	DWORD unknown1498;
	DWORD unknown149C;
	DWORD unknown14A0;
	DWORD unknown14A4;
	DWORD unknown14A8;
	DWORD unknown14AC;
	DWORD unknown14B0;
	DWORD unknown14B4;
	DWORD unknown14B8;
	DWORD unknown14BC;
	DWORD unknown14C0;
	DWORD unknown14C4;
	DWORD unknown14C8;
	DWORD unknown14CC;
	DWORD unknown14D0;
	DWORD unknown14D4;
	DWORD unknown14D8;
	DWORD unknown14DC;
	DWORD unknown14E0;
	DWORD unknown14E4;
	DWORD unknown14E8;
	DWORD unknown14EC;
	DWORD unknown14F0;
	DWORD unknown14F4;
	DWORD unknown14F8;
	DWORD unknown14FC;
	DWORD unknown1500;
	DWORD unknown1504;
	DWORD unknown1508;
	DWORD unknown150C;
	DWORD unknown1510;
	DWORD unknown1514;
	DWORD unknown1518;
	DWORD unknown151C;
	DWORD unknown1520;
	DWORD unknown1524;
	DWORD unknown1528;
	DWORD unknown152C;
	DWORD unknown1530;
	DWORD unknown1534;
	DWORD unknown1538;
	DWORD unknown153C;
	DWORD unknown1540;
	DWORD unknown1544;
	DWORD unknown1548;
	DWORD unknown154C;
	DWORD unknown1550;
	DWORD unknown1554;
	DWORD unknown1558;
	DWORD unknown155C;
	DWORD unknown1560;
	DWORD unknown1564;
	DWORD unknown1568;
	DWORD unknown156C;
	DWORD unknown1570;
	DWORD unknown1574;
	DWORD unknown1578;
	DWORD unknown157C;
	DWORD unknown1580;
	DWORD unknown1584;
	DWORD unknown1588;
	DWORD unknown158C;
	DWORD unknown1590;
	DWORD unknown1594;
	DWORD unknown1598;
	DWORD unknown159C;
	DWORD unknown15A0;
	DWORD unknown15A4;
	DWORD unknown15A8;
	DWORD unknown15AC;
	DWORD unknown15B0;
	DWORD unknown15B4;
	DWORD unknown15B8;
	DWORD unknown15BC;
	DWORD unknown15C0;
	DWORD unknown15C4;
	DWORD unknown15C8;
	DWORD unknown15CC;
	DWORD unknown15D0;
	DWORD unknown15D4;
	DWORD unknown15D8;
	DWORD unknown15DC;
	DWORD unknown15E0;
	DWORD unknown15E4;
	DWORD unknown15E8;
	DWORD unknown15EC;
	DWORD unknown15F0;
	DWORD unknown15F4;
	DWORD unknown15F8;
	DWORD unknown15FC;
	DWORD unknown1600;
	DWORD unknown1604;
	DWORD unknown1608;
	DWORD unknown160C;
	DWORD unknown1610;
	DWORD unknown1614;
	DWORD unknown1618;
	DWORD unknown161C;
	DWORD unknown1620;
	DWORD unknown1624;
	DWORD unknown1628;
	DWORD unknown162C;
	DWORD unknown1630;
	DWORD unknown1634;
	DWORD unknown1638;
	DWORD unknown163C;
	DWORD unknown1640;
	DWORD unknown1644;
	DWORD unknown1648;
	DWORD unknown164C;
	DWORD unknown1650;
	DWORD unknown1654;
	DWORD unknown1658;
	DWORD unknown165C;
	DWORD unknown1660;
	DWORD unknown1664;
	DWORD unknown1668;
	DWORD unknown166C;
	DWORD unknown1670;
	DWORD unknown1674;
	DWORD unknown1678;
	DWORD unknown167C;
	DWORD unknown1680;
	DWORD unknown1684;
	DWORD unknown1688;
	DWORD unknown168C;
	DWORD unknown1690;
	DWORD unknown1694;
	DWORD unknown1698;
	DWORD unknown169C;
	DWORD unknown16A0;
	DWORD unknown16A4;
	DWORD unknown16A8;
	DWORD unknown16AC;
	DWORD unknown16B0;
	DWORD unknown16B4;
	DWORD unknown16B8;
	DWORD unknown16BC;
	DWORD unknown16C0;
	DWORD unknown16C4;
	DWORD unknown16C8;
	DWORD unknown16CC;
	DWORD unknown16D0;
	DWORD unknown16D4;
	DWORD unknown16D8;
	DWORD unknown16DC;
	DWORD unknown16E0;
	DWORD unknown16E4;
	DWORD unknown16E8;
	DWORD unknown16EC;
	DWORD unknown16F0;
	DWORD unknown16F4;
	DWORD unknown16F8;
	DWORD unknown16FC;
	DWORD unknown1700;
	DWORD unknown1704;
	DWORD unknown1708;
	DWORD unknown170C;
	DWORD unknown1710;
	DWORD unknown1714;
	DWORD unknown1718;
	DWORD unknown171C;
	DWORD unknown1720;
	DWORD unknown1724;
	DWORD unknown1728;
	DWORD unknown172C;
	DWORD unknown1730;
	DWORD unknown1734;
	DWORD unknown1738;
	DWORD unknown173C;
	DWORD unknown1740;
	DWORD unknown1744;
	DWORD unknown1748;
	DWORD unknown174C;
	DWORD unknown1750;
	DWORD unknown1754;
	DWORD unknown1758;
	DWORD unknown175C;
	DWORD unknown1760;
	DWORD unknown1764;
	DWORD unknown1768;
	DWORD unknown176C;
	DWORD unknown1770;
	DWORD unknown1774;
	DWORD unknown1778;
	DWORD unknown177C;
	DWORD unknown1780;
	DWORD unknown1784;
	DWORD unknown1788;
	DWORD unknown178C;
	DWORD unknown1790;
	DWORD unknown1794;
	DWORD unknown1798;
	DWORD unknown179C;
	DWORD unknown17A0;
	DWORD unknown17A4;
	DWORD unknown17A8;
	DWORD unknown17AC;
	DWORD unknown17B0;
	DWORD unknown17B4;
	DWORD unknown17B8;
	DWORD unknown17BC;
	DWORD unknown17C0;
	DWORD unknown17C4;
	DWORD unknown17C8;
	DWORD unknown17CC;
	DWORD unknown17D0;
	DWORD unknown17D4;
	DWORD unknown17D8;
	DWORD unknown17DC;
	DWORD unknown17E0;
	DWORD unknown17E4;
	DWORD unknown17E8;
	DWORD unknown17EC;
	DWORD unknown17F0;
	DWORD unknown17F4;
	DWORD unknown17F8;
	DWORD unknown17FC;
	DWORD unknown1800;
	DWORD unknown1804;
	DWORD unknown1808;
	DWORD unknown180C;
	DWORD unknown1810;
	DWORD unknown1814;
	DWORD unknown1818;
	DWORD unknown181C;
	DWORD unknown1820;
	DWORD unknown1824;
	DWORD unknown1828;
	DWORD unknown182C;
	DWORD unknown1830;
	DWORD unknown1834;
	DWORD unknown1838;
	DWORD unknown183C;
	DWORD unknown1840;
	DWORD unknown1844;
	DWORD unknown1848;
	DWORD unknown184C;
	DWORD unknown1850;
	DWORD unknown1854;
	DWORD unknown1858;
	DWORD unknown185C;
	DWORD unknown1860;
	DWORD unknown1864;
	DWORD unknown1868;
	DWORD unknown186C;
	DWORD unknown1870;
	DWORD unknown1874;
	DWORD unknown1878;
	DWORD unknown187C;
	DWORD unknown1880;
	DWORD unknown1884;
	DWORD unknown1888;
	DWORD unknown188C;
	DWORD unknown1890;
	DWORD unknown1894;
	DWORD unknown1898;
	DWORD unknown189C;
	DWORD unknown18A0;
	DWORD unknown18A4;
	DWORD unknown18A8;
	DWORD unknown18AC;
	DWORD unknown18B0;
	DWORD unknown18B4;
	DWORD unknown18B8;
	DWORD unknown18BC;
	DWORD unknown18C0;
	DWORD unknown18C4;
	DWORD unknown18C8;
	DWORD unknown18CC;
	DWORD unknown18D0;
	DWORD unknown18D4;
	DWORD unknown18D8;
	DWORD unknown18DC;
	DWORD unknown18E0;
	DWORD unknown18E4;
	DWORD unknown18E8;
	DWORD unknown18EC;
	DWORD unknown18F0;
	DWORD unknown18F4;
	DWORD unknown18F8;
	DWORD unknown18FC;
	DWORD unknown1900;
	DWORD unknown1904;
	DWORD unknown1908;
	DWORD unknown190C;
	DWORD unknown1910;
	DWORD unknown1914;
	DWORD unknown1918;
	DWORD unknown191C;
	DWORD unknown1920;
	DWORD unknown1924;
	DWORD unknown1928;
	DWORD unknown192C;
	DWORD unknown1930;
	DWORD unknown1934;
	DWORD unknown1938;
	DWORD unknown193C;
	DWORD unknown1940;
	DWORD unknown1944;
	DWORD unknown1948;
	DWORD unknown194C;
	DWORD unknown1950;
	DWORD unknown1954;
	DWORD unknown1958;
	DWORD unknown195C;
	DWORD unknown1960;
	DWORD unknown1964;
	DWORD unknown1968;
	DWORD unknown196C;
	DWORD unknown1970;
	DWORD unknown1974;
	DWORD unknown1978;
	DWORD unknown197C;
	DWORD unknown1980;
	DWORD unknown1984;
	DWORD unknown1988;
	DWORD unknown198C;
	DWORD unknown1990;
	DWORD unknown1994;
	DWORD unknown1998;
	DWORD unknown199C;
	DWORD unknown19A0;
	DWORD unknown19A4;
	DWORD unknown19A8;
	DWORD unknown19AC;
	DWORD unknown19B0;
	DWORD unknown19B4;
	DWORD unknown19B8;
	DWORD unknown19BC;
	DWORD unknown19C0;
	DWORD unknown19C4;
	DWORD unknown19C8;
	DWORD unknown19CC;
	DWORD unknown19D0;
	DWORD unknown19D4;
	DWORD unknown19D8;
	DWORD unknown19DC;
	DWORD unknown19E0;
	DWORD unknown19E4;
	DWORD unknown19E8;
	DWORD unknown19EC;
	DWORD unknown19F0;
	DWORD unknown19F4;
	DWORD unknown19F8;
	DWORD unknown19FC;
	DWORD unknown1A00;
	DWORD unknown1A04;
	DWORD unknown1A08;
	DWORD unknown1A0C;
	DWORD unknown1A10;
	DWORD unknown1A14;
	DWORD unknown1A18;
	DWORD unknown1A1C;
	DWORD unknown1A20;
	DWORD unknown1A24;
	DWORD unknown1A28;
	DWORD unknown1A2C;
	DWORD unknown1A30;
	DWORD unknown1A34;
	DWORD unknown1A38;
	DWORD unknown1A3C;
	DWORD unknown1A40;
	DWORD unknown1A44;
	DWORD unknown1A48;
	DWORD unknown1A4C;
	DWORD unknown1A50;
	DWORD unknown1A54;
	DWORD unknown1A58;
	DWORD unknown1A5C;
	DWORD unknown1A60;
	DWORD unknown1A64;
	DWORD unknown1A68;
	DWORD unknown1A6C;
	DWORD unknown1A70;
	DWORD unknown1A74;
	DWORD unknown1A78;
	DWORD unknown1A7C;
	DWORD unknown1A80;
	DWORD unknown1A84;
	DWORD unknown1A88;
	DWORD unknown1A8C;
	DWORD unknown1A90;
	DWORD unknown1A94;
	DWORD unknown1A98;
	DWORD unknown1A9C;
	DWORD unknown1AA0;
	DWORD unknown1AA4;
	DWORD unknown1AA8;
	DWORD unknown1AAC;
	DWORD unknown1AB0;
	DWORD unknown1AB4;
	DWORD unknown1AB8;
	DWORD unknown1ABC;
	DWORD unknown1AC0;
	DWORD unknown1AC4;
	DWORD unknown1AC8;
	DWORD unknown1ACC;
	DWORD unknown1AD0;
	DWORD unknown1AD4;
	DWORD unknown1AD8;
	DWORD unknown1ADC;
	DWORD unknown1AE0;
	DWORD unknown1AE4;
	DWORD unknown1AE8;
	DWORD unknown1AEC;
	DWORD unknown1AF0;
	DWORD unknown1AF4;
	DWORD unknown1AF8;
	DWORD unknown1AFC;
	DWORD unknown1B00;
	DWORD unknown1B04;
	DWORD unknown1B08;
	DWORD unknown1B0C;
	DWORD unknown1B10;
	DWORD unknown1B14;
	DWORD unknown1B18;
	DWORD unknown1B1C;
	DWORD unknown1B20;
	DWORD unknown1B24;
	DWORD unknown1B28;
	DWORD unknown1B2C;
	DWORD unknown1B30;
	DWORD unknown1B34;
	DWORD unknown1B38;
	DWORD unknown1B3C;
	DWORD unknown1B40;
	DWORD unknown1B44;
	DWORD unknown1B48;
	DWORD unknown1B4C;
	DWORD unknown1B50;
	DWORD unknown1B54;
	DWORD unknown1B58;
	DWORD unknown1B5C;
	DWORD unknown1B60;
	DWORD unknown1B64;
	DWORD unknown1B68;
	DWORD unknown1B6C;
	DWORD unknown1B70;
	DWORD unknown1B74;
	DWORD unknown1B78;
	DWORD unknown1B7C;
	DWORD unknown1B80;
	DWORD unknown1B84;
	DWORD unknown1B88;
	DWORD unknown1B8C;
	DWORD unknown1B90;
	DWORD unknown1B94;
	DWORD unknown1B98;
	DWORD unknown1B9C;
	DWORD unknown1BA0;
	DWORD unknown1BA4;
	DWORD unknown1BA8;
	DWORD unknown1BAC;
	DWORD unknown1BB0;
	DWORD unknown1BB4;
	DWORD unknown1BB8;
	DWORD unknown1BBC;
	DWORD unknown1BC0;
	DWORD unknown1BC4;
	DWORD unknown1BC8;
	DWORD unknown1BCC;
	DWORD unknown1BD0;
	DWORD unknown1BD4;
	DWORD unknown1BD8;
	DWORD unknown1BDC;
	DWORD unknown1BE0;
	DWORD unknown1BE4;
	DWORD unknown1BE8;
	DWORD unknown1BEC;
	DWORD unknown1BF0;
	DWORD unknown1BF4;
	DWORD unknown1BF8;
	DWORD unknown1BFC;
	DWORD unknown1C00;
	DWORD unknown1C04;
	DWORD unknown1C08;
	DWORD unknown1C0C;
	DWORD unknown1C10;
	DWORD unknown1C14;
	DWORD unknown1C18;
	DWORD unknown1C1C;
	DWORD unknown1C20;
	DWORD unknown1C24;
	DWORD unknown1C28;
	DWORD unknown1C2C;
	DWORD unknown1C30;
	DWORD unknown1C34;
	DWORD unknown1C38;
	DWORD unknown1C3C;
	DWORD unknown1C40;
	DWORD unknown1C44;
	DWORD unknown1C48;
	DWORD unknown1C4C;
	DWORD unknown1C50;
	DWORD unknown1C54;
	DWORD unknown1C58;
	DWORD unknown1C5C;
	DWORD unknown1C60;
	DWORD unknown1C64;
	DWORD unknown1C68;
	DWORD unknown1C6C;
	DWORD unknown1C70;
	DWORD unknown1C74;
	DWORD unknown1C78;
	DWORD unknown1C7C;
	DWORD unknown1C80;
	DWORD unknown1C84;
	DWORD unknown1C88;
	DWORD unknown1C8C;
	DWORD unknown1C90;
	DWORD unknown1C94;
	DWORD unknown1C98;
	DWORD unknown1C9C;
	DWORD unknown1CA0;
	DWORD unknown1CA4;
	DWORD unknown1CA8;
	DWORD unknown1CAC;
	DWORD unknown1CB0;
	DWORD unknown1CB4;
	DWORD unknown1CB8;
	DWORD unknown1CBC;
	DWORD unknown1CC0;
	DWORD unknown1CC4;
	DWORD unknown1CC8;
	DWORD unknown1CCC;
	DWORD unknown1CD0;
	DWORD unknown1CD4;
	DWORD unknown1CD8;
	DWORD unknown1CDC;
	DWORD unknown1CE0;
	DWORD unknown1CE4;
	DWORD unknown1CE8;
	DWORD unknown1CEC;
	DWORD unknown1CF0;
	DWORD unknown1CF4;
	DWORD unknown1CF8;
	DWORD unknown1CFC;
	DWORD unknown1D00;
	DWORD unknown1D04;
	DWORD unknown1D08;
	DWORD unknown1D0C;
	DWORD unknown1D10;
	DWORD unknown1D14;
	DWORD unknown1D18;
	DWORD unknown1D1C;
	DWORD unknown1D20;
	DWORD unknown1D24;
	DWORD unknown1D28;
	DWORD unknown1D2C;
	DWORD unknown1D30;
	DWORD unknown1D34;
	DWORD unknown1D38;
	DWORD unknown1D3C;
	DWORD unknown1D40;
	DWORD unknown1D44;
	DWORD unknown1D48;
	DWORD unknown1D4C;
	DWORD unknown1D50;
	DWORD unknown1D54;
	DWORD unknown1D58;
	DWORD unknown1D5C;
	DWORD unknown1D60;
	DWORD unknown1D64;
	DWORD unknown1D68;
	DWORD unknown1D6C;
	DWORD unknown1D70;
	DWORD unknown1D74;
	DWORD unknown1D78;
	DWORD unknown1D7C;
	DWORD unknown1D80;
	DWORD unknown1D84;
	DWORD unknown1D88;
	DWORD unknown1D8C;
	DWORD unknown1D90;
	DWORD unknown1D94;
	DWORD unknown1D98;
	DWORD unknown1D9C;
	DWORD unknown1DA0;
	DWORD unknown1DA4;
	DWORD unknown1DA8;
	DWORD unknown1DAC;
	DWORD unknown1DB0;
	DWORD unknown1DB4;
	DWORD unknown1DB8;
	DWORD unknown1DBC;
	DWORD unknown1DC0;
	DWORD unknown1DC4;
	DWORD unknown1DC8;
	DWORD unknown1DCC;
	DWORD unknown1DD0;
	DWORD unknown1DD4;
	DWORD unknown1DD8;
	DWORD unknown1DDC;
	DWORD unknown1DE0;
	DWORD unknown1DE4;
	DWORD unknown1DE8;
	DWORD unknown1DEC;
	DWORD unknown1DF0;
	DWORD unknown1DF4;
	DWORD unknown1DF8;
	DWORD unknown1DFC;
	DWORD unknown1E00;
	DWORD unknown1E04;
	DWORD unknown1E08;
	DWORD unknown1E0C;
	DWORD unknown1E10;
	DWORD unknown1E14;
	DWORD unknown1E18;
	DWORD unknown1E1C;
	DWORD unknown1E20;
	DWORD unknown1E24;
	DWORD unknown1E28;
	DWORD unknown1E2C;
	DWORD unknown1E30;
	DWORD unknown1E34;
	DWORD unknown1E38;
	DWORD unknown1E3C;
	DWORD unknown1E40;
	DWORD unknown1E44;
	DWORD unknown1E48;
	DWORD unknown1E4C;
	DWORD unknown1E50;
	DWORD unknown1E54;
	DWORD unknown1E58;
	DWORD unknown1E5C;
	DWORD unknown1E60;
	DWORD unknown1E64;
	DWORD unknown1E68;
	DWORD unknown1E6C;
	DWORD unknown1E70;
	DWORD unknown1E74;
	DWORD unknown1E78;
	DWORD unknown1E7C;
	DWORD unknown1E80;
	DWORD unknown1E84;
	DWORD unknown1E88;
	DWORD unknown1E8C;
	DWORD unknown1E90;
	DWORD unknown1E94;
	DWORD unknown1E98;
	DWORD unknown1E9C;
	DWORD unknown1EA0;
	DWORD unknown1EA4;
	DWORD unknown1EA8;
	DWORD unknown1EAC;
	DWORD unknown1EB0;
	DWORD unknown1EB4;
	DWORD unknown1EB8;
	DWORD unknown1EBC;
	DWORD unknown1EC0;
	DWORD unknown1EC4;
	DWORD unknown1EC8;
	DWORD unknown1ECC;
	DWORD unknown1ED0;
	DWORD unknown1ED4;
	DWORD unknown1ED8;
	DWORD unknown1EDC;
	DWORD unknown1EE0;
	DWORD unknown1EE4;
	DWORD unknown1EE8;
	DWORD unknown1EEC;
	DWORD unknown1EF0;
	DWORD unknown1EF4;
	DWORD unknown1EF8;
	DWORD unknown1EFC;
	DWORD unknown1F00;
	DWORD unknown1F04;
	DWORD unknown1F08;
	DWORD unknown1F0C;
	DWORD unknown1F10;
	DWORD unknown1F14;
	DWORD unknown1F18;
	DWORD unknown1F1C;
	DWORD unknown1F20;
	DWORD unknown1F24;
	DWORD unknown1F28;
	DWORD unknown1F2C;
	DWORD unknown1F30;
	DWORD unknown1F34;
	DWORD unknown1F38;
	DWORD unknown1F3C;
	DWORD unknown1F40;
	DWORD unknown1F44;
	DWORD unknown1F48;
	DWORD unknown1F4C;
	DWORD unknown1F50;
	DWORD unknown1F54;
	DWORD unknown1F58;
	DWORD unknown1F5C;
	DWORD unknown1F60;
	DWORD unknown1F64;
	DWORD unknown1F68;
	DWORD unknown1F6C;
	DWORD unknown1F70;
	DWORD unknown1F74;
	DWORD unknown1F78;
	DWORD unknown1F7C;
	DWORD unknown1F80;
	DWORD unknown1F84;
	DWORD unknown1F88;
	DWORD unknown1F8C;
	DWORD unknown1F90;
	CACHED_TEXTURE curr_hw_texture[8];
	DWORD unknown1FD4;
	DWORD unknown1FD8;
	DWORD unknown1FDC;
	DWORD unknown1FE0;
	DWORD unknown1FE4;
	DWORD unknown1FE8;
	DWORD unknown1FEC;
	DWORD unknown1FF0;
	DWORD unknown1FF4;
	DWORD unknown1FF8;
	DWORD unknown1FFC;
	DWORD unknown2000;
	DWORD unknown2004;
	DWORD unknown2008;
	DWORD unknown200C;
	DWORD unknown2010;
	DWORD unknown2014;
	DWORD unknown2018;
	DWORD unknown201C;
	DWORD unknown2020;
	DWORD unknown2024;
	DWORD unknown2028;
	DWORD unknown202C;
	DWORD unknown2030;
	DWORD unknown2034;
	DWORD unknown2038;
	DWORD unknown203C;
	DWORD unknown2040;
	DWORD unknown2044;
	DWORD unknown2048;
	DWORD unknown204C;
	DWORD unknown2050;
	DWORD unknown2054;
	DWORD unknown2058;
	DWORD unknown205C;
	DWORD unknown2060;
	DWORD unknown2064;
	DWORD unknown2068;
	DWORD unknown206C;
	DWORD unknown2070;
	DWORD unknown2074;
	DWORD unknown2078;
	DWORD unknown207C;
	DWORD unknown2080;
	DWORD unknown2084;
	DWORD unknown2088;
	DWORD unknown208C;
	DWORD unknown2090;
	DWORD unknown2094;
	DWORD unknown2098;
	DWORD unknown209C;
	DWORD unknown20A0;
	DWORD unknown20A4;
	DWORD unknown20A8;
	DWORD unknown20AC;
	DWORD unknown20B0;
	DWORD unknown20B4;
	DWORD unknown20B8;
	DWORD unknown20BC;
	DWORD unknown20C0;
	DWORD unknown20C4;
	DWORD unknown20C8;
	DWORD unknown20CC;
	DWORD unknown20D0;
	DWORD unknown20D4;
	DWORD unknown20D8;
	DWORD unknown20DC;
	DWORD unknown20E0;
	DWORD unknown20E4;
	DWORD unknown20E8;
	DWORD unknown20EC;
	DWORD unknown20F0;
	DWORD unknown20F4;
	DWORD unknown20F8;
	DWORD unknown20FC;
	DWORD unknown2100;
	DWORD unknown2104;
	DWORD unknown2108;
	DWORD unknown210C;
	DWORD unknown2110;
	DWORD unknown2114;
	DWORD unknown2118;
	DWORD unknown211C;
	DWORD unknown2120;
	DWORD unknown2124;
	DWORD unknown2128;
	DWORD unknown212C;
	DWORD unknown2130;
	DWORD unknown2134;
	DWORD unknown2138;
	DWORD unknown213C;
	DWORD unknown2140;
	DWORD unknown2144;
	DWORD unknown2148;
	DWORD unknown214C;
	DWORD unknown2150;
	DWORD unknown2154;
	DWORD unknown2158;
	DWORD unknown215C;
	DWORD unknown2160;
	DWORD unknown2164;
	DWORD unknown2168;
	DWORD unknown216C;
	DWORD unknown2170;
	DWORD unknown2174;
	DWORD unknown2178;
	DWORD unknown217C;
	DWORD unknown2180;
	DWORD unknown2184;
	DWORD unknown2188;
	DWORD unknown218C;
	DWORD unknown2190;
	DWORD unknown2194;
	DWORD unknown2198;
	DWORD unknown219C;
	DWORD unknown21A0;
	DWORD unknown21A4;
	DWORD unknown21A8;
	DWORD unknown21AC;
	DWORD unknown21B0;
	DWORD unknown21B4;
	DWORD unknown21B8;
	DWORD unknown21BC;
	DWORD unknown21C0;
	DWORD unknown21C4;
	DWORD unknown21C8;
	DWORD unknown21CC;
	DWORD unknown21D0;
	DWORD unknown21D4;
	DWORD unknown21D8;
	DWORD unknown21DC;
	DWORD unknown21E0;
	DWORD unknown21E4;
	DWORD unknown21E8;
	DWORD unknown21EC;
	DWORD unknown21F0;
	DWORD unknown21F4;
	DWORD unknown21F8;
	CACHED_MATRIX curr_hw_view;
	CACHED_MATRIX curr_hw_world;
	CACHED_MATRIX curr_hw_projection;
	CACHED_VIEWPORT curr_hw_viewport;
	DWORD unknown22E4;
	DWORD unknown22E8;
	DWORD unknown22EC;
	DWORD unknown22F0;
	DWORD unknown22F4;
	DWORD unknown22F8;

	// New Fields
	HMODULE d3d8_module;
	//typedef st6::map<U32, U32>	LightInfoMap;
	//LightInfoMap				lights;

	BEGIN_DACOM_MAP_INBOUND(NewRenderPipeline)
		DACOM_INTERFACE_ENTRY(IRenderPipeline8B)
		DACOM_INTERFACE_ENTRY2(IID_IRenderPipeline8B, IRenderPipeline8B)
		DACOM_INTERFACE_ENTRY(IVertexBufferManager)
		DACOM_INTERFACE_ENTRY2(IID_IVertexBufferManager, IVertexBufferManager)
		DACOM_INTERFACE_ENTRY(IRPDraw)
		DACOM_INTERFACE_ENTRY2(IID_IRPDraw, IRPDraw)
		DACOM_INTERFACE_ENTRY(IRPIndexBuffer)
		DACOM_INTERFACE_ENTRY2(IID_IRPIndexBuffer, IRPIndexBuffer)
		DACOM_INTERFACE_ENTRY(IRPVertexBuffer)
		DACOM_INTERFACE_ENTRY2(IID_IRPVertexBuffer, IRPVertexBuffer)
		DACOM_INTERFACE_ENTRY(IGammaControl)
		DACOM_INTERFACE_ENTRY2(IID_IGammaControl, IGammaControl)
		DACOM_INTERFACE_ENTRY(IRPTexture)
		DACOM_INTERFACE_ENTRY2(IID_IRPTexture, IRPTexture)
		DACOM_INTERFACE_ENTRY(IAggregateComponent)
		DACOM_INTERFACE_ENTRY2(IID_IAggregateComponent, IAggregateComponent)
		END_DACOM_MAP()

	DA_HEAP_DEFINE_NEW_OPERATOR(NewRenderPipeline);

	NewRenderPipeline();
	~NewRenderPipeline();
	GENRESULT init(AGGDESC* pDesc);

	// IDAComponent methods

	DACOM_DEFMETHOD(QueryInterface)(const C8* interface_name, void** instance) override = 0;
	DACOM_DEFMETHOD_(U32, AddRef)(void) override = 0;
	DACOM_DEFMETHOD_(U32, Release)(void) override = 0;

	// IRenderPipeline8B methods

	DACOM_DEFMETHOD(startup)(const char* profile_name) override;
	DACOM_DEFMETHOD(shutdown)(void) override;
	DACOM_DEFMETHOD(set_pipeline_state)(RPPIPELINESTATE state, U32 value) override;
	DACOM_DEFMETHOD(get_pipeline_state)(RPPIPELINESTATE state, U32* value) override;
	DACOM_DEFMETHOD(get_device_info)(RPDEVICEINFO* info) override;
	DACOM_DEFMETHOD(query_device_ability)(RPDEVICEABILITY ability, U32* out_answer) override;
	DACOM_DEFMETHOD(get_num_display_modes)(U32* count) override;
	DACOM_DEFMETHOD(get_display_mode)(RPDISPLAYMODEINFO* mode, U32 mode_num) override;
	DACOM_DEFMETHOD(select_mode)(RPBUFFERSINFO* mode, U32* adapter) override;
	DACOM_DEFMETHOD(create_buffers)(HWND hwnd, RPBUFFERSINFO* buffersinfo, RPBUFFERSINFO* out_buffersinfo) override;
	DACOM_DEFMETHOD(get_buffers)(U32* adapter, RPBUFFERSINFO* out_buffersinfo) override;
	DACOM_DEFMETHOD(destroy_buffers)(void) override;
	DACOM_DEFMETHOD(clear_buffers)(U32 rp_clear_flags, RECT* viewport_sub_rect) override;
	DACOM_DEFMETHOD(swap_buffers)(void) override;
	DACOM_DEFMETHOD(lock_buffer)(RPLOCKDATA* lockData) override;
	DACOM_DEFMETHOD(unlock_buffer)(void) override;
	DACOM_DEFMETHOD(get_buffer_interface)(const char* iid, void** out_iif) override;
	DACOM_DEFMETHOD(get_device_stats)(RPDEVICESTATS* stat) override;
	DACOM_DEFMETHOD(set_viewport)(int x, int y, int w, int h) override;
	DACOM_DEFMETHOD(get_viewport)(int* out_x, int* out_y, int* out_w, int* out_h) override;
	DACOM_DEFMETHOD(set_depth_range)(float lower_z_bound, float upper_z_bound) override;
	DACOM_DEFMETHOD(get_depth_range)(float* lower_z_bound, float* upper_z_bound) override;
	DACOM_DEFMETHOD(set_window)(HWND hwnd, int x, int y, int w, int h) override;
	DACOM_DEFMETHOD(get_window)(HWND* out_hwnd, int* out_x, int* out_y, int* out_w, int* out_h) override;
	DACOM_DEFMETHOD(set_world)(const Transform& world) override;
	DACOM_DEFMETHOD(get_world)(Transform& world) override;
	DACOM_DEFMETHOD(set_view)(const Transform& view) override;
	DACOM_DEFMETHOD(get_view)(Transform& view) override;
	DACOM_DEFMETHOD(set_modelview)(const Transform& modelview) override;
	DACOM_DEFMETHOD(get_modelview)(Transform& modelview) override;
	DACOM_DEFMETHOD(set_projection)(const Matrix4& projection) override;
	DACOM_DEFMETHOD(get_projection)(Matrix4& projection) override;
	DACOM_DEFMETHOD(set_lookat)(float eyex, float eyey, float eyez, float centerx, float centery, float centerz, float upx, float upy, float upz) override;
	DACOM_DEFMETHOD(set_ortho)(float left, float right, float bottom, float top, float nearval, float farval) override;
	DACOM_DEFMETHOD(set_perspective)(float fovy, float aspect, float znear, float zfar) override;
	DACOM_DEFMETHOD(set_light)(IRP_LIGHTHANDLE handle, const D3DLIGHT8* light_values) override;
	DACOM_DEFMETHOD(destroy_light)(IRP_LIGHTHANDLE handle) override;
	DACOM_DEFMETHOD(get_light)(IRP_LIGHTHANDLE handle, D3DLIGHT8* out_light_values) override;
	DACOM_DEFMETHOD(set_light_enable)(IRP_LIGHTHANDLE handle, U32 enable) override;
	DACOM_DEFMETHOD(get_light_enable)(IRP_LIGHTHANDLE handle, U32* out_enable) override;
	DACOM_DEFMETHOD(set_material)(D3DMATERIAL8* material_values) override;
	DACOM_DEFMETHOD(get_material)(D3DMATERIAL8* out_material_values) override;
	DACOM_DEFMETHOD(create_texture)(int width, int height, const PFenum* desiredformat, int num_lod, U32 irp_ctf_flags, IRP_TEXTUREHANDLE* out_htexture) override;
	DACOM_DEFMETHOD(destroy_texture)(IRP_TEXTUREHANDLE htexture) override;
	DACOM_DEFMETHOD(is_texture)(IRP_TEXTUREHANDLE htexture) override;
	DACOM_DEFMETHOD(lock_texture)(IRP_TEXTUREHANDLE htexture, U32 subsurface, RPLOCKDATA* lockData) override;
	DACOM_DEFMETHOD(unlock_texture)(IRP_TEXTUREHANDLE htexture, U32 subsurface) override;
	DACOM_DEFMETHOD(get_texture_format)(IRP_TEXTUREHANDLE htexture, PFenum* out_pf) override;
	DACOM_DEFMETHOD(get_texture_dim)(IRP_TEXTUREHANDLE htexture, U32* out_width, U32* out_height, U32* out_num_lod) override;
	DACOM_DEFMETHOD(get_texture_interface)(IRP_TEXTUREHANDLE htexture, const char* iid, void** out_iif) override;
	DACOM_DEFMETHOD(set_texture_level_data)(IRP_TEXTUREHANDLE htexture, U32 subsurface, int src_width, int src_height, int src_stride, const PFenum* src_format, const void* src_pixel, const void* src_alpha, const RGB* src_palette) override;
	DACOM_DEFMETHOD(blit_texture)(IRP_TEXTUREHANDLE hDest, U32 destLevel, RECT destRect, IRP_TEXTUREHANDLE hSrc, U32 srcLevel, RECT srcRect) override;
	DACOM_DEFMETHOD(set_render_target)(UNKNOWN a2, UNKNOWN a3, UNKNOWN a4) override;
	DACOM_DEFMETHOD(get_render_target)(void* a2) override;
	DACOM_DEFMETHOD(begin_scene)(void) override;
	DACOM_DEFMETHOD(end_scene)(void) override;
	DACOM_DEFMETHOD(reset_render_states_to_defaults)(void) override;
	DACOM_DEFMETHOD(set_render_state)(D3DRENDERSTATETYPE state, U32 value) override;
	DACOM_DEFMETHOD(get_render_state)(D3DRENDERSTATETYPE state, U32* value) override;
	DACOM_DEFMETHOD(set_texture_stage_state)(U32 stage, D3DTEXTURESTAGESTATETYPE state, U32 value) override;
	DACOM_DEFMETHOD(get_texture_stage_state)(U32 stage, D3DTEXTURESTAGESTATETYPE state, U32* value) override;
	DACOM_DEFMETHOD(set_texture_stage_transform)(U32 stage, Matrix4 const& mat4) override;
	DACOM_DEFMETHOD(get_texture_stage_transform)(U32 stage, Matrix4& out_mat4) override;
	DACOM_DEFMETHOD(set_texture_stage_texture)(U32 stage, IRP_TEXTUREHANDLE htexture) override;
	DACOM_DEFMETHOD(get_texture_stage_texture)(U32 stage, IRP_TEXTUREHANDLE* out_htexture) override;
	DACOM_DEFMETHOD(verify_state)(void) override;
	DACOM_DEFMETHOD(draw_primitive)(D3DPRIMITIVETYPE type, U32 vertex_format, const void* verts, U32 num_verts, U32 flags) override;
	DACOM_DEFMETHOD(draw_indexed_primitive)(D3DPRIMITIVETYPE type, U32 vertex_format, const void* verts, U32 num_verts, const U16* indices, U32 num_indices, U32 flags) override;
	DACOM_DEFMETHOD(draw_primitive_vb)(D3DPRIMITIVETYPE type, IRP_VERTEXBUFFERHANDLE vbhandle, U32 start_vert, U32 num_verts, U32 flags) override;
	DACOM_DEFMETHOD(draw_indexed_primitive_vb)(D3DPRIMITIVETYPE type, IRP_VERTEXBUFFERHANDLE vbhandle, U32 start_vert, U32 num_verts, const U16* indices, U32 num_indices, U32 flags) override;
	DACOM_DEFMETHOD(add_light)(IRP_LIGHTHANDLE handle) override;
	DACOM_DEFMETHOD(remove_light)(IRP_LIGHTHANDLE handle) override;
	DACOM_DEFMETHOD(update_light)(IRP_LIGHTHANDLE handle) override;
	DACOM_DEFMETHOD(set_world_n)(UNKNOWN a2, Transform* transform) override;

	// IVertexBufferManager methods

	DACOM_DEFMETHOD(VertexBufferManager_UnknownC)(UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN) override;
	DACOM_DEFMETHOD(VertexBufferManager_Unknown10)() override;
	DACOM_DEFMETHOD(acquire_vertex_buffer)(UNKNOWN vertex_format, U32 num_verts, VertexBufferAcquire* out_result) override;
	DACOM_DEFMETHOD(release_vertex_buffer)(VertexBufferAcquire* vbacquire) override;
	DACOM_DEFMETHOD(VertexBufferManager_Unknown1C)() override;
	DACOM_DEFMETHOD(VertexBufferManager_Unknown20)(UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN) override;

	// IRPDraw methods

	DACOM_DEFMETHOD(draw_indexed_primitive)(D3DPRIMITIVETYPE type, U32 min_index, U32 num_verts, U32 start_index, U32 count) override;

	// IRPIndexBuffer methods

	DACOM_DEFMETHOD(create_index_buffer)(U32 count, IRP_INDEXBUFFERHANDLE* out_ibhandle, BYTE flags) override;
	DACOM_DEFMETHOD(destroy_index_buffer)(IRP_INDEXBUFFERHANDLE ibhandle) override;
	DACOM_DEFMETHOD(create_ib)(IRP_INDEXBUFFERHANDLE ibhandle, U32 count) override;
	DACOM_DEFMETHOD(copy_indices)(IRP_INDEXBUFFERHANDLE ibhandle, U32* offset, void* data, U32 count) override;
	DACOM_DEFMETHOD(lock_ib)(IRP_INDEXBUFFERHANDLE ibhandle, U32* offset, void** locked_data, U32 count) override;
	DACOM_DEFMETHOD(unlock_ib)(IRP_INDEXBUFFERHANDLE ibhandle) override;
	DACOM_DEFMETHOD(select_ib)(IRP_INDEXBUFFERHANDLE ibhandle, UNKNOWN a3, UNKNOWN a4, UNKNOWN a5) override;
	DACOM_DEFMETHOD(get_ib_count)(IRP_INDEXBUFFERHANDLE ibhandle, U32* out_count) override;
	DACOM_DEFMETHOD_(BOOL32, is_ib_valid)(IRP_INDEXBUFFERHANDLE ibhandle) override;

	// IRPVertexBuffer methods

	DACOM_DEFMETHOD(create_vb)(UNKNOWN format, U32 count, IRP_VERTEXBUFFERHANDLE* out_vbhandle, BYTE flags) override;
	DACOM_DEFMETHOD(destroy_vb)(IRP_VERTEXBUFFERHANDLE vbhandle) override;
	DACOM_DEFMETHOD(ressize_vb)(IRP_VERTEXBUFFERHANDLE vbhandle, UNKNOWN format, U32 count) override;
	DACOM_DEFMETHOD(copy_vertices)(IRP_VERTEXBUFFERHANDLE vbhandle, U32* offset, UNKNOWN* a4, UNKNOWN a5, UNKNOWN a6) override;
	DACOM_DEFMETHOD(lock_vb)(IRP_VERTEXBUFFERHANDLE vbhandle, U32* offset, void** locked_data, U32 count) override;
	DACOM_DEFMETHOD(unlock_vb)(IRP_VERTEXBUFFERHANDLE vbhandle) override;
	DACOM_DEFMETHOD(RPVertexBuffer_Unknown24)(UNKNOWN) override;
	DACOM_DEFMETHOD(select_vb)(IRP_VERTEXBUFFERHANDLE vbhandle) override;
	DACOM_DEFMETHOD(get_vb_count)(IRP_VERTEXBUFFERHANDLE vbhandle, UNKNOWN* vertex_format, U32* num_verts) override;
	DACOM_DEFMETHOD_(BOOL32, is_vb_valid)(IRP_VERTEXBUFFERHANDLE vbhandle) override;

	// IGammaControl methods

	DACOM_DEFMETHOD(set_gamma_function)(IGC_COMPONENT which, float display_gamma, float bias = 0.0, float slope = 1.0, float black_offset = 0.0) override;
	DACOM_DEFMETHOD(set_gamma_ramp)(IGC_COMPONENT igc_component, U16* ramp) override;
	DACOM_DEFMETHOD(get_gamma_ramp)(IGC_COMPONENT igc_component, U16* out_ramp) override;
	DACOM_DEFMETHOD(set_calibration_enable)(bool enabled) override;
	DACOM_DEFMETHOD(get_calibration_enable)(void) override;

	// IRPTexture methods

	DACOM_DEFMETHOD(print_screen)(IFileSystem* pFileSystem, const char* filepath) override;
	DACOM_DEFMETHOD(load_texture)(UNKNOWN* a2_interface, const char* filepath, IRP_TEXTUREHANDLE* out_texture) override;
	DACOM_DEFMETHOD(load_surface_from_file)(UNKNOWN* a2_interface, UNKNOWN a3, UNKNOWN a4, UNKNOWN a5) override;
	DACOM_DEFMETHOD(RPTexture_Unknown18)(UNKNOWN a2, UNKNOWN a3, UNKNOWN* a4) override;
	DACOM_DEFMETHOD(load_cubemap)(UNKNOWN* a2_interface, const char* filepath, IRP_TEXTUREHANDLE* out_texture) override;

	// IAggregateComponent methods

	DACOM_DEFMETHOD(Initialize) (void) override;
};



// 6D01143
NewRenderPipeline::NewRenderPipeline()
{
	d3d8_module = NULL;
	debug_point;
	DirectX8_Ctor(this);
	debug_point;
}

// 6D01689
NewRenderPipeline::~NewRenderPipeline()
{
	debug_point;
	DirectX8_Dtor(this);
	debug_point;
}

GENRESULT NewRenderPipeline::init(AGGDESC* descriptor)
{
	if (descriptor->description && strlen(descriptor->description))
	{
		strcpy_s(profile_name, descriptor->description);
	}
	else
	{
		strcpy_s(profile_name, "RenderPipeline");
	}

	GENERAL_NOTICE(profile_name);

	return GR_OK;
}

GENRESULT NewRenderPipeline::startup(const char* profile_name)
{
	if (!profile_name)
		profile_name = this->profile_name;
	memcpy(profile_nameB, this->profile_name, sizeof(this->profile_name));
	strcpy(configuration_database_file, "FLConfigDatabase.txt");

	if (d3d8_module == NULL)
		d3d8_module = LoadLibraryA("d3d8.dll");

	IDirect3D8* (WINAPI * pDirect3DCreate8)(UINT SDKVersion) = reinterpret_cast<decltype(pDirect3DCreate8)>(GetProcAddress(d3d8_module, "Direct3DCreate8"));
	ASSERT(pDirect3DCreate8 != NULL);

	direct3d = pDirect3DCreate8(D3D_SDK_VERSION);
	if (!direct3d)
	{
		GENERAL_ERROR("NewRenderPipeline: startup: couldnt create directx8 device");
	}

	direct3d_adapter = 0;
	direct3d_device_type = D3DDEVTYPE_HAL;
	direct3d_behavior_flags = 0;

	bool fpu_preserve = false; // IProfileParser | FPU_PRESERVE
	if (fpu_preserve)
		this->direct3d_behavior_flags |= D3DCREATE_FPU_PRESERVE;

	bool multithreaded = false; // IProfileParser | MULTITHREADED
	if (multithreaded)
		this->direct3d_behavior_flags |= D3DCREATE_MULTITHREADED;

	bool puredevice = false; // IProfileParser | PUREDEVICE
	if (puredevice)
		this->direct3d_behavior_flags |= D3DCREATE_PUREDEVICE;

	/*int vertex_processing_method = 2;*/ // IProfileParser | SOFTWARE_VERTEXPROCESSING/MIXED_VERTEXPROCESSING/HARDWARE_VERTEXPROCESSING
	this->direct3d_behavior_flags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;

	pipeline_states[RP_TEXTURE].unknown4 = true;
	pipeline_states[RP_LIGHTING].unknown4 = true;
	pipeline_states[RP_TEXTURE_LOD].unknown4 = true;
	pipeline_states[RP_TEXTURE_ALLOW_8BIT].unknown4 = true;
	pipeline_states[RP_TEXTURE_ALLOW_32BIT].unknown4 = true;
	pipeline_states[RP_TEXTURE_ALLOW_DXT].unknown4 = true;
	pipeline_states[RP_BROKEN_MULTITEXTURE].unknown4 = true;
	pipeline_states[RP_VIEWSPACE_LIGHTS].unknown4 = true;
	pipeline_states[RP_CLEAR_COLOR].unknown4 = true;
	pipeline_states[RP_CLEAR_ZDEPTH].unknown4 = true;
	pipeline_states[RP_CLEAR_STENCIL].unknown4 = true;
	pipeline_states[RP_STATE_CACHE].unknown4 = true;
	pipeline_states[RP_BROKEN_MOD2X].unknown4 = true;
	pipeline_states[RP_BROKEN_FLIP].unknown4 = true;

	set_pipeline_state(RP_TEXTURE, 1);
	set_pipeline_state(RP_LIGHTING, 1);
	set_pipeline_state(RP_TEXTURE_LOD, 1);
	set_pipeline_state(RP_TEXTURE_ALLOW_8BIT, 0);
	set_pipeline_state(RP_TEXTURE_ALLOW_32BIT, 1);
	set_pipeline_state(RP_TEXTURE_ALLOW_DXT, 1);
	set_pipeline_state(RP_BROKEN_MULTITEXTURE, 7);
	set_pipeline_state(RP_VIEWSPACE_LIGHTS, 1);
	set_pipeline_state(RP_CLEAR_COLOR, 0);
	set_pipeline_state(RP_CLEAR_ZDEPTH, coerce_cast<U32>(1.0f));
	set_pipeline_state(RP_CLEAR_STENCIL, 0xFFFFFFFFu);
	set_pipeline_state(RP_STATE_CACHE, 1);
	set_pipeline_state(RP_BROKEN_MOD2X, 0);
	set_pipeline_state(RP_BROKEN_FLIP, 0);

	device_abilities[RP_A_ABILITY0] = 0;
	device_abilities[RP_A_D3DPRASTERCAPS_ANISOTROPY] = 1;
	device_abilities[RP_A_D3DPRASTERCAPS_WFOG] = 1;
	device_abilities[RP_A_ABILITY3] = 0xC0000000u;
	device_abilities[RP_A_DEVICE_GEOMETRY] = 1;
	device_abilities[RP_A_TEXTURE_SQUARE_ONLY] = 0;
	device_abilities[RP_A_TEXTURE_MAX_WIDTH] = 16384;
	device_abilities[RP_A_TEXTURE_MAX_HEIGHT] = 16384;
	device_abilities[RP_A_TEXTURE_CUBEMAPS] = 1;
	device_abilities[RP_A_DEPTH_BIAS] = 1;
	device_abilities[RP_A_FOG_VERTEX] = 1;
	device_abilities[RP_A_FOG_PIXEL] = 1;
	device_abilities[RP_A_FOG_RANGE] = 1;
	device_abilities[RP_A_FOG_W] = 1;
	device_abilities[RP_A_RASTER_ANTIALIASEDGES] = 0;
	device_abilities[RP_A_DEVICE_SUPPORT_LEVEL] = 0; // Force Supported
	device_abilities[RP_A_TEXTURE_TRILINEAR] = 1;
	device_abilities[RP_A_DEVICE_BAD_MODE] = 0;
	device_abilities[RP_A_DEVICE_BAD_4444] = 0;
	device_abilities[RP_A_DEVICE_NO_PARALLELISM] = 0;

	this->unknown168 = 0xC0000000u;

	ICOManager* DACOM = DACOM_Acquire();
	unknown128 = 0;
	if (opt_get_u32(DACOM, profile_parser, profile_name, "LOCKABLE_BACKBUFFER", 0, 0) != 0)
	{
		unknown128 |= 1;
	}
	if (opt_get_u32(DACOM, profile_parser, profile_name, "USE_SYSLOCK", 0, 0) != 0)
	{
		unknown128 |= 2;
	}
	if (opt_get_u32(DACOM, profile_parser, profile_name, "HANDLE_SWAPLOSS", 0, 0) != 0)
	{
		unknown128 |= 4;
	}

	for (auto& rs : pipeline_state_info)
	{
		if (SUCCEEDED(opt_has_key(DACOM, profile_parser, profile_name, rs.second.key_name)))
		{
			rs.second.valid = true;
			auto previous_value = rs.second.rt_default_value;
			opt_get_u32(DACOM, profile_parser, profile_name, rs.second.key_name, rs.second.ct_default_value, &rs.second.rt_default_value);
			if (previous_value != rs.second.rt_default_value)
				GENERAL_TRACE_1(TEMPSTR("Config Pipeline State Changed: %s %u -> %u", rs.second.key_name, previous_value, rs.second.ct_default_value));
		}
	}

	for (auto& rs : render_state_info)
	{
		if (rs.second.valid)
		{
			auto previous_value = rs.second.rt_default_value;
			opt_get_u32(DACOM, profile_parser, profile_name, rs.second.key_name, rs.second.ct_default_value, &rs.second.rt_default_value);
			if (previous_value != rs.second.rt_default_value)
				GENERAL_TRACE_1(TEMPSTR("Config Render State Changed: %s %u -> %u", rs.second.key_name, previous_value, rs.second.ct_default_value));
		}
	}

	return GR_OK;
}

GENRESULT NewRenderPipeline::shutdown(void)
{
	destroy_buffers();

	ASSERT(unknown198.size() == 0); // nuked

	if (direct3d_device)
	{
		U32 refcount = direct3d_device->Release();
		if (refcount > 0)
		{
			GENERAL_WARNING(TEMPSTR("direct3d_device released with %u references", refcount));
		}
		direct3d_device = 0;
	}
	if (direct3d)
	{
		U32 refcount = direct3d->Release();
		if (refcount > 0)
		{
			GENERAL_WARNING(TEMPSTR("direct3d released with %u references", refcount));
		}
		direct3d = 0;
	}

	FreeLibrary(d3d8_module);

	return GR_OK;
}

GENRESULT NewRenderPipeline::set_pipeline_state(RPPIPELINESTATE state, U32 value)
{
	CHECK_STARTUP();
	ASSERT(state < RP_MAX_PIPELINE_STATE);

	CACHED_PIPELINE_STATE& pipeline_state = pipeline_states[state];
	if ((!pipeline_state.valid || pipeline_state.value != value) && pipeline_state.unknown4)
	{
		pipeline_state.valid = 1;
		pipeline_state.value = value;
	}
	return GR_OK;
}

GENRESULT NewRenderPipeline::get_pipeline_state(RPPIPELINESTATE state, U32* value)
{
	CHECK_STARTUP();
	ASSERT(state < RP_MAX_PIPELINE_STATE);

	CACHED_PIPELINE_STATE& pipeline_state = pipeline_states[state];
	*value = pipeline_state.value;
	return GR_OK;
}

GENRESULT NewRenderPipeline::get_device_info(RPDEVICEINFO* info)
{
	CHECK_STARTUP();

	GENRESULT result = GR_GENERIC;
	if (direct3d_adapter < unknown198.size())
	{
		*info = unknown198[direct3d_adapter];
		result = GR_OK;
	}

	return result;
}

GENRESULT NewRenderPipeline::query_device_ability(RPDEVICEABILITY ability, U32* out_answer)
{
	CHECK_STARTUP();
	ASSERT(ability < RP_A_MAX_ABILITY);
	ASSERT(out_answer != NULL);

	*out_answer = this->device_abilities[ability];
	return GR_OK;
}

GENRESULT NewRenderPipeline::get_num_display_modes(U32* count)
{
	CHECK_STARTUP();

	*count = direct3d->GetAdapterModeCount(this->direct3d_adapter);
	return GR_OK;
}

GENRESULT NewRenderPipeline::get_display_mode(RPDISPLAYMODEINFO* mode, U32 mode_num)
{
	CHECK_STARTUP();

	D3DDISPLAYMODE mode_1; // [esp+2018h] [ebp-10h] BYREF
	HRESULT v8 = this->direct3d->EnumAdapterModes(this->direct3d_adapter, mode_num, &mode_1);
	if (v8 >= 0)
	{
		mode->mode_num = mode_num;
		mode->width = mode_1.Width;
		mode->height = mode_1.Height;
		mode->refresh_rate = mode_1.RefreshRate;
		mode->render_pf = d3d_to_pf(mode_1.Format);
		if (this->device_abilities[17] && mode->width >= this->device_abilities[17])
		{
			return GR_DATA_NOT_FOUND;
		}
		else
		{
			return GR_OK;
		}
	}

	return GENRESULT(v8);
}

GENRESULT NewRenderPipeline::select_mode(RPBUFFERSINFO* mode, U32* adapter)
{
	CHECK_STARTUP();

	GENRESULT result = GR_GENERIC;

	U32 mode_count = direct3d->GetAdapterModeCount(direct3d_adapter);
	for (U32 mode_index = 0; mode_index < mode_count; ++mode_index)
	{
		D3DDISPLAYMODE current_mode;
		if (SUCCEEDED(direct3d->EnumAdapterModes(direct3d_adapter, mode_index, &current_mode)))
		{
			PFenum pixel_format = d3d_to_pf(current_mode.Format);
			if (current_mode.Width == mode->width &&
				current_mode.Height == mode->height &&
				pf_to_pixel_format(pixel_format)->d3d == mode->format &&
				(device_abilities[RP_A_DEVICE_BAD_MODE] == 0 || current_mode.Width < device_abilities[RP_A_DEVICE_BAD_MODE]))
			{
				*adapter = static_cast<U32>(mode_index);
				result = GR_OK;
				break;
			}
		}
	}

	return result;
}

GENRESULT NewRenderPipeline::create_buffers(HWND hwnd, RPBUFFERSINFO* buffersinfo, RPBUFFERSINFO* out_buffersinfo)
{
	debug_point;

	// Local variables

	U32 i, adapter;
	HWND hwnd_ancestor;
	D3DCAPS8 direct3d_caps;
	D3DDISPLAYMODE adapter_display_mode;
	D3DPRESENT_PARAMETERS present_parameters;
	PixelFormat* v65;
	PFenum v77;
	D3DFORMAT v32, Format, d3d, render_target_format, v84;
	_D3DSWAPEFFECT swapEffect;
	int v63, v73;
	DWORD a5, a4;
	unsigned int v79, v86;
	HRESULT v67, v69, v72, hr, create_index_buffer_result;
	int index_buffer_length;
	RPBUFFERSINFO selected_mode;
	GENRESULT v71;

	CHECK_STARTUP();

	if (this->direct3d_device) {
		v72 = this->direct3d_device->TestCooperativeLevel();
		if (v72 == D3DERR_DEVICELOST)
			return GR_GENERIC;
	}

	this->destroy_buffers();

	render_target_format = D3DFMT_UNKNOWN;
	adapter = UINT_MAX;
	hr = this->direct3d->GetAdapterDisplayMode(this->direct3d_adapter, &adapter_display_mode);
	if (FAILED(hr))
	{
		GENERAL_ERROR(TEMPSTR("create_buffers_select_mode: %s", HRESULT_GET_ERROR_STRING(hr)));
		return (GENRESULT)hr;
	}

	if (buffersinfo->unknown25_fullscreen) {
		adapter = buffersinfo->adapter;
		if (!(adapter & 0x80000000)) {
			v67 = this->direct3d->EnumAdapterModes(this->direct3d_adapter, adapter, &adapter_display_mode);
			if (FAILED(v67)) {
				GENERAL_ERROR(TEMPSTR("create_buffers_select_mode: %s", HRESULT_GET_ERROR_STRING(v67)));
				return (GENRESULT)v67;
			}
		}
		else {
			memcpy(&selected_mode, buffersinfo, sizeof(selected_mode));
			if (buffersinfo->format == D3DFORMAT(D3DFMT_FORCE_DWORD | 0x80000000)) // #TODO what is this?
			{
				d3d = adapter_display_mode.Format;
				selected_mode.format = pf_to_pixel_format(d3d_to_pf(d3d))->d3d;
			}
			v71 = this->select_mode(&selected_mode, &adapter);
			if (v71 < GR_OK) {
				GENERAL_ERROR(TEMPSTR("create_buffers_select_mode: %s", HRESULT_GET_ERROR_STRING(v71)));
				return (GENRESULT)v71;
			}
			v69 = this->direct3d->EnumAdapterModes(this->direct3d_adapter, adapter, &adapter_display_mode);
			if (FAILED(v69)) {
				GENERAL_ERROR(TEMPSTR("create_buffers_select_mode: %s", HRESULT_GET_ERROR_STRING(v69)));
				return (GENRESULT)v69;
			}
			render_target_format = adapter_display_mode.Format;
		}
	}

	Format = adapter_display_mode.Format;
	v77 = d3d_to_pf(Format);

	if (render_target_format == D3DFMT_UNKNOWN) {
		if (buffersinfo->format == D3DFORMAT(D3DFMT_FORCE_DWORD | 0x80000000) // #TODO what is this?
			|| pf_to_pixel_format(v77)->d3d == buffersinfo->format) {
			render_target_format = Format;
		}
		else {
			for (i = 0; i < 9; ++i) {
				v65 = pf_to_pixel_format((PFenum)i);
				if (v65->d3d == buffersinfo->format) {
					v32 = pf_to_d3d(v65->pf);
					v63 = this->direct3d->CheckDeviceFormat(
						this->direct3d_adapter,
						this->direct3d_device_type,
						Format,
						D3DUSAGE_RENDERTARGET,
						D3DRTYPE_SURFACE,
						v32);
					if (SUCCEEDED(v63)) {
						render_target_format = v32;
						break;
					}
				}
			}
		}
	}
	if (render_target_format == D3DFMT_UNKNOWN)
		return GR_GENERIC;

	a4 = buffersinfo->unknown14_auto_depth_stencil_format1;
	if (a4 == UINT_MAX)
		a4 = pf_to_pixel_format(d3d_to_pf(render_target_format))->d3d;
	a5 = buffersinfo->unknown18_auto_depth_stencil_format2;
	v84 = sub_6D159FF(this->direct3d, Format, render_target_format, &a4, &a5);
	if (v84 == D3DFMT_UNKNOWN)
		return GR_GENERIC;

	v73 = !buffersinfo->unknown25_fullscreen;
	v79 = 1;
	v86 = 0;
	swapEffect = D3DSWAPEFFECT_COPY;

	if (buffersinfo->unknown25_fullscreen) {
		if (!buffersinfo->unknown24 || this->pipeline_states[13].value != 0) {
			swapEffect = buffersinfo->unknown1C ? D3DSWAPEFFECT_COPY_VSYNC : D3DSWAPEFFECT_COPY;
		}
		else {
			v79 = buffersinfo->unknown20_buffer_count_plus_one - 1;
			swapEffect = D3DSWAPEFFECT_FLIP;
		}
		switch (buffersinfo->unknown1C) {
		case 0: v86 = 0x80000000; break;
		case 1: v86 = 1; break;
		case 2: v86 = 2; break;
		case 3: v86 = 4; break;
		case 4: v86 = 8; break;
		}
	}
	else if (buffersinfo->unknown24 && this->pipeline_states[13].value == 0) {
		swapEffect = buffersinfo->unknown1C ? D3DSWAPEFFECT_COPY_VSYNC : D3DSWAPEFFECT_COPY;
	}

	present_parameters.BackBufferWidth = buffersinfo->width;
	present_parameters.BackBufferHeight = buffersinfo->height;
	present_parameters.BackBufferFormat = render_target_format;
	present_parameters.BackBufferCount = v79;
	present_parameters.MultiSampleType = D3DMULTISAMPLE_NONE;
	present_parameters.SwapEffect = swapEffect;
	present_parameters.hDeviceWindow = hwnd;
	present_parameters.Windowed = v73;
	present_parameters.EnableAutoDepthStencil = TRUE;
	present_parameters.AutoDepthStencilFormat = v84;
	present_parameters.Flags = (this->unknown128 & 1) != 0;
	present_parameters.FullScreen_RefreshRateInHz = 0;
	present_parameters.FullScreen_PresentationInterval = v86;

	if (this->direct3d_device) {
		hr = this->direct3d_device->Reset(&present_parameters);
		if (FAILED(hr)) {
			GENERAL_ERROR(TEMPSTR("create_device: %s", HRESULT_GET_ERROR_STRING(hr)));
			return (GENRESULT)hr;
		}
	}
	else {
		hwnd_ancestor = GetAncestor(hwnd, GA_ROOT);
		do {
			hr = this->direct3d->CreateDevice(
				this->direct3d_adapter,
				this->direct3d_device_type,
				hwnd_ancestor,
				this->direct3d_behavior_flags,
				&present_parameters,
				&this->direct3d_device);
			if (SUCCEEDED(hr) || !(this->direct3d_behavior_flags & D3DCREATE_HARDWARE_VERTEXPROCESSING))
				break;
			this->direct3d_behavior_flags = (this->direct3d_behavior_flags & ~D3DCREATE_HARDWARE_VERTEXPROCESSING)
				| D3DCREATE_SOFTWARE_VERTEXPROCESSING;
		} while (true);
		if (FAILED(hr)) {
			GENERAL_ERROR(TEMPSTR("create_device: %s", HRESULT_GET_ERROR_STRING(hr)));
			return (GENRESULT)hr;
		}
		hr = this->direct3d_device->GetDeviceCaps(&direct3d_caps);
		if (FAILED(hr)) {
			GENERAL_ERROR(TEMPSTR("create_device_caps: %s", HRESULT_GET_ERROR_STRING(hr)));
			return (GENRESULT)hr;
		}
		update_device_capabilities(this, &direct3d_caps);
	}

	// Index buffer setup
	if (this->scratchIB.direct3d_index_buffer) {
		if (this->scratchIB.locked_data_ptr) {
			this->scratchIB.direct3d_index_buffer->Unlock();
			this->scratchIB.locked_data_ptr = nullptr;
		}
		this->scratchIB.direct3d_index_buffer->Release();
		this->scratchIB.direct3d_index_buffer = nullptr;
	}
	this->scratchIB.element_count = 0x4000;
	this->scratchIB.unknown14 = 0;
	index_buffer_length = 0x8000;
	create_index_buffer_result = this->direct3d_device->CreateIndexBuffer(
		index_buffer_length,
		this->scratchIB.unknown0_flags_or_usage,
		D3DFMT_INDEX16,
		D3DPOOL_DEFAULT,
		&this->scratchIB.direct3d_index_buffer);
	if (FAILED(create_index_buffer_result)) {
		GENERAL_ERROR(TEMPSTR("couldn't create ib (err:%x) %d bytes", create_index_buffer_result, index_buffer_length));
		return (GENRESULT)create_index_buffer_result;
	}
	hr = create_index_buffer_result;

	// Final state setup
	this->unknown184_is_locked |= 1;
	this->unknown230 = v77;
	this->pf_unknown234 = d3d_to_pf(present_parameters.BackBufferFormat);
	this->hwnd = hwnd;
	this->buffers_info.adapter = adapter;
	this->buffers_info.width = buffersinfo->width;
	this->buffers_info.height = buffersinfo->height;
	this->buffers_info.format = pf_to_pixel_format(this->pf_unknown234)->d3d;
	this->buffers_info.unknown14_auto_depth_stencil_format1 = sub_6D15B0D(present_parameters.AutoDepthStencilFormat);
	this->buffers_info.unknown18_auto_depth_stencil_format2 = sub_6D15B45(present_parameters.AutoDepthStencilFormat);
	this->buffers_info.unknown1C = buffersinfo->unknown1C;
	this->buffers_info.unknown20_buffer_count_plus_one = present_parameters.BackBufferCount + 1;
	this->buffers_info.unknown24 = (present_parameters.SwapEffect & 2) != 0;
	this->buffers_info.unknown25_fullscreen = !present_parameters.Windowed;
	if (out_buffersinfo)
		memcpy(out_buffersinfo, &this->buffers_info, sizeof(RPBUFFERSINFO));
	sub_6D0376F(this);
	sub_6D038BA(this);
	sub_6D03C94(this);
	sub_6D047DF(this);

	curr_hw_viewport.invalidate();

	GENRESULT result = GR_GENERIC;
	if (FAILED(result = set_viewport(0, 0, this->buffers_info.width, this->buffers_info.height)))
	{
		return result;
	}

	sub_6D2CE6A(&this->unknown21F4);

	curr_hw_geometry.invalidate();

	return (GENRESULT)hr;
}

GENRESULT NewRenderPipeline::get_buffers(U32* adapter, RPBUFFERSINFO* out_buffersinfo)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_get_buffers(this, adapter, out_buffersinfo);
	return result;
}

GENRESULT NewRenderPipeline::destroy_buffers(void)
{
	GENRESULT result = DirectX8_destroy_buffers(this);
	return result;
}

GENRESULT NewRenderPipeline::clear_buffers(U32 rp_clear_flags, RECT* viewport_sub_rect)
{
	GENRESULT result = DirectX8_clear_buffers(this, rp_clear_flags, viewport_sub_rect);
	return result;
}

GENRESULT NewRenderPipeline::swap_buffers(void)
{
	GENRESULT result = DirectX8_swap_buffers(this);
	return result;
}

GENRESULT NewRenderPipeline::lock_buffer(RPLOCKDATA* lockData)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_lock_buffer(this, lockData);
	return result;
}

GENRESULT NewRenderPipeline::unlock_buffer(void)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_unlock_buffer(this);
	return result;
}

GENRESULT NewRenderPipeline::get_buffer_interface(const char* iid, void** out_iif)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_get_buffer_interface(this, iid, out_iif);
	return result;
}

GENRESULT NewRenderPipeline::get_device_stats(RPDEVICESTATS* stat)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_get_device_stats(this, stat);
	return result;
}

GENRESULT NewRenderPipeline::set_viewport(int x, int y, int w, int h)
{
	CHECK_DEVICE_LIFETIME();

	curr_hw_viewport.set_viewport(
		direct3d_device,
		static_cast<U32>(x),
		static_cast<U32>(y),
		static_cast<U32>(w),
		static_cast<U32>(h),
		true);

	return GR_OK;
}

GENRESULT NewRenderPipeline::get_viewport(int* out_x, int* out_y, int* out_w, int* out_h)
{
	CHECK_DEVICE_LIFETIME();

	curr_hw_viewport.get_viewport(
		direct3d_device,
		reinterpret_cast<U32*>(out_x),
		reinterpret_cast<U32*>(out_y),
		reinterpret_cast<U32*>(out_w),
		reinterpret_cast<U32*>(out_h));

	return GR_OK;
}

GENRESULT NewRenderPipeline::set_depth_range(float lower_z_bound, float upper_z_bound)
{
	CHECK_DEVICE_LIFETIME();

	curr_hw_viewport.set_depth_range(direct3d_device, lower_z_bound, upper_z_bound, true);

	return GR_OK;
}

GENRESULT NewRenderPipeline::get_depth_range(float* lower_z_bound, float* upper_z_bound)
{
	CHECK_DEVICE_LIFETIME();

	curr_hw_viewport.get_depth_range(direct3d_device, lower_z_bound, upper_z_bound);

	return GR_OK;
}

GENRESULT NewRenderPipeline::set_window(HWND hwnd, int x, int y, int w, int h)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_set_window(this, hwnd, x, y, w, h);
	return result;
}

GENRESULT NewRenderPipeline::get_window(HWND* out_hwnd, int* out_x, int* out_y, int* out_w, int* out_h)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_get_window(this, out_hwnd, out_x, out_y, out_w, out_h);
	return result;
}

GENRESULT NewRenderPipeline::set_world(const Transform& world)
{
	CHECK_DEVICE_LIFETIME();

	D3DMATRIX W;
	Transform2D3D(W, world);
	curr_hw_world.set(direct3d_device, D3DTS_WORLD, &W);

	return GR_OK;
}

GENRESULT NewRenderPipeline::get_world(Transform& world)
{
	CHECK_DEVICE_LIFETIME();

	D3DMATRIX M;
	curr_hw_world.get(direct3d_device, D3DTS_WORLD, &M);
	D3D2Transform(M, world);

	return GR_OK;
}

GENRESULT NewRenderPipeline::set_view(const Transform& view)
{
	CHECK_DEVICE_LIFETIME();

	Transform T(false), M(false);
	T.set_identity();
	T.d[2][2] *= -1.0f;
	M = T * view;

	D3DMATRIX V;
	Transform2D3D(V, M);

	curr_hw_view.set(direct3d_device, D3DTS_VIEW, &V);

	return GR_OK;
}

GENRESULT NewRenderPipeline::get_view(Transform& view)
{
	CHECK_DEVICE_LIFETIME();


	D3DMATRIX M;
	curr_hw_view.get(direct3d_device, D3DTS_VIEW, &M);

	D3D2Transform(M, view);

	Transform T(false);
	T.set_identity();
	T.d[2][2] *= -1.0f;
	view = T * view;

	return GR_OK;
}

GENRESULT __stdcall NewRenderPipeline::set_modelview(const Transform& modelview)
{
	CHECK_DEVICE_LIFETIME();

	Transform T(false), M(false);
	T.set_identity();
	T.d[2][2] *= -1.0f;
	M = T * modelview;

	D3DMATRIX W;
	Transform2D3D(W, M);
	curr_hw_world.set(direct3d_device, D3DTS_WORLD, &W);

	return GR_OK;
}

GENRESULT NewRenderPipeline::get_modelview(Transform& modelview)
{
	CHECK_DEVICE_LIFETIME();

	D3DMATRIX M;
	Transform T(false);

	curr_hw_world.get(direct3d_device, D3DTS_WORLD, &M);

	D3D2Transform(M, modelview);

	T.set_identity();
	T.d[2][2] *= -1.0f;

	modelview = T * modelview;

	return GR_OK;
}

GENRESULT NewRenderPipeline::set_projection(const Matrix4& projection)
{
	CHECK_DEVICE_LIFETIME();

	D3DMATRIX P;
	Matrix2D3D(P, projection);
	curr_hw_projection.set(direct3d_device, D3DTS_PROJECTION, &P);
	return GR_OK;
}

GENRESULT NewRenderPipeline::get_projection(Matrix4& projection)
{
	CHECK_DEVICE_LIFETIME();

	D3DMATRIX P;

	curr_hw_projection.get(direct3d_device, D3DTS_PROJECTION, &P);

	D3D2Matrix(P, projection);

	return GR_OK;
}

GENRESULT NewRenderPipeline::set_lookat(float eyex, float eyey, float eyez, float centerx, float centery, float centerz, float upx, float upy, float upz)
{
	CHECK_DEVICE_LIFETIME();

	Vector target(centerx, centery, centerz);
	Vector pcam(eyex, eyey, eyez);
	Vector look = target - pcam;
	look.normalize();

	Vector k = -look; // We want the camera's -z axis parallel to look.
	Vector j(upx, upy, upz);

	Vector i = cross_product(j, k); // Should add some extra checks here in case j and k are parallel.
	i.normalize();
	j = cross_product(k, i);
	j.normalize();

	Matrix cam2world(i, j, k); // Now we have our basis vectors for the camera's orientation:
	Matrix world2cam = cam2world.get_transpose(); // Get world --> camera.
	Vector xlat = -(world2cam * pcam); // Express camera position in camera frame:
	Transform modelview(world2cam, xlat);

	Transform T(false), M(false);
	T.set_identity();
	T.d[2][2] *= -1.0f;
	M = T * modelview;

	D3DMATRIX W;
	Transform2D3D(W, M);

	curr_hw_world.set(direct3d_device, D3DTS_WORLD, &W);

	return GR_OK;
}

GENRESULT NewRenderPipeline::set_ortho(float left, float right, float bottom, float top, float nearval, float farval)
{
	CHECK_DEVICE_LIFETIME();

	D3DMATRIX P = {};
	P.m[0][0] = 2.0 / (right - left);
	P.m[1][1] = 2.0 / (top - bottom);
	P.m[2][2] = 2.0 / (farval - nearval);
	P.m[3][0] = -(right + left) / (right - left);
	P.m[3][1] = -(bottom + top) / (top - bottom);
	P.m[3][2] = -(nearval) / (farval - nearval);
	P.m[3][3] = 1.0;

	rp_rd_ortho(left, right, bottom, top, nearval, farval);

	curr_hw_projection.set(direct3d_device, D3DTS_PROJECTION, &P);

	return GR_OK;
}

GENRESULT NewRenderPipeline::set_perspective(float fovy, float aspect, float znear, float zfar)
{
	CHECK_DEVICE_LIFETIME();

	if (znear <= 0.0 || zfar <= 0.0 || zfar == znear)
	{
		GENERAL_TRACE_1("Direct3D_RenderPipeline: set_perspective: invalid parameters\n");
		return GR_GENERIC;
	}

	float cot_fovy = 1.0f / tan(Tdeg2rad(fovy));
	float inv_f_n = 1.0f / (zfar - znear);

	D3DMATRIX P = {};
	P.m[0][0] = cot_fovy / aspect;
	P.m[1][1] = cot_fovy;
	P.m[2][2] = zfar * inv_f_n;
	P.m[2][3] = 1.0f;
	P.m[3][2] = -zfar * znear * inv_f_n;

	rp_rd_perspective(fovy, aspect, znear, zfar);

	curr_hw_projection.set(direct3d_device, D3DTS_PROJECTION, &P);

	return GR_OK;
}

_extern U32 __thiscall sub_6D2C487(void* _this, IRP_LIGHTHANDLE, int a3);
GENRESULT NewRenderPipeline::set_light(IRP_LIGHTHANDLE handle, const D3DLIGHT8* light_values)
{
	// #TODO Remove me. But before you do... level with me... how much was the API designer smoking?
	ASSERT(handle == light_values);

	CHECK_DEVICE_LIFETIME();

	ASSERT(direct3d_device);	// assert after the create_buffers check

	U32 light_index = sub_6D2C487(&this->unknown21F4, handle, 0);
	if (FAILED(direct3d_device->SetLight(light_index, light_values)))
	{
		return GR_GENERIC;
	}

	//if (!disable_hw_caches) 
	//{
	//	lights[handle] = handle;
	//}

	rp_rd_light(handle, light_values);

	return GR_OK;
}

GENRESULT NewRenderPipeline::destroy_light(IRP_LIGHTHANDLE handle)
{
	GENRESULT result = DirectX8_destroy_light(this, handle);

	return result;

	//CHECK_DEVICE_LIFETIME();
	//sub_6D2CB6F(&this->unknown21F4, handle, 0);
	//sub_6D2D032((int*)&this->unknown21F4, this->direct3d_device);
	//sub_6D2C7CF(&this->unknown21F4, handle);
	//return GR_OK;
}

GENRESULT NewRenderPipeline::get_light(IRP_LIGHTHANDLE handle, D3DLIGHT8* out_light_values)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_get_light(this, handle, out_light_values);
	return result;
}

GENRESULT NewRenderPipeline::set_light_enable(IRP_LIGHTHANDLE handle, U32 enable)
{
	GENRESULT result = DirectX8_set_light_enable(this, handle, enable);
	return result;
}

GENRESULT NewRenderPipeline::get_light_enable(IRP_LIGHTHANDLE handle, U32* out_enable)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_get_light_enable(this, handle, out_enable);
	return result;
}

GENRESULT NewRenderPipeline::set_material(D3DMATERIAL8* material_values)
{
	CHECK_DEVICE_LIFETIME();

	if (FAILED(direct3d_device->SetMaterial(material_values)))
	{
		return GR_GENERIC;
	}

	rp_rd_material(material_values);

	return GR_OK;
}

GENRESULT NewRenderPipeline::get_material(D3DMATERIAL8* out_material_values)
{
	CHECK_DEVICE_LIFETIME();

	if (FAILED(direct3d_device->GetMaterial(out_material_values)))
	{
		return GR_GENERIC;
	}

	return GR_OK;
}

PFenum last_enum;
D3DFORMAT last_value;

_extern _naked void sub_6D0D628() // _sub_6D0D628
{
	__DEBUG_ASM(6D0D628);
	// chunk 0x6D0D628 _sub_6D0D628
	asm("loc_6D0D628: push %ebp;");
	asm("loc_6D0D629: mov %esp,%ebp;");
	asm("loc_6D0D62B: mov $0x2050,%eax;");
	asm("loc_6D0D630: call _sub_6D2F270;");
	asm("loc_6D0D635: mov 8(%ebp),%eax;");
	asm("loc_6D0D638: cmpl $0,0x130(%eax);");
	asm("loc_6D0D63F: jne loc_6D0D696;");
	asm("loc_6D0D641: mov $2,%ecx;");
	asm("loc_6D0D646: and $0xF,%ecx;");
	asm("loc_6D0D649: mov -0x34(%ebp),%edx;");
	asm("loc_6D0D64C: and $0xFFFFFFF0,%edx;");
	asm("loc_6D0D64F: or %ecx,%edx;");
	asm("loc_6D0D651: mov %edx,-0x34(%ebp);");
	asm("loc_6D0D654: mov $0x10000,%eax;");
	asm("loc_6D0D659: and $0xFFFFFFF,%eax;");
	asm("loc_6D0D65E: shl $4,%eax;");
	asm("loc_6D0D661: mov -0x34(%ebp),%ecx;");
	asm("loc_6D0D664: and $0xF,%ecx;");
	asm("loc_6D0D667: or %eax,%ecx;");
	asm("loc_6D0D669: mov %ecx,-0x34(%ebp);");
	asm("loc_6D0D66C: push $_data_6D674A0;");
	asm("loc_6D0D671: push $0xC1B;");
	asm("loc_6D0D676: push $_data_6D674D8;");
	asm("loc_6D0D67B: push $_data_6D67524;");
	asm("loc_6D0D680: mov -0x34(%ebp),%edx;");
	asm("loc_6D0D683: push %edx;");
	asm("loc_6D0D684: mov _import_6D5E018,%eax;");
	asm("loc_6D0D689: calll *(%eax);");
	asm("loc_6D0D68B: add $0x14,%esp;");
	asm("loc_6D0D68E: or $0xFFFFFFFF,%eax;");
	asm("loc_6D0D691: jmp loc_6D0D991;");
	asm("loc_6D0D696: mov 0x1C(%ebp),%ecx;");
	asm("loc_6D0D699: and $1,%ecx;");
	asm("loc_6D0D69C: neg %ecx;");
	asm("loc_6D0D69E: sbb %ecx,%ecx;");
	asm("loc_6D0D6A0: inc %ecx;");
	asm("loc_6D0D6A1: mov %cl,-0x1E(%ebp);");
	asm("loc_6D0D6A4: mov 0x1C(%ebp),%edx;");
	asm("loc_6D0D6A7: and $2,%edx;");
	asm("loc_6D0D6AA: neg %edx;");
	asm("loc_6D0D6AC: sbb %edx,%edx;");
	asm("loc_6D0D6AE: neg %edx;");
	asm("loc_6D0D6B0: mov %dl,-0x1D(%ebp);");
	asm("loc_6D0D6B3: movl $0,-0x14(%ebp);");
	asm("loc_6D0D6BA: push $0;");
	asm("loc_6D0D6BC: call _sub_6D5CC30;");
	asm("loc_6D0D6C1: add $4,%esp;");
	asm("loc_6D0D6C4: mov %eax,-0xC(%ebp);");
	asm("loc_6D0D6C7: movl $0,-0x1C(%ebp);");
	asm("loc_6D0D6CE: movl $3,-8(%ebp);");
	asm("loc_6D0D6D5: movzbl -0x1E(%ebp),%eax;");
	asm("loc_6D0D6D9: test %eax,%eax;");
	asm("loc_6D0D6DB: jne loc_6D0D6E7;");
	asm("loc_6D0D6DD: mov $0xFFFFFFFE,%eax;");
	asm("loc_6D0D6E2: jmp loc_6D0D991;");
	asm("loc_6D0D6E7: lea -4(%ebp),%ecx;");
	asm("loc_6D0D6EA: push %ecx;");
	asm("loc_6D0D6EB: push $5;");
	asm("loc_6D0D6ED: mov 8(%ebp),%edx;");
	asm("loc_6D0D6F0: mov (%edx),%eax;");
	asm("loc_6D0D6F2: mov 8(%ebp),%ecx;");
	asm("loc_6D0D6F5: push %ecx;");
	asm("loc_6D0D6F6: calll *0x20(%eax);");
	asm("loc_6D0D6F9: mov 0xC(%ebp),%edx;");
	asm("loc_6D0D6FC: cmp 0x10(%ebp),%edx;");
	asm("loc_6D0D6FF: je loc_6D0D71D;");
	asm("loc_6D0D701: cmpl $0,-4(%ebp);");
	asm("loc_6D0D705: je loc_6D0D71D;");
	asm("loc_6D0D707: mov 0xC(%ebp),%eax;");
	asm("loc_6D0D70A: cmp 0x10(%ebp),%eax;");
	asm("loc_6D0D70D: jle loc_6D0D717;");
	asm("loc_6D0D70F: mov 0xC(%ebp),%ecx;");
	asm("loc_6D0D712: mov %ecx,0x10(%ebp);");
	asm("loc_6D0D715: jmp loc_6D0D71D;");
	asm("loc_6D0D717: mov 0x10(%ebp),%edx;");
	asm("loc_6D0D71A: mov %edx,0xC(%ebp);");
	asm("loc_6D0D71D: mov -8(%ebp),%eax;");
	asm("loc_6D0D720: push %eax;");
	asm("loc_6D0D721: mov -0x1C(%ebp),%ecx;");
	asm("loc_6D0D724: push %ecx;");
	asm("loc_6D0D725: mov 0x14(%ebp),%edx;");
	asm("loc_6D0D728: push %edx;");
	asm("loc_6D0D729: mov 8(%ebp),%ecx;");
	asm("loc_6D0D72C: call _sub_6D0D444;");
	asm("loc_6D0D731: mov %al,-0xD(%ebp);");
	asm("loc_6D0D734: movzbl -0xD(%ebp),%eax;");
	asm("loc_6D0D738: test %eax,%eax;");
	asm("loc_6D0D73A: je loc_6D0D749;");
	asm("loc_6D0D73C: mov 0x14(%ebp),%ecx;");
	asm("loc_6D0D73F: mov (%ecx),%edx;");
	asm("loc_6D0D741: mov %edx,-0xC(%ebp);");
	asm("loc_6D0D744: jmp loc_6D0D7DC;");
	asm("loc_6D0D749: mov 0x14(%ebp),%eax;");
	asm("loc_6D0D74C: mov (%eax),%ecx;");
	asm("loc_6D0D74E: push %ecx;");
	asm("loc_6D0D74F: call _sub_6D5CC50;");
	asm("loc_6D0D754: add $4,%esp;");
	asm("loc_6D0D757: neg %eax;");
	asm("loc_6D0D759: sbb %eax,%eax;");
	asm("loc_6D0D75B: neg %eax;");
	asm("loc_6D0D75D: xor %edx,%edx;");
	asm("loc_6D0D75F: mov %al,%dl;");
	asm("loc_6D0D761: test %edx,%edx;");
	asm("loc_6D0D763: je loc_6D0D7DC;");
	asm("loc_6D0D765: mov 0x14(%ebp),%eax;");
	asm("loc_6D0D768: mov (%eax),%ecx;");
	asm("loc_6D0D76A: push %ecx;");
	asm("loc_6D0D76B: call _sub_6D5CC50;");
	asm("loc_6D0D770: add $4,%esp;");
	asm("loc_6D0D773: push %eax;");
	asm("loc_6D0D774: mov 8(%ebp),%ecx;");
	asm("loc_6D0D777: call _sub_6D04693;");
	asm("loc_6D0D77C: mov %eax,-0x28(%ebp);");
	asm("loc_6D0D77F: cmpl $0,-0x28(%ebp);");
	asm("loc_6D0D783: je loc_6D0D7DC;");
	asm("loc_6D0D785: movl $0,-0x2C(%ebp);");
	asm("loc_6D0D78C: jmp loc_6D0D797;");
	asm("loc_6D0D78E: mov -0x2C(%ebp),%edx;");
	asm("loc_6D0D791: add $1,%edx;");
	asm("loc_6D0D794: mov %edx,-0x2C(%ebp);");
	asm("loc_6D0D797: mov -0x28(%ebp),%eax;");
	asm("loc_6D0D79A: mov -0x2C(%ebp),%ecx;");
	asm("loc_6D0D79D: cmp 4(%eax),%ecx;");
	asm("loc_6D0D7A0: jae loc_6D0D7DC;");
	asm("loc_6D0D7A2: mov -0x28(%ebp),%edx;");
	asm("loc_6D0D7A5: mov 8(%edx),%eax;");
	asm("loc_6D0D7A8: mov -0x2C(%ebp),%ecx;");
	asm("loc_6D0D7AB: lea (%eax,%ecx,4),%edx;");
	asm("loc_6D0D7AE: mov %edx,-0x30(%ebp);");
	asm("loc_6D0D7B1: mov -8(%ebp),%eax;");
	asm("loc_6D0D7B4: push %eax;");
	asm("loc_6D0D7B5: mov -0x1C(%ebp),%ecx;");
	asm("loc_6D0D7B8: push %ecx;");
	asm("loc_6D0D7B9: mov -0x30(%ebp),%edx;");
	asm("loc_6D0D7BC: push %edx;");
	asm("loc_6D0D7BD: mov 8(%ebp),%ecx;");
	asm("loc_6D0D7C0: call _sub_6D0D444;");
	asm("loc_6D0D7C5: movzbl %al,%eax;");
	asm("loc_6D0D7C8: test %eax,%eax;");
	asm("loc_6D0D7CA: je loc_6D0D7DA;");
	asm("loc_6D0D7CC: movb $1,-0xD(%ebp);");
	asm("loc_6D0D7D0: mov -0x30(%ebp),%ecx;");
	asm("loc_6D0D7D3: mov (%ecx),%edx;");
	asm("loc_6D0D7D5: mov %edx,-0xC(%ebp);");
	asm("loc_6D0D7D8: jmp loc_6D0D7DC;");
	asm("loc_6D0D7DA: jmp loc_6D0D78E;");
	asm("loc_6D0D7DC: movzbl -0xD(%ebp),%eax;");
	asm("loc_6D0D7E0: test %eax,%eax;");
	asm("loc_6D0D7E2: jne loc_6D0D839;");
	asm("loc_6D0D7E4: mov $3,%ecx;");
	asm("loc_6D0D7E9: and $0xF,%ecx;");
	asm("loc_6D0D7EC: mov -0x38(%ebp),%edx;");
	asm("loc_6D0D7EF: and $0xFFFFFFF0,%edx;");
	asm("loc_6D0D7F2: or %ecx,%edx;");
	asm("loc_6D0D7F4: mov %edx,-0x38(%ebp);");
	asm("loc_6D0D7F7: mov $0x10000,%eax;");
	asm("loc_6D0D7FC: and $0xFFFFFFF,%eax;");
	asm("loc_6D0D801: shl $4,%eax;");
	asm("loc_6D0D804: mov -0x38(%ebp),%ecx;");
	asm("loc_6D0D807: and $0xF,%ecx;");
	asm("loc_6D0D80A: or %eax,%ecx;");
	asm("loc_6D0D80C: mov %ecx,-0x38(%ebp);");
	asm("loc_6D0D80F: push $_data_6D67540;");
	asm("loc_6D0D814: push $0xC67;");
	asm("loc_6D0D819: push $_data_6D67580;");
	asm("loc_6D0D81E: push $_data_6D675CC;");
	asm("loc_6D0D823: mov -0x38(%ebp),%edx;");
	asm("loc_6D0D826: push %edx;");
	asm("loc_6D0D827: mov _import_6D5E018,%eax;");
	asm("loc_6D0D82C: calll *(%eax);");
	asm("loc_6D0D82E: add $0x14,%esp;");
	asm("loc_6D0D831: or $0xFFFFFFFF,%eax;");
	asm("loc_6D0D834: jmp loc_6D0D991;");
	asm("loc_6D0D839: movl $0,-0x24(%ebp);");
	asm("loc_6D0D840: lea -0x24(%ebp),%ecx;");
	asm("loc_6D0D843: push %ecx;");
	asm("loc_6D0D844: push $1;");
	asm("loc_6D0D846: mov -0xC(%ebp),%edx;");
	asm("loc_6D0D849: push %edx;");

	__asm { mov last_enum, edx };

	asm("loc_6D0D84A: call _sub_6D5CBB0;");

	__asm { mov last_value, eax };

	asm("loc_6D0D84F: add $4,%esp;");
	asm("loc_6D0D852: push %eax;");
	asm("loc_6D0D853: mov -0x1C(%ebp),%eax;");
	asm("loc_6D0D856: push %eax;");
	asm("loc_6D0D857: mov 0x18(%ebp),%ecx;");
	asm("loc_6D0D85A: push %ecx;");
	asm("loc_6D0D85B: mov 0x10(%ebp),%edx;");
	asm("loc_6D0D85E: push %edx;");
	asm("loc_6D0D85F: mov 0xC(%ebp),%eax;");
	asm("loc_6D0D862: push %eax;");
	asm("loc_6D0D863: mov 8(%ebp),%ecx;");
	asm("loc_6D0D866: mov 0x130(%ecx),%edx;");
	asm("loc_6D0D86C: mov 8(%ebp),%eax;");
	asm("loc_6D0D86F: mov 0x130(%eax),%ecx;");
	asm("loc_6D0D875: mov (%edx),%edx;");
	asm("loc_6D0D877: push %ecx;");
	asm("loc_6D0D878: calll *0x50(%edx);");
	asm("loc_6D0D87B: mov %eax,-0x18(%ebp);");
	asm("loc_6D0D87E: cmpl $0,-0x18(%ebp);");
	asm("loc_6D0D882: jge loc_6D0D90F;");
	asm("loc_6D0D888: mov -0x18(%ebp),%eax;");
	asm("loc_6D0D88B: push %eax;");
	asm("loc_6D0D88C: call _sub_6D2B964;");
	asm("loc_6D0D891: add $4,%esp;");
	asm("loc_6D0D894: push %eax;");
	asm("loc_6D0D895: push $_data_6D675E8;");
	asm("loc_6D0D89A: lea -0x2038(%ebp),%ecx;");
	asm("loc_6D0D8A0: push %ecx;");
	asm("loc_6D0D8A1: call _sub_6D168F0;");
	asm("loc_6D0D8A6: add $0xC,%esp;");
	asm("loc_6D0D8A9: mov %eax,-0x2040(%ebp);");
	asm("loc_6D0D8AF: mov $2,%edx;");
	asm("loc_6D0D8B4: and $0xF,%edx;");
	asm("loc_6D0D8B7: mov -0x203C(%ebp),%eax;");
	asm("loc_6D0D8BD: and $0xFFFFFFF0,%eax;");
	asm("loc_6D0D8C0: or %edx,%eax;");
	asm("loc_6D0D8C2: mov %eax,-0x203C(%ebp);");
	asm("loc_6D0D8C8: mov $0x10000,%ecx;");
	asm("loc_6D0D8CD: and $0xFFFFFFF,%ecx;");
	asm("loc_6D0D8D3: shl $4,%ecx;");
	asm("loc_6D0D8D6: mov -0x203C(%ebp),%edx;");
	asm("loc_6D0D8DC: and $0xF,%edx;");
	asm("loc_6D0D8DF: or %ecx,%edx;");
	asm("loc_6D0D8E1: mov %edx,-0x203C(%ebp);");
	asm("loc_6D0D8E7: mov -0x2040(%ebp),%eax;");
	asm("loc_6D0D8ED: push %eax;");
	asm("loc_6D0D8EE: push $0xC87;");
	asm("loc_6D0D8F3: push $_data_6D67600;");
	asm("loc_6D0D8F8: push $_data_6D6764C;");
	asm("loc_6D0D8FD: mov -0x203C(%ebp),%ecx;");
	asm("loc_6D0D903: push %ecx;");
	asm("loc_6D0D904: mov _import_6D5E018,%edx;");
	asm("loc_6D0D90A: calll *(%edx);");
	asm("loc_6D0D90C: add $0x14,%esp;");
	asm("loc_6D0D90F: mov -0x24(%ebp),%eax;");
	asm("loc_6D0D912: mov %eax,-0x204C(%ebp);");
	asm("loc_6D0D918: movl $0,-0x2044(%ebp);");
	asm("loc_6D0D922: cmpl $0,-0x204C(%ebp);");
	asm("loc_6D0D929: je loc_6D0D983;");
	asm("loc_6D0D92B: push $8;");
	asm("loc_6D0D92D: call _sub_6D2F2A0;");
	asm("loc_6D0D932: add $4,%esp;");
	asm("loc_6D0D935: mov %eax,-0x2048(%ebp);");
	asm("loc_6D0D93B: cmpl $0,-0x2048(%ebp);");
	asm("loc_6D0D942: je loc_6D0D96D;");
	asm("loc_6D0D944: mov -0x2048(%ebp),%ecx;");
	asm("loc_6D0D94A: mov -0x204C(%ebp),%edx;");
	asm("loc_6D0D950: mov %edx,(%ecx);");
	asm("loc_6D0D952: mov -0x2048(%ebp),%eax;");
	asm("loc_6D0D958: movl $0,4(%eax);");
	asm("loc_6D0D95F: mov -0x2048(%ebp),%ecx;");
	asm("loc_6D0D965: mov %ecx,-0x2050(%ebp);");
	asm("loc_6D0D96B: jmp loc_6D0D977;");
	asm("loc_6D0D96D: movl $0,-0x2050(%ebp);");
	asm("loc_6D0D977: mov -0x2050(%ebp),%edx;");
	asm("loc_6D0D97D: mov %edx,-0x2044(%ebp);");
	asm("loc_6D0D983: mov 0x20(%ebp),%eax;");
	asm("loc_6D0D986: mov -0x2044(%ebp),%ecx;");
	asm("loc_6D0D98C: mov %ecx,(%eax);");
	asm("loc_6D0D98E: mov -0x18(%ebp),%eax;");
	asm("loc_6D0D991: mov %ebp,%esp;");
	asm("loc_6D0D993: pop %ebp;");
	asm("loc_6D0D994: ret $0x1C;");
	asm("int3;"); // unreachable
}

GENRESULT NewRenderPipeline::create_texture(int width, int height, const PFenum* desiredformat, int num_lod, U32 irp_ctf_flags, IRP_TEXTUREHANDLE* out_htexture)
{
	CHECK_DEVICE_LIFETIME();

	D3DFORMAT format = pf_to_d3d(*desiredformat);

	switch ((U32)format)
	{
	case PF_4CC_DAOP:
		format = D3DFMT_R5G6B5;
		break;
	case PF_4CC_DAOT:
		format = D3DFMT_X8R8G8B8;
		break;
	case PF_4CC_DAA1:
		format = D3DFMT_A1R5G5B5;
		break;
	case PF_4CC_DAA4:
		format = D3DFMT_A4R4G4B4;
		break;
	case PF_4CC_DAA8:
		format = D3DFMT_A8R8G8B8;
		break;
	case PF_4CC_DAAA:
	case PF_4CC_DAAL:
		// unused and unsupported
		return GR_GENERIC;
	}

	IDirect3DTexture8* direct3d_texture;
	HRESULT hr;
	if (FAILED(hr = direct3d_device->CreateTexture(
		width,
		height,
		num_lod,
		0,
		format,
		D3DPOOL_MANAGED,
		&direct3d_texture)))
	{
		GENERAL_ERROR(TEMPSTR("create_texture: %s", HRESULT_GET_ERROR_STRING(hr)));
		return GR_GENERIC;
	}

	*out_htexture = new RPTEXTUREHANDLE
	{
		.direct3d_texture = direct3d_texture
	};

	return GR_OK;
}

GENRESULT NewRenderPipeline::destroy_texture(IRP_TEXTUREHANDLE htexture)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = GR_OK;
	if (SUCCEEDED(is_texture(htexture)))
	{
		for (U32 stage_index = 0; stage_index < _countof(curr_hw_texture); stage_index++)
		{
			CACHED_TEXTURE& cached_texture = curr_hw_texture[stage_index];
			if (cached_texture.value == htexture)
			{
				cached_texture.set(direct3d_device, stage_index, NULL);
			}
		}

		RPTEXTUREHANDLE* texture = htexture;

		U32 refcount = texture->direct3d_texture->Release();
		if (refcount > 0)
		{
			GENERAL_WARNING(TEMPSTR("direct3d_texture released with %u references", refcount));
		}
		texture->direct3d_texture = 0;
		delete texture;
	}
	else
	{
		result = GR_INVALID_PARMS;
	}

	return result;
}

GENRESULT NewRenderPipeline::is_texture(IRP_TEXTUREHANDLE htexture)
{
	CHECK_DEVICE_LIFETIME();

	if (htexture && htexture->direct3d_texture)
	{
		return GR_OK;
	}
	else
	{
		return GR_GENERIC;
	}
}

static GENRESULT get_texture_surface(IRP_TEXTUREHANDLE htexture, U32 subsurface, IDirect3DSurface8** out_direct3d_texture_surface)
{
	GENRESULT result = GR_GENERIC;
	HRESULT hr = E_FAIL;

	IDirect3DSurface8* direct3d_texture_surface = nullptr;
	RPTEXTUREHANDLE* texture = htexture;
	IDirect3DBaseTexture8* direct3d_basetexture = texture->direct3d_texture;
	D3DRESOURCETYPE resource_type = direct3d_basetexture->GetType();

	switch (resource_type)
	{
	case D3DRTYPE_TEXTURE:
	{
		ASSERT(texture->unknown4 == 0);

		IDirect3DTexture8* direct3d_2d_texture;
		if (SUCCEEDED(hr = direct3d_basetexture->QueryInterface(
			IID_IDirect3DTexture8,
			reinterpret_cast<void**>(&direct3d_2d_texture))))
		{
			if (FAILED(hr = direct3d_2d_texture->GetSurfaceLevel(subsurface, &direct3d_texture_surface)))
			{
				GENERAL_ERROR(TEMPSTR("%s GetSurfaceLevel failed %s", __FUNCTION__, HRESULT_GET_ERROR_STRING(hr)));
				result = GR_GENERIC;
			}
			else
			{
				result = GR_OK;
			}
			direct3d_2d_texture->Release();
		}
		else
		{
			GENERAL_ERROR(TEMPSTR("%s QueryInterface for IDirect3DTexture8 failed %s", __FUNCTION__, HRESULT_GET_ERROR_STRING(hr)));
			result = GR_GENERIC;
		}
	}
	break;
	case D3DRTYPE_CUBETEXTURE:
	{
		ASSERT(texture->unknown4 == 1);

		IDirect3DCubeTexture8* direct3d_cube_texture;
		if (SUCCEEDED(hr = direct3d_basetexture->QueryInterface(
			IID_IDirect3DCubeTexture8,
			reinterpret_cast<void**>(&direct3d_cube_texture))))
		{
			D3DCUBEMAP_FACES face = static_cast<D3DCUBEMAP_FACES>(subsurface);
			if (FAILED(hr = direct3d_cube_texture->GetCubeMapSurface(face, 0, &direct3d_texture_surface)))
			{
				GENERAL_ERROR(TEMPSTR("%s GetCubeMapSurface failed %s", __FUNCTION__, HRESULT_GET_ERROR_STRING(hr)));
				result = GR_GENERIC;
			}
			else
			{
				result = GR_OK;
			}
			direct3d_cube_texture->Release();
		}
		else
		{
			GENERAL_ERROR(TEMPSTR("%s QueryInterface for IDirect3DCubeTexture8 failed %s", __FUNCTION__, HRESULT_GET_ERROR_STRING(hr)));
			result = GR_GENERIC;
		}
	}
	break;
	default:
	{
		GENERAL_FATAL(TEMPSTR("%s unsupported resource type %u", __FUNCTION__, static_cast<U32>(resource_type)));
		result = GR_NOT_IMPLEMENTED;
	}
	break;
	}

	if (SUCCEEDED(result))
	{
		*out_direct3d_texture_surface = direct3d_texture_surface;
	}
	else
	{
		GENERAL_ERROR(TEMPSTR("%s failed to get surface", __FUNCTION__));
		if (direct3d_texture_surface != nullptr)
		{
			direct3d_texture_surface->Release();
		}
	}

	return result;
}

GENRESULT NewRenderPipeline::lock_texture(IRP_TEXTUREHANDLE htexture, U32 subsurface, RPLOCKDATA* lockData)
{
	GENRESULT result = GR_OK;
	HRESULT hr = E_FAIL;
	if (FAILED(is_texture(htexture)))
	{
		GENERAL_ERROR(TEMPSTR("%s invalid texture handle", __FUNCTION__));
		result = GR_INVALID_PARMS;
	}
	else
	{
		IDirect3DSurface8* direct3d_texture_surface;
		if (SUCCEEDED(result = get_texture_surface(htexture, subsurface, &direct3d_texture_surface)))
		{
			D3DSURFACE_DESC desc;
			if (SUCCEEDED(hr = direct3d_texture_surface->GetDesc(&desc)))
			{
				D3DLOCKED_RECT rect;
				DWORD flags = 0;
				if (unknown128 & 2)
				{
					flags |= D3DLOCK_NOSYSLOCK;
				}
				if (SUCCEEDED(hr = direct3d_texture_surface->LockRect(&rect, NULL, flags)))
				{
					lockData->pf = d3d_to_pf(desc.Format);
					lockData->width = desc.Width;
					lockData->height = desc.Height;
					lockData->pitch = rect.Pitch;
					lockData->pixels = rect.pBits;

					result = GR_OK;
				}
				else
				{
					GENERAL_ERROR(TEMPSTR("%s LockRect failed %s", __FUNCTION__, HRESULT_GET_ERROR_STRING(hr)));
					result = GR_GENERIC;
				}
			}
			else
			{
				GENERAL_ERROR(TEMPSTR("%s GetDesc failed %s", __FUNCTION__, HRESULT_GET_ERROR_STRING(hr)));
				result = GR_GENERIC;
			}
			direct3d_texture_surface->Release();
		}
	}

	return result;
}

GENRESULT NewRenderPipeline::unlock_texture(IRP_TEXTUREHANDLE htexture, U32 subsurface)
{
	GENRESULT result = GR_OK;
	HRESULT hr = E_FAIL;
	if (FAILED(is_texture(htexture)))
	{
		GENERAL_ERROR(TEMPSTR("%s invalid texture handle", __FUNCTION__));
		result = GR_INVALID_PARMS;
	}
	else
	{
		IDirect3DSurface8* direct3d_texture_surface;
		if (SUCCEEDED(result = get_texture_surface(htexture, subsurface, &direct3d_texture_surface)))
		{
			if (FAILED(hr = direct3d_texture_surface->UnlockRect()))
			{
				GENERAL_ERROR(TEMPSTR("%s UnlockRect failed %s", __FUNCTION__, HRESULT_GET_ERROR_STRING(hr)));
				result = GR_GENERIC;
			}
		}
	}

	return result;
}

GENRESULT NewRenderPipeline::get_texture_format(IRP_TEXTUREHANDLE htexture, PFenum* out_pf)
{
	GENRESULT result = GR_OK;
	HRESULT hr = E_FAIL;
	if (FAILED(is_texture(htexture)))
	{
		GENERAL_ERROR(TEMPSTR("%s invalid texture handle", __FUNCTION__));
		result = GR_INVALID_PARMS;
	}
	else
	{
		IDirect3DSurface8* direct3d_texture_surface;
		if (SUCCEEDED(result = get_texture_surface(htexture, 0, &direct3d_texture_surface)))
		{
			ASSERT(direct3d_texture_surface != nullptr);

			D3DSURFACE_DESC desc;
			if (SUCCEEDED(hr = direct3d_texture_surface->GetDesc(&desc)))
			{
				*out_pf = d3d_to_pf(desc.Format);
				result = GR_OK;
			}
			else
			{
				GENERAL_ERROR(TEMPSTR("%s GetDesc failed %s", __FUNCTION__, HRESULT_GET_ERROR_STRING(hr)));
				result = GR_GENERIC;
			}
			direct3d_texture_surface->Release();
		}
	}

	return result;
}

GENRESULT NewRenderPipeline::get_texture_dim(IRP_TEXTUREHANDLE htexture, U32* out_width, U32* out_height, U32* out_num_lod)
{
	GENRESULT result = GR_OK;
	HRESULT hr = E_FAIL;
	if (FAILED(is_texture(htexture)))
	{
		GENERAL_ERROR(TEMPSTR("%s invalid texture handle", __FUNCTION__));
		result = GR_INVALID_PARMS;
	}
	else
	{
		RPTEXTUREHANDLE* texture = htexture;
		IDirect3DBaseTexture8* direct3d_basetexture = texture->direct3d_texture;
		D3DRESOURCETYPE resource_type = direct3d_basetexture->GetType();

		switch (resource_type)
		{
		case D3DRTYPE_TEXTURE:
		{
			ASSERT(texture->unknown4 == 0);

			IDirect3DTexture8* direct3d_2d_texture;
			if (SUCCEEDED(hr = direct3d_basetexture->QueryInterface(
				IID_IDirect3DTexture8,
				reinterpret_cast<void**>(&direct3d_2d_texture))))
			{
				D3DSURFACE_DESC desc;
				if (SUCCEEDED(direct3d_2d_texture->GetLevelDesc(0, &desc)))
				{
					*out_width = desc.Width;
					*out_height = desc.Height;
					*out_num_lod = direct3d_2d_texture->GetLevelCount();
				}
				else
				{
					GENERAL_ERROR(TEMPSTR("%s GetLevelDesc failed %s", __FUNCTION__, HRESULT_GET_ERROR_STRING(hr)));
					result = GR_GENERIC;
				}
				direct3d_2d_texture->Release();
			}
			else
			{
				GENERAL_ERROR(TEMPSTR("%s QueryInterface for IDirect3DTexture8 failed %s", __FUNCTION__, HRESULT_GET_ERROR_STRING(hr)));
				result = GR_GENERIC;
			}
		}
		break;
		case D3DRTYPE_CUBETEXTURE:
		{
			ASSERT(texture->unknown4 == 1);

			IDirect3DCubeTexture8* direct3d_cube_texture;
			if (SUCCEEDED(hr = direct3d_basetexture->QueryInterface(
				IID_IDirect3DCubeTexture8,
				reinterpret_cast<void**>(&direct3d_cube_texture))))
			{
				D3DSURFACE_DESC desc;
				if (SUCCEEDED(direct3d_cube_texture->GetLevelDesc(0, &desc)))
				{
					*out_width = desc.Width;
					*out_height = desc.Height;
					*out_num_lod = 6;
				}
				else
				{
					GENERAL_ERROR(TEMPSTR("%s GetLevelDesc failed %s", __FUNCTION__, HRESULT_GET_ERROR_STRING(hr)));
					result = GR_GENERIC;
				}
				direct3d_cube_texture->Release();
			}
			else
			{
				GENERAL_ERROR(TEMPSTR("%s QueryInterface for IDirect3DCubeTexture8 failed %s", __FUNCTION__, HRESULT_GET_ERROR_STRING(hr)));
				result = GR_GENERIC;
			}
		}
		break;
		default:
		{
			GENERAL_FATAL(TEMPSTR("%s unsupported resource type %u", __FUNCTION__, static_cast<U32>(resource_type)));
			result = GR_NOT_IMPLEMENTED;
		}
		break;
		}
	}

	return result;
}

GENRESULT NewRenderPipeline::get_texture_interface(IRP_TEXTUREHANDLE htexture, const char* iid, void** out_iif)
{
	CHECK_DEVICE_LIFETIME();
	return GR_NOT_IMPLEMENTED;
}

GENRESULT NewRenderPipeline::set_texture_level_data(IRP_TEXTUREHANDLE htexture, U32 subsurface, int src_width, int src_height, int src_stride, const PFenum* src_format, const void* src_pixel, const void* src_alpha, const RGB* src_palette)
{
	GENRESULT result = DirectX8_set_texture_level_data(this, htexture, subsurface, src_width, src_height, src_stride, src_format, src_pixel, src_alpha, src_palette);
	return result;
}

GENRESULT NewRenderPipeline::blit_texture(IRP_TEXTUREHANDLE hDest, U32 destLevel, RECT destRect, IRP_TEXTUREHANDLE hSrc, U32 srcLevel, RECT srcRect)
{
	CHECK_DEVICE_LIFETIME();
	return GR_NOT_IMPLEMENTED;
}

GENRESULT NewRenderPipeline::set_render_target(UNKNOWN a2, UNKNOWN a3, UNKNOWN a4)
{
	CHECK_DEVICE_LIFETIME();
	return GR_NOT_IMPLEMENTED;
}

GENRESULT NewRenderPipeline::get_render_target(void* a2)
{
	CHECK_DEVICE_LIFETIME();
	return GR_NOT_IMPLEMENTED;
}

GENRESULT NewRenderPipeline::begin_scene(void)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = GR_OK;
	HRESULT hr;
	if (FAILED(hr = direct3d_device->BeginScene()))
	{
		GENERAL_ERROR(TEMPSTR("begin_scene: %s", HRESULT_GET_ERROR_STRING(hr)));
		result = GR_GENERIC;
	}
	return result;
}

GENRESULT NewRenderPipeline::end_scene(void)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = GR_OK;
	HRESULT hr;
	if (FAILED(hr = direct3d_device->EndScene()))
	{
		GENERAL_ERROR(TEMPSTR("end_scene: %s", HRESULT_GET_ERROR_STRING(hr)));
		result = GR_GENERIC;
	}
	return result;
}

GENRESULT NewRenderPipeline::reset_render_states_to_defaults(void)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_reset_render_states_to_defaults(this);
	return result;
}

GENRESULT NewRenderPipeline::set_render_state(D3DRENDERSTATETYPE state, U32 value)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = GR_OK;
	HRESULT hr;
	if (FAILED(hr = direct3d_device->SetRenderState(state, value)))
	{
		GENERAL_ERROR(TEMPSTR("SetRenderState: %s", HRESULT_GET_ERROR_STRING(hr)));
		result = GR_GENERIC;
	}
	return result;
}

GENRESULT NewRenderPipeline::get_render_state(D3DRENDERSTATETYPE state, U32* value)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = GR_OK;
	HRESULT hr;
	if (FAILED(hr = direct3d_device->GetRenderState(state, reinterpret_cast<DWORD*>(value))))
	{
		GENERAL_ERROR(TEMPSTR("SetRenderState: %s", HRESULT_GET_ERROR_STRING(hr)));
		result = GR_GENERIC;
	}
	return result;
}

GENRESULT NewRenderPipeline::set_texture_stage_state(U32 stage, D3DTEXTURESTAGESTATETYPE state, U32 value)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = GR_OK;
	HRESULT hr;
	if (FAILED(hr = direct3d_device->SetTextureStageState(stage, state, value)))
	{
		GENERAL_ERROR(TEMPSTR("SetRenderState: %s", HRESULT_GET_ERROR_STRING(hr)));
		result = GR_GENERIC;
	}
	return result;
}

GENRESULT NewRenderPipeline::get_texture_stage_state(U32 stage, D3DTEXTURESTAGESTATETYPE state, U32* value)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = GR_OK;
	HRESULT hr;
	if (FAILED(hr = direct3d_device->GetTextureStageState(stage, state, reinterpret_cast<DWORD*>(value))))
	{
		GENERAL_ERROR(TEMPSTR("SetRenderState: %s", HRESULT_GET_ERROR_STRING(hr)));
		result = GR_GENERIC;
	}
	return result;
}

GENRESULT NewRenderPipeline::set_texture_stage_transform(U32 stage, Matrix4 const& mat4)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = GR_OK;

	D3DMATRIX M;
	Matrix2D3D(M, mat4);

	HRESULT hr;
	D3DTRANSFORMSTATETYPE state = static_cast<D3DTRANSFORMSTATETYPE>(stage + 16);
	if (FAILED(hr = direct3d_device->SetTransform(state, &M)))
	{
		GENERAL_ERROR(TEMPSTR("SetRenderState: %s", HRESULT_GET_ERROR_STRING(hr)));
		result = GR_GENERIC;
	}
	return result;
}

GENRESULT NewRenderPipeline::get_texture_stage_transform(U32 stage, Matrix4& out_mat4)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = GR_OK;

	D3DMATRIX M;

	HRESULT hr;
	D3DTRANSFORMSTATETYPE state = static_cast<D3DTRANSFORMSTATETYPE>(stage);
	if (FAILED(hr = direct3d_device->GetTransform(state, &M)))
	{
		GENERAL_ERROR(TEMPSTR("SetRenderState: %s", HRESULT_GET_ERROR_STRING(hr)));
		result = GR_GENERIC;
	}

	D3D2Matrix(M, out_mat4);

	return result;
}

GENRESULT NewRenderPipeline::set_texture_stage_texture(U32 stage, IRP_TEXTUREHANDLE htexture)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = curr_hw_texture->set(direct3d_device, stage, htexture);
	return result;
}

GENRESULT NewRenderPipeline::get_texture_stage_texture(U32 stage, IRP_TEXTUREHANDLE* out_htexture)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = curr_hw_texture->get(direct3d_device, stage, out_htexture);
	return result;
}

GENRESULT NewRenderPipeline::verify_state(void)
{
	return GR_OK;
}

_extern _naked int __thiscall sub_6D04138_OLD(NewRenderPipeline* _this)
{
	__DEBUG_ASM(6D04138);
	// chunk 0x6D04138 _sub_6D04138
	asm("loc_6D04138: push %ebp;");
	asm("loc_6D04139: mov %esp,%ebp;");
	asm("loc_6D0413B: mov $0x6074,%eax;");
	asm("loc_6D04140: call _sub_6D2F270;");
	asm("loc_6D04145: push %esi;");
	asm("loc_6D04146: push %edi;");
	asm("loc_6D04147: mov %ecx,-0x6070(%ebp);");
	asm("loc_6D0414D: mov -0x6070(%ebp),%eax;");
	asm("loc_6D04153: mov 0x130(%eax),%ecx;");
	asm("loc_6D04159: push %ecx;");
	asm("loc_6D0415A: mov -0x6070(%ebp),%ecx;");
	asm("loc_6D04160: add $0x44C,%ecx;");
	asm("loc_6D04166: call _sub_6D2BBBA;");
	asm("loc_6D0416B: mov -0x6070(%ebp),%edx;");
	asm("loc_6D04171: mov 0x130(%edx),%eax;");
	asm("loc_6D04177: mov %eax,-0x603C(%ebp);");
	asm("loc_6D0417D: mov -0x6070(%ebp),%ecx;");
	asm("loc_6D04183: add $0x138,%ecx;");
	asm("loc_6D04189: mov %ecx,-0x6038(%ebp);");
	asm("loc_6D0418F: movl $0,-8(%ebp);");
	/*asm("loc_6D04196: mov -0x6038(%ebp),%edx;");
	asm("loc_6D0419C: mov 0x10(%edx),%al;");
	asm("loc_6D0419F: shr $1,%al;");
	asm("loc_6D041A1: and $1,%al;");
	asm("loc_6D041A3: xor %ecx,%ecx;");
	asm("loc_6D041A5: mov %al,%cl;");
	asm("loc_6D041A7: test %ecx,%ecx;");
	asm("loc_6D041A9: je loc_6D042AE;");
	asm("loc_6D041AF: mov -0x6038(%ebp),%edx;");
	asm("loc_6D041B5: mov 0x10(%edx),%al;");
	asm("loc_6D041B8: and $0xFD,%al;");
	asm("loc_6D041BA: mov -0x6038(%ebp),%ecx;");
	asm("loc_6D041C0: mov %al,0x10(%ecx);");
	asm("loc_6D041C3: mov -0x6038(%ebp),%edx;");
	asm("loc_6D041C9: cmpl $0,8(%edx);");
	asm("loc_6D041CD: je loc_6D041E3;");
	asm("loc_6D041CF: mov -0x6038(%ebp),%eax;");
	asm("loc_6D041D5: mov 8(%eax),%ecx;");
	asm("loc_6D041D8: mov 4(%ecx),%edx;");
	asm("loc_6D041DB: mov %edx,-0x6074(%ebp);");
	asm("loc_6D041E1: jmp loc_6D041ED;");
	asm("loc_6D041E3: movl $0,-0x6074(%ebp);");
	asm("loc_6D041ED: mov -0x6074(%ebp),%eax;");
	asm("loc_6D041F3: mov %eax,-0xC(%ebp);");
	asm("loc_6D041F6: mov -0x6038(%ebp),%ecx;");
	asm("loc_6D041FC: mov 0xC(%ecx),%edx;");
	asm("loc_6D041FF: push %edx;");
	asm("loc_6D04200: mov -0xC(%ebp),%eax;");
	asm("loc_6D04203: push %eax;");
	asm("loc_6D04204: mov -0x603C(%ebp),%ecx;");
	asm("loc_6D0420A: mov (%ecx),%edx;");
	asm("loc_6D0420C: mov -0x603C(%ebp),%eax;");
	asm("loc_6D04212: push %eax;");
	asm("loc_6D04213: calll *0x154(%edx);");
	asm("loc_6D04219: mov %eax,-8(%ebp);");
	asm("loc_6D0421C: cmpl $0,-8(%ebp);");
	asm("loc_6D04220: jge loc_6D042AE;");
	asm("loc_6D04226: mov -8(%ebp),%ecx;");
	asm("loc_6D04229: push %ecx;");
	asm("loc_6D0422A: call _sub_6D2B964;");
	asm("loc_6D0422F: add $4,%esp;");
	asm("loc_6D04232: push %eax;");
	asm("loc_6D04233: push $_data_6D6AF04;");
	asm("loc_6D04238: lea -0x2018(%ebp),%edx;");
	asm("loc_6D0423E: push %edx;");
	asm("loc_6D0423F: call _sub_6D168F0;");
	asm("loc_6D04244: add $0xC,%esp;");
	asm("loc_6D04247: mov %eax,-0x6028(%ebp);");
	asm("loc_6D0424D: mov $2,%eax;");
	asm("loc_6D04252: and $0xF,%eax;");
	asm("loc_6D04255: mov -0x201C(%ebp),%ecx;");
	asm("loc_6D0425B: and $0xFFFFFFF0,%ecx;");
	asm("loc_6D0425E: or %eax,%ecx;");
	asm("loc_6D04260: mov %ecx,-0x201C(%ebp);");
	asm("loc_6D04266: mov $0x10000,%edx;");
	asm("loc_6D0426B: and $0xFFFFFFF,%edx;");
	asm("loc_6D04271: shl $4,%edx;");
	asm("loc_6D04274: mov -0x201C(%ebp),%eax;");
	asm("loc_6D0427A: and $0xF,%eax;");
	asm("loc_6D0427D: or %edx,%eax;");
	asm("loc_6D0427F: mov %eax,-0x201C(%ebp);");
	asm("loc_6D04285: mov -0x6028(%ebp),%ecx;");
	asm("loc_6D0428B: push %ecx;");
	asm("loc_6D0428C: push $0x34;");
	asm("loc_6D0428E: push $_data_6D6AEB0;");
	asm("loc_6D04293: push $_data_6D6ADE0;");
	asm("loc_6D04298: mov -0x201C(%ebp),%edx;");
	asm("loc_6D0429E: push %edx;");
	asm("loc_6D0429F: mov _import_6D5E018,%eax;");
	asm("loc_6D042A4: calll *(%eax);");
	asm("loc_6D042A6: add $0x14,%esp;");
	asm("loc_6D042A9: jmp loc_6D044C3;");*/
	asm("loc_6D042AE: mov -0x6038(%ebp),%ecx;");
	asm("loc_6D042B4: mov 0x10(%ecx),%dl;");
	asm("loc_6D042B7: and $1,%dl;");
	asm("loc_6D042BA: xor %eax,%eax;");
	asm("loc_6D042BC: mov %dl,%al;");
	asm("loc_6D042BE: test %eax,%eax;");
	asm("loc_6D042C0: je loc_6D044C3;");
	asm("loc_6D042C6: mov -0x6038(%ebp),%ecx;");
	asm("loc_6D042CC: mov 0x10(%ecx),%dl;");
	asm("loc_6D042CF: and $0xFE,%dl;");
	asm("loc_6D042D2: mov -0x6038(%ebp),%eax;");
	asm("loc_6D042D8: mov %dl,0x10(%eax);");
	asm("loc_6D042DB: mov -0x6038(%ebp),%ecx;");
	asm("loc_6D042E1: cmpl $0,4(%ecx);");
	asm("loc_6D042E5: jne loc_6D042FB;");
	asm("loc_6D042E7: movl $0,-0x14(%ebp);");
	asm("loc_6D042EE: mov -0x6038(%ebp),%edx;");
	asm("loc_6D042F4: mov (%edx),%eax;");
	asm("loc_6D042F6: mov %eax,-0x18(%ebp);");
	asm("loc_6D042F9: jmp loc_6D04331;");
	asm("loc_6D042FB: mov -0x6038(%ebp),%ecx;");
	asm("loc_6D04301: mov 4(%ecx),%edx;");
	asm("loc_6D04304: mov 0x20(%edx),%eax;");
	asm("loc_6D04307: mov %eax,-0x14(%ebp);");
	asm("loc_6D0430A: mov -0x6038(%ebp),%ecx;");
	asm("loc_6D04310: cmpl $0,(%ecx);");
	asm("loc_6D04313: je loc_6D04322;");
	asm("loc_6D04315: mov -0x6038(%ebp),%edx;");
	asm("loc_6D0431B: mov (%edx),%eax;");
	asm("loc_6D0431D: mov %eax,-0x18(%ebp);");
	asm("loc_6D04320: jmp loc_6D04331;");
	asm("loc_6D04322: mov -0x6038(%ebp),%ecx;");
	asm("loc_6D04328: mov 4(%ecx),%edx;");
	asm("loc_6D0432B: mov 4(%edx),%eax;");
	asm("loc_6D0432E: mov %eax,-0x18(%ebp);");
	asm("loc_6D04331: cmpl $0,-0x18(%ebp);");
	asm("loc_6D04335: je loc_6D043E9;");
	asm("loc_6D0433B: mov -0x18(%ebp),%ecx;");
	asm("loc_6D0433E: push %ecx;");
	asm("loc_6D0433F: mov -0x603C(%ebp),%edx;");
	asm("loc_6D04345: mov (%edx),%eax;");
	asm("loc_6D04347: mov -0x603C(%ebp),%ecx;");
	asm("loc_6D0434D: push %ecx;");
	asm("loc_6D0434E: calll *0x130(%eax);");
	asm("loc_6D04354: mov %eax,-8(%ebp);");
	asm("loc_6D04357: cmpl $0,-8(%ebp);");
	asm("loc_6D0435B: jge loc_6D043E9;");
	asm("loc_6D04361: mov -8(%ebp),%edx;");
	asm("loc_6D04364: push %edx;");
	asm("loc_6D04365: call _sub_6D2B964;");
	asm("loc_6D0436A: add $4,%esp;");
	asm("loc_6D0436D: push %eax;");
	asm("loc_6D0436E: push $_data_6D6AE94;");
	asm("loc_6D04373: lea -0x401C(%ebp),%eax;");
	asm("loc_6D04379: push %eax;");
	asm("loc_6D0437A: call _sub_6D168F0;");
	asm("loc_6D0437F: add $0xC,%esp;");
	asm("loc_6D04382: mov %eax,-0x602C(%ebp);");
	asm("loc_6D04388: mov $2,%ecx;");
	asm("loc_6D0438D: and $0xF,%ecx;");
	asm("loc_6D04390: mov -0x4020(%ebp),%edx;");
	asm("loc_6D04396: and $0xFFFFFFF0,%edx;");
	asm("loc_6D04399: or %ecx,%edx;");
	asm("loc_6D0439B: mov %edx,-0x4020(%ebp);");
	asm("loc_6D043A1: mov $0x10000,%eax;");
	asm("loc_6D043A6: and $0xFFFFFFF,%eax;");
	asm("loc_6D043AB: shl $4,%eax;");
	asm("loc_6D043AE: mov -0x4020(%ebp),%ecx;");
	asm("loc_6D043B4: and $0xF,%ecx;");
	asm("loc_6D043B7: or %eax,%ecx;");
	asm("loc_6D043B9: mov %ecx,-0x4020(%ebp);");
	asm("loc_6D043BF: mov -0x602C(%ebp),%edx;");
	asm("loc_6D043C5: push %edx;");
	asm("loc_6D043C6: push $0x4F;");
	asm("loc_6D043C8: push $_data_6D6AEB0;");
	asm("loc_6D043CD: push $_data_6D6ADE0;");
	asm("loc_6D043D2: mov -0x4020(%ebp),%eax;");
	asm("loc_6D043D8: push %eax;");
	asm("loc_6D043D9: mov _import_6D5E018,%ecx;");
	asm("loc_6D043DF: calll *(%ecx);");
	asm("loc_6D043E1: add $0x14,%esp;");
	asm("loc_6D043E4: jmp loc_6D044C3;");
	asm("loc_6D043E9: movl $0,-0x6030(%ebp);");
	asm("loc_6D043F3: mov -0x18(%ebp),%edx;");
	asm("loc_6D043F6: and $0x10,%edx;");
	asm("loc_6D043F9: test %edx,%edx;");
	asm("loc_6D043FB: je loc_6D0440C;");
	asm("loc_6D043FD: mov -0x6030(%ebp),%eax;");
	asm("loc_6D04403: add $0xC,%eax;");
	asm("loc_6D04406: mov %eax,-0x6030(%ebp);");
	asm("loc_6D0440C: mov -0x18(%ebp),%ecx;");
	asm("loc_6D0440F: push %ecx;");
	asm("loc_6D04410: call _sub_6D16310;");
	asm("loc_6D04415: add $4,%esp;");
	asm("loc_6D04418: mov %eax,%esi;");
	asm("loc_6D0441A: mov -0x18(%ebp),%edx;");
	asm("loc_6D0441D: push %edx;");
	asm("loc_6D0441E: call _sub_6D163B0;");
	asm("loc_6D04423: add $4,%esp;");
	asm("loc_6D04426: mov %eax,%edi;");
	asm("loc_6D04428: mov -0x18(%ebp),%eax;");
	asm("loc_6D0442B: push %eax;");
	asm("loc_6D0442C: call _sub_6D162B0;");
	asm("loc_6D04431: add $4,%esp;");
	asm("loc_6D04434: add -0x6030(%ebp),%eax;");
	asm("loc_6D0443A: add %edi,%eax;");
	asm("loc_6D0443C: add %esi,%eax;");
	asm("loc_6D0443E: mov %eax,-0x10(%ebp);");
	asm("loc_6D04441: mov -0x10(%ebp),%ecx;");
	asm("loc_6D04444: push %ecx;");
	asm("loc_6D04445: mov -0x14(%ebp),%edx;");
	asm("loc_6D04448: push %edx;");
	asm("loc_6D04449: push $0;");
	asm("loc_6D0444B: mov -0x603C(%ebp),%eax;");
	asm("loc_6D04451: mov (%eax),%ecx;");
	asm("loc_6D04453: mov -0x603C(%ebp),%edx;");
	asm("loc_6D04459: push %edx;");
	asm("loc_6D0445A: calll *0x14C(%ecx);");
	asm("loc_6D04460: mov %eax,-8(%ebp);");
	asm("loc_6D04463: cmpl $0,-8(%ebp);");
	asm("loc_6D04467: jge loc_6D044C3;");
	asm("loc_6D04469: mov -8(%ebp),%eax;");
	asm("loc_6D0446C: push %eax;");
	asm("loc_6D0446D: call _sub_6D2B964;");
	asm("loc_6D04472: add $4,%esp;");
	asm("loc_6D04475: push %eax;");
	asm("loc_6D04476: push $_data_6D6AE7C;");
	asm("loc_6D0447B: lea -0x6020(%ebp),%ecx;");
	asm("loc_6D04481: push %ecx;");
	asm("loc_6D04482: call _sub_6D168F0;");
	asm("loc_6D04487: add $0xC,%esp;");
	asm("loc_6D0448A: mov %eax,-0x6034(%ebp);");
	asm("loc_6D04490: mov -0x6034(%ebp),%edx;");
	asm("loc_6D04496: push %edx;");
	asm("loc_6D04497: push $0x54;");
	asm("loc_6D04499: push $_data_6D6AEB0;");
	asm("loc_6D0449E: push $_data_6D6ADE0;");
	asm("loc_6D044A3: push $2;");
	asm("loc_6D044A5: push $0x10000;");
	asm("loc_6D044AA: lea -0x6024(%ebp),%ecx;");
	asm("loc_6D044B0: call _sub_6D15B80;");
	asm("loc_6D044B5: mov (%eax),%eax;");
	asm("loc_6D044B7: push %eax;");
	asm("loc_6D044B8: mov _import_6D5E018,%ecx;");
	asm("loc_6D044BE: calll *(%ecx);");
	asm("loc_6D044C0: add $0x14,%esp;");
	asm("loc_6D044C3: movl $3,-0x604C(%ebp);");
	asm("loc_6D044CD: mov -0x6070(%ebp),%edx;");
	asm("loc_6D044D3: mov 0x130(%edx),%eax;");
	asm("loc_6D044D9: mov %eax,-0x6048(%ebp);");
	asm("loc_6D044DF: mov -0x6070(%ebp),%ecx;");
	asm("loc_6D044E5: add $0x2284,%ecx;");
	asm("loc_6D044EB: mov %ecx,-0x6044(%ebp);");
	asm("loc_6D044F1: mov -0x6044(%ebp),%edx;");
	asm("loc_6D044F7: movzbl 0x40(%edx),%eax;");
	asm("loc_6D044FB: test %eax,%eax;");
	asm("loc_6D044FD: jne loc_6D04551;");
	asm("loc_6D044FF: mov -0x6044(%ebp),%ecx;");
	asm("loc_6D04505: movb $0,0x41(%ecx);");
	asm("loc_6D04509: mov -0x6044(%ebp),%edx;");
	asm("loc_6D0450F: movb $1,0x40(%edx);");
	asm("loc_6D04513: mov -0x6044(%ebp),%eax;");
	asm("loc_6D04519: push %eax;");
	asm("loc_6D0451A: mov -0x604C(%ebp),%ecx;");
	asm("loc_6D04520: push %ecx;");
	asm("loc_6D04521: mov -0x6048(%ebp),%edx;");
	asm("loc_6D04527: mov (%edx),%eax;");
	asm("loc_6D04529: mov -0x6048(%ebp),%ecx;");
	asm("loc_6D0452F: push %ecx;");
	asm("loc_6D04530: calll *0x94(%eax);");
	asm("loc_6D04536: mov %eax,-0x6040(%ebp);");
	asm("loc_6D0453C: xor %edx,%edx;");
	asm("loc_6D0453E: cmpl $0,-0x6040(%ebp);");
	asm("loc_6D04545: setge %dl;");
	asm("loc_6D04548: mov -0x6044(%ebp),%eax;");
	asm("loc_6D0454E: mov %dl,0x40(%eax);");
	asm("loc_6D04551: movl $2,-0x605C(%ebp);");
	asm("loc_6D0455B: mov -0x6070(%ebp),%eax;");
	asm("loc_6D04561: mov 0x130(%eax),%ecx;");
	asm("loc_6D04567: mov %ecx,-0x6058(%ebp);");
	asm("loc_6D0456D: mov -0x6070(%ebp),%edx;");
	asm("loc_6D04573: add $0x21FC,%edx;");
	asm("loc_6D04579: mov %edx,-0x6054(%ebp);");
	asm("loc_6D0457F: mov -0x6054(%ebp),%eax;");
	asm("loc_6D04585: movzbl 0x40(%eax),%ecx;");
	asm("loc_6D04589: test %ecx,%ecx;");
	asm("loc_6D0458B: jne loc_6D045DF;");
	asm("loc_6D0458D: mov -0x6054(%ebp),%edx;");
	asm("loc_6D04593: movb $0,0x41(%edx);");
	asm("loc_6D04597: mov -0x6054(%ebp),%eax;");
	asm("loc_6D0459D: movb $1,0x40(%eax);");
	asm("loc_6D045A1: mov -0x6054(%ebp),%ecx;");
	asm("loc_6D045A7: push %ecx;");
	asm("loc_6D045A8: mov -0x605C(%ebp),%edx;");
	asm("loc_6D045AE: push %edx;");
	asm("loc_6D045AF: mov -0x6058(%ebp),%eax;");
	asm("loc_6D045B5: mov (%eax),%ecx;");
	asm("loc_6D045B7: mov -0x6058(%ebp),%edx;");
	asm("loc_6D045BD: push %edx;");
	asm("loc_6D045BE: calll *0x94(%ecx);");
	asm("loc_6D045C4: mov %eax,-0x6050(%ebp);");
	asm("loc_6D045CA: xor %eax,%eax;");
	asm("loc_6D045CC: cmpl $0,-0x6050(%ebp);");
	asm("loc_6D045D3: setge %al;");
	asm("loc_6D045D6: mov -0x6054(%ebp),%ecx;");
	asm("loc_6D045DC: mov %al,0x40(%ecx);");
	asm("loc_6D045DF: movl $0x100,-0x606C(%ebp);");
	asm("loc_6D045E9: mov -0x6070(%ebp),%ecx;");
	asm("loc_6D045EF: mov 0x130(%ecx),%edx;");
	asm("loc_6D045F5: mov %edx,-0x6068(%ebp);");
	asm("loc_6D045FB: mov -0x6070(%ebp),%eax;");
	asm("loc_6D04601: add $0x2240,%eax;");
	asm("loc_6D04606: mov %eax,-0x6064(%ebp);");
	asm("loc_6D0460C: mov -0x6064(%ebp),%ecx;");
	asm("loc_6D04612: movzbl 0x40(%ecx),%edx;");
	asm("loc_6D04616: test %edx,%edx;");
	asm("loc_6D04618: jne loc_6D0466C;");
	asm("loc_6D0461A: mov -0x6064(%ebp),%eax;");
	asm("loc_6D04620: movb $0,0x41(%eax);");
	asm("loc_6D04624: mov -0x6064(%ebp),%ecx;");
	asm("loc_6D0462A: movb $1,0x40(%ecx);");
	asm("loc_6D0462E: mov -0x6064(%ebp),%edx;");
	asm("loc_6D04634: push %edx;");
	asm("loc_6D04635: mov -0x606C(%ebp),%eax;");
	asm("loc_6D0463B: push %eax;");
	asm("loc_6D0463C: mov -0x6068(%ebp),%ecx;");
	asm("loc_6D04642: mov (%ecx),%edx;");
	asm("loc_6D04644: mov -0x6068(%ebp),%eax;");
	asm("loc_6D0464A: push %eax;");
	asm("loc_6D0464B: calll *0x94(%edx);");
	asm("loc_6D04651: mov %eax,-0x6060(%ebp);");
	asm("loc_6D04657: xor %ecx,%ecx;");
	asm("loc_6D04659: cmpl $0,-0x6060(%ebp);");
	asm("loc_6D04660: setge %cl;");
	asm("loc_6D04663: mov -0x6064(%ebp),%edx;");
	asm("loc_6D04669: mov %cl,0x40(%edx);");
	asm("loc_6D0466C: mov -0x6070(%ebp),%edx;");
	asm("loc_6D04672: mov 0x130(%edx),%eax;");
	asm("loc_6D04678: push %eax;");
	asm("loc_6D04679: mov -0x6070(%ebp),%ecx;");
	asm("loc_6D0467F: add $0x21F4,%ecx;");
	asm("loc_6D04685: call _sub_6D2D032;");
	asm("loc_6D0468A: mov %eax,-4(%ebp);");
	asm("loc_6D0468D: pop %edi;");
	asm("loc_6D0468E: pop %esi;");
	asm("loc_6D0468F: mov %ebp,%esp;");
	asm("loc_6D04691: pop %ebp;");
	asm("loc_6D04692: ret;");
	asm("int3;"); // unreachable
}

_extern int __thiscall sub_6D04138(NewRenderPipeline* _this)
{
	_this->curr_hw_geometry.flush_indices(_this->direct3d_device);


	int result = sub_6D04138_OLD(_this);
	return result;
}


_extern int __cdecl sub_6D010CF(int a1, int a2);
_extern int __cdecl sub_6D163B0(char a1);
_extern int __cdecl sub_6D162B0(char a1);
_extern int __cdecl sub_6D16310(unsigned int a1);
_extern HRESULT __thiscall sub_6D1C660(RPIndexBufferInternal* _this, DWORD offset, DWORD num_indices, void** out_locked_data_ptr, DWORD* out_start_index, bool a6);
#define copy_indices_sub_6D1C660 sub_6D1C660
GENRESULT copy_indices2(NewRenderPipeline* _this, const U16* indices, U32 num_indices, U32* out_start_index)
{
	GENRESULT result = GR_GENERIC;

	void* locked_data_ptr;
	DWORD start_index;

	bool unknown = (_this->unknown128 & 2) != 0; // #TODO Better name for this bit, it's set in initialization
	HRESULT hr = copy_indices_sub_6D1C660(
		&_this->scratchIB,
		0,
		num_indices,
		&locked_data_ptr,
		&start_index,
		unknown);

	if (SUCCEEDED(hr))
	{
		memcpy(locked_data_ptr, indices, 2 * num_indices);
		_this->scratchIB.locked_data_ptr = 0;
		_this->scratchIB.direct3d_index_buffer->Unlock();
		if (out_start_index)
		{
			*out_start_index = start_index;
		}
		result = GR_OK;
	}
	else
	{
		GENERAL_ERROR(TEMPSTR("%s: %s", __FUNCTION__, HRESULT_GET_ERROR_STRING(hr)));
		result = GR_GENERIC;
	}
	return result;
}

GENRESULT NewRenderPipeline::draw_primitive(D3DPRIMITIVETYPE type, U32 vertex_format, const void* verts, U32 num_verts, U32 flags)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = GR_GENERIC;
	if (!verts || !num_verts || !direct3d_device)
	{
		return GR_INVALID_PARMS;
	}
	else if (unknown184_is_locked & 1)
	{
		if (num_verts > 0)
		{
			U32 primCount = GetPrimCount(type, num_verts);
			if (primCount > 0)
			{
				curr_hw_geometry.set_vertex_buffer(NULL, vertex_format);
				sub_6D04138(this);

				U32 stride = FVF_SIZEOF_VERT(vertex_format);
				ASSERT(stride > 0);
				HRESULT hr;
				if (SUCCEEDED(hr = direct3d_device->DrawPrimitiveUP(type, primCount, verts, stride)))
				{
					result = GR_OK;
				}
				else
				{
					GENERAL_ERROR(TEMPSTR("DrawPrimitiveUP: %s", HRESULT_GET_ERROR_STRING(hr)));
					result = GR_GENERIC;
				}

				curr_hw_geometry.invalidate();
			}
		}
	}
	return result;
}

GENRESULT NewRenderPipeline::draw_indexed_primitive(D3DPRIMITIVETYPE type, U32 vertex_format, const void* verts, U32 num_verts, const U16* indices, U32 num_indices, U32 flags)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = GR_GENERIC;
	if (!verts || !num_verts || !indices || !num_indices || !direct3d_device)
	{
		return GR_INVALID_PARMS;
	}
	else if (unknown184_is_locked & 1)
	{
		if (num_indices > 0)
		{
			U32 primCount = GetPrimCount(type, num_indices);
			if (primCount > 0)
			{
				curr_hw_geometry.set_vertex_buffer(NULL, vertex_format);
				sub_6D04138(this);

				U32 stride = FVF_SIZEOF_VERT(vertex_format);
				ASSERT(stride > 0);
				HRESULT hr;
				if (SUCCEEDED(hr = direct3d_device->DrawIndexedPrimitiveUP(type, 0, num_verts, primCount, indices, D3DFMT_INDEX16, verts, stride)))
				{
					result = GR_OK;
				}
				else
				{
					GENERAL_ERROR(TEMPSTR("DrawIndexedPrimitiveUP: %s", HRESULT_GET_ERROR_STRING(hr)));
					result = GR_GENERIC;
				}

				curr_hw_geometry.invalidate();
			}
		}
	}
	return result;
}

GENRESULT NewRenderPipeline::draw_primitive_vb(D3DPRIMITIVETYPE type, IRP_VERTEXBUFFERHANDLE vbhandle, U32 start_vert, U32 num_verts, U32 flags)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = GR_GENERIC;

	if (!vbhandle || !num_verts || !direct3d_device)
	{
		return GR_INVALID_PARMS;
	}
	else if (unknown184_is_locked & 1)
	{
		if (num_verts > 0)
		{
			U32 primCount = GetPrimCount(type, num_verts);
			if (primCount > 0)
			{

				curr_hw_geometry.set_vertex_buffer(vbhandle, D3DFMT_UNKNOWN);
				sub_6D04138(this);

				HRESULT hr;
				if (SUCCEEDED(hr = direct3d_device->DrawPrimitive(type, start_vert, primCount)))
				{
					result = GR_OK;
				}
				else
				{
					GENERAL_ERROR(TEMPSTR("DrawPrimitive: %s", HRESULT_GET_ERROR_STRING(hr)));
					result = GR_GENERIC;
				}

				curr_hw_geometry.invalidate();
			}
		}
	}
	return result;
}

GENRESULT NewRenderPipeline::draw_indexed_primitive_vb(D3DPRIMITIVETYPE type, IRP_VERTEXBUFFERHANDLE vbhandle, U32 start_vert, U32 num_verts, const U16* indices, U32 num_indices, U32 flags)
{
	CHECK_DEVICE_LIFETIME();

	GENRESULT result = GR_GENERIC;
	if (vbhandle && !num_verts && !indices && !num_indices && !direct3d_device)
	{
		return GR_INVALID_PARMS;
	}
	else if (unknown184_is_locked & 1)
	{
		if (num_verts > 0)
		{
			U32 primCount = GetPrimCount(type, num_indices);
			if (primCount > 0)
			{
				U32 start_index;
				if (SUCCEEDED(result = copy_indices2(this, indices, num_indices, &start_index)))
				{
					IRP_INDEXBUFFERHANDLE index_buffer = reinterpret_cast<IRP_INDEXBUFFERHANDLE>(&scratchIB); // #TODO This is nasty, is there a better way to do this?

					curr_hw_geometry.set_vertex_buffer(vbhandle, 0);
					curr_hw_geometry.set_index_buffer(index_buffer, start_vert);

					sub_6D04138(this);

					HRESULT hr;
					if (SUCCEEDED(hr = direct3d_device->DrawIndexedPrimitive(type, 0, num_verts, start_index, primCount)))
					{
						result = GR_OK;
					}
					else
					{
						GENERAL_ERROR(TEMPSTR("DrawPrimitive: %s", HRESULT_GET_ERROR_STRING(hr)));
						result = GR_GENERIC;
					}

					curr_hw_geometry.invalidate();
				}
			}
		}
	}
	return result;
}

GENRESULT NewRenderPipeline::add_light(IRP_LIGHTHANDLE handle)
{
	//	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_add_light(this, handle);
	return result;
}

GENRESULT NewRenderPipeline::remove_light(IRP_LIGHTHANDLE handle)
{
	GENRESULT result = DirectX8_remove_light(this, handle);
	return result;
}

GENRESULT NewRenderPipeline::update_light(IRP_LIGHTHANDLE handle)
{
	//NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_update_light(this, handle);
	return result;
}

GENRESULT NewRenderPipeline::set_world_n(UNKNOWN a2, Transform* transform)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_set_world_n(this, a2, transform);
	return result;
}

GENRESULT NewRenderPipeline::VertexBufferManager_UnknownC(UNKNOWN a2, UNKNOWN a3, UNKNOWN a4, UNKNOWN a5)
{
	GENRESULT result = DirectX8_VertexBufferManager_UnknownC(this, a2, a3, a4, a5);
	return result;
}

GENRESULT NewRenderPipeline::VertexBufferManager_Unknown10()
{
	GENRESULT result = DirectX8_VertexBufferManager_Unknown10(this);
	return result;
}

GENRESULT NewRenderPipeline::acquire_vertex_buffer(UNKNOWN vertex_format, U32 num_verts, VertexBufferAcquire* out_result)
{
	GENRESULT result = DirectX8_acquire_vertex_buffer(this, vertex_format, num_verts, out_result);
	return result;
}

GENRESULT NewRenderPipeline::release_vertex_buffer(VertexBufferAcquire* vbacquire)
{
	GENRESULT result = DirectX8_release_vertex_buffer(this, vbacquire);
	return result;
}

GENRESULT NewRenderPipeline::VertexBufferManager_Unknown1C()
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_VertexBufferManager_Unknown1C(this);
	return result;
}

GENRESULT NewRenderPipeline::VertexBufferManager_Unknown20(UNKNOWN a2, UNKNOWN a3, UNKNOWN a4, UNKNOWN a5, UNKNOWN a6)
{
	GENRESULT result = DirectX8_VertexBufferManager_Unknown20(this, a2, a3, a4, a5, a6);
	return result;
}

GENRESULT NewRenderPipeline::draw_indexed_primitive(D3DPRIMITIVETYPE type, U32 min_index, U32 num_verts, U32 start_index, U32 count)
{
	GENRESULT result = DirectX8_draw_indexed_primitive2(this, type, min_index, num_verts, start_index, count);
	return result;
}

GENRESULT NewRenderPipeline::create_index_buffer(U32 count, IRP_INDEXBUFFERHANDLE* out_ibhandle, BYTE flags)
{
	GENRESULT result = DirectX8_create_index_buffer(this, count, out_ibhandle, flags);
	return result;
}

GENRESULT NewRenderPipeline::destroy_index_buffer(IRP_INDEXBUFFERHANDLE ibhandle)
{
	GENRESULT result = DirectX8_destroy_index_buffer(this, ibhandle);
	return result;
}

GENRESULT NewRenderPipeline::create_ib(IRP_INDEXBUFFERHANDLE ibhandle, U32 count)
{
	GENRESULT result = DirectX8_create_ib(this, ibhandle, count);
	return result;
}

GENRESULT NewRenderPipeline::copy_indices(IRP_INDEXBUFFERHANDLE ibhandle, U32* offset, void* data, U32 count)
{
	GENRESULT result = DirectX8_copy_indices(this, ibhandle, offset, data, count);
	return result;
}

GENRESULT NewRenderPipeline::lock_ib(IRP_INDEXBUFFERHANDLE ibhandle, U32* offset, void** locked_data, U32 count)
{
	GENRESULT result = DirectX8_lock_ib(this, ibhandle, offset, locked_data, count);
	return result;
}

GENRESULT NewRenderPipeline::unlock_ib(IRP_INDEXBUFFERHANDLE ibhandle)
{
	GENRESULT result = DirectX8_unlock_ib(this, ibhandle);
	return result;
}

GENRESULT NewRenderPipeline::select_ib(IRP_INDEXBUFFERHANDLE ibhandle, UNKNOWN a3, UNKNOWN a4, UNKNOWN a5)
{
	GENRESULT result = DirectX8_select_ib(this, ibhandle, a3, a4, a5);
	return result;
}

GENRESULT NewRenderPipeline::get_ib_count(IRP_INDEXBUFFERHANDLE ibhandle, U32* out_count)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_get_ib_count(this, ibhandle, out_count);
	return result;
}

BOOL32 NewRenderPipeline::is_ib_valid(IRP_INDEXBUFFERHANDLE ibhandle)
{
	BOOL32 result = DirectX8_is_ib_valid(this, ibhandle);
	return result;
}

GENRESULT NewRenderPipeline::create_vb(UNKNOWN format, U32 count, IRP_VERTEXBUFFERHANDLE* out_vbhandle, BYTE flags)
{
	GENRESULT result = DirectX8_create_vb(this, format, count, out_vbhandle, flags);
	return result;
}

GENRESULT NewRenderPipeline::destroy_vb(IRP_VERTEXBUFFERHANDLE vbhandle)
{
	GENRESULT result = DirectX8_destroy_vb(this, vbhandle);
	return result;
}

GENRESULT NewRenderPipeline::ressize_vb(IRP_VERTEXBUFFERHANDLE vbhandle, UNKNOWN format, U32 count)
{
	GENRESULT result = DirectX8_ressize_vb(this, vbhandle, format, count);
	return result;
}

GENRESULT NewRenderPipeline::copy_vertices(IRP_VERTEXBUFFERHANDLE vbhandle, U32* offset, UNKNOWN* a4, UNKNOWN a5, UNKNOWN a6)
{
	GENRESULT result = DirectX8_copy_vertices(this, vbhandle, offset, a4, a5, a6);
	return result;
}

GENRESULT NewRenderPipeline::lock_vb(IRP_VERTEXBUFFERHANDLE vbhandle, U32* offset, void** locked_data, U32 count)
{
	GENRESULT result = DirectX8_lock_vb(this, vbhandle, offset, locked_data, count);
	return result;
}

GENRESULT NewRenderPipeline::unlock_vb(IRP_VERTEXBUFFERHANDLE vbhandle)
{
	GENRESULT result = DirectX8_unlock_vb(this, vbhandle);
	return result;
}

GENRESULT NewRenderPipeline::RPVertexBuffer_Unknown24(UNKNOWN a2)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_RPVertexBuffer_Unknown24(this, a2);
	return result;
}

GENRESULT NewRenderPipeline::select_vb(IRP_VERTEXBUFFERHANDLE vbhandle)
{
	GENRESULT result = DirectX8_select_vb(this, vbhandle);
	return result;
}

GENRESULT NewRenderPipeline::get_vb_count(IRP_VERTEXBUFFERHANDLE vbhandle, UNKNOWN* vertex_format, U32* num_verts)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_get_vb_count(this, vbhandle, vertex_format, num_verts);
	return result;
}

BOOL32 NewRenderPipeline::is_vb_valid(IRP_VERTEXBUFFERHANDLE vbhandle)
{
	BOOL32 result = DirectX8_is_vb_valid(this, vbhandle);
	return result;
}

GENRESULT NewRenderPipeline::set_gamma_function(IGC_COMPONENT which, float display_gamma, float bias, float slope, float black_offset)
{
	GENRESULT result = DirectX8_set_gamma_function(this, which, display_gamma, bias, slope, black_offset);
	return result;
}

GENRESULT NewRenderPipeline::set_gamma_ramp(IGC_COMPONENT igc_component, U16* ramp)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_set_gamma_ramp(this, igc_component, ramp);
	return result;
}

GENRESULT NewRenderPipeline::get_gamma_ramp(IGC_COMPONENT igc_component, U16* out_ramp)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_get_gamma_ramp(this, igc_component, out_ramp);
	return result;
}

GENRESULT NewRenderPipeline::set_calibration_enable(bool enabled)
{
	GENRESULT result = DirectX8_set_calibration_enable(this, enabled);
	return result;
}

GENRESULT NewRenderPipeline::get_calibration_enable(void)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_get_calibration_enable(this);
	return result;
}

GENRESULT NewRenderPipeline::print_screen(IFileSystem* pFileSystem, const char* filepath)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_print_screen(this, pFileSystem, filepath);
	return result;
}

GENRESULT NewRenderPipeline::load_texture(UNKNOWN* a2_interface, const char* filepath, IRP_TEXTUREHANDLE* out_texture)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_load_texture(this, a2_interface, filepath, out_texture);
	return result;
}

GENRESULT NewRenderPipeline::load_surface_from_file(UNKNOWN* a2_interface, UNKNOWN a3, UNKNOWN a4, UNKNOWN a5)
{
	NOT_IMPLEMENTED;
	GENRESULT result = DirectX8_load_surface_from_file(this, a2_interface, a3, a4, a5);
	return result;
}

GENRESULT NewRenderPipeline::RPTexture_Unknown18(UNKNOWN a2, UNKNOWN a3, UNKNOWN* a4)
{
	GENRESULT result = DirectX8_RPTexture_Unknown18(this, a2, a3, a4);
	return result;
}

GENRESULT NewRenderPipeline::load_cubemap(UNKNOWN* a2_interface, const char* filepath, IRP_TEXTUREHANDLE* out_texture)
{
	GENRESULT result = DirectX8_load_cubemap(this, a2_interface, filepath, out_texture);
	return result;
}

GENRESULT NewRenderPipeline::Initialize(void)
{
	GENRESULT result = DirectX8_Initialize(this);
	return result;
}

extern "C"
{
	IComponentFactory* CreateNewRenderPipelineFactory()
	{
		return new DAComponentFactory2<DAComponentAggregate<NewRenderPipeline>, AGGDESC>(CLSID_NewRenderPipeline);
	}

	void Register_NewRenderPipeline()
	{
		GENRESULT result = GR_GENERIC;
		if (ICOManager* DACOM = DACOM_Acquire())
		{
			if (IComponentFactory* pFactory = CreateNewRenderPipelineFactory())
			{
				result = DACOM->RegisterComponent(pFactory, CLSID_NewRenderPipeline, DACOM_LOW_PRIORITY);
				pFactory->Release();
			}
		}
		unused(result);
	}
}

static void Transform2D3D(D3DMATRIX& dst, const Transform& src)
{
	// Transpose rotation.

	dst.m[0][0] = src.d[0][0];
	dst.m[1][0] = src.d[0][1];
	dst.m[2][0] = src.d[0][2];
	dst.m[0][1] = src.d[1][0];
	dst.m[1][1] = src.d[1][1];
	dst.m[2][1] = src.d[1][2];
	dst.m[0][2] = src.d[2][0];
	dst.m[1][2] = src.d[2][1];
	dst.m[2][2] = src.d[2][2];

	dst.m[3][0] = src.translation.x;
	dst.m[3][1] = src.translation.y;
	dst.m[3][2] = src.translation.z;
	dst.m[0][3] =
		dst.m[1][3] =
		dst.m[2][3] = 0;
	dst.m[3][3] = 1;
}

static void D3D2Transform(const D3DMATRIX& src, Transform& dest)
{
	// Transpose rotation.

	dest.d[0][0] = src.m[0][0];
	dest.d[0][1] = src.m[1][0];
	dest.d[0][2] = src.m[2][0];
	dest.d[1][0] = src.m[0][1];
	dest.d[1][1] = src.m[1][1];
	dest.d[1][2] = src.m[2][1];
	dest.d[2][0] = src.m[0][2];
	dest.d[2][1] = src.m[1][2];
	dest.d[2][2] = src.m[2][2];

	dest.translation.x = src.m[3][0];
	dest.translation.y = src.m[3][1];
	dest.translation.z = src.m[3][2];
}

static void Matrix2D3D(D3DMATRIX& dst, const Matrix4& src)
{
	// Transpose rotation.

	dst.m[0][0] = src.d[0][0];
	dst.m[1][0] = src.d[0][1];
	dst.m[2][0] = src.d[0][2];
	dst.m[3][0] = src.d[0][3];

	dst.m[0][1] = src.d[1][0];
	dst.m[1][1] = src.d[1][1];
	dst.m[2][1] = src.d[1][2];
	dst.m[3][1] = src.d[1][3];

	dst.m[0][2] = src.d[2][0];
	dst.m[1][2] = src.d[2][1];
	dst.m[2][2] = src.d[2][2];
	dst.m[3][2] = src.d[2][3];

	dst.m[0][3] = src.d[3][0];
	dst.m[1][3] = src.d[3][1];
	dst.m[2][3] = src.d[3][2];
	dst.m[3][3] = src.d[3][3];
}

static void D3D2Matrix(const D3DMATRIX& src, Matrix4& dest)
{
	// Transpose rotation.

	dest.d[0][0] = src.m[0][0];
	dest.d[0][1] = src.m[1][0];
	dest.d[0][2] = src.m[2][0];
	dest.d[0][3] = src.m[3][0];

	dest.d[1][0] = src.m[0][1];
	dest.d[1][1] = src.m[1][1];
	dest.d[1][2] = src.m[2][1];
	dest.d[1][3] = src.m[3][1];

	dest.d[2][0] = src.m[0][2];
	dest.d[2][1] = src.m[1][2];
	dest.d[2][2] = src.m[2][2];
	dest.d[2][3] = src.m[3][2];

	dest.d[3][0] = src.m[0][3];
	dest.d[3][1] = src.m[1][3];
	dest.d[3][2] = src.m[2][3];
	dest.d[3][3] = src.m[3][3];
}

static U32 GetPrimCount(D3DPRIMITIVETYPE type, U32 indexCount)
{
	switch (type)
	{
	case D3DPT_POINTLIST:
		return indexCount;
	case D3DPT_LINELIST:
		return indexCount / 2;
	case D3DPT_LINESTRIP:
		return indexCount - 1;
	case D3DPT_TRIANGLELIST:
		return indexCount / 3;
	case D3DPT_TRIANGLESTRIP:
		return indexCount - 2;
	case D3DPT_TRIANGLEFAN:
		return indexCount - 2;
	default:
		ASSERT(false);
	}
	return 0;
}
