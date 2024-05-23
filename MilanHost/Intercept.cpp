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
    : Label{ InLabel ? InLabel : L"???" }
{
    // ...
}

void MiddleIntercept::Init(void* const Address)
{
    MILAN_CHECKW(FirstJumpSource == nullptr, L"");
    MILAN_CHECKW(SecondJumpDest == nullptr, L"");
    MILAN_CHECKW(Address != nullptr, L"");

    // Allocate a page of memory for the code.

    AllocatedCode = ::VirtualAlloc(nullptr, GetPageSize(), MEM_COMMIT, PAGE_READWRITE);


    // Prepare a jump to the allocated memory.

    std::array<unsigned char, k_maxFirstJump> FirstJumpBuffer{ 0u };
    size_t const FirstJumpSize = EncodeFirstJump(FirstJumpBuffer);


    // Exchange instructions at intercepted location with the prepared jump.

    FirstJumpSource = Address;

    std::array<unsigned char, k_maxFirstJump> FirstJumpOriginalBuffer{ 0u };
    size_t const FirstJumpOriginalSize = InstallFirstJump(
        std::span{ FirstJumpBuffer }.subspan(0, FirstJumpSize),
        FirstJumpOriginalBuffer);

    SecondJumpDest = reinterpret_cast<unsigned char*>(FirstJumpSource) + FirstJumpOriginalSize;


    // TODO: Generate code cave stub.


    // Protect complete code cave.

    UpdateProtect(AllocatedCode, GetPageSize(), true);
}

void MiddleIntercept::EncodeInternal(const wchar_t* Desc,
    ZydisEncoderRequest* const Request, unsigned char*& Buffer) const
{
    MILAN_CHECKW(Request != nullptr, L"");
    MILAN_CHECKW(Buffer != nullptr, L"");

    ZyanUSize Length = 0;
    ZyanStatus const Status = ZydisEncoderEncodeInstruction(Request, Buffer, &Length);

    if (ZYAN_FAILED(Status))
    {
        Desc = Desc ? Desc : L"???";
        MILAN_FATALW(L"failed to encode instruction, label = %s/%s, status = %d", Label, Desc, Status);
    }

    std::memset(Request, 0, sizeof(ZydisEncoderRequest));
    Buffer += Length;
}

void MiddleIntercept::UpdateProtect(void* const Address, size_t const Length, bool const bEnable) const
{
    if (bEnable)
    {
        DWORD PrevProtect{};
        if (::VirtualProtect(Address, Length, PAGE_EXECUTE_READ, &PrevProtect) == FALSE)
            MILAN_FATALW(L"failed to protect memory, address = %p, label = %s, error = %d", Address, Label, ::GetLastError());
    }
    else
    {
        DWORD PrevProtect{};
        if (::VirtualProtect(Address, Length, PAGE_READWRITE, &PrevProtect) == FALSE)
            MILAN_FATALW(L"failed to unprotect memory, address = %p, label = %s, error = %d", Address, Label, ::GetLastError());
    }
}

size_t MiddleIntercept::EncodeFirstJump(std::array<unsigned char, k_maxFirstJump>& Buffer) const
{
    ZydisEncoderRequest FirstJumpRequest{};
    std::memset(&FirstJumpRequest, 0, sizeof FirstJumpRequest);

    unsigned char* CodeEnd = Buffer.data();

    FirstJumpRequest.mnemonic = ZYDIS_MNEMONIC_MOV;
    FirstJumpRequest.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    FirstJumpRequest.operand_count = 2;
    FirstJumpRequest.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
    FirstJumpRequest.operands[0].reg.value = ZYDIS_REGISTER_RAX;
    FirstJumpRequest.operands[1].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
    FirstJumpRequest.operands[1].imm.s = (std::ptrdiff_t)AllocatedCode;

    EncodeInternal(L"mov", &FirstJumpRequest, CodeEnd);

    FirstJumpRequest.mnemonic = ZYDIS_MNEMONIC_JMP;
    FirstJumpRequest.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    FirstJumpRequest.operand_count = 1;
    FirstJumpRequest.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
    FirstJumpRequest.operands[0].reg.value = ZYDIS_REGISTER_RAX;

    EncodeInternal(L"jmp", &FirstJumpRequest, CodeEnd);

    auto const Size = static_cast<size_t>(CodeEnd - Buffer.data());
    MILAN_CHECKW(Size < Buffer.size(), L"");

    return Size;
}

size_t MiddleIntercept::InstallFirstJump(std::span<unsigned char> const JumpBytes,
    std::array<unsigned char, k_maxFirstJump>& OutOrigBuffer) const
{
    // Determine the complete size of all instructions we need to override.

    ZyanUSize Offset = 0;
    ZydisDisassembledInstruction Instruction{};

    while (Offset < JumpBytes.size())
    {
        auto const Status = ZydisDisassembleIntel(ZYDIS_MACHINE_MODE_LONG_64,
            reinterpret_cast<ZyanU64>(FirstJumpSource) + Offset, FirstJumpSource,
            k_maxFirstJump, &Instruction);

        if (ZYAN_FAILED(Status))
        {
            MILAN_FATALW(L"failed to decode instruction, label = %s, status = %d", Label, Status);
        }

        MILAN_TRACEW(L"decoded instruction (label = %s) at %p: %S", Label, (void*)FirstJumpSource, Instruction.text);
        Offset += Instruction.info.length;
    }

    // Now offset contains the number of bytes up to first unmodified instruction.
    // Use it to preserve the original instruction bytes.

    std::copy_n(reinterpret_cast<unsigned char*>(FirstJumpSource), Offset, OutOrigBuffer.begin());

    // Fill all modified instructions with NOP, then copy the jump bytes.
    // Cheat a bit by hardcoding NOP instruction.

    MILAN_CHECKW(JumpBytes.size() <= Offset, L"");

    UpdateProtect(FirstJumpSource, Offset, false);

    std::fill_n(reinterpret_cast<unsigned char*>(FirstJumpSource), Offset, static_cast<unsigned char>(0x90));
    std::copy_n(JumpBytes.begin(), JumpBytes.size(), reinterpret_cast<unsigned char*>(FirstJumpSource));

    UpdateProtect(FirstJumpSource, Offset, true);

    return Offset;
}

size_t MiddleIntercept::GetPageSize() noexcept
{
    static size_t PageSize = SIZE_MAX;

    if (PageSize == PageSize)
    {
        SYSTEM_INFO SystemInfo;
        ::GetSystemInfo(&SystemInfo);
        PageSize = SystemInfo.dwPageSize;
    }

    return PageSize;
}
