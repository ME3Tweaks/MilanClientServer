// Compiled with SKIP_PRECOMPILE_HEADERS; see MatchmakingNet.hpp.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <memory>

#include "Online/MatchmakingNet.hpp"


namespace
{
    constexpr std::size_t k_maxResponseBytes = 256 * 1024;

    struct WinHttpHandle
    {
        HINTERNET Handle{ nullptr };
        explicit WinHttpHandle(HINTERNET InHandle) : Handle{ InHandle } {}
        ~WinHttpHandle() { if (Handle) ::WinHttpCloseHandle(Handle); }
        WinHttpHandle(WinHttpHandle const&) = delete;
        WinHttpHandle& operator=(WinHttpHandle const&) = delete;
    };

    std::wstring Widen(std::string const& Text)
    {
        if (Text.empty())
            return {};
        int const Length = ::MultiByteToWideChar(CP_UTF8, 0, Text.data(), (int)Text.size(), nullptr, 0);
        std::wstring Wide(static_cast<std::size_t>(Length), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, Text.data(), (int)Text.size(), Wide.data(), Length);
        return Wide;
    }

    std::string LastErrorText(char const* What)
    {
        return std::string{ What } + " failed, error " + std::to_string(::GetLastError());
    }

    struct CrackedUrl
    {
        std::wstring Host{};
        INTERNET_PORT Port{ 0 };
        bool Secure{ false };
    };

    bool CrackBaseUrl(std::string const& BaseUrl, CrackedUrl& Out)
    {
        std::wstring const Wide = Widen(BaseUrl);
        wchar_t HostBuffer[256]{};
        URL_COMPONENTS Components{};
        Components.dwStructSize = sizeof(Components);
        Components.lpszHostName = HostBuffer;
        Components.dwHostNameLength = static_cast<DWORD>(std::size(HostBuffer));
        Components.dwSchemeLength = 1;
        Components.dwUrlPathLength = 1;

        if (!::WinHttpCrackUrl(Wide.c_str(), static_cast<DWORD>(Wide.size()), 0, &Components))
            return false;
        if (Components.nScheme != INTERNET_SCHEME_HTTP && Components.nScheme != INTERNET_SCHEME_HTTPS)
            return false;

        Out.Host.assign(HostBuffer, Components.dwHostNameLength);
        Out.Port = Components.nPort;
        Out.Secure = Components.nScheme == INTERNET_SCHEME_HTTPS;
        return !Out.Host.empty();
    }

    bool IsLanAddress(std::uint8_t const* b)
    {
        return b[0] == 10
            || (b[0] == 172 && (b[1] & 0xF0) == 16)
            || (b[0] == 192 && b[1] == 168)
            || (b[0] == 169 && b[1] == 254)
            || (b[0] == 100 && (b[1] & 0xC0) == 64);
    }
}


namespace MatchmakingNet
{
    HttpResult Request(std::string const& BaseUrl, wchar_t const* const Method, std::string const& Path,
        std::string const& Bearer, std::string const& JsonBody, int const ReceiveTimeoutMs)
    {
        HttpResult Result{};

        CrackedUrl Url{};
        if (!CrackBaseUrl(BaseUrl, Url))
        {
            Result.Error = "matchmaker URL must be http:// or https:// with a host";
            return Result;
        }

        WinHttpHandle Session{ ::WinHttpOpen(L"MilanHost", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
        if (!Session.Handle)
        {
            Result.Error = LastErrorText("WinHttpOpen");
            return Result;
        }
        ::WinHttpSetTimeouts(Session.Handle, 5000, 5000, 10000, ReceiveTimeoutMs);

        WinHttpHandle Connection{ ::WinHttpConnect(Session.Handle, Url.Host.c_str(), Url.Port, 0) };
        if (!Connection.Handle)
        {
            Result.Error = LastErrorText("WinHttpConnect");
            return Result;
        }

        std::wstring const WidePath = Widen(Path);
        WinHttpHandle Req{ ::WinHttpOpenRequest(Connection.Handle, Method, WidePath.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, Url.Secure ? WINHTTP_FLAG_SECURE : 0) };
        if (!Req.Handle)
        {
            Result.Error = LastErrorText("WinHttpOpenRequest");
            return Result;
        }

        std::wstring Headers = L"Accept: application/json\r\n";
        if (!JsonBody.empty())
            Headers += L"Content-Type: application/json\r\n";
        if (!Bearer.empty())
            Headers += L"Authorization: Bearer " + Widen(Bearer) + L"\r\n";

        void* const BodyData = JsonBody.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(JsonBody.data());
        DWORD const BodySize = static_cast<DWORD>(JsonBody.size());
        if (!::WinHttpSendRequest(Req.Handle, Headers.c_str(), static_cast<DWORD>(-1L), BodyData, BodySize, BodySize, 0)
            || !::WinHttpReceiveResponse(Req.Handle, nullptr))
        {
            Result.Error = LastErrorText("HTTP request");
            return Result;
        }

        DWORD StatusCode = 0;
        DWORD StatusSize = sizeof(StatusCode);
        if (!::WinHttpQueryHeaders(Req.Handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &StatusCode, &StatusSize, WINHTTP_NO_HEADER_INDEX))
        {
            Result.Error = LastErrorText("WinHttpQueryHeaders");
            return Result;
        }

        for (;;)
        {
            DWORD Available = 0;
            if (!::WinHttpQueryDataAvailable(Req.Handle, &Available))
            {
                Result.Error = LastErrorText("WinHttpQueryDataAvailable");
                return Result;
            }
            if (Available == 0)
                break;
            if (Result.Body.size() + Available > k_maxResponseBytes)
            {
                Result.Error = "response too large";
                return Result;
            }

            std::size_t const Offset = Result.Body.size();
            Result.Body.resize(Offset + Available);
            DWORD Read = 0;
            if (!::WinHttpReadData(Req.Handle, Result.Body.data() + Offset, Available, &Read))
            {
                Result.Error = LastErrorText("WinHttpReadData");
                return Result;
            }
            Result.Body.resize(Offset + Read);
        }

        Result.Status = static_cast<int>(StatusCode);
        return Result;
    }

    bool ResolveUrlHostIPv4(std::string const& BaseUrl, std::uint32_t& OutAddress, std::string& OutError)
    {
        CrackedUrl Url{};
        if (!CrackBaseUrl(BaseUrl, Url))
        {
            OutError = "matchmaker URL must be http:// or https:// with a host";
            return false;
        }

        ADDRINFOW Hints{};
        Hints.ai_family = AF_INET;
        Hints.ai_socktype = SOCK_DGRAM;
        ADDRINFOW* Found = nullptr;
        if (int const Error = ::GetAddrInfoW(Url.Host.c_str(), nullptr, &Hints, &Found); Error != 0 || !Found)
        {
            OutError = "could not resolve matchmaker host to IPv4, error " + std::to_string(Error);
            return false;
        }

        OutAddress = reinterpret_cast<sockaddr_in const*>(Found->ai_addr)->sin_addr.s_addr;
        ::FreeAddrInfoW(Found);
        return true;
    }

    bool HmacSha256(std::span<std::uint8_t const> const Key, std::span<std::uint8_t const> const Data,
        std::span<std::uint8_t, 32> const Out)
    {
        static BCRYPT_ALG_HANDLE s_algorithm = []
        {
            BCRYPT_ALG_HANDLE Handle = nullptr;
            if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&Handle, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)))
                return static_cast<BCRYPT_ALG_HANDLE>(nullptr);
            return Handle;
        }();
        if (!s_algorithm)
            return false;

