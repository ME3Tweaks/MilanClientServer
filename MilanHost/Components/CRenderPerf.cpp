#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <intrin.h>
#include <d3d11.h>

#include "Components/CCommands.hpp"
#include "Components/CRenderPerf.hpp"
#include "Shell/CmdArgs.hpp"
#include "Shell/Entry.hpp"


// ! Input layout cache.
// ========================================

namespace
{
    constexpr char const* k_createDeviceHookName = "RenderPerf::D3D11CreateDevice";
    constexpr char const* k_createInputLayoutHookName = "RenderPerf::ID3D11Device::CreateInputLayout";
    constexpr int k_createInputLayoutSlot = 11;
    constexpr std::uint32_t k_dxbcMagic = 0x43425844;  // 'DXBC'

    using t_D3D11CreateDevice = HRESULT WINAPI(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, D3D_FEATURE_LEVEL const*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    using t_CreateInputLayout = HRESULT STDMETHODCALLTYPE(ID3D11Device*, D3D11_INPUT_ELEMENT_DESC const*, UINT, void const*, SIZE_T, ID3D11InputLayout**);

    t_D3D11CreateDevice* D3D11CreateDevice_orig = nullptr;
    t_CreateInputLayout* CreateInputLayout_orig = nullptr;
    void* g_createInputLayoutTarget = nullptr;
    std::atomic<ID3D11Device*> g_gameDevice{ nullptr };

    struct LayoutEntry
    {
        ID3D11Device* Device;
        std::vector<D3D11_INPUT_ELEMENT_DESC> Elements;  // SemanticName points into Names.
        std::vector<std::string> Names;
        std::vector<std::uint8_t> Bytecode;              // Only kept for non-DXBC bytecode.
        std::uint8_t Checksum[16];
        bool bDxbc;
        SIZE_T Length;
        ID3D11InputLayout* Layout;
    };

    std::unordered_multimap<std::uint64_t, LayoutEntry*> g_layouts{};
    std::shared_mutex g_layoutLock{};
    std::atomic<std::uint64_t> g_layoutHits{ 0 };
    std::atomic<std::uint64_t> g_layoutMisses{ 0 };

    bool IsDxbc(void const* const Bytecode, SIZE_T const Length)
    {
        return Length >= 20 && *static_cast<std::uint32_t const*>(Bytecode) == k_dxbcMagic;
    }

    std::uint64_t Fnv(std::uint64_t Hash, void const* const Data, std::size_t const Size)
    {
        auto const* const Bytes = static_cast<std::uint8_t const*>(Data);
        for (std::size_t i = 0; i < Size; ++i)
        {
            Hash ^= Bytes[i];
            Hash *= 1099511628211ull;
        }
        return Hash;
    }

    // Hashes the layout so we can find it in the cache
    std::uint64_t HashLayout(ID3D11Device* const Device, D3D11_INPUT_ELEMENT_DESC const* const Elements, UINT const Count,
        void const* const Bytecode, SIZE_T const Length)
    {
        static_assert(offsetof(D3D11_INPUT_ELEMENT_DESC, InstanceDataStepRate) - offsetof(D3D11_INPUT_ELEMENT_DESC, SemanticIndex) == sizeof(UINT) * 5);

        std::uint64_t Hash = 1469598103934665603ull;
        Hash = Fnv(Hash, &Device, sizeof Device);
        Hash = Fnv(Hash, &Count, sizeof Count);
        for (UINT i = 0; i < Count; ++i)
        {
            // Lowercased so the hash lookup is consistent
            for (char const* Name = Elements[i].SemanticName; Name != nullptr && *Name != '\0'; ++Name)
            {
                char const Lower = (*Name >= 'A' && *Name <= 'Z') ? char(*Name - 'A' + 'a') : *Name;
                Hash = Fnv(Hash, &Lower, 1);
            }
            Hash = Fnv(Hash, &Elements[i].SemanticIndex, sizeof(UINT) * 6);
        }
        Hash = Fnv(Hash, &Length, sizeof Length);

        // A DXBC container carries a 16-byte checksum of the whole shader at offset 4
        if (IsDxbc(Bytecode, Length)) {
            // Checksum can be used to cache
            return Fnv(Hash, static_cast<std::uint8_t const*>(Bytecode) + 4, 16);
        }
        return Fnv(Hash, Bytecode, Length);
    }

    bool LayoutMatches(LayoutEntry const& Entry, ID3D11Device* const Device, D3D11_INPUT_ELEMENT_DESC const* const Elements, UINT const Count, void const* const Bytecode, SIZE_T const Length)
    {
        if (Entry.Device != Device || Entry.Elements.size() != Count || Entry.Length != Length) {
            // Obviously not a match
            return false;
        }

        for (UINT i = 0; i < Count; ++i)
        {
            // Compare inputs
            D3D11_INPUT_ELEMENT_DESC const& A = Entry.Elements[i];
            D3D11_INPUT_ELEMENT_DESC const& B = Elements[i];
            if (A.SemanticIndex != B.SemanticIndex || A.Format != B.Format || A.InputSlot != B.InputSlot
                || A.AlignedByteOffset != B.AlignedByteOffset || A.InputSlotClass != B.InputSlotClass
                || A.InstanceDataStepRate != B.InstanceDataStepRate) {
                return false;
            }
            if (B.SemanticName == nullptr || ::_stricmp(Entry.Names[i].c_str(), B.SemanticName) != 0)
            {
                return false;
            }
        }

        bool const bDxbc = IsDxbc(Bytecode, Length);
        if (bDxbc != Entry.bDxbc) {
            return false;
        }
        if (bDxbc)
        {
            // Is checksum identical?
            return std::memcmp(Entry.Checksum, static_cast<std::uint8_t const*>(Bytecode) + 4, 16) == 0;
        }

        // Is bytecode identical?
        return std::memcmp(Entry.Bytecode.data(), Bytecode, Length) == 0;
    }

    HRESULT STDMETHODCALLTYPE CreateInputLayout_hook(ID3D11Device* const Device, D3D11_INPUT_ELEMENT_DESC const* const Elements, UINT const Count, void const* const Bytecode, SIZE_T const Length, ID3D11InputLayout** const ppLayout)
    {
        // Validation-only (null output) and malformed calls go straight through.
        if (ppLayout == nullptr || Elements == nullptr || Bytecode == nullptr || Length == 0 || Count == 0 || Count > D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT) {
            return CreateInputLayout_orig(Device, Elements, Count, Bytecode, Length, ppLayout);
        }

        std::uint64_t const Hash = HashLayout(Device, Elements, Count, Bytecode, Length);
        {
            std::shared_lock const Lock{ g_layoutLock };
            auto const [Begin, End] = g_layouts.equal_range(Hash);
            for (auto It = Begin; It != End; ++It)
            {
                if (LayoutMatches(*It->second, Device, Elements, Count, Bytecode, Length))
                {
                    // REturn the cached hit
                    It->second->Layout->AddRef();
                    *ppLayout = It->second->Layout;
                    g_layoutHits.fetch_add(1, std::memory_order_relaxed);
                    return S_OK;
                }
            }
        }

        g_layoutMisses.fetch_add(1, std::memory_order_relaxed);
        HRESULT const Result = CreateInputLayout_orig(Device, Elements, Count, Bytecode, Length, ppLayout);
        if (FAILED(Result) || *ppLayout == nullptr)
        {
            return Result;
        }

        // Cache miss, make the entry and cache it
        auto* const Entry = new LayoutEntry{};
        Entry->Device = Device;
        Entry->Elements.assign(Elements, Elements + Count);
        Entry->Length = Length;
        Entry->Layout = *ppLayout;
        Entry->bDxbc = IsDxbc(Bytecode, Length);
        if (Entry->bDxbc)
        {
            std::memcpy(Entry->Checksum, static_cast<std::uint8_t const*>(Bytecode) + 4, 16);
        }
        else
        {
            Entry->Bytecode.assign(static_cast<std::uint8_t const*>(Bytecode), static_cast<std::uint8_t const*>(Bytecode) + Length);
        }
        Entry->Names.reserve(Count);
        for (UINT i = 0; i < Count; ++i)
        {
            Entry->Names.emplace_back(Elements[i].SemanticName != nullptr ? Elements[i].SemanticName : "");
        }
        for (UINT i = 0; i < Count; ++i) {
            Entry->Elements[i].SemanticName = Entry->Names[i].c_str();

        }

        Entry->Layout->AddRef();

        std::unique_lock const Lock{ g_layoutLock };
        g_layouts.emplace(Hash, Entry);
        return Result;
    }

    /// <summary>
    /// Is this address from the game itself
    /// </summary>
    /// <param name="Address"></param>
    /// <returns></returns>
    bool IsGameModuleAddress(void* const Address)
    {
        HMODULE Module = nullptr;
        return ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(Address), &Module) && Module == ::GetModuleHandleW(nullptr);
    }

