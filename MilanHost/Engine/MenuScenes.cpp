#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Base/Logger.hpp"
#include "Components/CCommands.hpp"
#include "Engine/ExtObject.hpp"
#include "Engine/Hooks.hpp"
#include "Engine/MenuScenes.hpp"
#include "Shell/CmdArgs.hpp"


namespace MenuScenes
{
    constexpr wchar_t k_entryMenu[] = L"EntryMenu";
    constexpr wchar_t k_scenePrefix[] = L"EntryMenu_";

    /** The scene packages the mod ships, by package name */
    std::vector<std::wstring> const& GetScenes()
    {
        static std::vector<std::wstring> s_scenes = []
        {
            std::vector<std::wstring> Scenes{};
            wchar_t Executable[MAX_PATH]{};
            ::GetModuleFileNameW(nullptr, Executable, MAX_PATH);

            // <root>\Binaries\Win64\MassEffect3.exe
            auto const Folder = std::filesystem::path{ Executable }.parent_path().parent_path().parent_path()
                / L"BioGame" / L"DLC" / L"DLC_MOD_ProjectMilan" / L"CookedPCConsole" / L"EntryMenu" / L"Scenes";
            std::error_code Error{};
            for (auto const& Entry : std::filesystem::directory_iterator{ Folder, Error })
            {
                auto const Name = Entry.path().stem().wstring();
                if (Entry.path().extension() == L".pcc" && Name.starts_with(k_scenePrefix))
                {
                    Scenes.push_back(Name);
                }
            }

            MILAN_INFOW(ELOG_EngineHooks, L"%zu main menu scene(s) in %s", Scenes.size(), Folder.c_str());
            return Scenes;
        }();
        return s_scenes;
    }

#ifndef _FINAL_RELEASE
    /** In non final release builds we allow forced picking of scenes */
    std::wstring GetForcedScene()
    {
        constexpr wchar_t k_flag[] = L"-menuscene=";
        wchar_t const* const Start = std::wcsstr(CmdArgs::Get(), k_flag);
        if (Start == nullptr)
        {
            return {};
        }

        std::wstring Map{ Start + std::size(k_flag) - 1 };
        Map = Map.substr(0, Map.find_first_of(L" \t\""));
        return k_scenePrefix + Map;
    }
#endif

    std::wstring PickScene()
    {
        static std::wstring s_lastScene{};
        auto const& Scenes = GetScenes();

#ifndef _FINAL_RELEASE
        static std::wstring const s_forcedScene = GetForcedScene();
        if (!s_forcedScene.empty())
        {
            for (auto const& Scene : Scenes)
            {
                if (::_wcsicmp(Scene.c_str(), s_forcedScene.c_str()) == 0)
                {
                    return Scene;
                }
            }
            MILAN_WARNW(ELOG_EngineHooks, L"-menuscene: %s isn't a available; picking one at random", s_forcedScene.c_str());
        }
#endif

        // Don't show the same backdrop twice in a row if returning to menu
        std::vector<std::wstring const*> Choices{};
        for (auto const& Scene : Scenes)
        {
            if (Scenes.size() == 1 || Scene != s_lastScene)
            {
                Choices.push_back(&Scene);
            }
        }

        static std::mt19937 s_random{ std::random_device{}() };
        std::uniform_int_distribution<std::size_t> Distribution{ 0, Choices.size() - 1 };
        s_lastScene = *Choices[Distribution(s_random)];
        return s_lastScene;
    }

    void RedirectEntryMenu(FURL* const Url)
    {
        if (::_wcsicmp(*Url->Map, k_entryMenu) != 0 || GetScenes().empty())
        {
            return;
        }

        auto const Scene = PickScene();
        MILAN_INFOW(ELOG_EngineHooks, L"main menu scene: %s", Scene.c_str());
        Url->Map = FString::Printf(L"%s", Scene.c_str());
    }

