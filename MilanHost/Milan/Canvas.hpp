/**
 * @file        MilanHost/Milan/Canvas.hpp
 * @brief       Drawing profilers on @ref UCanvas instances.
 */

#pragma once
#undef DrawText

#include <string_view>
#include <LESDK/Headers.hpp>
#include "Proxies/Engine.hpp"


// ! Enumerations.
// ========================================

enum ETextAlign : int
{
    ETA_Left,
    ETA_Center,
    ETA_Right,
};

inline bool IsValidAlign(ETextAlign const Value) noexcept
{
    return Value == ETA_Left || Value == ETA_Center || Value == ETA_Right;
}

enum ECanvasProfilerMode : int
{
    CPM_None = 0,
    CPM_PendingPackages = 1,
    CPM_PlayerControllers = 2,
    CPM_Network = 3,
    CPM_WorldInfo = 4,

    CPM_Max
};


// ! Classes and other types.
// ========================================

class CanvasDrawer
{
protected:

    UCanvas* Canvas{ nullptr };
    TArray<FColor> ColorStack{};

public:

    virtual ~CanvasDrawer() noexcept = default;
    void UpdateCanvas(UCanvas* InCanvas);


    // Primitive drawing.
    // ----------------------------------------

    void Reset();

    void DrawText(std::wstring_view Text, bool bNewLine, ETextAlign Align = ETA_Left, float Scale = 1.f);
    void DrawTextColored(std::wstring_view Text, FColor Color, bool bNewLine, ETextAlign Align = ETA_Left, float Scale = 1.f);

    FVector2D GetTextSize(std::wstring_view Text, float Scale);

    void PushColor(FColor Color);
    void PushColor(FLinearColor Color);
    void PushColor(byte R, byte G, byte B, byte A);

    FColor PopColor();

    /** Draws a full-sized rectangle under all the drawing. */
    void SetBackground(FColor Color);
    /** Updates canvas drawing color and clears out the stack. */
    void SetColor(FColor Color);

    FVector2D GetPos() const;

    /** Adds given values to current coordinates. */
    void AdjustPos(float DX, float DY);

    void SetPos(float X, float Y);
    void SetPosX(float X);
    void SetPosY(float Y);

    /** Draws a 2D line using the current draw color. */
    void DrawLine(float X1, float Y1, float X2, float Y2);

    /** Draws horizontal 2D line using the current vertical offset and draw color. */
    void DrawHorizontalLine(float X1, float X2);

    /** Draws a heavier horizontal 2D line using the current vertical offset and draw color. */
    void DrawBoldHorizontalLine(float X1, float X2, unsigned char Factor = 2);

};


class MilanCanvasDrawer final : public CanvasDrawer
{
    USFXEngine*     Engine{ nullptr };
    FString         TargetName{};
    float           Indent{ 0.f };

public:

    static constexpr float      k_canvasPadding = 20.f;
    static constexpr float      k_indentSize = 10.f;

    // Remember that these colors are BGRA!!!

    static constexpr FColor     k_warnColor{ 0x00, 0x80, 0xFF, 0xFF };
    static constexpr FColor     k_infoColor{ 0xFF, 0x40, 0x40, 0xFF };
    static constexpr FColor     k_textColor{ 0xFF, 0xFF, 0xFF, 0xFF };

public:

    ~MilanCanvasDrawer() noexcept override {}
    void UpdateGlobals(USFXEngine* InEngine);

    void PushIndent();
    void PopIndent();

    void DrawHeader(std::wstring_view Subtitle, std::wstring_view Description, UObject* Target, FColor Color);
    void DrawString(std::wstring_view String, float Scale = 1.f);
    void DrawProperty(std::wstring_view Name, std::wstring_view String);
    void DrawSeparator();

private:

    FString const& FormatObjectName(UObject* Object);
};

struct CanvasProfilerRecord
{
    wchar_t const* Subtitle;
    wchar_t const* Description;
    void (*Function)(MilanCanvasDrawer& Drawer, UObject* Target);
};


// ! Implementation of canvas profilers.
// ========================================

void ProfilePendingPackages(MilanCanvasDrawer& Drawer, UObject* Target);
void ProfilePlayerControllers(MilanCanvasDrawer& Drawer, UObject* Target);
void ProfileNetwork(MilanCanvasDrawer& Drawer, UObject* Target);
void ProfileWorldInfo(MilanCanvasDrawer& Drawer, UObject* Target);

void DrawActorChannel(MilanCanvasDrawer& Drawer, UActorChannel_Proxy* Channel, unsigned int Index);