    // Hook the device so we can cache the inputs
    void HookGameDevice(ID3D11Device* const Device)
    {
        ID3D11Device* Expected = nullptr;
        if (!g_gameDevice.compare_exchange_strong(Expected, Device))
        {
            MILAN_WARNW(ELOG_RenderPerf, L"game created another device %p (first was %p)", (void*)Device, (void*)Expected);
            return;
        }

        void* const Target = (*reinterpret_cast<void***>(Device))[k_createInputLayoutSlot];
        auto& Context = *gp_sdkContext;
        CreateInputLayout_orig = static_cast<t_CreateInputLayout*>(Context.InstallHookDeferred(k_createInputLayoutHookName, Target, reinterpret_cast<void*>(&CreateInputLayout_hook)));

        if (CreateInputLayout_orig == nullptr)
        {
            MILAN_ERRORW(ELOG_RenderPerf, L"failed to hook ID3D11Device::CreateInputLayout at %p", Target);
            return;
        }

        // Enable hook so we don't get a race condition
        Context.EnableHook(k_createInputLayoutHookName);
        g_createInputLayoutTarget = Target;
        MILAN_INFOW(ELOG_RenderPerf, L"input layout cache enabled on game device %p (flags 0x%x), CreateInputLayout at %p", (void*)Device, Device->GetCreationFlags(), Target);
    }

