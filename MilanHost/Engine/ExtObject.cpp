#include "Engine/ExtObject.hpp"


// ! ActorUtils namespace.
// ========================================

AWorldInfo* ActorUtils::GlobalWorldInfo() noexcept
{
    if (GWorld == nullptr || *GWorld == nullptr) return nullptr;

    auto* const Level = (*GWorld)->PersistentLevel;
    return (Level != nullptr && Level->Actors.Count() > 0)
        ? reinterpret_cast<AWorldInfo*>(Level->Actors(0))
        : nullptr;
}

bool ActorUtils::GlobalBegunPlay() noexcept
{
    auto* const WorldInfo = GlobalWorldInfo();
    return WorldInfo ? WorldInfo->bBegunPlay : false;
}
