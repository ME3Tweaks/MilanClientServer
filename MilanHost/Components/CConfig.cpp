#include "Components/CConfig.hpp"


// ! CConfig implementation.
// ========================================

bool CConfig::Initialize(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(L"component '%s' initialized", k_typeName);
    return true;
}

void CConfig::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(L"component '%s' terminating", k_typeName);
}

void CConfig::LoadConfig()
{
    FString ConfigError{};

    if (!m_config.TryRead(k_configFilename, ConfigError)) [[unlikely]]
    {
        MILAN_ERRORW(L"failed to load config '%s': %s", k_configFilename, *ConfigError);
        MILAN_WARNW(L"default configuration will be used, which is probably not what you want");
        m_config = ClientConfig{};
    }
}
