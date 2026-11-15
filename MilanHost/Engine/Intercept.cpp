#include <tlhelp32.h>

#include "Base/Base.hpp"
#include "Engine/ExtObject.hpp"
#include "Engine/Intercept.hpp"


// ! Located global variables.
// ========================================

UProperty**         pGProperty = nullptr;
UObject**           pGPropObject = nullptr;
BYTE**              pGPropAddr = nullptr;
unsigned int*       pGRuntimeUCFlags = nullptr;


// ! FunctionNamedIntercept implementation.
// ========================================

TArray<FunctionNamedIntercept*> FunctionNamedIntercept::RegisteredIntercepts;

FunctionNamedIntercept::FunctionNamedIntercept()
{
    // This requires all instances to be static.
    RegisteredIntercepts.Add(this);
}

bool FunctionNamedIntercept::Applies (
    UStruct* const Node,
    wchar_t const* const PackageName,
    wchar_t const* const ClassName,
    wchar_t const* const FunctionName
)
{
    MILAN_CHECKW(Node != nullptr, L"");

    if (!FunctionDescriptor)
    {
        MILAN_CHECKW(PackageName != nullptr, L"");
        MILAN_CHECKW(ClassName != nullptr, L"");
        MILAN_CHECKW(FunctionName != nullptr, L"");

        FunctionDescriptor = std::make_unique<TargetDescriptor>();
        FunctionDescriptor->Package = SFXName(PackageName, 0);
        FunctionDescriptor->Class = SFXName(ClassName, 0);
        FunctionDescriptor->Function = SFXName(FunctionName, 0);
    }

    // Performance - check only name of function before doing more
    if (!(Node->Name == FunctionDescriptor->Function))
    {
        return false;
    }

    UObject* const Outer = Node->Outer;
    if (Outer == nullptr || !(Outer->Name == FunctionDescriptor->Class))
    {
        // Performance - check only name of class before doing more
        return false;
    }

    UPackage* const Package = Node->GetOutermost();
    return Package != nullptr && Package->Name == FunctionDescriptor->Package;
}

void FunctionNamedIntercept::InvalidateRegistered()
{
    for (FunctionNamedIntercept* Intercept : RegisteredIntercepts)
    {
        MILAN_CHECKW(Intercept->FunctionDescriptor, L"");
        Intercept->FunctionDescriptor = nullptr;
    }
}

void FunctionNamedIntercept::CleanupRegistered()
{
    RegisteredIntercepts.Clear();
    RegisteredIntercepts.Shrink();
}


// ! FunctionStructIntercept implementation.
// ========================================

TArray<FunctionStructIntercept*> FunctionStructIntercept::RegisteredIntercepts;

FunctionStructIntercept::FunctionStructIntercept()
{
    // This requires all instances to be static.
    RegisteredIntercepts.Add(this);
}

bool FunctionStructIntercept::Applies(UStruct* const Node, wchar_t const* const FullName)
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

        // Performance
        // A miss is a full object scan on every script call
        // Wait until we have a different count of objects before trying to find the target node again
        UINT const ObjectCount = UObject::GObjObjects->Count();
        if (MissedObjectCount == ObjectCount)
        {
            return false;
        }
        TargetNode = UObject::FindObject<UStruct>(TargetFullName);
        MissedObjectCount = TargetNode == nullptr ? ObjectCount : 0;
    }

    return TargetNode == Node;
}

void FunctionStructIntercept::RetryMissing()
{
    for (FunctionStructIntercept* Intercept : RegisteredIntercepts)
    {
        Intercept->MissedObjectCount = 0;
    }
}

void FunctionStructIntercept::InvalidateRegistered()
{
    for (FunctionStructIntercept* Intercept : RegisteredIntercepts)
    {
        MILAN_CHECKW(Intercept->TargetFullName != nullptr, L"");
        Intercept->TargetNode = nullptr;
        Intercept->MissedObjectCount = 0;
    }
}

void FunctionStructIntercept::CleanupRegistered()
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
    MILAN_CHECKW(MinSize > 0, L"");

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
                if (ModuleName == std::strstr(ModuleName, "MassEffectMultiplayer"))
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


// ! BinaryInjection implementation.
// ========================================

#ifndef _FINAL_RELEASE

