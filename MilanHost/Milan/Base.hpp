/**
 * @file        MilanHost/Milan/Base.hpp
 * @header      Very basic things used across MilanHost.
 */

#pragma once

#include <bit>
#include <cstdarg>
#include <memory>
#include <string_view>
#include <type_traits>

#include <LESDK/Headers.hpp>


// ! Utility for stripping project root from __FILE__ at compile time.
// ========================================

#ifndef __cpp_consteval
    #error Support for consteval functions is required.
#endif

namespace Detail
{
    consteval size_t FileStringLengthA(char const* String) noexcept
    {
        size_t Length = 0;
        while (*String++ != 0)
            Length++;
        return Length;
    }

    consteval size_t FileStringLengthW(wchar_t const* String) noexcept
    {
        size_t Length = 0;
        while (*String++ != 0)
            Length++;
        return Length;
    }

    consteval bool FileStartsWithA(char const* Needle, char const* Haystack) noexcept
    {
        while (*Needle && *Haystack)
        {
            char const N = *(Needle++);
            char const H = *(Haystack++);
            if (N != H)
                return false;
        }
        return true;
    }

    consteval bool FileStartsWithW(wchar_t const* Needle, wchar_t const* Haystack) noexcept
    {
        while (*Needle && *Haystack)
        {
            wchar_t const N = *(Needle++);
            wchar_t const H = *(Haystack++);
            if (N != H)
                return false;
        }
        return true;
    }

    consteval char const* FileSourcePathA(char const* const File) noexcept
    {
        char const* Chunk = nullptr;
        char const* Cursor = File + FileStringLengthA(File);

        while (--Cursor != File)
        {
            if (*Cursor == '/' || *Cursor == '\\')
            {
                if (Chunk == nullptr)
                {
                    Chunk = Cursor;
                    continue;
                }

                if (FileStartsWithA("/MilanHost/", Chunk)
                    || FileStartsWithA("\\MilanHost\\", Chunk))
                {
                    return Chunk + 11;
                }

                Chunk = Cursor;
            }
        }

        return File;
    }

    consteval wchar_t const* FileSourcePathW(wchar_t const* const File) noexcept
    {
        wchar_t const* Chunk = nullptr;
        wchar_t const* Cursor = File + FileStringLengthW(File);

        while (--Cursor != File)
        {
            if (*Cursor == L'/' || *Cursor == L'\\')
            {
                if (Chunk == nullptr)
                {
                    Chunk = Cursor;
                    continue;
                }

                if (FileStartsWithW(L"/MilanHost/", Chunk)
                    || FileStartsWithW(L"\\MilanHost\\", Chunk))
                {
                    return Chunk + 11;
                }

                Chunk = Cursor;
            }
        }

        return File;
    }
}


// ! Common macros.
// ========================================

#define MILAN_UNREACHABLE                   __assume(false)

#define MILAN_BREAK()                       __debugbreak()
#define MILAN_BREAK_ATTACHED()              do { if (::Details::ShouldBreak()) { ::DebugBreak(); } } while (false)

#define MILAN_UNUSED(v)                     (void)v
#define MILAN_UNUSED_2(v1, v2)              do { MILAN_UNUSED(v1); MILAN_UNUSED(v2); } while (false)
#define MILAN_UNUSED_3(v1, v2, v3)          do { MILAN_UNUSED(v1); MILAN_UNUSED(v2); MILAN_UNUSED(v3); } while (false)
#define MILAN_UNUSED_4(v1, v2, v3, v4)      do { MILAN_UNUSED(v1); MILAN_UNUSED(v2); MILAN_UNUSED(v3); MILAN_UNUSED(v4); } while (false)

#define MILAN_FUNCA                         __FUNCSIG__
#define MILAN_FILEA                         ::Detail::FileSourcePathA(__FILE__)
#define MILAN_LINEA                         __LINE__

#define MILAN_FUNCW                         L"" __FUNCSIG__
#define MILAN_FILEW                         ::Detail::FileSourcePathW(L"" __FILE__)
#define MILAN_LINEW                         __LINE__


/** Description of a location in source code, using @c char literals. */
struct CodePlaceA final
{
    char const*     Func{ nullptr };
    char const*     File{ nullptr };
    signed int      Line{ 0 };
};

/** Description of a location in source code, using @c wchar_t literals. */
struct CodePlaceW final
{
    wchar_t const*  Func{ nullptr };
    wchar_t const*  File{ nullptr };
    signed int      Line{ 0 };
};

/**
 * @def         MILAN_HEREW
 * @brief       Generates a @ref CodePlace value at the place of invocation.
 */
#define MILAN_HEREA  ::CodePlaceA { .Func = MILAN_FUNCA, .File = MILAN_FILEA, .Line = MILAN_LINEA }

