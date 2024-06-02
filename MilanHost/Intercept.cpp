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
    Init(InAddress, InStubFunction, InCaveIndexer);
}

MiddleIntercept::~MiddleIntercept() noexcept
{
    // ...
}

void MiddleIntercept::Init(void* const InAddress,
    void* const InStubFunction, CaveIndexer* const InCaveIndexer)
{
    Function = InStubFunction;
    Indexer = InCaveIndexer;

    // 1. Replace original instruction with jump to relay cave, nop out the rest.
    // 2. In relay cave, execute original instructions and jump to call cave.
    // 3. In call cave, push rax | movabs rax, <stub> | call rax | pop rax | jmp <back>.

    auto const [RelayCaveAddress, RelayCaveSize] = *Indexer->GetForSize(20);
    auto const [CallCaveAddress, CallCaveSize] = *Indexer->GetForSize(20);

    unsigned char* RelayCursor = reinterpret_cast<unsigned char*>(RelayCaveAddress);
    unsigned char* CallCursor = reinterpret_cast<unsigned char*>(CallCaveAddress);

    Address = reinterpret_cast<unsigned char*>(InAddress);
    unsigned char* AddressCursor = Address;

    // 1. Get size of a jump to relay cave.

    ZydisEncoderRequest FirstJumpRequest{};
    std::memset(&FirstJumpRequest, 0, sizeof FirstJumpRequest);

    FirstJumpRequest.mnemonic = ZYDIS_MNEMONIC_JMP;
    FirstJumpRequest.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    FirstJumpRequest.operand_count = 1;
    FirstJumpRequest.operands[0].type = ZYDIS_OPERAND_TYPE_MEMORY;
    FirstJumpRequest.operands[0].mem.base = ZYDIS_REGISTER_RIP;
    FirstJumpRequest.operands[0].mem.displacement = RelayCaveAddress;  // Zydis will transform it to relative displacement.

    std::array<unsigned char, 32> FirstJumpTemporary{ 0u };
    ZyanUSize FirstJumpSize = FirstJumpTemporary.size();

    ZyanStatus const FirstJumpStatus = ZydisEncoderEncodeInstructionAbsolute(&FirstJumpRequest,
        FirstJumpTemporary.data(), &FirstJumpSize, reinterpret_cast<ZyanU64>(Address));
    MILAN_CHECKW(ZYAN_SUCCESS(FirstJumpStatus), L"failed to encode first jump, status = %d", (FirstJumpStatus & 0xFFFFF));

    // 2. Decode necessary instructions at location and re-encode them into the relay cave.

    ZydisDecoder                Decoder{};
    ZydisDecodedInstruction     Instruction{};
    ZydisDecodedOperand         Operands[ZYDIS_MAX_OPERAND_COUNT_VISIBLE];
    ZydisEncoderRequest         Request{};

    ZydisDecoderInit(&Decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    while (AddressCursor < Address + FirstJumpSize)
    {
        std::memset(&Instruction, 0, sizeof Instruction);
        std::memset(Operands, 0, sizeof Operands);
        std::memset(&Request, 0, sizeof Request);

        ZyanStatus const DecodeStatus = ZydisDecoderDecodeFull(&Decoder, AddressCursor, 64, &Instruction, Operands);
        MILAN_CHECKW(ZYAN_SUCCESS(DecodeStatus), L"failed to decode original instruction, status = %d", (DecodeStatus & 0xFFFFF));

        // FIXME: Convert decoded instruction addresses to absolute.

        ZyanStatus const ConvStatus = ZydisEncoderDecodedInstructionToEncoderRequest(&Instruction, Operands, Instruction.operand_count, &Request);
        MILAN_CHECKW(ZYAN_SUCCESS(ConvStatus), L"failed to convert decoded instruction to request, status = %d", (ConvStatus & 0xFFFFF));

        ZyanUSize EncodeLength = RelayCaveSize;
        ZyanStatus const EncodeStatus = ZydisEncoderEncodeInstructionAbsolute(&Request, RelayCursor, &EncodeLength, reinterpret_cast<ZyanU64>(RelayCursor));
        MILAN_CHECKW(ZYAN_SUCCESS(EncodeStatus), L"failed to encode decoded instruction, status = %d", (EncodeStatus & 0xFFFFF));

        AddressCursor += Instruction.length;
        RelayCursor += EncodeLength;
    }

    // 3. Encode a jump from the relay cave to the call cave.

    std::memset(&Request, 0, sizeof Request);  // re-use this chonky variable

    Request.mnemonic = ZYDIS_MNEMONIC_JMP;
    Request.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    Request.operand_count = 1;
    Request.operands[0].type = ZYDIS_OPERAND_TYPE_MEMORY;
    Request.operands[0].mem.base = ZYDIS_REGISTER_RIP;
    Request.operands[0].mem.displacement = CallCaveAddress;  // Zydis will transform it to relative displacement.

    ZyanUSize SecondJumpLength = RelayCaveSize - (reinterpret_cast<std::intptr_t>(RelayCursor) - RelayCaveAddress);
    ZyanStatus const SecondJumpStatus = ZydisEncoderEncodeInstructionAbsolute(&Request, RelayCursor, &SecondJumpLength, reinterpret_cast<ZyanU64>(RelayCursor));
    MILAN_CHECKW(ZYAN_SUCCESS(SecondJumpStatus), L"failed to encode second jump, status = %d", (SecondJumpStatus & 0xFFFFF));

    RelayCursor += SecondJumpLength;

    // 4. Nop out injection location and copy in the jump to the relay cave.

    auto const ReplaceLength = AddressCursor - Address;

    std::fill_n(Address, ReplaceLength, unsigned char (0x90));
    std::copy_n(FirstJumpTemporary.data(), FirstJumpSize, Address);

    unsigned char* const JumpBackAddress = AddressCursor;  // jumping back to the first untouched instruction

    // 5. TODO: Encode the call cave.

    MILAN_UNUSED_2(JumpBackAddress, CallCursor);

    // 6. Cleanup temporaries.

    Indexer = nullptr;
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

void MiddleIntercept::DebugDisassemble(unsigned char* const Pointer, size_t Length)
{
    MILAN_CHECKW(Pointer != nullptr, L"");
    MILAN_CHECKW(Length != 0, L"");

    ZydisDisassembledInstruction Instruction{};
    ZyanU64 PtrAddress = reinterpret_cast<ZyanU64>(Pointer);

    while (ZYAN_SUCCESS(ZydisDisassembleIntel(ZYDIS_MACHINE_MODE_LONG_64, PtrAddress, Pointer, Length, &Instruction)))
    {
        MILAN_DEBUGW(L" %016llu %S", PtrAddress, Instruction.text);
        
        if (Instruction.info.length >= Length)
            break;

        PtrAddress += Instruction.info.length;
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