    bool IsMainMenuMap(wchar_t const* const MapName)
    {
        return MapName != nullptr && ::_wcsnicmp(MapName, k_entryMenu, std::size(k_entryMenu) - 1) == 0;
    }

    void FinishLoading()
    {
        UWorld* const World = (GWorld != nullptr) ? *GWorld : nullptr;
        if (World == nullptr)
        {
            return;
        }

        auto const Start = std::chrono::steady_clock::now();
        auto const Elapsed = [&Start]
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        };

        // LoadMap ends before the scene's streaming levels load
        // This makes it keep loading
        if (UWorld_FlushLevelStreaming != nullptr)
        {
            (*UWorld_FlushLevelStreaming)(World, nullptr, FALSE);
            MILAN_INFOW(ELOG_EngineHooks, L"menu scene: streaming levels loaded in %.0f ms", Elapsed());
        }

        // Stream in textures
        void* const Manager = (GStreamingManager != nullptr) ? *GStreamingManager : nullptr;
        if (Manager != nullptr)
        {
            constexpr std::size_t k_blockOnTexturesSlot = 0x40 / sizeof(void*);
            using t_BlockOnTextures = void(void* Manager, int, int, int);
            auto* const BlockOnTextures = reinterpret_cast<t_BlockOnTextures*>((*reinterpret_cast<void***>(Manager))[k_blockOnTexturesSlot]);
            BlockOnTextures(Manager, 1, 1, 1);
            MILAN_INFOW(ELOG_EngineHooks, L"menu scene: textures streamed, %.0f ms in total", Elapsed());
        }
    }