/**
 * @def         MILAN_HEREW
 * @brief       Generates a @ref CodePlace value at the place of invocation.
 */
#define MILAN_HEREW  ::CodePlaceW { .Func = MILAN_FUNCW, .File = MILAN_FILEW, .Line = MILAN_LINEW }


// ! Core type traits.
//========================================

/** Integer or a floating-point value type. */
template<typename T>
concept NumericType = std::is_integral<T>::value || std::is_floating_point<T>::value;

/** Integer number `S` for which there is a standard integer type of size `S`. */
template<int S>
concept NumericRegularSize = S == 1 || S == 2 || S == 4 || S == 8;

/** Integer or a floating-point value type with one of the common sizes. */
template<typename T>
concept NumericRegularType = NumericType<T> && NumericRegularSize<sizeof(T)>;

/** Struct or a class of standard layout, i.e. "plain old data". */
template<typename T>
concept PlainOldType = std::is_class<T>::value && std::is_standard_layout<T>::value;

/** Enum type concept. */
template<typename T>
concept EnumType = std::is_enum<T>::value;

constexpr bool IsPowerOfTwoOrZero(std::size_t const N) noexcept { return (N & (N - 1)) == 0; }
constexpr bool IsPowerOfTwo(std::size_t const N) noexcept { return (N != 0) && IsPowerOfTwoOrZero(N); }

/** Named constraint for power-of-two numeric template parameters. */
template<std::size_t Number>
concept PowerOfTwo = IsPowerOfTwo(Number);


// ! Endianness utilities.
//========================================

static constexpr std::endian NATIVE = std::endian::native;
static constexpr std::endian LITTLE = std::endian::little;
static constexpr std::endian BIG = std::endian::big;


/**
 * @brief       Reverse byte order of an integer or floating-point value.
 * @tparam      T - Numeric type of 1 / 2 / 4 / 8 bytes.
 * @returns     The reversed value.
 */
template<NumericRegularType T>
inline T Byteswap(T const V) noexcept
{
    if constexpr (sizeof(V) == 1) { return V; }
    if constexpr (sizeof(V) == 2) { return std::bit_cast<T>(_byteswap_ushort(std::bit_cast<uint16_t>(V))); }
    if constexpr (sizeof(V) == 4) { return std::bit_cast<T>(_byteswap_ulong (std::bit_cast<uint32_t>(V))); }
    if constexpr (sizeof(V) == 8) { return std::bit_cast<T>(_byteswap_uint64(std::bit_cast<uint64_t>(V))); }
    // Other sizes are cut off by the type constraint.
}

/**
 * @brief       Alternative to @ref Byteswap guaranteed to run in compile-time.
 * @tparam      T - Numeric type of 1 / 2 / 4 / 8 bytes.
 * @returns     The reversed value.
 */
template<NumericRegularType T>
consteval T ConstevalByteswap(T const V) noexcept
{
    using UnsignedT = std::make_unsigned<T>::type;
    UnsignedT const U = std::bit_cast<UnsignedT, T>(V);
    UnsignedT const R =
          ((U >> 0x18) & 0x000000ff)
        | ((U << 0x08) & 0x00ff0000)
        | ((U >> 0x08) & 0x0000ff00)
        | ((U << 0x18) & 0xff000000);
    return std::bit_cast<T, UnsignedT>(R);
}


// ! Memory utilities.
// ========================================

namespace game
{
    /**
     * @brief       Duck-typed @c std::allocator implementation using the game's allocator.
     * @tparam      T Type being allocated, i.e. what the using container stores.
     */
    template<class T>
    struct allocator final
    {
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using propagate_on_container_move_assignment = std::true_type;
        using is_always_equal = std::true_type;

        allocator() noexcept = default;
        allocator(allocator const&) noexcept = default;

        template<class U> allocator(allocator<U> const&) noexcept {}

        T* allocate(std::size_t n);
        void deallocate(T* p, std::size_t n);
    };

    template<class T>
    T* allocator<T>::allocate(std::size_t const n)
    {
        auto const Size = static_cast<DWORD>(n * sizeof(T));
        return reinterpret_cast<T*>((*GMalloc)->Malloc(Size, UN_DEFAULT_ALIGNMENT));
    }

    template<class T>
    void allocator<T>::deallocate(T* const p, std::size_t const n)
    {
        MILAN_UNUSED(n);
        (*GMalloc)->Free(p);
    }
}

/**
 * @brief
 * A mixin class which ensures that derived types,
 * when using new / delete operators, are (de)allocated
 * through the game's allocator (GMalloc).
 */
class AllocateThroughEngine
{
public:
    inline void* operator new(size_t s);
    inline void* operator new[](size_t s);
    inline void  operator delete(void* p);
    inline void  operator delete[](void* p);
};

