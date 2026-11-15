#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "fpng/fpng.h"

#include "Components/CCommands.hpp"
#include "Engine/ExtObject.hpp"
#include "Engine/TransparentRender.hpp"
#include "Shell/CmdArgs.hpp"


namespace TransparentRender
{
    namespace
    {
        enum class CaptureStage
        {
            Idle,
            AwaitingWhite,
            AwaitingBlack,
        };

        CaptureStage stage = CaptureStage::Idle;
        int rendersUntilCapture = -1;

        UFunction* gHudPostRender = nullptr;
        UFunction* gHudMPPostRender = nullptr;

        struct CachedComponentMaterials
        {
            UStaticMeshComponent* Component = nullptr;
            std::vector<UMaterialInterface*> OriginalMaterials;
        };

        std::vector<CachedComponentMaterials> gCachedComponents;
        std::vector<unsigned char> gWhiteCapture;
        std::wstring gCaptureBaseName;
        int gTransparentIndex = 1;
        constexpr int kMaxTransparentIndex = 99999;

        UMaterial* GetBlackUnlitMaterial()
        {
            // Find black material for light cycle
            static UMaterial* cached = nullptr;
            if (cached == nullptr)
            {
                cached = UObject::FindObject<UMaterial>(L"Material EngineDebugMaterials.BlackUnlitMaterial");
            }
            return cached;
        }

        void RunConsoleCommand(wchar_t const* command)
        {
            UEngine* const Engine = (GEngine != nullptr) ? *GEngine : nullptr;
            if (Engine == nullptr || Engine->GamePlayers.Count() == 0)
            {
                return;
            }

            ULocalPlayer* const Player = Engine->GamePlayers(0);
            if (Player == nullptr || Player->Actor == nullptr)
            {
                return;
            }

            Player->Actor->ConsoleCommand(FString{ command }, FALSE);
        }

        // LevelColorationUnlitMaterial is unlit, use it for white cycle
        void ApplyWhiteMaterial(UStaticMeshComponent* component, int elementCount)
        {
            UEngine* const Engine = (GEngine != nullptr) ? *GEngine : nullptr;
            auto whiteBase = (Engine != nullptr) ? Engine->LevelColorationUnlitMaterial : nullptr;
            if (!whiteBase) {
                return;
            }

            // Tune color to white
            FLinearColor white{ 1.f, 1.f, 1.f, 1.f };
            for (int i = 0; i < elementCount; i++)
            {
                component->SetMaterial(i, whiteBase);
                auto mic = component->CreateAndSetMaterialInstanceConstant(i);
                if (mic)
                {
                    mic->SetVectorParameterValue(SFXName(L"Color", 0), &white);
                }
            }
        }

        void ApplyBlackMaterial(UStaticMeshComponent* component, int elementCount)
        {
            auto black = GetBlackUnlitMaterial();
            if (!black) {
                return;
            }

            for (int i = 0; i < elementCount; i++)
            {
                component->SetMaterial(i, black);
            }
        }

        void RestoreOriginalMaterials()
        {
            for (auto& cached : gCachedComponents)
            {
                if (!cached.Component)
                {
                    continue;
                }

                for (int i = 0; i < static_cast<int>(cached.OriginalMaterials.size()); i++)
                {
                    cached.Component->SetMaterial(i, cached.OriginalMaterials[i]);
                }
            }
            gCachedComponents.clear();
        }

        // Screenshot takes two frames so we slow the game down as much as possible to slow the movement down to
        // make a cleaner shot. Sadly particles and shaders do not care about this.
        constexpr float kCaptureTimeDilation = 0.0001f;
        std::vector<std::pair<AWorldInfo*, float>> gSavedTimeDilation;

        void SlowWorldTime()
        {
            gSavedTimeDilation.clear();
            for (TypedObjectIterator<AWorldInfo, true, false> it; it; ++it)
            {
                auto info = *it;
                if (!info) {
                    continue;
                }

                gSavedTimeDilation.emplace_back(info, info->TimeDilation);
                info->TimeDilation = kCaptureTimeDilation;
            }
        }

        void RestoreWorldTime()
        {
            for (auto& [info, original] : gSavedTimeDilation)
            {
                info->TimeDilation = original;
            }
            gSavedTimeDilation.clear();
        }

        void StartCapture()
        {
            if (stage != CaptureStage::Idle) {
                // Already mid-sequence, ignore
                return;
            }

            gHudPostRender = UObject::FindObject<UFunction>(L"Function SFXGame.BioHUD.PostRender");
            // SFXHUDMP overrides PostRender in multiplayer
            gHudMPPostRender = UObject::FindObject<UFunction>(L"Function SFXGameMPContent.SFXHUDMP.PostRender");
            if (gHudPostRender == nullptr && gHudMPPostRender == nullptr)
            {
                MILAN_WARNW(L"transparentrender: no HUD PostRender function found, capture skipped");
                return;
            }

            // Slow down time and prepare
            gCachedComponents.clear();
            gWhiteCapture.clear();
            SlowWorldTime();

            for (TypedObjectIterator<UStaticMeshComponent, true, false> it; it; ++it)
            {
                auto component = *it;
                if (!component || !component->Owner || !component->bAttached)
                {
                    continue;
                }

                const int elementCount = component->GetNumElements();
                if (elementCount <= 0)
                {
                    continue;
                }

                CachedComponentMaterials cached;
                cached.Component = component;
                cached.OriginalMaterials.reserve(elementCount);
                for (int i = 0; i < elementCount; i++)
                {
                    cached.OriginalMaterials.push_back(component->GetMaterial(i));
                }
                gCachedComponents.push_back(std::move(cached));

                ApplyWhiteMaterial(component, elementCount);
            }

            // We're at the white shot stage
            stage = CaptureStage::AwaitingWhite;
            rendersUntilCapture = 1;
        }

