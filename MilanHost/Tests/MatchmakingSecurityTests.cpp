#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "Online/PeerSecurity.hpp"
#include "Online/MatchmakingNet.hpp"
#include "Online/Rendezvous.hpp"

void Require(bool const value, char const* const message)
{
    if (!value)
        throw std::runtime_error(message);
}

std::uint32_t Address(PeerSecurity::IPv4 const& bytes)
{
    std::uint32_t result{};
    std::memcpy(&result, bytes.data(), bytes.size());
    return result;
}

int main()
{
    using namespace PeerSecurity;
    Require(OnSubnet({192,168,1,30}, {192,168,1,20}, 24), "same LAN subnet rejected");
    Require(!OnSubnet({192,168,2,30}, {192,168,1,20}, 24), "foreign LAN subnet accepted");
    Require(!OnSubnet({169,254,169,254}, {169,254,1,20}, 16), "link-local target accepted");
    Require(!OnSubnet({192,168,1,0}, {192,168,1,20}, 24), "network address accepted");
    Require(!OnSubnet({192,168,1,255}, {192,168,1,20}, 24), "broadcast accepted");
    Require(OnSubnet({100,100,1,30}, {100,100,1,20}, 10), "CGNAT overlay rejected");
    Require(!OnSubnet({10,1,1,20}, {192,168,1,20}, 0), "default route accepted as LAN");
    Require(OnSubnet({10,1,1,3}, {10,1,1,2}, 31), "point-to-point subnet rejected");
    Require(!OnSubnet({10,1,1,3}, {10,1,1,2}, 33), "invalid prefix accepted");
    Require(IsModVersion("project-milan-1.2.3"), "valid mod version rejected");
    Require(!IsModVersion("") && !IsModVersion("with spaces") && !IsModVersion(std::string(65, 'a')), "unknown/invalid mod version accepted");
    Require(UrlOption("host:7777?MilanMember=member?MilanProof=proof?other=x", "MilanMember") == "member", "member option lost");
    Require(UrlOption("host:7777?MilanMember=member?MilanProof=proof", "MilanProof") == "proof", "proof option lost");
    Require(!UrlOption("host?MilanMember=a?MilanMember=b", "MilanMember"), "duplicate option accepted");
    Require(!UrlOption("host?OtherMilanMember=a", "MilanMember"), "unrelated option accepted");
    Require(!UrlOption(std::string(4097, 'a'), "MilanMember"), "oversized URL accepted");
    Require(EqualProof(std::string(32, 'a'), std::string(32, 'a')), "equal proof rejected");
    Require(!EqualProof(std::string(32, 'a'), std::string(31, 'a') + 'b'), "wrong proof accepted");
    Rendezvous::Key key{};
    Require(Rendezvous::AdmissionProof(key) == "1b9e7d04a15791351f2c1ef9c26368a2", "admission HMAC does not match independent oracle");
    key[0] = 1;
    Require(Rendezvous::AdmissionProof(key) != "1b9e7d04a15791351f2c1ef9c26368a2", "proof is not keyed");
    Require(!MatchmakingNet::IsSafePeerAddress(Address({169,254,169,254})), "metadata target accepted");
    Require(!MatchmakingNet::IsSafePeerAddress(Address({224,0,0,1})), "multicast accepted");
    Require(!MatchmakingNet::IsSafePeerAddress(Address({0,0,0,0})), "unspecified target accepted");
    Require(!MatchmakingNet::IsSafePeerAddress(Address({127,0,0,1})), "remote loopback accepted");
    Require(MatchmakingNet::IsSafePeerAddress(Address({127,0,0,1}), true), "local-test loopback rejected");
    Require(MatchmakingNet::IsSafePeerAddress(Address({203,0,113,20})), "public unicast rejected");
    MatchmakingNet::SetAccessKey("http://127.0.0.1:15999", "party-password");
    auto const result = MatchmakingNet::Request("http://127.0.0.1:15999", L"GET", "/v1/info", "", "", 100);
    Require(result.Status == 0 && result.Error == "server passwords require an https:// matchmaker URL", "client attempted plaintext password transmission");
    std::cout << "Matchmaking security policies passed (subnets, URL admission, HMAC, mod identity, plaintext password rejection).\n";
}
