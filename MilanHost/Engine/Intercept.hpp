/**
 * @file        MilanHost/Engine/Intercept.hpp
 * @brief       Utilities for intercepting and overriding functions.
 */

#pragma once

#include <array>
#include <span>

#include <Zydis/Zydis.h>
#include <LESDK/Headers.hpp>

#include "Containers/vector.hpp"


// ! Located global variables.
// ========================================

extern UProperty**          pGProperty;
extern UObject**            pGPropObject;
extern BYTE**               pGPropAddr;
extern unsigned int*        pGRuntimeUCFlags;


// ! Unreal Function interception logic.
// ========================================

/**
 * Provides an interface for declaratively hooking an @c UFunction by its package-class-function name triple.
 */
class FunctionNamedIntercept
{
    struct TargetDescriptor
    {
        SFXName Package; SFXName Class; SFXName Function;
        inline bool operator==(TargetDescriptor const& Other) const
        {
            return Package == Other.Package && Class == Other.Class && Function == Other.Function;
        }
    };

    std::unique_ptr<TargetDescriptor> FunctionDescriptor{};

    // This array stores bare pointers because instances of this class are
    // expected to be stored statically, so on cleanup we just clear the array
    // without deallocating individual instances.
    static TArray<FunctionNamedIntercept*> RegisteredIntercepts;

public:

    FunctionNamedIntercept();
    bool Applies(UStruct* Node, wchar_t const* PackageName, wchar_t const* ClassName, wchar_t const* FunctionName);

    static void InvalidateRegistered();
    static void CleanupRegistered();
};

/**
 * Provides an interface for declaratively hooking an @c UFunction by caching its pointer looked up by full name.
 * 
 */
class FunctionStructIntercept
{
    UStruct*            TargetNode{ nullptr };
    wchar_t const*      TargetFullName{ nullptr };
    UINT                MissedObjectCount{ 0 };     // GObjObjects count at the last failed lookup

    // Can't use ::game::vector here because it would pre-allocate
    // in dynamic initializer, i.e. before we have the GMalloc pointer.
    static TArray<FunctionStructIntercept*> RegisteredIntercepts;

public:

    FunctionStructIntercept();
    bool Applies(UStruct* Node, wchar_t const* FullName);

    static void RetryMissing();
    static void InvalidateRegistered();
    static void CleanupRegistered();
};


// ! Binary injection logic.
// ========================================

/**
 * @brief       Self-initializing registry for potential code cave locations.
 * @warning     This might conflict with @c minhook built into @c LEBinkProxy.
 */
class CaveIndexer final : public NonCopyable
{
public:

    struct CCaveInfo
    {
        std::intptr_t Address = 0;
        std::ptrdiff_t Size = 0;
    };

    static constexpr std::ptrdiff_t k_minCaveSize = 8;

    /** Initializes this type and scans main module for @c INT3 sequences. */
    explicit CaveIndexer(std::ptrdiff_t MinSize);

    /**
     * @brief   Retrieves an available cave entry and marks it as occupied.
     * @param   MinSize - Smallest number of bytes the cave must have.
     * @return  Pointer to a cave info, or @c nullptr if there are no caves bigger than @c MinSize.
     */
    CCaveInfo const* GetForSize(std::ptrdiff_t MinSize);

private:

    static std::span<unsigned char> ViewMainModule();

    struct CCaveOccupancy
    {
        bool Occupied = false;
        CCaveOccupancy(bool const Value) : Occupied{ Value } { }
        operator bool() const noexcept { return Occupied; }
    };

    game::vector<CCaveInfo>         Caves{};
    game::vector<CCaveOccupancy>    Occupancy{};
    std::ptrdiff_t                  IndexMinSize{ 0 };
    std::ptrdiff_t                  IndexMaxSize{ 0 };
};


#ifndef _FINAL_RELEASE

/**
 * @brief       Primitive for interception of a native function at an arbitrary place.
 * @remarks     This basically handles most of the dirty work for setting up code caves.
 */
class BinaryInjection : public NonCopyable
{
    /** Brief description of this intercept. */
    FString Label{ L"" };

    /** Address of an ASM function which should be injected. */
    void* Function{ nullptr };

    /** Cave indexer to use throughout initialization. */
    CaveIndexer* Indexer{ nullptr };