BinaryInjection::BinaryInjection(wchar_t const* const InLabel, void* const InAddress,
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

BinaryInjection::~BinaryInjection() noexcept
{
    // ...
}

void BinaryInjection::Install(wchar_t const* const InLabel, int const InRVA, void* const InStubFunction, CaveIndexer& InCaveIndexer)
{
    auto* const MainBase = reinterpret_cast<unsigned char*>(GetMainBase());
    BinaryInjection Instance{ InLabel, MainBase + InRVA, InStubFunction, &InCaveIndexer };
    MILAN_TRACEW(ELOG_StartupGlobals, L"installed intercept '%s' (0x%p)", InLabel, (void*)Instance.Address);
    MILAN_UNUSED(Instance);
}

void BinaryInjection::Init(void* const InAddress,
    void* const InStubFunction, CaveIndexer* const InCaveIndexer)
{
    Function = InStubFunction;
    Indexer = InCaveIndexer;

    Address = reinterpret_cast<unsigned char*>(InAddress);
    unsigned char* AddressCursor = Address;

    // > Replace original instruction with jump to call cave, nop out the rest.
    // > In call cave, push rax | movabs rax, <stub> | call rax | pop rax | jmp <relay>.
    // > In relay cave, execute original instructions and jump to <back>.

    auto const CallCave = *Indexer->GetForSize(20);
    auto const RelayCave = *Indexer->GetForSize(20);

    UpdateProtect(CallCave, false);
    UpdateProtect(RelayCave, false);

    unsigned char* CallCursor = reinterpret_cast<unsigned char*>(CallCave.Address);
    unsigned char* RelayCursor = reinterpret_cast<unsigned char*>(RelayCave.Address);

    std::ptrdiff_t CallAvailLength = CallCave.Size;
    std::ptrdiff_t RelayAvailLength = RelayCave.Size;

    // 1. Get size of a jump to call cave by encoding it to a temporary buffer.

    ZydisEncoderRequest Request{};  // quite chonky, so will be re-used at later stages
    std::memset(&Request, 0, sizeof Request);

    Request.mnemonic = ZYDIS_MNEMONIC_JMP;
    Request.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    Request.operand_count = 1;
    Request.operands[0].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
    Request.operands[0].imm.s = CallCave.Address;  // Zydis will transform it to relative displacement

    std::array<unsigned char, 32> FirstJumpTemporary{ 0u };
    ZyanUSize FirstJumpSize = FirstJumpTemporary.size();

    ZyanStatus const FirstJumpStatus = ZydisEncoderEncodeInstructionAbsolute(&Request, FirstJumpTemporary.data(), &FirstJumpSize, reinterpret_cast<ZyanU64>(Address));
    MILAN_CHECKW(ZYAN_SUCCESS(FirstJumpStatus), L"failed to encode first jump, status = %d", (FirstJumpStatus & 0xFFFFF));

    // 2. Decode necessary instructions at location and re-encode them into the relay cave.

    ZydisDecoder                Decoder{};
    ZydisDecodedInstruction     Instruction{};
    ZydisDecodedOperand         Operands[ZYDIS_MAX_OPERAND_COUNT]{};

    ZydisDecoderInit(&Decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    while (AddressCursor < Address + FirstJumpSize)
    {
        std::memset(&Instruction, 0, sizeof Instruction);
        std::memset(&Operands[0], 0, sizeof Operands);
        std::memset(&Request, 0, sizeof Request);

        ZyanStatus const DecodeStatus = ZydisDecoderDecodeFull(&Decoder, AddressCursor, 64, &Instruction, &Operands[0]);
        MILAN_CHECKW(ZYAN_SUCCESS(DecodeStatus), L"failed to decode original instruction, status = %d", (DecodeStatus & 0xFFFFF));

        for (ZyanU8 OpIdx = 0; OpIdx < Instruction.operand_count; ++OpIdx)
        {
            ZydisDecodedOperand& Op = Operands[OpIdx];
            ZyanU64 const OpAddr = reinterpret_cast<ZyanU64>(AddressCursor);

            if (Op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && Op.imm.is_relative)
            {
                ZyanU64 Absolute = 0;
                ZyanStatus const Status = ZydisCalcAbsoluteAddress(&Instruction, &Op, OpAddr, &Absolute);
                MILAN_CHECKW(ZYAN_SUCCESS(Status), L"failed to calc absolute address for operand %d, status = %d", OpIdx, (Status & 0xFFFFF));

                //Op.imm.is_relative = false;
                Op.imm.value.u = Absolute;
            }
            else if (Op.type == ZYDIS_OPERAND_TYPE_MEMORY && (Op.mem.base == ZYDIS_REGISTER_EIP || Op.mem.base == ZYDIS_REGISTER_RIP))
            {
                ZyanU64 Absolute = 0;
                ZyanStatus const Status = ZydisCalcAbsoluteAddress(&Instruction, &Op, OpAddr, &Absolute);
                MILAN_CHECKW(ZYAN_SUCCESS(Status), L"failed to calc absolute address for operand %d, status = %d", OpIdx, (Status & 0xFFFFF));

                MILAN_CHECKW(Op.mem.disp.has_displacement, L"");
                Op.mem.disp.value = static_cast<ZyanI64>(Absolute);
            }
            //else if (Op.type == ZYDIS_OPERAND_TYPE_MEMORY && Op.mem. ...)  // might be another case necessary to handle
        }

        ZyanStatus const ConvStatus = ZydisEncoderDecodedInstructionToEncoderRequest(&Instruction, Operands, Instruction.operand_count_visible, &Request);
        MILAN_CHECKW(ZYAN_SUCCESS(ConvStatus), L"failed to convert decoded instruction to request, status = %d", (ConvStatus & 0xFFFFF));

        ZyanUSize EncodeLength = static_cast<ZyanUSize>(RelayAvailLength);
        ZyanStatus const EncodeStatus = ZydisEncoderEncodeInstructionAbsolute(&Request, RelayCursor, &EncodeLength, reinterpret_cast<ZyanU64>(RelayCursor));
        MILAN_CHECKW(ZYAN_SUCCESS(EncodeStatus), L"failed to encode decoded instruction, status = %d", (EncodeStatus & 0xFFFFF));

        MILAN_CHECKW(EncodeLength == Instruction.length, L"");

        RelayCursor += EncodeLength;
        RelayAvailLength -= EncodeLength;
        AddressCursor += Instruction.length;
    }

    // 3. Encode a jump from the relay cave back to original code.

    std::memset(&Request, 0, sizeof Request);

    Request.mnemonic = ZYDIS_MNEMONIC_JMP;
    Request.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    Request.operand_count = 1;
    Request.operands[0].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
    Request.operands[0].imm.s = reinterpret_cast<ZyanI64>(AddressCursor);  // Zydis will transform it to relative displacement.

    ZyanUSize SecondJumpLength = static_cast<ZyanUSize>(RelayAvailLength);
    ZyanStatus const SecondJumpStatus = ZydisEncoderEncodeInstructionAbsolute(&Request, RelayCursor, &SecondJumpLength, reinterpret_cast<ZyanU64>(RelayCursor));
    MILAN_CHECKW(ZYAN_SUCCESS(SecondJumpStatus), L"failed to encode second jump, status = %d", (SecondJumpStatus & 0xFFFFF));

    RelayCursor += SecondJumpLength;
    RelayAvailLength -= SecondJumpLength;

    // 4. Nop out injection location and copy in the jump to the call cave.

    auto const ReplaceLength = AddressCursor - Address;
    UpdateProtect(Address, ReplaceLength, false);
    std::fill_n(Address, ReplaceLength, unsigned char (0x90));
    std::copy_n(FirstJumpTemporary.data(), FirstJumpSize, Address);
    UpdateProtect(Address, ReplaceLength, true);

    // 5. Encode the call cave.

    EncodeCallCave(&Request, reinterpret_cast<void*>(RelayCave.Address), CallCursor, CallAvailLength);

    // 6. Clean up stuff.

    UpdateProtect(RelayCave, true);
    UpdateProtect(CallCave, true);

    Indexer = nullptr;
}

void BinaryInjection::EncodeCallCave(ZydisEncoderRequest* const Scratch, void* const RelayAddress,
    unsigned char*& CallCursor, std::ptrdiff_t& CallAvailLength) const
{
    std::memset(Scratch, 0, sizeof *Scratch);

    ZyanStatus  Status = 0;
    ZyanU64     InstrLength = 0;

    // 1. PUSH RAX

    Scratch->mnemonic = ZYDIS_MNEMONIC_PUSH;
    Scratch->machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    Scratch->operand_count = 1;
    Scratch->operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
    Scratch->operands[0].reg.value = ZYDIS_REGISTER_RAX;

    InstrLength = CallAvailLength;
    Status = ZydisEncoderEncodeInstructionAbsolute(Scratch, CallCursor, &InstrLength, reinterpret_cast<ZyanU64>(CallCursor));
    MILAN_CHECKW(ZYAN_SUCCESS(Status), L"failed to encode a call cave (%s), status = %d", L"push rax", (Status & 0xFFFFF));
    std::memset(Scratch, 0, sizeof *Scratch);
    CallCursor += InstrLength;
    CallAvailLength -= InstrLength;

    // 2. MOV RAX, <Function>

    Scratch->mnemonic = ZYDIS_MNEMONIC_MOV;
    Scratch->machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    Scratch->operand_count = 2;
    Scratch->operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
    Scratch->operands[0].reg.value = ZYDIS_REGISTER_RAX;
    Scratch->operands[1].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
    Scratch->operands[1].imm.u = reinterpret_cast<ZyanU64>(Function);

    InstrLength = CallAvailLength;
    Status = ZydisEncoderEncodeInstructionAbsolute(Scratch, CallCursor, &InstrLength, reinterpret_cast<ZyanU64>(CallCursor));
    MILAN_CHECKW(ZYAN_SUCCESS(Status), L"failed to encode a call cave (%s), status = %d", L"mov rax, <func>", (Status & 0xFFFFF));
    std::memset(Scratch, 0, sizeof *Scratch);
    CallCursor += InstrLength;
    CallAvailLength -= InstrLength;

    // 3. CALL RAX

    Scratch->mnemonic = ZYDIS_MNEMONIC_CALL;
    Scratch->machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    Scratch->operand_count = 1;
    Scratch->operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
    Scratch->operands[0].reg.value = ZYDIS_REGISTER_RAX;

    InstrLength = CallAvailLength;
    Status = ZydisEncoderEncodeInstructionAbsolute(Scratch, CallCursor, &InstrLength, reinterpret_cast<ZyanU64>(CallCursor));
    MILAN_CHECKW(ZYAN_SUCCESS(Status), L"failed to encode a call cave (%s), status = %d", L"call rax", (Status & 0xFFFFF));
    std::memset(Scratch, 0, sizeof *Scratch);
    CallCursor += InstrLength;
    CallAvailLength -= InstrLength;

    // 4. POP RAX

    Scratch->mnemonic = ZYDIS_MNEMONIC_POP;
    Scratch->machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    Scratch->operand_count = 1;
    Scratch->operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
    Scratch->operands[0].reg.value = ZYDIS_REGISTER_RAX;

    InstrLength = CallAvailLength;
    Status = ZydisEncoderEncodeInstructionAbsolute(Scratch, CallCursor, &InstrLength, reinterpret_cast<ZyanU64>(CallCursor));
    MILAN_CHECKW(ZYAN_SUCCESS(Status), L"failed to encode a call cave (%s), status = %d", L"pop rax", (Status & 0xFFFFF));
    std::memset(Scratch, 0, sizeof *Scratch);
    CallCursor += InstrLength;
    CallAvailLength -= InstrLength;

    // 5. JMP <RelayAddress>

    Scratch->mnemonic = ZYDIS_MNEMONIC_JMP;
    Scratch->machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    Scratch->operand_count = 1;
    Scratch->operands[0].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
    Scratch->operands[0].imm.u = reinterpret_cast<ZyanU64>(RelayAddress);  // Zydis will transform it to relative

    InstrLength = CallAvailLength;
    Status = ZydisEncoderEncodeInstructionAbsolute(Scratch, CallCursor, &InstrLength, reinterpret_cast<ZyanU64>(CallCursor));
    MILAN_CHECKW(ZYAN_SUCCESS(Status), L"failed to encode a call cave (%s), status = %d", L"jmp <back>", (Status & 0xFFFFF));
    std::memset(Scratch, 0, sizeof *Scratch);
    CallCursor += InstrLength;
    CallAvailLength -= InstrLength;
}


void BinaryInjection::UpdateProtect(void* const InAddress, size_t const Length, bool const bEnable) const
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

void BinaryInjection::UpdateProtect(CaveIndexer::CCaveInfo const& InCave, bool const bEnable) const
{
    UpdateProtect(reinterpret_cast<void*>(InCave.Address), InCave.Size, bEnable);
}

void BinaryInjection::DebugDisassemble(unsigned char* const Pointer, size_t Length)
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

size_t BinaryInjection::GetPageSize() noexcept
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

std::intptr_t BinaryInjection::GetMainBase() noexcept
{
    static std::intptr_t MainBase = INTPTR_MAX;

    if (MainBase == INTPTR_MAX)
    {
        auto* const MainAddress = ::LESDK::GetMainModuleBase();
        MainBase = reinterpret_cast<std::intptr_t>(MainAddress);
    }

    return MainBase;
}

#endif // !_FINAL_RELEASE


// ! SFXOnlineUtils namespace.
// ========================================

UClass* HijackUtils::FindClass(SFXName const ClassName)
{
    for (TypedObjectIterator<UClass, false, false> Iter{}; Iter; ++Iter)
    {
        if ((*Iter)->Name == ClassName)
            return *Iter;
    }

    return nullptr;
}

UFunction* HijackUtils::FindClassMethod(UClass* const Class, SFXName const FuncName)
{
    for (UField* Child = Class->Children; Child != nullptr; Child = Child->Next)
    {
        UFunction* const CheckFunction = Child->Cast<UFunction>();
        if (CheckFunction != nullptr && CheckFunction->Name == FuncName)
            return CheckFunction;
    }

    return nullptr;
}

void HijackUtils::InstallFunction(UFunction* const Target, void* const Hijack, void** Original)
{
    MILAN_CHECKW(Target != nullptr, L"");
    MILAN_CHECKW(Hijack != nullptr, L"");

    *Original = Target->Func;
    Target->Func = Hijack;

    static constexpr DWORD FUNC_Native = 0x400;
    //if ((Target->FunctionFlags & FUNC_Native) == 0)
    Target->FunctionFlags |= FUNC_Native;
}