        return BCRYPT_SUCCESS(::BCryptHash(s_algorithm,
            const_cast<PUCHAR>(Key.data()), static_cast<ULONG>(Key.size()),
            const_cast<PUCHAR>(Data.data()), static_cast<ULONG>(Data.size()),
            Out.data(), static_cast<ULONG>(Out.size())));
    }

    std::vector<std::string> LanIPv4Addresses(std::size_t const Max)
    {
        std::vector<std::string> Addresses{};

        ULONG Size = 16 * 1024;
        std::unique_ptr<std::uint8_t[]> Buffer{};
        ULONG Status = ERROR_BUFFER_OVERFLOW;
        for (int Attempt = 0; Attempt < 3 && Status == ERROR_BUFFER_OVERFLOW; ++Attempt)
        {
            Buffer = std::make_unique<std::uint8_t[]>(Size);
            Status = ::GetAdaptersAddresses(AF_INET,
                GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(Buffer.get()), &Size);
        }
        if (Status != NO_ERROR)
            return Addresses;

        for (auto const* Adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES const*>(Buffer.get());
             Adapter != nullptr && Addresses.size() < Max; Adapter = Adapter->Next)
        {
            if (Adapter->OperStatus != IfOperStatusUp || Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;

            for (auto const* Unicast = Adapter->FirstUnicastAddress; Unicast != nullptr && Addresses.size() < Max; Unicast = Unicast->Next)
            {
                auto const* const Sockaddr = reinterpret_cast<sockaddr_in const*>(Unicast->Address.lpSockaddr);
                if (Sockaddr == nullptr || Sockaddr->sin_family != AF_INET)
                    continue;

                auto const* const Bytes = reinterpret_cast<std::uint8_t const*>(&Sockaddr->sin_addr.s_addr);
                if (!IsLanAddress(Bytes))
                    continue;

                std::string Text = FormatIPv4(Sockaddr->sin_addr.s_addr, 0);
                Text.resize(Text.rfind(':'));
                bool Duplicate = false;
                for (auto const& Existing : Addresses)
                    Duplicate = Duplicate || Existing == Text;
                if (!Duplicate)
                    Addresses.push_back(std::move(Text));
            }
        }
        return Addresses;
    }

    int LocalPort(std::uintptr_t const SocketHandle)
    {
        if (SocketHandle == 0 || SocketHandle == static_cast<std::uintptr_t>(INVALID_SOCKET))
            return -1;

        sockaddr_in Local{};
        int Length = sizeof(Local);
        if (::getsockname(static_cast<SOCKET>(SocketHandle), reinterpret_cast<sockaddr*>(&Local), &Length) != 0
            || Local.sin_family != AF_INET)
        {
            return -1;
        }
        return ntohs(Local.sin_port);
    }

    std::string FormatIPv4(std::uint32_t const AddressNetworkOrder, std::uint16_t const PortHostOrder)
    {
        auto const* const b = reinterpret_cast<std::uint8_t const*>(&AddressNetworkOrder);
        return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "."
            + std::to_string(b[3]) + ":" + std::to_string(PortHostOrder);
    }
}
