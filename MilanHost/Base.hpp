/**
 * @file        MilanHost/Base.hpp
 * @header      Very basic things used across MilanHost.
 */

#pragma once

#include <cstdarg>
#include <memory>
#include <string_view>

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
#define MILAN_BREAK_ATTACHED()              do { if (::IsDebuggerPresent()) { ::DebugBreak(); } } while (false)

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

    virtual void AcceptAnsi(ESeverity Severity, char const* Data, size_t Size) = 0;
    virtual void AcceptUnicode(ESeverity Severity, wchar_t const* Data, size_t Size) = 0;

    virtual void Flush(bool bError) = 0;
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


#ifndef _DEBUG

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

#endif // _DEBUG


// ! Misc types.
// ========================================

class NonCopyable
{
public:
    NonCopyable() = default;
    NonCopyable(NonCopyable const&) = delete;
    NonCopyable& operator=(NonCopyable const&) = delete;
};
