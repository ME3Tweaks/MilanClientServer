/**
 * @file        MilanHost/Online/MatchmakingNet.hpp
 * @header      OS networking and crypto used by matchmaking, behind a plain interface.
 *
 * The implementation is compiled without the SDK precompiled header, because that
 * header pulls in <Windows.h> with Winsock 1, which conflicts with Winsock 2,
 * WinHTTP, and IP Helper headers.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>


namespace MatchmakingNet
{
    /** True when @p BaseUrl is an http:// or https:// URL with a host. */
    bool IsValidBaseUrl(std::string const& BaseUrl);

    struct HttpResult
    {
        int                         Status{ 0 };        // 0 when the request did not complete.
        std::string                 Body{};
        std::string                 Error{};            // Transport error description, if any.
    };

    /**
     * Performs a blocking HTTP(S) request. @p BaseUrl is e.g. "http://127.0.0.1:5080",
     * @p Path starts with '/'. Empty @p Bearer and @p JsonBody are omitted.
     * Response bodies over 256 KiB are rejected.
     *
     * Requests share one WinHTTP session, so they reuse open keep-alive connections. HTTPS
     * requires TLS 1.2 or newer and a certificate Windows trusts. Redirects are never followed,
     * so a 3xx comes back as the status.
     */
    HttpResult Request(std::string const& BaseUrl, wchar_t const* Method, std::string const& Path,
        std::string const& Bearer, std::string const& JsonBody, int ReceiveTimeoutMs);

    /** True when @p BaseUrl is plain http:// to a host other than this PC or a LAN address. */
    bool IsPlainHttpToRemote(std::string const& BaseUrl);

    /** Resolves the host part of @p BaseUrl to an IPv4 address in network byte order. */
    bool ResolveUrlHostIPv4(std::string const& BaseUrl, std::uint32_t& OutAddress, std::string& OutError);

    bool HmacSha256(std::span<std::uint8_t const> Key, std::span<std::uint8_t const> Data, std::span<std::uint8_t, 32> Out);

    /**
     * Canonical dotted-quad private IPv4 addresses of up adapters (RFC 1918, 169.254/16, 100.64/10),
     * adapters with a default gateway first.
     */
    std::vector<std::string> LanIPv4Addresses(std::size_t Max);

    /** Local port of a bound IPv4 socket handle, or -1. */
    int LocalPort(std::uintptr_t SocketHandle);

    std::string FormatIPv4(std::uint32_t AddressNetworkOrder, std::uint16_t PortHostOrder);

    /**
     * Local control channel for other programs, such as an invite handler. It is a
     * message-mode named pipe that only the current Windows user can open, and remote
     * clients are rejected. Each connection sends one request message (at most 4 KiB)
     * and receives one reply message. @p Handler runs on the pipe's own thread.
     */
    struct ControlPipe;
    using PipeHandler = std::function<std::string(std::string const& Request)>;

    /** Serves \\.\pipe\<Name> on a detached thread. Returns null and sets @p OutError on failure. */
    std::shared_ptr<ControlPipe> StartControlPipe(std::wstring const& Name, PipeHandler Handler, std::string& OutError);

    /** Asks the pipe thread to exit. It never calls the handler afterwards. */
    void StopControlPipe(ControlPipe& Pipe);
}