    /** Pointer for the injection's location. */
    unsigned char* Address{ nullptr };

public:

    BinaryInjection(wchar_t const* InLabel, void* InAddress, void* InStubFunction, CaveIndexer* InCaveIndexer);
    ~BinaryInjection() noexcept;

    static void Install(wchar_t const* InLabel, int InRVA, void* InStubFunction, CaveIndexer& InCaveIndexer);

private:

    void Init(void* InAddress, void* InStubFunction, CaveIndexer* InCaveIndexer);

    void EncodeCallCave(ZydisEncoderRequest* Scratch, void* RelayAddress, unsigned char*& CallCursor, std::ptrdiff_t& CallAvailLength) const;

    void UpdateProtect(void* InAddress, size_t Length, bool bEnable) const;
    void UpdateProtect(CaveIndexer::CCaveInfo const& InCave, bool bEnable) const;
    void DebugDisassemble(unsigned char* Pointer, size_t Length);

    static size_t GetPageSize() noexcept;
    static std::intptr_t GetMainBase() noexcept;
};

#endif // !_FINAL_RELEASE


// ! UFunction hijacking.
// ========================================

namespace HijackUtils
{
    /**
     * @brief   Searches for a class with the given name.
     * @param   ClassName - Unique name of the class to look for.
     * @return  The class instance if found, and @c nullptr otherwise.
     */
    UClass* FindClass(SFXName ClassName);

    /**
     * @brief   Searches the given class for a function with the given name.
     * @param   Class - Class instance to search for a function.
     * @param   FuncName - Unique name of the function to look for.
     * @return  The function instance if found, or @c nullptr otherwise.
     */
    UFunction* FindClassMethod(UClass* Class, SFXName FuncName);

    /**
     * @brief   Hijacks a given @c UFunction, probably found using @c FindClassMethod, while preserving the original pointer.
     * @param   Target - The @c UFunction instance to hijack, it will receive a new @c UFunction::Func pointer.
     * @param   Hijack - Native function implementation that will replace @c Target's @c UFunction::Func pointer.
     * @param   Original - Output place to store the original @c UFunction::Func pointer.
     */
    void InstallFunction(UFunction* Target, void* Hijack, void** Original);
}

#define DECLARE_HIJACK(type_, name)                                                         \
    void MILAN_CONCAT(name, _Hook) (type_* Context, FFrame* Stack, void* Result);           \
    decltype(MILAN_CONCAT(name, _Hook))* MILAN_CONCAT(name, _Original) = nullptr

#define INSTALL_HIJACK_EX(class_, name, func) do {                                                                      \
        UFunction* const Target = HijackUtils::FindClassMethod(class_, SFXName(func, 0));                               \
        MILAN_CHECKW(Target != nullptr, L"failed to find function to hijack: %s.%s", class_, func);                     \
        HijackUtils::InstallFunction(Target, MILAN_CONCAT(name, _Hook), (void**)&MILAN_CONCAT(name, _Original));        \
    } while (false)

#define INSTALL_HIJACK(class_, name)    INSTALL_HIJACK_EX(class_, name, L"" #name)


// ! UScript VM macros to write custom native functions.
// ========================================

#define _NATIVE_DECLARE(TYPE, NAME, DEFAULT)        [[maybe_unused]] TYPE NAME{ DEFAULT }
#define _NATIVE_CLEAR_ADDR()                        (*pGPropAddr) = nullptr
#define _NATIVE_ADDR_OR(TYPE, NAME)                 (*pGPropAddr != nullptr) ? *(TYPE*)*pGPropAddr : NAME
#define _NATIVE_STACK_STEP(NAME)                    Stack->Step(Stack->Object, &NAME)
#define _NATIVE_OPT_BEGIN()                         (*pGRuntimeUCFlags) &= ~2
#define _NATIVE_OPT_END(NAME, DEFAULT)              do { if ( ( (*pGRuntimeUCFlags) & 2 ) != 0 ) NAME = DEFAULT; } while (false)
#define _NATIVE_DIRTY_PROP()                        do { if ( *pGPropObject != nullptr ) (*pGPropObject)->NetDirty(*pGProperty); } while (false)