inline void* AllocateThroughEngine::operator new(size_t const s)
{
    assert(GMalloc != nullptr);
    return (*GMalloc)->Malloc(static_cast<DWORD>(s), UN_DEFAULT_ALIGNMENT);
}

inline void* AllocateThroughEngine::operator new[](size_t s)
{
    assert(GMalloc != nullptr);
    return (*GMalloc)->Malloc(static_cast<DWORD>(s), UN_DEFAULT_ALIGNMENT);
}

inline void AllocateThroughEngine::operator delete(void* const p)
{
    assert(GMalloc != nullptr);
    (*GMalloc)->Free(p);
}

inline void AllocateThroughEngine::operator delete[](void* const p)
{
    assert(GMalloc != nullptr);
    (*GMalloc)->Free(p);
}


// ! Core logging interfaces.
// ========================================

enum struct ESeverity : int
{
    Trace = 1,
    Debug = 2,
    Info = 3,
    Warn = 4,
    Error = 5,
};

static constexpr ESeverity k_logTrace = ESeverity::Trace;
static constexpr ESeverity k_logDebug = ESeverity::Debug;
static constexpr ESeverity k_logInfo = ESeverity::Info;
static constexpr ESeverity k_logWarn = ESeverity::Warn;
static constexpr ESeverity k_logError = ESeverity::Error;


/**
 * @brief
 * Interface for a type that persists pre-formatted logging output,
 * for example by printing it to console or writing to a file.
 *
 * @details
 * Both @c Accept* methods expect @c Size argument to exclude null termination!
 */
class ILoggerOutput : public AllocateThroughEngine
{
public:

    virtual ~ILoggerOutput() noexcept {}

    virtual void AcceptAnsi(CodePlaceA const& Place, ESeverity Severity, char const* Data, size_t Size, float Seconds) = 0;
    virtual void AcceptUnicode(CodePlaceW const& Place, ESeverity Severity, wchar_t const* Data, size_t Size, float Seconds) = 0;

    virtual void Flush() = 0;
};

/**
 * @brief
 * Interface for a type that implements common logging output.
 *
 * @details
 * The design assumes that all output will be directed to one or more
 * instances of @ref ILoggerOutput class.
 */
class ILogger : public AllocateThroughEngine
{
public:

    virtual ~ILogger() noexcept {}

    virtual void WriteAnsi(CodePlaceA const& Place, ESeverity Severity, std::string_view Message) = 0;
    virtual void WriteUnicode(CodePlaceW const& Place, ESeverity Severity, std::wstring_view Message) = 0;

    virtual void Flush() {};

    void WriteFormatAnsi(CodePlaceA const& Place, ESeverity Severity, std::string_view Format, ...);
    void WriteFormatUnicode(CodePlaceW const& Place, ESeverity Severity, std::wstring_view Format, ...);

    void WriteFormatAnsiVararg(CodePlaceA const& Place, ESeverity Severity, std::string_view Format, std::va_list Args);
    void WriteFormatUnicodeVararg(CodePlaceW const& Place, ESeverity Severity, std::wstring_view Message, std::va_list Args);
};


#define _MILAN_LOGW_INTO(Instance, Severity, Message) \
    (Instance).WriteUnicode(MILAN_HEREW, Severity, Message)

#define _MILAN_LOGW_FMT_INTO(Instance, Severity, Format, ...) \
    (Instance).WriteFormatUnicode(MILAN_HEREW, Severity, Format, __VA_ARGS__)


#define MILAN_TRACEW_INTO(Instance, Format, ...)        _MILAN_LOGW_FMT_INTO(Instance, ESeverity::Trace, Format, __VA_ARGS__)
#define MILAN_DEBUGW_INTO(Instance, Format, ...)        _MILAN_LOGW_FMT_INTO(Instance, ESeverity::Debug, Format, __VA_ARGS__)
#define MILAN_INFOW_INTO(Instance, Format, ...)         _MILAN_LOGW_FMT_INTO(Instance, ESeverity::Info,  Format, __VA_ARGS__)
#define MILAN_WARNW_INTO(Instance, Format, ...)         _MILAN_LOGW_FMT_INTO(Instance, ESeverity::Warn,  Format, __VA_ARGS__)
#define MILAN_ERRORW_INTO(Instance, Format, ...)        _MILAN_LOGW_FMT_INTO(Instance, ESeverity::Error, Format, __VA_ARGS__)