    HRESULT WINAPI D3D11CreateDevice_hook(IDXGIAdapter* const Adapter, D3D_DRIVER_TYPE const DriverType, HMODULE const Software, UINT const Flags, D3D_FEATURE_LEVEL const* const Levels,
        UINT const LevelCount, UINT const SdkVersion, ID3D11Device** const ppDevice, D3D_FEATURE_LEVEL* const pLevel, ID3D11DeviceContext** const ppContext)
    {
        void* const Caller = _ReturnAddress();
        HRESULT const Result = D3D11CreateDevice_orig(Adapter, DriverType, Software, Flags, Levels, LevelCount, SdkVersion, ppDevice, pLevel, ppContext);

        // Other modules (overlays, MilanHost's own dummy swap chain) create devices too; only the game's counts.
        if (SUCCEEDED(Result) && ppDevice != nullptr && *ppDevice != nullptr && IsGameModuleAddress(Caller))
            HookGameDevice(*ppDevice);

        return Result;
    }
}


// ! CRenderPerf
// ========================================

bool CRenderPerf::Initialize(::LESDK::Initializer& Context)
{
    mb_enabled = CmdArgs::HasFlag(L"-perf");
    RegisterCommands();

    if (!mb_enabled)
    {
        MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized (disabled, launch with -perf to enable)", k_typeName);
        return true;
    }

    // The game delay-loads d3d11.dll, so it may not be mapped yet.
    HMODULE const D3D11 = ::LoadLibraryW(L"d3d11.dll");
    void* const Target = D3D11 != nullptr ? reinterpret_cast<void*>(::GetProcAddress(D3D11, "D3D11CreateDevice")) : nullptr;
    if (Target == nullptr)
    {
        MILAN_ERRORW(ELOG_RenderPerf, L"failed to find D3D11CreateDevice, input layout cache disabled");
        return true;
    }

    D3D11CreateDevice_orig = static_cast<t_D3D11CreateDevice*>(
        Context.InstallHookDeferred(k_createDeviceHookName, Target, reinterpret_cast<void*>(&D3D11CreateDevice_hook)));
    if (D3D11CreateDevice_orig == nullptr)
    {
        MILAN_ERRORW(ELOG_RenderPerf, L"failed to hook D3D11CreateDevice, input layout cache disabled");
        return true;
    }

    Context.EnableHook(k_createDeviceHookName);
    mb_createDeviceHooked = true;

    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized, waiting for the game's device", k_typeName);
    return true;
}

void CRenderPerf::Terminate(::LESDK::Initializer& Context)
{
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);

    if (g_createInputLayoutTarget != nullptr)
        Context.UninstallHook(k_createInputLayoutHookName);
    if (mb_createDeviceHooked)
        Context.UninstallHook(k_createDeviceHookName);
}

void CRenderPerf::RegisterCommands()
{
    COMPONENT(CCommands).RegisterCommand(L"perf", L"status",
        [this](wchar_t const*)
        {
            if (!mb_enabled)
            {
                MILAN_INFOW(L"renderperf: disabled (launch with -perf)");
                return true;
            }

            if (g_createInputLayoutTarget == nullptr)
            {
                MILAN_WARNW(L"renderperf: enabled, but the game's device was not caught");
                return true;
            }

            std::size_t Cached = 0;
            {
                std::shared_lock const Lock{ g_layoutLock };
                Cached = g_layouts.size();
            }

            std::uint64_t const Hits = g_layoutHits.load(std::memory_order_relaxed);
            std::uint64_t const Misses = g_layoutMisses.load(std::memory_order_relaxed);
            LARGE_INTEGER Now{}, Frequency{};
            ::QueryPerformanceCounter(&Now);
            ::QueryPerformanceFrequency(&Frequency);

            MILAN_INFOW(L"renderperf: input layout cache: %llu hits, %llu misses, %zu cached",
                (unsigned long long)Hits, (unsigned long long)Misses, Cached);

            if (m_lastTicks != 0)
            {
                double const Seconds = double(Now.QuadPart - m_lastTicks) / double(Frequency.QuadPart);
                MILAN_INFOW(L"renderperf: since last perf.status (%.1f s): %.0f hits/s, %.1f misses/s",
                    Seconds, double(Hits - m_lastHits) / Seconds, double(Misses - m_lastMisses) / Seconds);
            }

            m_lastHits = Hits;
            m_lastMisses = Misses;
            m_lastTicks = Now.QuadPart;
            return true;
        });
}