#ifndef _FINAL_RELEASE
    // Helper for placing menu scenes
    // ! Scene capture: records cameras and the terminal placement in game, for Pipelines/EntryMenu/Scenes/<Map>.json.
    // ========================================

    constexpr char k_terminalCameraTag[] = "MilanMenu_TerminalCam";

    /** The terminal actor in BioD_MilanMenu. Its menu screen is attached to it, so it moves with it. */
    constexpr wchar_t k_terminalActorPath[] = L"BioD_MilanMenu.TheWorld.PersistentLevel.SFXSkeletalMeshActor_0";

    /** How far the anchor trace looks for a surface. */
    constexpr float k_anchorTraceDistance = 100000.f;

    /**
     * How far above the traced surface the anchor goes. The terminal's origin is where it sat on its desk in the stock
     * menu, and the desk's base reaches about 95 units below it, so on a floor this stands the desk on the floor.
     */
    constexpr float k_anchorDeskHeight = 90.f;

    struct CapturedView
    {
        FVector Location{};
        FRotator Rotation{};
        float FOV = 0.f;
    };

    struct SceneCaptures
    {
        std::wstring Map{};
        std::optional<CapturedView> Vista{};
        std::optional<CapturedView> VistaEnd{};
        std::optional<CapturedView> TerminalCamera{};
        std::optional<CapturedView> TerminalAnchor{};
        std::optional<FVector> FillLight{};
    };

    /**
     * The current map without its EntryMenu_ or BioP_ prefix, e.g. MPJngl. Scene coordinates are world space in the map's
     * own levels, so a scene can be captured in its menu scene or in the multiplayer level itself (where the debug
     * camera works).
     */
    std::wstring GetCurrentSceneMap()
    {
        UWorld* const World = (GWorld != nullptr) ? *GWorld : nullptr;
        if (World == nullptr || World->Outer == nullptr)
        {
            return {};
        }
        std::wstring Name = *World->Outer->GetName();
        for (wchar_t const* const Prefix : { k_scenePrefix, L"BioP_" })
        {
            std::size_t const Length = std::wcslen(Prefix);
            if (Name.size() > Length && ::_wcsnicmp(Name.c_str(), Prefix, Length) == 0)
            {
                Name.erase(0, Length);
                break;
            }
        }
        return Name;
    }

    /** What has been captured for the current map. Starts over when the map changes. */
    SceneCaptures& GetCaptures()
    {
        static SceneCaptures s_captures{};
        auto Map = GetCurrentSceneMap();
        if (s_captures.Map != Map)
        {
            s_captures = SceneCaptures{};
            s_captures.Map = std::move(Map);
        }
        return s_captures;
    }

    /** The player controller the screen shows: the debug camera's while it is on (it takes the player), else the player's. */
    APlayerController* GetViewingController()
    {
        for (TypedObjectIterator<APlayerController, true, false> Iter{}; Iter; ++Iter)
        {
            if ((*Iter)->Player != nullptr && (*Iter)->PlayerCamera != nullptr)
            {
                return *Iter;
            }
        }
        return nullptr;
    }

    /** The player's own controller, not the debug camera's, so the scene's camera can be previewed on it. */
    APlayerController* GetPlayerController()
    {
        APlayerController* const Viewing = GetViewingController();
        if (Viewing != nullptr && Viewing->IsA(ADebugCameraController::StaticClass()))
        {
            return static_cast<ADebugCameraController*>(Viewing)->OryginalControllerRef;
        }
        return Viewing;
    }

    std::optional<CapturedView> GetCurrentView()
    {
        APlayerController* const Controller = GetViewingController();
        if (Controller == nullptr)
        {
            return std::nullopt;
        }
        auto const& POV = Controller->PlayerCamera->CameraCache.POV;
        // The debug camera's rotation keeps counting past a full turn; wrap it into -32768..32767 so a camera blend
        // between two captures takes the short way round.
        auto const Wrap = [](int const Value) { return static_cast<int>(static_cast<short>(Value & 0xFFFF)); };
        FRotator const Rotation{ Wrap(POV.Rotation.Pitch), Wrap(POV.Rotation.Yaw), Wrap(POV.Rotation.Roll) };
        return CapturedView{ POV.location, Rotation, POV.FOV };
    }

    AActor* FindTerminal()
    {
        for (TypedObjectIterator<AActor, true, false> Iter{}; Iter; ++Iter)
        {
            if ((*Iter)->GetFullName().Contains(k_terminalActorPath, true))
            {
                return *Iter;
            }
        }
        return nullptr;
    }

    ACameraActor* FindTaggedCamera(char const* const Tag)
    {
        for (TypedObjectIterator<ACameraActor, true, false> Iter{}; Iter; ++Iter)
        {
            if (::_stricmp((*Iter)->Tag.GetName(), Tag) == 0)
            {
                return *Iter;
            }
        }
        return nullptr;
    }

    FVector GetViewDirection(FRotator const& Rotation)
    {
        constexpr double k_unitsToRadians = 3.14159265358979323846 * 2.0 / 65536.0;
        double const Pitch = Rotation.Pitch * k_unitsToRadians;
        double const Yaw = Rotation.Yaw * k_unitsToRadians;
        return FVector{
            static_cast<float>(std::cos(Pitch) * std::cos(Yaw)),
            static_cast<float>(std::cos(Pitch) * std::sin(Yaw)),
            static_cast<float>(std::sin(Pitch)) };
    }

    /**
     * Where the terminal isn't loaded (capturing in the multiplayer level itself), draws its bounding box at the anchor,
     * turned to the anchor's yaw, and a green arrow the way its screen faces. Replaces the previous outline.
     */
    void DrawTerminalOutline(AActor* const Drawer, CapturedView const& Anchor)
    {
        // Gui_Terminal01_Base's bounds at the terminal actor's DrawScale3D (0.27034) in BioD_MilanMenu.
        constexpr float k_centerX = 26.7f;
        constexpr float k_centerY = 2.0f;
        constexpr float k_centerZ = 58.5f;
        constexpr float k_extentX = 80.3f;
        constexpr float k_extentY = 120.5f;
        constexpr float k_extentZ = 62.6f;
        constexpr float k_arrowLength = 250.f;

        constexpr double k_unitsToRadians = 3.14159265358979323846 * 2.0 / 65536.0;
        double const Yaw = Anchor.Rotation.Yaw * k_unitsToRadians;
        float const Cos = static_cast<float>(std::cos(Yaw));
        float const Sin = static_cast<float>(std::sin(Yaw));
        auto const ToWorld = [&](float const X, float const Y, float const Z)
        {
            return FVector{
                Anchor.Location.X + X * Cos - Y * Sin,
                Anchor.Location.Y + X * Sin + Y * Cos,
                Anchor.Location.Z + Z };
        };

        FVector Corners[8]{};
        for (int Index = 0; Index < 8; ++Index)
        {
            float const X = k_centerX + ((Index & 1) != 0 ? k_extentX : -k_extentX);
            float const Y = k_centerY + ((Index & 2) != 0 ? k_extentY : -k_extentY);
            float const Z = k_centerZ + ((Index & 4) != 0 ? k_extentZ : -k_extentZ);
            Corners[Index] = ToWorld(X, Y, Z);
        }

        Drawer->FlushPersistentDebugLines();
        for (int Index = 0; Index < 8; ++Index)
        {
            for (int const Bit : { 1, 2, 4 })
            {
                if ((Index & Bit) == 0)
                {
                    Drawer->DrawDebugLine(Corners[Index], Corners[Index | Bit], 255, 200, 0, TRUE);
                }
            }
        }

        FVector const ArrowStart = ToWorld(k_centerX, k_centerY, k_centerZ);
        FVector const ArrowEnd = ToWorld(k_centerX + k_arrowLength, k_centerY, k_centerZ);
        Drawer->DrawDebugLine(ArrowStart, ArrowEnd, 0, 255, 0, TRUE);
        Drawer->DrawDebugLine(ArrowEnd, ToWorld(k_centerX + k_arrowLength - 50.f, k_centerY - 30.f, k_centerZ), 0, 255, 0, TRUE);
        Drawer->DrawDebugLine(ArrowEnd, ToWorld(k_centerX + k_arrowLength - 50.f, k_centerY + 30.f, k_centerZ), 0, 255, 0, TRUE);
    }

    /**
     * Traces from the view along its centre to the first surface that isn't the terminal (or something attached to it),
     * and stands the terminal on it facing the view, so its screen faces wherever the camera is. The anchor goes
     * k_anchorDeskHeight above the hit, so aim at the floor where the desk should stand.
     */
    std::optional<CapturedView> PlaceTerminalAtCrosshair(AActor* const Terminal)
    {
        APlayerController* const Controller = GetViewingController();
        if (Controller == nullptr)
        {
            return std::nullopt;
        }
        auto const& POV = Controller->PlayerCamera->CameraCache.POV;
        FVector const Direction = GetViewDirection(POV.Rotation);

        FVector Start = POV.location;
        constexpr int k_maxSkips = 8;
        for (int Attempt = 0; Attempt <= k_maxSkips; ++Attempt)
        {
            FVector const End{
                Start.X + Direction.X * k_anchorTraceDistance,
                Start.Y + Direction.Y * k_anchorTraceDistance,
                Start.Z + Direction.Z * k_anchorTraceDistance };
            FVector Hit{};
            FVector Normal{};
            FTraceHitInfo HitInfo{};
            AActor* const HitActor = Controller->Trace(End, Start, TRUE, FVector{}, 0, &Hit, &Normal, &HitInfo);
            if (HitActor == nullptr)
            {
                return std::nullopt;
            }

            bool const bHitTerminal = HitActor == Terminal || (Terminal != nullptr && HitActor->Base == Terminal);
            if (bHitTerminal)
            {
                // Step just past the terminal and look again.
                Start = FVector{ Hit.X + Direction.X * 5.f, Hit.Y + Direction.Y * 5.f, Hit.Z + Direction.Z * 5.f };
                continue;
            }

            constexpr double k_radiansToUnits = 65536.0 / (3.14159265358979323846 * 2.0);
            double const FacingYaw = std::atan2(POV.location.Y - Hit.Y, POV.location.X - Hit.X) * k_radiansToUnits;
            int const Yaw = static_cast<int>(static_cast<short>(std::lround(FacingYaw) & 0xFFFF));
            CapturedView Anchor{ FVector{ Hit.X, Hit.Y, Hit.Z + k_anchorDeskHeight }, FRotator{ 0, Yaw, 0 }, 0.f };
            if (Terminal != nullptr)
            {
                Terminal->SetLocation(Anchor.Location, FALSE);
                Terminal->SetRotation(Anchor.Rotation);
            }
            else
            {
                DrawTerminalOutline(Controller, Anchor);
            }
            return Anchor;
        }
        return std::nullopt;
    }

    /**
     * The terminal camera a scene gets when its file sets none: the stock EntryMenu's terminal camera (CerberusNetwork_Cam01)
     * where it stands relative to the stock terminal, moved to the anchor. Keep in step with the pipeline's MenuSceneFraming.
     */
    CapturedView GetStockTerminalCamera(CapturedView const& Anchor)
    {
        // The stock EntryMenu's SFXSkeletalMeshActor_0 (the terminal) and CameraActor_1.
        constexpr float k_terminalX = 32268.05f;
        constexpr float k_terminalY = -135.403f;
        constexpr float k_terminalZ = 470.574f;
        constexpr int k_terminalYaw = -32128;
        constexpr float k_cameraX = 31923.87f;
        constexpr float k_cameraY = -235.175f;
        constexpr float k_cameraZ = 570.544f;
        constexpr int k_cameraPitch = -1425;
        constexpr int k_cameraYaw = 5179;
        constexpr int k_cameraRoll = -177;
        constexpr float k_cameraFov = 46.f;

        constexpr double k_unitsToRadians = 3.14159265358979323846 * 2.0 / 65536.0;
        double const Turn = (Anchor.Rotation.Yaw - k_terminalYaw) * k_unitsToRadians;
        float const Cos = static_cast<float>(std::cos(Turn));
        float const Sin = static_cast<float>(std::sin(Turn));
        float const OffsetX = k_cameraX - k_terminalX;
        float const OffsetY = k_cameraY - k_terminalY;
        FVector const Location{
            Anchor.Location.X + OffsetX * Cos - OffsetY * Sin,
            Anchor.Location.Y + OffsetX * Sin + OffsetY * Cos,
            Anchor.Location.Z + (k_cameraZ - k_terminalZ) };
        int const Yaw = static_cast<int>(static_cast<short>((Anchor.Rotation.Yaw + k_cameraYaw - k_terminalYaw) & 0xFFFF));
        return CapturedView{ Location, FRotator{ k_cameraPitch, Yaw, k_cameraRoll }, k_cameraFov };
    }

    /** The terminal camera the scene will use: the captured one if there is one, else the stock framing of the captured anchor. */
    std::optional<CapturedView> GetTerminalView(SceneCaptures const& Captures)
    {
        if (Captures.TerminalCamera)
        {
            return Captures.TerminalCamera;
        }
        if (Captures.TerminalAnchor)
        {
            return GetStockTerminalCamera(*Captures.TerminalAnchor);
        }
        return std::nullopt;
    }

    nlohmann::json ToJson(FVector const& Vector)
    {
        auto const Round = [](float const Value) { return std::round(Value * 10.0) / 10.0; };
        return { { "X", Round(Vector.X) }, { "Y", Round(Vector.Y) }, { "Z", Round(Vector.Z) } };
    }

    nlohmann::json ToJson(FRotator const& Rotation)
    {
        return { { "Pitch", Rotation.Pitch }, { "Yaw", Rotation.Yaw }, { "Roll", Rotation.Roll } };
    }

    nlohmann::json ToJson(CapturedView const& View, bool const bWithFov)
    {
        nlohmann::json Json{ { "Location", ToJson(View.Location) }, { "Rotation", ToJson(View.Rotation) } };
        if (bWithFov)
        {
            Json["FOV"] = std::round(View.FOV * 10.0) / 10.0;
        }
        return Json;
    }

    std::string ToUtf8(std::wstring const& Text)
    {
        int const Size = ::WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), nullptr, 0, nullptr, nullptr);
        std::string Result(static_cast<std::size_t>(Size), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), Result.data(), Size, nullptr, nullptr);
        return Result;
    }

    /** The captured fields in the scene file's layout. Fields not captured yet are left out. */
    nlohmann::json CapturesToJson(SceneCaptures const& Captures)
    {
        nlohmann::json Json{ { "Map", ToUtf8(Captures.Map) } };
        if (Captures.TerminalAnchor)
        {
            Json["TerminalAnchor"] = ToJson(*Captures.TerminalAnchor, false);
        }
        if (Captures.Vista)
        {
            Json["VistaCamera"]["Start"] = ToJson(*Captures.Vista, true);
        }
        if (Captures.VistaEnd)
        {
            Json["VistaCamera"]["End"] = ToJson(*Captures.VistaEnd, true);
        }
        if (Captures.TerminalCamera)
        {
            Json["TerminalCamera"] = ToJson(*Captures.TerminalCamera, true);
        }
        if (Captures.FillLight)
        {
            Json["FillLight"] = { { "Location", ToJson(*Captures.FillLight) } };
        }
        return Json;
    }

    std::wstring TrimParams(wchar_t const* const InParams)
    {
        std::wstring Params = InParams != nullptr ? InParams : L"";
        auto const First = Params.find_first_not_of(L" \t");
        auto const Last = Params.find_last_not_of(L" \t");
        return First == std::wstring::npos ? std::wstring{} : Params.substr(First, Last - First + 1);
    }