        // Generalized (Smith/Blinn) difference-matting is used
        void WriteComposite(int width, int height, const std::vector<unsigned char>& white, const std::vector<unsigned char>& black, const std::wstring& baseName)
        {
            if (white.size() != black.size() || white.empty()) {
                return;
            }

            const auto sampleCorners = [&](const std::vector<unsigned char>& buf, float outRgb[3])
            {
                const size_t corners[4] = {
                    0,
                    static_cast<size_t>(width - 1) * 3,
                    static_cast<size_t>(height - 1) * static_cast<size_t>(width) * 3,
                    (static_cast<size_t>(height - 1) * static_cast<size_t>(width) + (width - 1)) * 3
                };
                for (int c = 0; c < 3; c++)
                {
                    float sum = 0.f;
                    for (size_t corner : corners) {
                        sum += buf[corner + c] / 255.f;
                    }
                    outRgb[c] = sum / 4.f;
                }
            };

            float backdropWhite[3], backdropBlack[3], backdropDiff[3];
            sampleCorners(white, backdropWhite);
            sampleCorners(black, backdropBlack);
            for (int c = 0; c < 3; c++)
            {
                backdropDiff[c] = backdropWhite[c] - backdropBlack[c];
                if (std::fabs(backdropDiff[c]) < 0.05f) {
                    backdropDiff[c] = 1.f; // Degenerate backdrop sample
                }
            }

            std::vector<unsigned char> rgba(static_cast<size_t>(width) * height * 4);

            const int pixelCount = width * height;
            for (int p = 0; p < pixelCount; p++)
            {
                const size_t rgbOffset = static_cast<size_t>(p) * 3;
                const size_t rgbaOffset = static_cast<size_t>(p) * 4;
                if (rgbOffset + 2 >= white.size()) {
                    break;
                }

                float alphaSum = 0.f;
                for (int c = 0; c < 3; c++)
                {
                    const float w = white[rgbOffset + c] / 255.f;
                    const float b = black[rgbOffset + c] / 255.f;
                    alphaSum += std::clamp(1.f - (w - b) / backdropDiff[c], 0.f, 1.f);
                }
                const float alpha = alphaSum / 3.f;

                for (int c = 0; c < 3; c++)
                {
                    const float b = black[rgbOffset + c] / 255.f;
                    const float value = (alpha > 0.001f) ? std::clamp((b - (1.f - alpha) * backdropBlack[c]) / alpha, 0.f, 1.f) : 0.f;
                    rgba[rgbaOffset + c] = static_cast<unsigned char>(value * 255.f + 0.5f);
                }
                rgba[rgbaOffset + 3] = static_cast<unsigned char>(alpha * 255.f + 0.5f);
            }

            static bool fpngInitialized = false;
            if (!fpngInitialized)
            {
                fpng::fpng_init();
                fpngInitialized = true;
            }

            auto parentDir = std::filesystem::path(baseName).parent_path();
            auto outDir = parentDir / L"LMP";

            std::error_code ec;
            std::filesystem::create_directories(outDir, ec);
            if (!std::filesystem::exists(outDir)) {
                return; // Cannot create output folder
            }

            std::filesystem::path outPath;
            while (gTransparentIndex < kMaxTransparentIndex)
            {
                auto fname = std::format(L"TransparentRender{:05}.png", gTransparentIndex);
                gTransparentIndex++;

                outPath = outDir / fname;
                if (!std::filesystem::exists(outPath))
                {
                    break;
                }
            }

            if (gTransparentIndex >= kMaxTransparentIndex) {
                return; // Can't take any more.
            }

            fpng::fpng_encode_image_to_wfile(outPath.c_str(), rgba.data(), width, height, 4, 0U); // 4bpp (RGBA), no flags
        }
    }

    bool IsTransparentRenderEnabled()
    {
        return CmdArgs::HasFlag(L"-transparentrender");
    }

    void RegisterCommands(CCommands& Commands)
    {
        Commands.RegisterCommand(nullptr, L"transparentrender",
            [](wchar_t const*)
            {
                StartCapture();
                return true;
            });
    }

    void OnProcessEvent(UFunction* const Function)
    {
        if ((stage == CaptureStage::AwaitingWhite || stage == CaptureStage::AwaitingBlack) && rendersUntilCapture >= 0)
        {
            if (Function == gHudPostRender || Function == gHudMPPostRender)
            {
                if (rendersUntilCapture > 0)
                {
                    rendersUntilCapture--;
                }
                else
                {
                    rendersUntilCapture = -1; // Disarm until HandleCapture rearms it for the next stage
                    RunConsoleCommand(L"screenshot");
                }
            }
        }
    }

    bool HandleCapture(wchar_t const* inputBaseName, int width, int height, std::vector<unsigned char> const& rgb)
    {
        if (stage == CaptureStage::AwaitingWhite)
        {
            gWhiteCapture = rgb;
            gCaptureBaseName = inputBaseName;

            for (auto& cached : gCachedComponents)
            {
                if (cached.Component)
                {
                    ApplyBlackMaterial(cached.Component, static_cast<int>(cached.OriginalMaterials.size()));
                }
            }

            stage = CaptureStage::AwaitingBlack;
            rendersUntilCapture = 1;
            return true;
        }

        if (stage == CaptureStage::AwaitingBlack)
        {
            RestoreOriginalMaterials();
            RestoreWorldTime();
            WriteComposite(width, height, gWhiteCapture, rgb, gCaptureBaseName);
            gWhiteCapture.clear();
            stage = CaptureStage::Idle;
            return true;
        }

        return false;
    }
}
