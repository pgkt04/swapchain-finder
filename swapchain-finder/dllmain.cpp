#include "util.h"

#include <d3d11.h>
#include <cstdint>
#include <cstdio>

#pragma comment(lib, "d3d11.lib")

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#ifdef _AMD64_
constexpr std::uintptr_t kMinScanAddress       = 0x10000;
constexpr std::uintptr_t kMaxScanAddress       = 0x7FFFFFFEFFFF;
constexpr std::uintptr_t kHighAddressThreshold = 0xFFFFFFFFFF;
#else
constexpr std::uintptr_t kMinScanAddress       = 0x10000;
constexpr std::uintptr_t kMaxScanAddress       = 0xFFE00000;
constexpr std::uintptr_t kHighAddressThreshold = 0xFFE00000;
#endif

constexpr WORD kColorGreen   = 10;
constexpr WORD kColorRed     = 12;
constexpr WORD kColorDefault = 15;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static LRESULT CALLBACK stub_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr&)            = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** address_of() noexcept { return &ptr_; }
    T*  get()  const noexcept { return ptr_; }
    T*  operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    void reset() noexcept
    {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_ = nullptr;
};

// ---------------------------------------------------------------------------
// D3D11 swap chain creation
// ---------------------------------------------------------------------------

struct SwapChainResult {
    ComPtr<IDXGISwapChain>      swap_chain;
    ComPtr<ID3D11Device>        device;
    ComPtr<ID3D11DeviceContext>  device_context;
    HWND                        window = nullptr;
};

static bool create_temp_swap_chain(SwapChainResult& out)
{
    WNDCLASSEXA wc{};
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = stub_wndproc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "SwapChainFinderDummy";

    RegisterClassExA(&wc);

    out.window = CreateWindowA(
        wc.lpszClassName, nullptr, WS_OVERLAPPEDWINDOW,
        0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);

    if (!out.window)
        return false;

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount                        = 1;
    desc.BufferDesc.Width                   = 1;
    desc.BufferDesc.Height                  = 1;
    desc.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferDesc.Scaling                 = DXGI_MODE_SCALING_UNSPECIFIED;
    desc.BufferDesc.ScanlineOrdering        = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    desc.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    desc.OutputWindow                       = out.window;
    desc.SampleDesc.Count                   = 1;
    desc.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;
    desc.Windowed                           = TRUE;

    constexpr D3D_FEATURE_LEVEL requested_levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
    };

    D3D_FEATURE_LEVEL obtained_level{};

    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        requested_levels,
        _countof(requested_levels),
        D3D11_SDK_VERSION,
        &desc,
        out.swap_chain.address_of(),
        out.device.address_of(),
        &obtained_level,
        out.device_context.address_of());

    return SUCCEEDED(hr);
}

/// COM objects store their vtable pointer as the first member.
static DWORD_PTR get_vtable_ptr(IUnknown* obj)
{
    return *reinterpret_cast<DWORD_PTR*>(obj);
}

// ---------------------------------------------------------------------------
// Memory scanner
// ---------------------------------------------------------------------------

static bool is_readable_region(const MEMORY_BASIC_INFORMATION& mbi)
{
    if (mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect == PAGE_NOACCESS)
        return false;
    if (mbi.Protect & PAGE_GUARD)
        return false;
    return true;
}

static void scan_memory_for_swapchain(DWORD_PTR vtable_ptr, const IDXGISwapChain* exclude)
{
    const HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    MEMORY_BASIC_INFORMATION mbi{};

    std::size_t matches_found = 0;

    for (auto addr = kMinScanAddress; addr < kMaxScanAddress; )
    {
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) {
            addr += 0x1000;
            continue;
        }

        const auto region_base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const auto region_end  = region_base + mbi.RegionSize;

        if (!is_readable_region(mbi)) {
            addr = region_end;
            continue;
        }

        for (auto current = region_base; current + sizeof(DWORD_PTR) <= region_end; current += sizeof(DWORD_PTR))
        {
            const auto value = *reinterpret_cast<const DWORD_PTR*>(current);

            if (value != vtable_ptr)
                continue;

            if (current == reinterpret_cast<std::uintptr_t>(exclude))
                continue;

            const WORD color = (current > kHighAddressThreshold) ? kColorGreen : kColorRed;
            SetConsoleTextAttribute(console, color);
            std::printf("  [%zu] swapchain found at: 0x%p\n",
                        ++matches_found,
                        reinterpret_cast<void*>(current));
        }

        addr = region_end;
    }

    SetConsoleTextAttribute(console, kColorDefault);

    if (matches_found == 0)
        std::printf("\nno swapchain instances found.\n");
    else
        std::printf("\nscan complete — %zu match(es) found.\n", matches_found);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

static DWORD WINAPI scanner_thread(LPVOID /*param*/)
{
    util::setup_console("swapchain finder");

    std::printf("creating temporary d3d11 swap chain...\n");

    SwapChainResult ctx;
    if (!create_temp_swap_chain(ctx)) {
        std::printf("[error] failed to create d3d11 device / swap chain.\n");
        return 1;
    }

    const DWORD_PTR vtable_ptr = get_vtable_ptr(ctx.swap_chain.get());
    std::printf("captured vtable pointer: 0x%p\n", reinterpret_cast<void*>(vtable_ptr));
    std::printf("scanning process memory...\n\n");

    scan_memory_for_swapchain(vtable_ptr, ctx.swap_chain.get());

    if (ctx.window)
        DestroyWindow(ctx.window);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hmodule, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hmodule);
        CreateThread(nullptr, 0, scanner_thread, nullptr, 0, nullptr);
    }
    return TRUE;
}
