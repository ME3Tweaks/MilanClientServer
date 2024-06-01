#include <tlhelp32.h>

#include "Base.hpp"
#include "Intercept.hpp"
#include "Systems/Output.hpp"


// ! FunctionInterceptLegacy implementation.
// ========================================

TArray<FunctionInterceptLegacy*> FunctionInterceptLegacy::RegisteredIntercepts;

FunctionInterceptLegacy::FunctionInterceptLegacy()
{
    // This requires all instances to be static.
    RegisteredIntercepts.Add(this);
}

bool FunctionInterceptLegacy::Applies(UStruct* const Node, wchar_t const* const FullName)
{
    MILAN_CHECKW(Node != nullptr, L"");

    if (TargetFullName == nullptr)
    {
        MILAN_CHECKW(FullName != nullptr, L"");
        MILAN_CHECKW(::wcslen(FullName) > 0, L"");
        TargetFullName = FullName;
    }

    if (TargetNode == nullptr)
    {
        MILAN_CHECKW(TargetFullName != nullptr, L"");
        TargetNode = UObject::FindObject<UStruct>(TargetFullName);
    }

    return TargetNode == Node;
}

void FunctionInterceptLegacy::InvalidateAllRegistered()
{
    for (FunctionInterceptLegacy* Intercept : RegisteredIntercepts)
    {
        MILAN_CHECKW(Intercept->TargetFullName != nullptr, L"");
        Intercept->TargetNode = nullptr;
    }
}

void FunctionInterceptLegacy::CleanupRegistered()
{
    RegisteredIntercepts.Clear();
    RegisteredIntercepts.Shrink();
}


// ! CaveIndexer implementation.
// ========================================

CaveIndexer::CaveIndexer(std::ptrdiff_t const MinSize)
{
    MILAN_CHECKW(MinSize >= k_minCaveSize, L"");
    IndexMinSize = MinSize;

    auto const Text = ViewMainModule();
    for (size_t i = 0; i < Text.size(); /* nada */)
    {
        auto Byte = Text[i];
        auto Start = i;

        if (Byte == 0xCC)
        {
            while (Byte == 0xCC && i < Text.size())
            {
                Byte = Text[i];
                i++;
            }

            auto const Size = static_cast<std::ptrdiff_t>(i - Start) - 1;
            if (Size >= IndexMinSize)
            {
                auto const Address = reinterpret_cast<std::intptr_t>(Text.data() + Start);
                Caves.push_back(CCaveInfo{ .Address = Address, .Size = Size });

                IndexMaxSize = std::max(IndexMaxSize, Size);
            }
        }
        else
        {
            i++;
        }
    }

    Occupancy.insert(Occupancy.begin(), Caves.size(), false);
}

CaveIndexer::CCaveInfo const* CaveIndexer::GetForSize(std::ptrdiff_t const MinSize)
{
    if (MinSize <= IndexMaxSize)
    {
        MILAN_CHECKW(Caves.size() == Occupancy.size(), L"");
        for (size_t i = 0; i < Caves.size(); ++i)
        {
            if (!Occupancy[i])
            {
                Occupancy[i] = true;
                return &Caves[i];
            }
        }
    }

    return nullptr;
}

std::span<unsigned char> CaveIndexer::ViewMainModule()
{
    DWORD const CurrentProcess = GetCurrentProcessId();
    HANDLE const ProcessSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, CurrentProcess);

    BYTE* BaseAddress = nullptr;
    size_t ModuleLength = 0;

    if (ProcessSnapshot != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32 ModuleEntry{};
        ModuleEntry.dwSize = sizeof ModuleEntry;

        if (Module32First(ProcessSnapshot, &ModuleEntry))
        {
            do
            {
                char const* const ModuleName = ModuleEntry.szModule;
                if (ModuleName == std::strstr(ModuleName, "MassEffect3"))
                {
                    BaseAddress = ModuleEntry.modBaseAddr;
                    ModuleLength = ModuleEntry.modBaseSize;
                    break;
                }
            } while (Module32Next(ProcessSnapshot, &ModuleEntry));
        }

        CloseHandle(ProcessSnapshot);
    }

    MILAN_CHECKW(BaseAddress != nullptr, L"failed to locate main module");
    return std::span<unsigned char>{ BaseAddress, ModuleLength };
}


