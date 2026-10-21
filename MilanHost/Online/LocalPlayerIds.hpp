#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>

#include <Windows.h>
#include <bcrypt.h>

// We don't have any fingerprinting so it's just random
// IDs are used for kick votes
class LocalPlayerIds final
{
    std::array<std::uint64_t, 256> Ids{};
    std::mutex Mutex{};

public:
    bool Get(std::uint8_t const LocalUser, std::uint64_t& Out)
    {
        std::lock_guard Lock{ Mutex };
        Out = 0;
        std::uint64_t& Id = Ids[LocalUser];
        if (Id == 0)
        {
            for (int Attempt = 0; Attempt < 16; ++Attempt)
            {
                std::uint64_t Candidate{};
                if (::BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&Candidate), sizeof(Candidate),
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
                {
                    return false;
                }
                if (Candidate != 0 && std::find(Ids.begin(), Ids.end(), Candidate) == Ids.end())
                {
                    Id = Candidate;
                    break;
                }
            }
        }
        Out = Id;
        return Id != 0;
    }
};