#define NATIVE_GET_BYTE(NAME)                                       \
    _NATIVE_DECLARE(BYTE, NAME, );                                  \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_BYTE_BYREF(NAME)                                 \
    _NATIVE_DECLARE(BYTE, NAME##Local, );                           \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(BYTE&, NAME,                                    \
        _NATIVE_ADDR_OR(BYTE, NAME##Local))

#define NATIVE_GET_BYTE_OPTIONAL(NAME, DEFAULT)                     \
    _NATIVE_DECLARE(BYTE, NAME, DEFAULT);                           \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_BYTE_OPTIONAL_BYREF(NAME, DEFAULT)               \
    _NATIVE_DECLARE(BYTE, NAME##Local, DEFAULT);                    \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(BYTE&, NAME,                                    \
        _NATIVE_ADDR_OR(BYTE, NAME##Local))


#define NATIVE_GET_INT(NAME)                                        \
    _NATIVE_DECLARE(std::int32_t, NAME, );                          \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_INT_BYREF(NAME)                                  \
    _NATIVE_DECLARE(std::int32_t, NAME##Local, );                   \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(std::int32_t&, NAME,                            \
        _NATIVE_ADDR_OR(std::int32_t, NAME##Local))

#define NATIVE_GET_INT_OPTIONAL(NAME, DEFAULT)                      \
    _NATIVE_DECLARE(std::int32_t, NAME, DEFAULT);                   \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_INT_OPTIONAL_BYREF(NAME, DEFAULT)                \
    _NATIVE_DECLARE(std::int32_t, NAME##Local, DEFAULT);            \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(std::int32_t&, NAME,                            \
        _NATIVE_ADDR_OR(std::int32_t, NAME##Local))


#define NATIVE_GET_FLOAT(NAME)                                      \
    _NATIVE_DECLARE(float, NAME, );                                 \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_FLOAT_BYREF(NAME)                                \
    _NATIVE_DECLARE(float, NAME##Local, );                          \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(float&, NAME,                                   \
        _NATIVE_ADDR_OR(float, NAME##Local))

#define NATIVE_GET_FLOAT_OPTIONAL(NAME, DEFAULT)                    \
    _NATIVE_DECLARE(float, NAME, DEFAULT);                          \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_FLOAT_OPTIONAL_BYREF(NAME, DEFAULT)              \
    _NATIVE_DECLARE(float, NAME##Local, DEFAULT);                   \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(float&, NAME,                                   \
        _NATIVE_ADDR_OR(float, NAME##Local))


#define NATIVE_GET_NAME(NAME)                                       \
    _NATIVE_DECLARE(SFXName, NAME, );                               \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_NAME_BYREF(NAME)                                 \
    _NATIVE_DECLARE(SFXName, NAME##Local, );                        \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(SFXName&, NAME,                                 \
        _NATIVE_ADDR_OR(SFXName, NAME##Local))

#define NATIVE_GET_NAME_OPTIONAL(NAME, DEFAULT)                     \
    _NATIVE_DECLARE(SFXName, NAME, DEFAULT);                        \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_NAME_OPTIONAL_BYREF(NAME, DEFAULT)               \
    _NATIVE_DECLARE(SFXName, NAME##Local, DEFAULT);                 \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(SFXName&, NAME,                                 \
        _NATIVE_ADDR_OR(SFXName, NAME##Local))


#define NATIVE_GET_STRING(NAME)                                     \
    _NATIVE_DECLARE(FString, NAME, );                               \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_STRING_BYREF(NAME)                               \
    _NATIVE_DECLARE(FString, NAME##Local, );                        \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(FString&, NAME,                                 \
        _NATIVE_ADDR_OR(FString, NAME##Local))

#define NATIVE_GET_STRING_OPTIONAL(NAME, DEFAULT)                   \
    _NATIVE_DECLARE(FString, NAME, DEFAULT);                        \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_STRING_OPTIONAL_BYREF(NAME, DEFAULT)             \
    _NATIVE_DECLARE(FString, NAME##Local, DEFAULT);                 \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(FString&, NAME,                                 \
        _NATIVE_ADDR_OR(FString, NAME##Local))


#define NATIVE_GET_STRUCT(TYPE, NAME)                               \
    _NATIVE_DECLARE(TYPE, NAME, );                                  \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_STRUCT_BYREF(TYPE, NAME)                         \
    _NATIVE_DECLARE(TYPE, NAME##Local, );                           \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(TYPE&, NAME,                                    \
        _NATIVE_ADDR_OR(TYPE, NAME##Local))

#define NATIVE_GET_STRUCT_OPTIONAL(TYPE, NAME, DEFAULT)             \
    _NATIVE_DECLARE(TYPE, NAME, );                                  \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME);                                       \
    _NATIVE_OPT_END(NAME, DEFAULT)

#define NATIVE_GET_STRUCT_OPTIONAL_BYREF(TYPE, NAME, DEFAULT)       \
    _NATIVE_DECLARE(TYPE, NAME##Local, );                           \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(TYPE&, NAME,                                    \
        _NATIVE_ADDR_OR(TYPE, NAME##Local));                        \
    _NATIVE_OPT_END(NAME, DEFAULT)


#define NATIVE_GET_OBJECT(TYPE, NAME)                               \
    _NATIVE_DECLARE(TYPE*, NAME, nullptr);                          \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_OBJECT_BYREF(TYPE, NAME)                         \
    _NATIVE_DECLARE(TYPE*, NAME##Local, nullptr);                   \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(TYPE*&, NAME,                                   \
        _NATIVE_ADDR_OR(TYPE*, NAME##Local))

#define NATIVE_GET_OBJECT_OPTIONAL(TYPE, NAME, DEFAULT)             \
    _NATIVE_DECLARE(TYPE*, NAME, DEFAULT);                          \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_OBJECT_OPTIONAL_BYREF(TYPE, NAME, DEFAULT)       \
    _NATIVE_DECLARE(TYPE*, NAME##Local, DEFAULT);                   \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(TYPE*&, NAME,                                   \
        _NATIVE_ADDR_OR(TYPE*, NAME##Local))


#define NATIVE_GET_DELEGATE(NAME)                                   \
    _NATIVE_DECLARE(FScriptDelegate, NAME, );                       \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_DELEGATE_BYREF(NAME)                             \
    _NATIVE_DECLARE(FScriptDelegate, NAME##Local, );                \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DECLARE(FScriptDelegate&, NAME,                         \
        _NATIVE_ADDR_OR(FScriptDelegate, NAME##Local))

#define NATIVE_GET_DELEGATE_OPTIONAL(NAME, DEFAULT)                 \
    _NATIVE_DECLARE(FScriptDelegate, NAME, DEFAULT);                \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_DELEGATE_OPTIONAL_BYREF(NAME, DEFAULT)           \
    _NATIVE_DECLARE(FScriptDelegate, NAME##Local, DEFAULT);         \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DECLARE(FScriptDelegate&, NAME,                         \
        _NATIVE_ADDR_OR(FScriptDelegate, NAME##Local))


#define NATIVE_GET_TARRAY(TYPE, NAME)                               \
    _NATIVE_DECLARE(TArray<TYPE>, NAME, );                          \
    _NATIVE_STACK_STEP(NAME)

#define NATIVE_GET_TARRAY_OPTIONAL(TYPE, NAME, DEFAULT)             \
    _NATIVE_DECLARE(TArray<TYPE>, NAME, );                          \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME);                                       \
    _NATIVE_OPT_END(NAME, DEFAULT)

#define NATIVE_GET_TARRAY_BYREF(TYPE, NAME)                         \
    _NATIVE_DECLARE(TArray<TYPE>, NAME##Local, );                   \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(TArray<TYPE>&, NAME,                            \
        _NATIVE_ADDR_OR(TArray<TYPE>, NAME##Local))

#define NATIVE_GET_TARRAY_OPTIONAL_BYREF(TYPE, NAME, DEFAULT)       \
    _NATIVE_DECLARE(TArray<TYPE>, NAME##Local, );                   \
    _NATIVE_CLEAR_ADDR();                                           \
    _NATIVE_OPT_BEGIN();                                            \
    _NATIVE_STACK_STEP(NAME##Local);                                \
    _NATIVE_DIRTY_PROP();                                           \
    _NATIVE_DECLARE(TArray<TYPE>&, NAME,                            \
        _NATIVE_ADDR_OR(TArray<TYPE>, NAME##Local));                \
    _NATIVE_OPT_END(NAME, DEFAULT)


#define NATIVE_FINISH() \
    Stack->Code++; \
    if (*Stack->Code == 0x41) { \
        Stack->Step(Stack->Object, NULL); \
    } \
    do {} while (false)
