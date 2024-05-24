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


// ! MiddleIntercept implementation.
// ========================================

MiddleIntercept::MiddleIntercept(wchar_t const* const InLabel) noexcept
{
    MILAN_CHECKW(InLabel != nullptr, L"");
    MILAN_CHECKW(::wcslen(InLabel) > 0, L"");
    Label = InLabel;
}

MiddleIntercept::MiddleIntercept(wchar_t const* const InLabel, void* const InAddress, void* const InStubFunction)
    : MiddleIntercept{ InLabel }
{
    MILAN_CHECKW(InAddress != nullptr, L"");
    MILAN_CHECKW(InStubFunction != nullptr, L"");
    InitInternal(InAddress, InStubFunction);
}

MiddleIntercept::~MiddleIntercept() noexcept
{
    if (AllocatedAddress != nullptr)
    {
        MILAN_CHECKW(AllocatedSize != 0, L"");
        ::VirtualFree(AllocatedAddress, 0, MEM_RELEASE);
        AllocatedCursor = AllocatedAddress = nullptr;
        AllocatedSize = 0;
    }
}

void MiddleIntercept::InitInternal(void* const InAddress, void* const InStubFunction)
{
    MILAN_CHECKW(InterceptAddress == nullptr, L"");
    MILAN_CHECKW(JumpBackAddress == nullptr, L"");

    AllocatedSize = GetPageSize();
    AllocatedAddress = (unsigned char*)::VirtualAlloc(nullptr, AllocatedSize, MEM_COMMIT, PAGE_READWRITE);
    AllocatedCursor = AllocatedAddress;

    InterceptAddress = (unsigned char*)InAddress;
    StubFunction = InStubFunction;

    WriteCallStub();
    WriteInstallInstrs();
    WriteJumpBack();

    UpdateProtect(AllocatedAddress, AllocatedSize, true);
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

void MiddleIntercept::EncodeInternal(ZydisEncoderRequest* const Request, unsigned char*& Buffer, const wchar_t* const Desc) const
{
    EncodeInternalAt(Request, Buffer, Buffer, Desc);
}

void MiddleIntercept::EncodeInternalAt(ZydisEncoderRequest* Request, unsigned char*& Buffer, unsigned char*& Address, const wchar_t* Desc) const
{
    MILAN_CHECKW(Request != nullptr, L"");
    MILAN_CHECKW(Buffer != nullptr, L"");
    MILAN_CHECKW(Desc != nullptr, L"");

    ZyanUSize Length = 0;
    ZyanU64 const AddressU64 = reinterpret_cast<ZyanU64>(Address);

    if (ZyanStatus const Status = ZydisEncoderEncodeInstructionAbsolute(Request, Buffer, &Length, AddressU64); ZYAN_FAILED(Status))
        MILAN_FATALW(L"failed to encode instruction, label = %s / %s, status = %d", *Label, Desc, Status);

    std::memset(Request, 0, sizeof(ZydisEncoderRequest));

    Buffer += Length;

    if (&Buffer != &Address)
        Address += Length;
}

void MiddleIntercept::WriteCallStub()
{
    ZydisEncoderRequest Request{};
    std::memset(&Request, 0, sizeof Request);

    Request.mnemonic = ZYDIS_MNEMONIC_MOV;
    Request.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    Request.operand_count = 2;
    Request.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
    Request.operands[0].reg.value = ZYDIS_REGISTER_RAX;
    Request.operands[1].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
    Request.operands[1].imm.s = (std::ptrdiff_t)StubFunction;

    EncodeInternal(&Request, AllocatedCursor, L"mov-1");

    Request.mnemonic = ZYDIS_MNEMONIC_CALL;
    Request.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    Request.operand_count = 1;
    Request.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
    Request.operands[0].reg.value = ZYDIS_REGISTER_RAX;

    EncodeInternal(&Request, AllocatedCursor, L"call-1");
}

void MiddleIntercept::WriteInstallInstrs()
{
    // First, encode the jump from intercepted address to the code cave, into a temporary buffer.

    std::array<unsigned char, 128> InitJumpBuffer{ 0u };

    unsigned char* InitJumpCursor = InitJumpBuffer.data();
    unsigned char* InterceptCursor = InterceptAddress;

    {
        ZydisEncoderRequest InitJumpRequest{};
        std::memset(&InitJumpRequest, 0, sizeof InitJumpRequest);

        InitJumpRequest.mnemonic = ZYDIS_MNEMONIC_MOV;
        InitJumpRequest.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
        InitJumpRequest.operand_count = 2;
        InitJumpRequest.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
        InitJumpRequest.operands[0].reg.value = ZYDIS_REGISTER_RAX;
        InitJumpRequest.operands[1].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
        InitJumpRequest.operands[1].imm.s = (std::ptrdiff_t)AllocatedAddress;

        EncodeInternalAt(&InitJumpRequest, InitJumpCursor, InterceptCursor, L"mov-2");

        InitJumpRequest.mnemonic = ZYDIS_MNEMONIC_JMP;
        InitJumpRequest.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
        InitJumpRequest.operand_count = 1;
        InitJumpRequest.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
        InitJumpRequest.operands[0].reg.value = ZYDIS_REGISTER_RAX;

        EncodeInternalAt(&InitJumpRequest, InitJumpCursor, InterceptCursor, L"jmp-2");
    }

    size_t const InitJumpSize = InitJumpCursor - InitJumpBuffer.data();


    // Second, relocate all instructions touching the initial jump into the code cave.

    ZydisDecoder Decoder{};
    ZydisDecoderInit(&Decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    unsigned char* OrigCodeCursor = InterceptAddress;
    while (OrigCodeCursor < InterceptAddress + InitJumpSize)
    {
        ZydisDecodedInstruction Instruction{};
        ZydisDecodedOperand Operands[ZYDIS_MAX_OPERAND_COUNT]{};

        std::memset(&Instruction, 0, sizeof Instruction);
        std::memset(Operands, 0, sizeof Operands);

        ZyanStatus const DecodeStatus = ZydisDecoderDecodeFull(&Decoder, OrigCodeCursor, 128, &Instruction, Operands);
        MILAN_CHECKW(ZYAN_SUCCESS(DecodeStatus), L"failed to decode instruction at %p", (void*)OrigCodeCursor);

        ZydisEncoderRequest Request{};
        std::memset(&Request, 0, sizeof Request);

        ZyanStatus const ConvStatus = ZydisEncoderDecodedInstructionToEncoderRequest(&Instruction, Operands, Instruction.operand_count, &Request);
        MILAN_CHECKW(ZYAN_SUCCESS(ConvStatus), L"failed to convert decoded instruction at %p into encoder request", (void*)OrigCodeCursor);

        EncodeInternal(&Request, AllocatedCursor, L"any-3");
        OrigCodeCursor += Instruction.length;
    }

    // Third, now that we know how much bytes the initial jump takes,
    // plus all the touched instructions, nop out the intercept location,
    // and insert the pre-encoded jump instructions at it.

    JumpBackAddress = OrigCodeCursor;
    size_t const InterceptTouchedSize = JumpBackAddress - InterceptAddress;

    UpdateProtect(InterceptAddress, InterceptTouchedSize, false);

    // Cheat a little by hardcoding 0x90 NOP instructions...
    std::fill_n(InterceptAddress, InterceptTouchedSize, static_cast<unsigned char>(0x90u));
    std::copy_n(InitJumpBuffer.begin(), InitJumpSize, InterceptAddress);

    UpdateProtect(InterceptAddress, InterceptTouchedSize, true);
}

void MiddleIntercept::WriteJumpBack()
{
    ZydisEncoderRequest Request{};
    std::memset(&Request, 0, sizeof Request);

    Request.mnemonic = ZYDIS_MNEMONIC_MOV;
    Request.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    Request.operand_count = 2;
    Request.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
    Request.operands[0].reg.value = ZYDIS_REGISTER_RAX;
    Request.operands[1].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
    Request.operands[1].imm.s = (std::ptrdiff_t)JumpBackAddress;

    EncodeInternal(&Request, AllocatedCursor, L"mov-3");

    Request.mnemonic = ZYDIS_MNEMONIC_JMP;
    Request.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    Request.operand_count = 1;
    Request.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
    Request.operands[0].reg.value = ZYDIS_REGISTER_RAX;

    EncodeInternal(&Request, AllocatedCursor, L"jmp-3");
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