// ! MiddleIntercept implementation.
// ========================================

MiddleIntercept::MiddleIntercept(wchar_t const* const InLabel, void* const InAddress,
    void* const InStubFunction, CaveIndexer* const InCaveIndexer)
{
    MILAN_CHECKW(InLabel != nullptr, L"");
    MILAN_CHECKW(::wcslen(InLabel) > 0, L"");
    Label = InLabel;

    MILAN_CHECKW(InAddress != nullptr, L"");
    MILAN_CHECKW(InStubFunction != nullptr, L"");
    MILAN_CHECKW(InCaveIndexer != nullptr, L"");
    InitInternal(InAddress, InStubFunction, InCaveIndexer);
}

MiddleIntercept::~MiddleIntercept() noexcept
{
    // ...
}

void MiddleIntercept::InitInternal(void* const InAddress,
    void* const InStubFunction, CaveIndexer* const InCaveIndexer)
{
    MILAN_UNUSED_3(InAddress, InStubFunction, InCaveIndexer);

    // 1. Replace original instruction with jump to relay cave, nop out the rest.
    // 2. In relay cave, execute original instruction and jump to code cave.
    // 3. In code cave, push rax | movabs rax, <stub> | call rax | pop rax | jmp <back>.
}



void MiddleIntercept::UpdateProtect(void* const InAddress, size_t const Length, bool const bEnable) const
{
    DWORD Previous = 0u;

    if (bEnable)
    {
        if (::VirtualProtect(InAddress, Length, PAGE_EXECUTE_READ, &Previous) == FALSE)
            MILAN_FATALW(L"failed to protect memory, addr = %p, label = %s, error = %d", InAddress, *Label, ::GetLastError());
    }
    else
    {
        if (::VirtualProtect(InAddress, Length, PAGE_READWRITE, &Previous) == FALSE)
            MILAN_FATALW(L"failed to unprotect memory, addr = %p, label = %s, error = %d", InAddress, *Label, ::GetLastError());
    }
}

void MiddleIntercept::EncodeRequest(ZydisEncoderRequest* const Request, unsigned char*& Buffer) const
{
    EncodeRequestEx(Request, Buffer, Buffer);
}

void MiddleIntercept::EncodeRequestEx(ZydisEncoderRequest* Request, unsigned char*& Buffer, unsigned char*& Address) const
{
    MILAN_CHECKW(Request != nullptr, L"");
    MILAN_CHECKW(Buffer != nullptr, L"");

    ZyanUSize   Length = 64;
    ZyanU64     AddressU64 = reinterpret_cast<ZyanU64>(Address);

    if (ZyanStatus const Status = ZydisEncoderEncodeInstructionAbsolute(Request, Buffer, &Length, AddressU64); ZYAN_FAILED(Status))
        MILAN_FATALW(L"failed to encode instruction, label = %s, status = %u", *Label, Status & 0xFFFFF);

    std::memset(Request, 0, sizeof *Request);
    Buffer += Length;

    // Avoid moving cursor twice when encoding to the actual location.
    if (&Buffer != &Address) Address += Length;
}

void MiddleIntercept::DebugDisassemble(unsigned char* const Pointer, size_t Length)
{
    MILAN_CHECKW(Pointer != nullptr, L"");
    MILAN_CHECKW(Length != 0, L"");

    ZydisDisassembledInstruction Instruction{};
    ZyanU64 Address = reinterpret_cast<ZyanU64>(Pointer);

    while (ZYAN_SUCCESS(ZydisDisassembleIntel(ZYDIS_MACHINE_MODE_LONG_64, Address, Pointer, Length, &Instruction)))
    {
        MILAN_DEBUGW(L" %016llu %S", Address, Instruction.text);
        
        if (Instruction.info.length >= Length)
            break;

        Address += Instruction.info.length;
        Length -= Instruction.info.length;
    }
}

size_t MiddleIntercept::GetPageSize() noexcept
{
    static size_t PageSize = SIZE_MAX;

    if (PageSize == SIZE_MAX)
    {
        SYSTEM_INFO SystemInfo;
        ::GetSystemInfo(&SystemInfo);
        PageSize = SystemInfo.dwPageSize;
    }

    return PageSize;
}