#define MILAN_TRACEW(Format, ...)                       _MILAN_LOGW_FMT_INTO(::Milan::Output::GetMainLogger(), ESeverity::Trace, Format, __VA_ARGS__)
#define MILAN_DEBUGW(Format, ...)                       _MILAN_LOGW_FMT_INTO(::Milan::Output::GetMainLogger(), ESeverity::Debug, Format, __VA_ARGS__)
#define MILAN_INFOW(Format, ...)                        _MILAN_LOGW_FMT_INTO(::Milan::Output::GetMainLogger(), ESeverity::Info,  Format, __VA_ARGS__)
#define MILAN_WARNW(Format, ...)                        _MILAN_LOGW_FMT_INTO(::Milan::Output::GetMainLogger(), ESeverity::Warn,  Format, __VA_ARGS__)
#define MILAN_ERRORW(Format, ...)                       _MILAN_LOGW_FMT_INTO(::Milan::Output::GetMainLogger(), ESeverity::Error, Format, __VA_ARGS__)


#ifndef _MLN_DEBUG

    // In RELEASE and FINAL builds, disable TRACE.

    #undef MILAN_TRACEW_INTO
    #undef MILAN_TRACEW

    #define MILAN_TRACEW_INTO(Instance, Format, ...)    do { } while (false)
    #define MILAN_TRACEW(Format, ...)                   do { } while (false)

#ifdef _FINAL_RELEASE

    // In FINAL builds, also disable DEBUG and INFO.

    #undef MILAN_DEBUGW_INTO
    #undef MILAN_DEBUGW

    #define MILAN_DEBUGW_INTO(Instance, Format, ...)    do { } while (false)
    #define MILAN_DEBUGW(Format, ...)                   do { } while (false)

    #undef MILAN_INFOW_INTO
    #undef MILAN_INFOW

    #define MILAN_INFOW_INTO(Instance, Format, ...)     do { } while (false)
    #define MILAN_INFOW(Format, ...)                    do { } while (false)

#endif // _FINAL_RELEASE

#endif // _MLN_DEBUG


namespace Details
{
    void OnFatalErrorW(::CodePlaceW const& Place, wchar_t const* Expression, wchar_t const* Format, ...);
    bool ShouldBreak() noexcept;

    extern bool bDisableSafeBreak;
}

#define _MILAN_FATALW_IMPL(Expression, Format, ...)                                 \
    do {                                                                            \
        MILAN_BREAK_ATTACHED();                                                     \
        ::Details::OnFatalErrorW(MILAN_HEREW, Expression, Format, __VA_ARGS__);     \
        MILAN_UNREACHABLE;                                                          \
    } while (false)

/**
 * @def         MILAN_FATALW(Format, ...)
 * @brief       Terminates this .asi module and the host game due to an error.
 */
#define MILAN_FATALW(Format, ...) \
    _MILAN_FATALW_IMPL(L"", Format, __VA_ARGS__)

/**
 * @def         MILAN_VERIFYW(Expression, Format, ...)
 * @brief       Calls @ref MILAN_FATALW if @c Expression evaluates false.
 */
#define MILAN_VERIFYW(Expression, Format, ...) \
    do { if (!(Expression)) { \
        _MILAN_FATALW_IMPL(L"" #Expression, Format, __VA_ARGS__); \
    } } while (false)

#ifndef _FINAL_RELEASE

    /**
     * @def         MILAN_CHECKW(Expression, Format, ...)
     * @brief       Identical to @ref MILAN_VERIFYW in DEBUG and RELEASE builds.
     */
    #define MILAN_CHECKW(Expression, Format, ...) \
        MILAN_VERIFYW(Expression, Format, __VA_ARGS__)
#else

    /**
     * @def         MILAN_CHECKW(Expression, Format, ...)
     * @brief       Resolves to a no-op in FINAL builds.
     */
    #define MILAN_CHECKW(Expression, Format, ...) \
        do { } while (false)

#endif


// ! Input/output definitions.
// ========================================

enum struct IoMode : int {
    Read = 1,
    ReadWrite = 2,
};

static constexpr auto k_ioRead = IoMode::Read;
static constexpr auto k_ioReadWrite = IoMode::ReadWrite;

enum struct SeekOrigin : int
{
    Begin = 1,
    Current = 2,
    End = 3,
};

static constexpr auto k_seekBegin = SeekOrigin::Begin;
static constexpr auto k_seekCurrent = SeekOrigin::Current;
static constexpr auto k_seekEnd = SeekOrigin::End;


// ! Misc types.
// ========================================

class NonCopyable
{
public:
    NonCopyable() = default;
    NonCopyable(NonCopyable const&) = delete;
    NonCopyable& operator=(NonCopyable const&) = delete;
};


/**
 * Re-declared and implemented in Entry.hpp / Entry.cpp.
 * Needs to be declared here first so that hooks can call it.
 */
extern void SingleDetachImpl();


#define _MILAN_VARCAT(a, b) a##b
#define MILAN_VARCAT(a, b) _MILAN_VARCAT(a, b)
