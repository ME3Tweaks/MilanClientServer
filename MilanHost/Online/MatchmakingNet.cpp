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
#include <sddl.h>
#include <winhttp.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "Online/MatchmakingNet.hpp"
#include "Online/PeerSecurity.hpp"

// Older Windows SDKs do not define it; the value is fixed by WinHTTP.
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif


namespace
{
    constexpr std::size_t k_maxResponseBytes = 256 * 1024;
    constexpr DWORD k_maxPipeMessage = 4096;
    constexpr DWORD k_pipeIoTimeoutMs = 2000;
    constexpr int k_resolveTimeoutMs = 5000;
    constexpr int k_connectTimeoutMs = 5000;
    constexpr int k_sendTimeoutMs = 10000;

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

    // Meanings of the WinHTTP errors a misconfigured server or network usually causes.
    char const* DescribeWinHttpError(DWORD const Error)
    {
        switch (Error)
        {
        case ERROR_WINHTTP_TIMEOUT:                 return "timed out";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:       return "host name not found";
        case ERROR_WINHTTP_CANNOT_CONNECT:          return "could not connect";
        case ERROR_WINHTTP_CONNECTION_ERROR:        return "connection reset";
        case ERROR_WINHTTP_SECURE_FAILURE:          return "TLS failed: certificate not trusted or not for this host name, or no TLS 1.2+";
        case ERROR_WINHTTP_SECURE_INVALID_CA:       return "certificate not trusted";
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:  return "certificate is not for this host name";
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID: return "certificate expired or not yet valid";
        default:                                    return nullptr;
        }
    }

    std::string LastErrorText(char const* What)
    {
        DWORD const Error = ::GetLastError();
        std::string Text = std::string{ What } + " failed, error " + std::to_string(Error);
        if (char const* const Meaning = DescribeWinHttpError(Error))
            Text += std::string{ " (" } + Meaning + ")";
        return Text;
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
            || (b[0] == 100 && (b[1] & 0xC0) == 64);
    }

    // One WinHTTP session for the process. WinHTTP keeps finished connections open in the
    // session's pool and reuses them, so a request skips the TCP and TLS handshakes. Handles are
    // thread-safe: the control and poll workers each get their own pooled connection.
    class SharedHttpClient
    {
    public:

        // The connection handle for @p Url's server, opening the session on first use.
        HINTERNET Connect(CrackedUrl const& Url, std::string& OutError)
        {
            std::lock_guard Lock{ m_mutex };
            if (m_session == nullptr && (m_session = OpenSession(OutError)) == nullptr)
                return nullptr;

            auto const Key = std::make_pair(Url.Host, Url.Port);
            if (auto const Found = m_connections.find(Key); Found != m_connections.end())
                return Found->second;

            HINTERNET const Connection = ::WinHttpConnect(m_session, Url.Host.c_str(), Url.Port, 0);
            if (Connection == nullptr)
            {
                OutError = LastErrorText("WinHttpConnect");
                return nullptr;
            }
            m_connections.emplace(Key, Connection);
            return Connection;
        }

    private:

        // TLS 1.2 is the floor; TLS 1.3 is added where Windows supports it, and older systems
        // reject that flag. Certificates are checked against the Windows store as usual.
        static HINTERNET OpenSession(std::string& OutError)
        {
            HINTERNET const Session = ::WinHttpOpen(L"MilanHost", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (Session == nullptr)
            {
                OutError = LastErrorText("WinHttpOpen");
                return nullptr;
            }

            DWORD Protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
            if (!::WinHttpSetOption(Session, WINHTTP_OPTION_SECURE_PROTOCOLS, &Protocols, sizeof(Protocols)))
            {
                Protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
                if (!::WinHttpSetOption(Session, WINHTTP_OPTION_SECURE_PROTOCOLS, &Protocols, sizeof(Protocols)))
                {
                    OutError = LastErrorText("WinHttpSetOption(TLS 1.2)");
                    ::WinHttpCloseHandle(Session);
                    return nullptr;
                }
            }
            return Session;
        }

        std::mutex                  m_mutex{};
        HINTERNET                   m_session{ nullptr };
        std::map<std::pair<std::wstring, INTERNET_PORT>, HINTERNET> m_connections{};
    };