#endif

    void RegisterCommands(CCommands& Commands)
    {
#ifndef _FINAL_RELEASE
        // Logs the current view (the debug camera's while it is on) as the JSON a scene file uses for its cameras.
        Commands.RegisterCommand(L"milanmenu", L"pos",
            [](wchar_t const*)
            {
                auto const View = GetCurrentView();
                if (!View)
                {
                    MILAN_WARNW(ELOG_EngineHooks, L"milanmenu.pos: no player camera");
                    return true;
                }
                MILAN_INFOW(ELOG_EngineHooks, L"milanmenu.pos: %S", ToJson(*View, true).dump().c_str());
                return true;
            });

        // milanmenu.capture vista|vistaend|anchor: records part of the current scene's placement. anchor traces from the
        // view's centre and stands the terminal on what it hits, facing the view. The terminal camera and fill light follow
        // from the anchor (the stock main menu's framing); terminalcam and light capture overrides for them.
        Commands.RegisterCommand(L"milanmenu", L"capture",
            [](wchar_t const* const InParams)
            {
                auto const Slot = TrimParams(InParams);
                auto& Captures = GetCaptures();
                auto const View = GetCurrentView();
                if (!View)
                {
                    MILAN_WARNW(ELOG_EngineHooks, L"milanmenu.capture: no player camera");
                    return true;
                }

                if (::_wcsicmp(Slot.c_str(), L"vista") == 0)
                {
                    Captures.Vista = View;
                }
                else if (::_wcsicmp(Slot.c_str(), L"vistaend") == 0)
                {
                    Captures.VistaEnd = View;
                }
                else if (::_wcsicmp(Slot.c_str(), L"terminalcam") == 0)
                {
                    Captures.TerminalCamera = View;
                }
                else if (::_wcsicmp(Slot.c_str(), L"light") == 0)
                {
                    Captures.FillLight = View->Location;
                }
                else if (::_wcsicmp(Slot.c_str(), L"anchor") == 0)
                {
                    AActor* const Terminal = FindTerminal();
                    if (Terminal == nullptr)
                    {
                        MILAN_WARNW(ELOG_EngineHooks, L"milanmenu.capture anchor: the terminal isn't loaded (load a menu scene)");
                    }
                    auto const Anchor = PlaceTerminalAtCrosshair(Terminal);
                    if (!Anchor)
                    {
                        MILAN_WARNW(ELOG_EngineHooks, L"milanmenu.capture anchor: the trace hit nothing");
                        return true;
                    }
                    Captures.TerminalAnchor = Anchor;
                }
                else
                {
                    MILAN_WARNW(ELOG_EngineHooks, L"milanmenu.capture: usage: milanmenu.capture vista|vistaend|anchor (overrides: terminalcam|light)");
                    return true;
                }

                MILAN_INFOW(ELOG_EngineHooks, L"milanmenu.capture %s for %s: %S", Slot.c_str(), Captures.Map.c_str(),
                    CapturesToJson(Captures).dump().c_str());
                return true;
            });

        // Moves the current view (the debug camera's while it is on) to the scene's terminal camera, FOV included, to check
        // the framing of a captured anchor. Works in the multiplayer level too, where the terminal is drawn as an outline.
        Commands.RegisterCommand(L"milanmenu", L"look",
            [](wchar_t const*)
            {
                auto const View = GetTerminalView(GetCaptures());
                APlayerController* const Controller = GetViewingController();
                if (!View || Controller == nullptr)
                {
                    MILAN_WARNW(ELOG_EngineHooks, L"milanmenu.look: capture the anchor first (needs a player camera)");
                    return true;
                }

                Controller->SetLocation(View->Location, FALSE);
                Controller->SetRotation(View->Rotation);
                Controller->PlayerCamera->SetFOV(View->FOV);
                MILAN_INFOW(ELOG_EngineHooks, L"milanmenu.look: at the terminal camera, FOV %.0f (milanmenu.pos to log it)", View->FOV);
                return true;
            });

        // Shows the scene's terminal camera (and terminal placement) on the player's view. Turn the debug camera off to see it.
        Commands.RegisterCommand(L"milanmenu", L"preview",
            [](wchar_t const*)
            {
                auto& Captures = GetCaptures();
                ACameraActor* const Camera = FindTaggedCamera(k_terminalCameraTag);
                APlayerController* const Player = GetPlayerController();
                if (Camera == nullptr || Player == nullptr)
                {
                    MILAN_WARNW(ELOG_EngineHooks, L"milanmenu.preview: needs a menu scene with its terminal camera and a player");
                    return true;
                }

                if (auto const View = GetTerminalView(Captures))
                {
                    Camera->SetLocation(View->Location, FALSE);
                    Camera->SetRotation(View->Rotation);
                    Camera->FOVAngle = View->FOV;
                }
                if (Captures.TerminalAnchor)
                {
                    if (AActor* const Terminal = FindTerminal(); Terminal != nullptr)
                    {
                        Terminal->SetLocation(Captures.TerminalAnchor->Location, FALSE);
                        Terminal->SetRotation(Captures.TerminalAnchor->Rotation);
                    }
                }

                FViewTargetTransitionParams Transition{};
                Player->SetViewTarget(Camera, Transition);
                MILAN_INFOW(ELOG_EngineHooks, L"milanmenu.preview: showing the terminal camera%s",
                    GetViewingController() != Player ? L" (turn the debug camera off to see it)" : L"");
                return true;
            });

        // Writes what was captured for the current scene to Binaries\Win64\MilanMenuScenes\<Map>.json.
        Commands.RegisterCommand(L"milanmenu", L"save",
            [](wchar_t const*)
            {
                auto const& Captures = GetCaptures();
                if (Captures.Map.empty())
                {
                    MILAN_WARNW(ELOG_EngineHooks, L"milanmenu.save: no map loaded");
                    return true;
                }

                wchar_t Executable[MAX_PATH]{};
                ::GetModuleFileNameW(nullptr, Executable, MAX_PATH);
                auto const Folder = std::filesystem::path{ Executable }.parent_path() / L"MilanMenuScenes";
                std::error_code Error{};
                std::filesystem::create_directories(Folder, Error);
                auto const Path = Folder / (Captures.Map + L".json");

                std::ofstream File{ Path, std::ios::binary | std::ios::trunc };
                if (!File)
                {
                    MILAN_WARNW(ELOG_EngineHooks, L"milanmenu.save: couldn't write %s", Path.c_str());
                    return true;
                }
                File << CapturesToJson(Captures).dump(2) << '\n';
                MILAN_INFOW(ELOG_EngineHooks, L"milanmenu.save: wrote %s", Path.c_str());
                return true;
            });

        // Logs what a menu scene needs to show anything: the game type, the player controllers and the streaming levels.
        Commands.RegisterCommand(L"milanmenu", L"state",
            [](wchar_t const*)
            {
                UEngine* const Engine = (GEngine != nullptr) ? *GEngine : nullptr;
                AWorldInfo* const WorldInfo = Engine != nullptr ? static_cast<UGameEngine*>(Engine)->GetCurrentWorldInfo() : nullptr;
                if (WorldInfo == nullptr)
                {
                    MILAN_WARNW(ELOG_EngineHooks, L"milanmenu.state: no world");
                    return true;
                }

                MILAN_INFOW(ELOG_EngineHooks, L"milanmenu.state: game %s, menu level %d",
                    WorldInfo->Game != nullptr ? *WorldInfo->Game->Class->GetName() : L"none", WorldInfo->bIsMenuLevel ? 1 : 0);

                int Controllers = 0;
                for (TypedObjectIterator<APlayerController, true, false> Iter{}; Iter; ++Iter)
                {
                    MILAN_INFOW(ELOG_EngineHooks, L"milanmenu.state: player controller %s", *(*Iter)->Class->GetName());
                    ++Controllers;
                }
                MILAN_INFOW(ELOG_EngineHooks, L"milanmenu.state: %d player controller(s)", Controllers);

                for (unsigned i = 0; i < WorldInfo->StreamingLevels.Count(); ++i)
                {
                    ULevelStreaming const* const Level = WorldInfo->StreamingLevels(i);
                    if (Level == nullptr)
                    {
                        continue;
                    }
                    MILAN_INFOW(ELOG_EngineHooks, L"milanmenu.state: %s should load %d, loaded %d, should show %d, visible %d, load pending %d",
                        *Level->PackageName.ToString(), Level->bShouldBeLoaded ? 1 : 0, Level->LoadedLevel != nullptr ? 1 : 0,
                        Level->bShouldBeVisible ? 1 : 0, Level->bIsVisible ? 1 : 0, Level->bHasLoadRequestPending ? 1 : 0);
                }
                return true;
            });

        // Logs the loaded objects whose full name contains the parameter (case-insensitive), to check what a scene loaded.
        Commands.RegisterCommand(L"milanmenu", L"find",
            [](wchar_t const* const InParams)
            {
                constexpr int k_maxResults = 40;
                if (InParams == nullptr || *InParams == L'\0')
                {
                    MILAN_WARNW(ELOG_EngineHooks, L"milanmenu.find: usage: milanmenu.find <text>");
                    return true;
                }

                int Found = 0;
                for (TypedObjectIterator<UObject, true, false> Iter{}; Iter; ++Iter)
                {
                    FString const FullName = (*Iter)->GetFullName();
                    if (!FullName.Contains(InParams, true))
                    {
                        continue;
                    }
                    if (++Found <= k_maxResults)
                    {
                        MILAN_INFOW(ELOG_EngineHooks, L"milanmenu.find: %s", *FullName);
                    }
                }
                MILAN_INFOW(ELOG_EngineHooks, L"milanmenu.find: %d object(s) match '%s'", Found, InParams);
                return true;
            });
#else
        MILAN_UNUSED(Commands);
#endif
    }
}
