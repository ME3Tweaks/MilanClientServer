/**
 * @file        MilanHost/Canvas.hpp
 * @brief       Utilities for drawing profilers on @ref UCanvas instances.
 */

#pragma once
#undef DrawText

#include <string_view>
#include <LESDK/Headers.hpp>


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

    /** Updates canvas drawing color and clears out the stack. */
    void SetColor(FColor Color);

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

    float const     k_canvasPadding = 20.f;
    float const     k_indentSize = 10.f;

    FColor const    k_warnColor{ .B = 0x00, .G = 0x64, .R = 0xFF, .A = 0xFF };
    FColor const    k_infoColor{ .B = 0xFF, .G = 0x40, .R = 0x40, .A = 0xFF };
    FColor const    k_textColor{ .B = 0xFF, .G = 0xFF, .R = 0xFF, .A = 0xFF };

public:

    ~MilanCanvasDrawer() noexcept override {}
    void UpdateGlobals(USFXEngine* InEngine);

    void PushIndent();
    void PopIndent();

    void DrawHeader(std::wstring_view Subtitle, std::wstring_view Description, UObject* Target, FColor Color);
    void DrawString(std::wstring_view String);
    void DrawProperty(std::wstring_view Name, std::wstring_view String);
    void DrawSeparator();

private:

    FString const& FormatObjectName(UObject* Object);
};
