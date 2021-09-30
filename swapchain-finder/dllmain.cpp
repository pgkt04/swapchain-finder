#include "util.h"
#include <d3d11.h>
#pragma comment (lib, "d3d11.lib")

LRESULT CALLBACK DXGIMsgProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
  return DefWindowProc( hwnd, uMsg, wParam, lParam );
}

void main()
{
  // allocate a console
  UTIL::setup_console( "sc finder" );

  // create a temp swapchain
  ID3D11Device* Device = nullptr;
  ID3D11DeviceContext* DeviceContext = nullptr;
  ID3D11RenderTargetView* pRenderTargetView = NULL;
  DWORD_PTR* swapchain_tbl = nullptr;

  IDXGISwapChain* temp_swap_chain = nullptr;
  WNDCLASSEXA wc = {sizeof( WNDCLASSEX ), CS_CLASSDC, DXGIMsgProc, 0L, 0L, GetModuleHandleA( 0 ), NULL, NULL, NULL, NULL, "x", NULL};
  RegisterClassExA( &wc );
  HWND window = CreateWindowA( "x", NULL, WS_OVERLAPPEDWINDOW, 100, 100, 300, 300, NULL, NULL, wc.hInstance, NULL );
  D3D_FEATURE_LEVEL requestedLevels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
  D3D_FEATURE_LEVEL obtainedLevel;
  DXGI_SWAP_CHAIN_DESC scd;
  ZeroMemory( &scd, sizeof( scd ) );
  scd.BufferCount = 1;
  scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  scd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
  scd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
  scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  scd.OutputWindow = window;
  scd.SampleDesc.Count = 1;
  scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  scd.Windowed = TRUE;
  scd.BufferDesc.Width = 1;
  scd.BufferDesc.Height = 1;
  scd.BufferDesc.RefreshRate.Numerator = 0;
  scd.BufferDesc.RefreshRate.Denominator = 1;
  UINT createFlags = 0;

  HRESULT hr = D3D11CreateDeviceAndSwapChain
    (
     nullptr,
     D3D_DRIVER_TYPE_HARDWARE,
     nullptr,
     createFlags,
     requestedLevels,
     sizeof( requestedLevels ) / sizeof( D3D_FEATURE_LEVEL ),
     D3D11_SDK_VERSION,
     &scd,
     &temp_swap_chain,
     &Device,
     &obtainedLevel,
     &DeviceContext
    );

  if ( FAILED( hr ) )
  {
    printf( "api fail\n" );
    return;
  }

  swapchain_tbl = (DWORD_PTR*)temp_swap_chain;
  swapchain_tbl = (DWORD_PTR*)swapchain_tbl[0];

  // scan memory for the chain
#ifdef _AMD64_
#define _PTR_MAX_VALUE 0x7FFFFFFEFFFF
  MEMORY_BASIC_INFORMATION64 mbi = {0};
  auto error = 0xffffffffff;
#else
#define _PTR_MAX_VALUE 0xFFE00000
  MEMORY_BASIC_INFORMATION32 mbi = {0};
#endif

  DWORD_PTR dw_addr_table{};
  HANDLE hConsole = GetStdHandle( STD_OUTPUT_HANDLE );

  for ( DWORD_PTR memptr = 0x10000; memptr < _PTR_MAX_VALUE; memptr = mbi.BaseAddress + mbi.RegionSize )
  {
    if ( !VirtualQuery( reinterpret_cast<LPCVOID>( memptr ),
          reinterpret_cast<PMEMORY_BASIC_INFORMATION>( &mbi ),
          sizeof( MEMORY_BASIC_INFORMATION ) )
       )
      continue;

    if ( mbi.State != MEM_COMMIT || 
        mbi.Protect == PAGE_NOACCESS ||
        mbi.Protect & PAGE_GUARD )
      continue;

    // move to the next region
    DWORD_PTR len = mbi.BaseAddress + mbi.RegionSize;

    for ( DWORD_PTR current = mbi.BaseAddress; current < len; ++current )
    {
      try
      {
        dw_addr_table = *(DWORD_PTR*)current;

        if ( dw_addr_table == (DWORD_PTR)swapchain_tbl )
        {
          if ( current == (DWORD_PTR)temp_swap_chain )
            continue;

          if ( current > error )
            SetConsoleTextAttribute( hConsole, 10 );
          else
            SetConsoleTextAttribute( hConsole, 12 );

          printf( "swapchain found at: %p \n", (IDXGISwapChain*)current );
        }
      }
      catch ( ... )
      {
        continue;
      }

    }
  }

  SetConsoleTextAttribute( hConsole, 15 );
  printf( "reached end of pointer scan...\n" );
}

BOOL APIENTRY DllMain( HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
    )
{
  switch ( ul_reason_for_call )
  {
    case DLL_PROCESS_ATTACH:
      CreateThread( 0, 0, (LPTHREAD_START_ROUTINE)main, 0, 0, 0 );
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
      break;
  }
  return TRUE;
}