    // Never destroyed: closing WinHTTP handles from static destruction runs under the loader
    // lock at process exit, and the pool is only useful for the process's lifetime anyway.
    SharedHttpClient& HttpClient()
    {
        static SharedHttpClient* const s_client = new SharedHttpClient{};
        return *s_client;
    }

    // Sends one request on @p Connection and reads the whole response, which lets WinHTTP
    // return the connection to the pool. @p OutError is the Windows error of a failed request.
    bool SendOnce(HINTERNET const Connection, bool const Secure, wchar_t const* const Method, std::wstring const& Path,
        std::wstring const& Headers, std::string const& JsonBody, int const ReceiveTimeoutMs,
        MatchmakingNet::HttpResult& Result, DWORD& OutError)
    {
        auto const Fail = [&](char const* const What)
        {
            OutError = ::GetLastError();
            Result.Error = LastErrorText(What);
            return false;
        };

        WinHttpHandle Req{ ::WinHttpOpenRequest(Connection, Method, Path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, Secure ? WINHTTP_FLAG_SECURE : 0) };
        if (!Req.Handle)
            return Fail("WinHttpOpenRequest");

        // Bearer keys go to the configured server only: a redirect is returned as its status.
        DWORD Redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!::WinHttpSetOption(Req.Handle, WINHTTP_OPTION_REDIRECT_POLICY, &Redirects, sizeof(Redirects)))
            return Fail("WinHttpSetOption(redirect policy)");
        ::WinHttpSetTimeouts(Req.Handle, k_resolveTimeoutMs, k_connectTimeoutMs, k_sendTimeoutMs, ReceiveTimeoutMs);

        void* const BodyData = JsonBody.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(JsonBody.data());
        DWORD const BodySize = static_cast<DWORD>(JsonBody.size());
        if (!::WinHttpSendRequest(Req.Handle, Headers.c_str(), static_cast<DWORD>(-1L), BodyData, BodySize, BodySize, 0)
            || !::WinHttpReceiveResponse(Req.Handle, nullptr))
        {
            return Fail("HTTP request");
        }

        DWORD StatusCode = 0;
        DWORD StatusSize = sizeof(StatusCode);
        if (!::WinHttpQueryHeaders(Req.Handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &StatusCode, &StatusSize, WINHTTP_NO_HEADER_INDEX))
        {
            return Fail("WinHttpQueryHeaders");
        }

        for (;;)
        {
            DWORD Available = 0;
            if (!::WinHttpQueryDataAvailable(Req.Handle, &Available))
                return Fail("WinHttpQueryDataAvailable");
            if (Available == 0)
                break;
            if (Result.Body.size() + Available > k_maxResponseBytes)
            {
                OutError = ERROR_SUCCESS;
                Result.Error = "response too large";
                return false;
            }

            std::size_t const Offset = Result.Body.size();
            Result.Body.resize(Offset + Available);
            DWORD Read = 0;
            if (!::WinHttpReadData(Req.Handle, Result.Body.data() + Offset, Available, &Read))
                return Fail("WinHttpReadData");
            Result.Body.resize(Offset + Read);
        }

        Result.Status = static_cast<int>(StatusCode);
        return true;
    }

    // A protected DACL that grants access to the current user only.
    bool CurrentUserOnlyDescriptor(PSECURITY_DESCRIPTOR& Out, std::string& OutError)
    {
        HANDLE Token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &Token))
        {
            OutError = LastErrorText("OpenProcessToken");
            return false;
        }
        DWORD Size = 0;
        ::GetTokenInformation(Token, TokenUser, nullptr, 0, &Size);
        std::vector<std::uint8_t> User(Size);
        bool const HaveUser = Size != 0 && ::GetTokenInformation(Token, TokenUser, User.data(), Size, &Size);
        ::CloseHandle(Token);
        if (!HaveUser)
        {
            OutError = LastErrorText("GetTokenInformation");
            return false;
        }

        LPWSTR Sid = nullptr;
        if (!::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER const*>(User.data())->User.Sid, &Sid))
        {
            OutError = LastErrorText("ConvertSidToStringSid");
            return false;
        }
        std::wstring const Sddl = std::wstring{ L"D:P(A;;GA;;;" } + Sid + L")";
        ::LocalFree(Sid);

        if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl.c_str(), SDDL_REVISION_1, &Out, nullptr))
        {
            OutError = LastErrorText("ConvertStringSecurityDescriptorToSecurityDescriptor");
            return false;
        }
        return true;
    }

    enum class EPipeIo { Done, Failed, Stopped };

    // Completes an overlapped pipe operation started with @p Started, waiting at most
    // @p TimeoutMs or until @p Stop is signaled. Pending I/O is cancelled on timeout or stop.
    EPipeIo FinishPipeIo(HANDLE const Instance, OVERLAPPED& Overlapped, BOOL const Started, HANDLE const Stop,
        DWORD const TimeoutMs, DWORD& OutBytes)
    {
        OutBytes = 0;
        if (!Started)
        {
            DWORD const Error = ::GetLastError();
            if (Error == ERROR_PIPE_CONNECTED)
                return EPipeIo::Done;
            if (Error != ERROR_IO_PENDING)
                return EPipeIo::Failed;

            HANDLE const Handles[2]{ Overlapped.hEvent, Stop };
            DWORD const Wait = ::WaitForMultipleObjects(2, Handles, FALSE, TimeoutMs);
            if (Wait != WAIT_OBJECT_0)
            {
                ::CancelIoEx(Instance, &Overlapped);
                ::GetOverlappedResult(Instance, &Overlapped, &OutBytes, TRUE);
                return Wait == WAIT_OBJECT_0 + 1 ? EPipeIo::Stopped : EPipeIo::Failed;
            }
        }
        return ::GetOverlappedResult(Instance, &Overlapped, &OutBytes, FALSE) ? EPipeIo::Done : EPipeIo::Failed;
    }

    // Set on the game thread, read by the HTTP worker threads. Keys stay per server, so requests
    // still queued for a previous matchmaker (such as leaving its lobby) keep sending its key.
    struct AccessKeyState
    {
        std::mutex                          Mutex{};
        std::map<std::string, std::string>  Keys{};
    };

    AccessKeyState& AccessKey()
    {
        static AccessKeyState State{};
        return State;
    }
}


struct MatchmakingNet::ControlPipe
{
    HANDLE                          StopEvent{ nullptr };

    ControlPipe() = default;
    ~ControlPipe() { if (StopEvent) ::CloseHandle(StopEvent); }
    ControlPipe(ControlPipe const&) = delete;
    ControlPipe& operator=(ControlPipe const&) = delete;
};


namespace
{
    // One pipe instance serves clients in turn, so the name is never released
    // between clients for another process to claim.
    void ServeControlPipe(std::shared_ptr<MatchmakingNet::ControlPipe> const Pipe, HANDLE const Instance,
        MatchmakingNet::PipeHandler const Handler)
    {
        OVERLAPPED Overlapped{};
        Overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE const Stop = Pipe->StopEvent;
        DWORD Bytes = 0;

        auto const Step = [&](auto&& Start, DWORD const TimeoutMs)
        {
            ::ResetEvent(Overlapped.hEvent);
            return FinishPipeIo(Instance, Overlapped, Start(), Stop, TimeoutMs, Bytes);
        };

        while (Overlapped.hEvent != nullptr)
        {
            EPipeIo const Connected = Step([&] { return ::ConnectNamedPipe(Instance, &Overlapped); }, INFINITE);
            if (Connected == EPipeIo::Stopped)
                break;

            if (Connected == EPipeIo::Done)
            {
                std::string Request(k_maxPipeMessage, '\0');
                EPipeIo const Read = Step([&] { return ::ReadFile(Instance, Request.data(), k_maxPipeMessage, nullptr, &Overlapped); },
                    k_pipeIoTimeoutMs);
                if (Read == EPipeIo::Stopped)
                    break;

                if (Read == EPipeIo::Done)
                {
                    Request.resize(Bytes);
                    std::string const Reply = Handler(Request);
                    if (Step([&] { return ::WriteFile(Instance, Reply.data(), static_cast<DWORD>(Reply.size()), nullptr, &Overlapped); },
                        k_pipeIoTimeoutMs) == EPipeIo::Stopped)
                    {
                        break;
                    }

                    // Disconnecting discards unread data, so wait for the client to close first.
                    char Drain = 0;
                    if (Step([&] { return ::ReadFile(Instance, &Drain, 1, nullptr, &Overlapped); }, k_pipeIoTimeoutMs) == EPipeIo::Stopped)
                        break;
                }
            }
            else if (::WaitForSingleObject(Stop, 100) == WAIT_OBJECT_0)
            {
                break;
            }
            ::DisconnectNamedPipe(Instance);
        }

        if (Overlapped.hEvent != nullptr)
            ::CloseHandle(Overlapped.hEvent);
        ::CloseHandle(Instance);
    }
}


namespace MatchmakingNet
{
    bool IsValidBaseUrl(std::string const& BaseUrl)
    {
        CrackedUrl Url{};
        return CrackBaseUrl(BaseUrl, Url);
    }

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

        std::wstring const WidePath = Widen(Path);
        std::wstring Headers = L"Accept: application/json\r\n";
        if (!JsonBody.empty())
            Headers += L"Content-Type: application/json\r\n";
        if (!Bearer.empty())
            Headers += L"Authorization: Bearer " + Widen(Bearer) + L"\r\n";
        {
            AccessKeyState& Access = AccessKey();
            std::lock_guard Lock{ Access.Mutex };
            if (auto const Key = Access.Keys.find(BaseUrl); Key != Access.Keys.end())
            {
                if (!Url.Secure)
                {
                    Result.Error = "server passwords require an https:// matchmaker URL";
                    return Result; // Never transmit the password, even to a local HTTP server.
                }
                Headers += L"X-Milan-Access-Key: " + Widen(Key->second) + L"\r\n";
            }
        }

        HINTERNET const Connection = HttpClient().Connect(Url, Result.Error);
        if (Connection == nullptr)
            return Result;

        // A pooled connection that the server or a proxy closed while it was idle fails before
        // any response arrives; the second attempt gets a new connection. A server that cannot
        // be reached at all fails with CANNOT_CONNECT instead and is not retried.
        for (int Attempt = 0;; ++Attempt)
        {
            DWORD Error = ERROR_SUCCESS;
            if (SendOnce(Connection, Url.Secure, Method, WidePath, Headers, JsonBody, ReceiveTimeoutMs, Result, Error))
                return Result;
            if (Attempt > 0 || (Error != ERROR_WINHTTP_CONNECTION_ERROR && Error != ERROR_WINHTTP_INVALID_SERVER_RESPONSE))
                return Result;
            Result = {};
        }
    }

    bool IsValidAccessKey(std::string const& Key)
    {
        return Key.size() <= 256 && std::all_of(Key.begin(), Key.end(), [](char const C) { return C > 0x20 && C < 0x7F; });
    }

    void SetAccessKey(std::string const& BaseUrl, std::string const& Key)
    {
        AccessKeyState& Access = AccessKey();
        std::lock_guard Lock{ Access.Mutex };
        if (!Key.empty() && IsValidAccessKey(Key))
            Access.Keys[BaseUrl] = Key;
        else
            Access.Keys.erase(BaseUrl);
    }

    bool IsPlainHttpToRemote(std::string const& BaseUrl)
    {
        CrackedUrl Url{};
        if (!CrackBaseUrl(BaseUrl, Url) || Url.Secure || ::_wcsicmp(Url.Host.c_str(), L"localhost") == 0)
            return false;

        in_addr Address{};
        if (::InetPtonW(AF_INET, Url.Host.c_str(), &Address) != 1)
            return true;
        auto const* const Bytes = reinterpret_cast<std::uint8_t const*>(&Address.s_addr);
        return Bytes[0] != 127 && !IsLanAddress(Bytes);
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
                GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS,
                nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(Buffer.get()), &Size);
        }
        if (Status != NO_ERROR)
            return Addresses;

        // Joiners on the host's network take the first address. Adapters with a default
        // gateway come first: gateway-less virtual switches (Hyper-V, WSL) are only
        // reachable from this PC.
        for (bool const WantGateway : { true, false })
        {
            for (auto const* Adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES const*>(Buffer.get());
                 Adapter != nullptr && Addresses.size() < Max; Adapter = Adapter->Next)
            {
                if (Adapter->OperStatus != IfOperStatusUp || Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK
                    || (Adapter->FirstGatewayAddress != nullptr) != WantGateway)
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
        }
        return Addresses;
    }

    bool IsSafePeerAddress(std::uint32_t const AddressNetworkOrder, bool const AllowLoopback)
    {
        PeerSecurity::IPv4 target{};
        std::memcpy(target.data(), &AddressNetworkOrder, target.size());
        if (target[0] == 127)
            return AllowLoopback;
        if (target[0] == 0 || target[0] >= 224 || (target[0] == 169 && target[1] == 254))
            return false;
        if (!PeerSecurity::IsPrivate(target))
            return true;

        ULONG size = 16 * 1024;
        std::unique_ptr<std::uint8_t[]> buffer;
        ULONG status = ERROR_BUFFER_OVERFLOW;
        for (int attempt = 0; attempt < 3 && status == ERROR_BUFFER_OVERFLOW; ++attempt)
        {
            buffer = std::make_unique<std::uint8_t[]>(size);
            status = ::GetAdaptersAddresses(AF_INET,
                GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS,
                nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.get()), &size);
        }
        if (status != NO_ERROR)
            return false;
        bool onSubnet = false;
        for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.get()); adapter != nullptr; adapter = adapter->Next)
        {
            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;
            for (auto* gateway = adapter->FirstGatewayAddress; gateway != nullptr; gateway = gateway->Next)
            {
                auto* address = reinterpret_cast<sockaddr_in*>(gateway->Address.lpSockaddr);
                if (address != nullptr && address->sin_family == AF_INET && address->sin_addr.s_addr == AddressNetworkOrder)
                    return false;
            }
            for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next)
            {
                auto* address = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
                if (address == nullptr || address->sin_family != AF_INET)
                    continue;
                PeerSecurity::IPv4 local{};
                std::memcpy(local.data(), &address->sin_addr.s_addr, local.size());
                unsigned const prefix = unicast->OnLinkPrefixLength;
                if (PeerSecurity::IsPrivate(local) && prefix > 0 && prefix < 31)
                {
                    std::uint32_t const mask = 0xFFFFFFFFu << (32 - prefix);
                    if ((PeerSecurity::Number(target) & mask) == (PeerSecurity::Number(local) & mask)
                        && !PeerSecurity::OnSubnet(target, local, prefix))
                        return false;
                }
                onSubnet = onSubnet || PeerSecurity::OnSubnet(target, local, unicast->OnLinkPrefixLength);
            }
        }
        if (onSubnet)
            return true;
        // Overlay adapters may use /32 local addresses plus explicit on-link routes.
        SOCKADDR_INET destination{};
        destination.Ipv4.sin_family = AF_INET;
        destination.Ipv4.sin_addr.s_addr = AddressNetworkOrder;
        MIB_IPFORWARD_ROW2 route{};
        SOCKADDR_INET source{};
        if (::GetBestRoute2(nullptr, 0, nullptr, &destination, 0, &route, &source) != NO_ERROR
            || route.DestinationPrefix.PrefixLength == 0 || route.NextHop.Ipv4.sin_addr.s_addr != 0)
            return false;
        PeerSecurity::IPv4 local{};
        std::memcpy(local.data(), &source.Ipv4.sin_addr.s_addr, local.size());
        return PeerSecurity::IsPrivate(local);
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

    std::shared_ptr<ControlPipe> StartControlPipe(std::wstring const& Name, PipeHandler Handler, std::string& OutError)
    {
        PSECURITY_DESCRIPTOR Descriptor = nullptr;
        if (!CurrentUserOnlyDescriptor(Descriptor, OutError))
            return nullptr;

        SECURITY_ATTRIBUTES Attributes{ sizeof(Attributes), Descriptor, FALSE };
        std::wstring const Path = L"\\\\.\\pipe\\" + Name;
        HANDLE const Instance = ::CreateNamedPipeW(Path.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, k_maxPipeMessage, k_maxPipeMessage, 0, &Attributes);
        ::LocalFree(Descriptor);
        if (Instance == INVALID_HANDLE_VALUE)
        {
            OutError = LastErrorText("CreateNamedPipe");
            return nullptr;
        }

        auto Pipe = std::make_shared<ControlPipe>();
        Pipe->StopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (Pipe->StopEvent == nullptr)
        {
            OutError = LastErrorText("CreateEvent");
            ::CloseHandle(Instance);
            return nullptr;
        }

        std::thread{ ServeControlPipe, Pipe, Instance, std::move(Handler) }.detach();
        return Pipe;
    }

    void StopControlPipe(ControlPipe& Pipe)
    {
        ::SetEvent(Pipe.StopEvent);
    }
}
