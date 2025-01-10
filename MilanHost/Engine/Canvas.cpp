#include "Base/Base.hpp"
#include "Engine/Canvas.hpp"
#include "Engine/ExtObject.hpp"
#include "Engine/Hooks.hpp"


// ! CanvasDrawer implementation.
// ========================================

void CanvasDrawer::UpdateCanvas(UCanvas* const InCanvas)
{
    MILAN_CHECKW(InCanvas != nullptr, L"");
    Canvas = InCanvas;
}

void CanvasDrawer::Reset()
{
    SetPos(0.f, 0.f);
    SetColor(FColor{ 0xFF, 0xFF, 0xFF, 0xFF });
}

void CanvasDrawer::DrawText(std::wstring_view Text, bool const bNewLine, ETextAlign const Align, float const Scale)
{
    //MILAN_CHECKW(!Text.empty(), L"");
    MILAN_CHECKW(IsValidAlign(Align), L"");
    MILAN_CHECKW(Scale > 0.f, L"");
    MILAN_CHECKW(Canvas->Font != nullptr, L"");

    int SizeX = 0, SizeY = 0;
    FFontRenderInfo const FontRenderInfo{};

    if (Align == ETA_Right)
    {
        // Get size of the rendered text without actually drawing it, use it to calculate right-aligned position.
        auto const [DX, DY] = GetTextSize(Text, Scale);
        AdjustPos(-(float)DX, 0.f);
    }

    if (Text.empty())
    {
        // Get size of bogus rendered text to skip an empty line.
        AdjustPos(0.f, GetTextSize(L"---", Scale).Y);
    }
    else
    {
        (*UCanvas_WrappedPrint)(Canvas, TRUE, &SizeX, &SizeY, Canvas->Font, Scale, Scale, Align == ETA_Center, Text.data(), &FontRenderInfo);
    }

    if (!bNewLine)
    {
        AdjustPos((float)SizeX, 0.f);
    }
    else
    {
        AdjustPos(0.f, (float)SizeY);
    }
}

void CanvasDrawer::DrawTextColored(std::wstring_view const Text, FColor const Color, bool const bNewLine, ETextAlign const Align, float const Scale)
{
    PushColor(Color);
    DrawText(Text, bNewLine, Align, Scale);
    PopColor();
}

FVector2D CanvasDrawer::GetTextSize(std::wstring_view const Text, float const Scale)
{
    MILAN_CHECKW(!Text.empty(), L"");
    MILAN_CHECKW(Scale > 0.f, L"");

    CStringSizeParams Params{};
    std::memset(&Params, 0, sizeof Params);

    Params.Scaling = FVector2D{ .X = Scale, .Y = Scale };
    Params.DrawFont = Canvas->Font;

    (*UUIString_StringSize)(&Params, Text.data(), nullptr, TRUE);
    return Params.OutSize;
}

void CanvasDrawer::PushColor(FColor const Color)
{
    ColorStack.Add(Canvas->DrawColor);
    Canvas->DrawColor = Color;
}

void CanvasDrawer::PushColor(FLinearColor const Color)
{
    FColor const Converted
    {
        .B = static_cast<BYTE>(Color.B * 255.f),
        .G = static_cast<BYTE>(Color.G * 255.f),
        .R = static_cast<BYTE>(Color.R * 255.f),
        .A = static_cast<BYTE>(Color.A * 255.f),
    };
    PushColor(Converted);
}

void CanvasDrawer::PushColor(byte const R, byte const G, byte const B, byte const A)
{
    PushColor(FColor{ R, G, B, A });
}

FColor CanvasDrawer::PopColor()
{
    MILAN_CHECKW(!ColorStack.Empty(), L"");
    Canvas->DrawColor = ColorStack(ColorStack.Count() - 1);
    ColorStack.Erase(ColorStack.Count() - 1);
    return Canvas->DrawColor;
}

void CanvasDrawer::SetBackground(FColor const Color)
{
    PushColor(Color);
    FVector2D const Stored = GetPos();
    SetPos(0.f, 0.f);
    Canvas->DrawRect((float)Canvas->SizeX, (float)Canvas->SizeY, Canvas->DefaultTexture);
    SetPos(Stored.X, Stored.Y);
    PopColor();
}

void CanvasDrawer::SetColor(FColor const Color)
{
    ColorStack.Clear();
    Canvas->DrawColor = Color;
}

FVector2D CanvasDrawer::GetPos() const
{
    return FVector2D{ .X = Canvas->CurX, .Y = Canvas->CurY };
}

void CanvasDrawer::AdjustPos(float const DX, float const DY)
{
    Canvas->CurX += DX;
    Canvas->CurY += DY;
}

void CanvasDrawer::SetPos(float const X, float const Y)
{
    SetPosX(X);
    SetPosY(Y);
}

void CanvasDrawer::SetPosX(float X)
{
    if (X < 0.f) X += Canvas->SizeX;
    Canvas->CurX = X;
}

void CanvasDrawer::SetPosY(float Y)
{
    if (Y < 0.f) Y += Canvas->SizeY;
    Canvas->CurY = Y;
}

void CanvasDrawer::DrawLine(float const X1, float const Y1, float const X2, float const Y2)
{
    Canvas->Draw2DLine(X1, Y1, X2, Y2, Canvas->DrawColor);
}

void CanvasDrawer::DrawHorizontalLine(float X1, float X2)
{
    float const Y = Canvas->CurY;
    if (X1 < 0.f) X1 += Canvas->SizeX;
    if (X2 < 0.f) X2 += Canvas->SizeX;
    Canvas->Draw2DLine(X1, Y, X2, Y, Canvas->DrawColor);
}

void CanvasDrawer::DrawBoldHorizontalLine(float X1, float X2, unsigned char const Factor)
{
    if (X1 < 0.f) X1 += Canvas->SizeX;
    if (X2 < 0.f) X2 += Canvas->SizeX;

    MILAN_CHECKW(Factor > 0, L"");
    for (unsigned char i = 0; i < Factor; ++i)
    {
        Canvas->Draw2DLine(X1, Canvas->CurY, X2, Canvas->CurY, Canvas->DrawColor);
        Canvas->CurY += 1.f;
    }
}


// ! MilanCanvasDrawer implementation.
// ========================================

void MilanCanvasDrawer::UpdateGlobals(USFXEngine* const InEngine)
{
    MILAN_CHECKW(InEngine != nullptr, L"");
    Engine = InEngine;
}

void MilanCanvasDrawer::PushIndent()
{
    Indent += k_indentSize;
}

void MilanCanvasDrawer::PopIndent()
{
    Indent = std::max<float>(Indent - k_indentSize, 0.f);
}

void MilanCanvasDrawer::DrawHeader(std::wstring_view const Subtitle,
    std::wstring_view const Description, UObject* const Target, FColor const Color)
{
    MILAN_CHECKW(!Subtitle.empty(), L"");
    MILAN_CHECKW(!Description.empty(), L"");

    PushColor(Color);

    SetPos(k_canvasPadding, k_canvasPadding);
    DrawText(L"Milan Debug HUD / ", false, ETA_Left);
    DrawText(Subtitle, false, ETA_Left);

    if (Target != nullptr)
    {
        SetPosX(0.f);
        DrawText(FString::Printf(L"<< %s >>", *FormatObjectName(Target)), false, ETA_Center);
    }

    SetPosX(-k_canvasPadding);
    DrawText(Description, true, ETA_Right);

    AdjustPos(0.f, k_canvasPadding / 2);
    DrawHorizontalLine(k_canvasPadding, -k_canvasPadding);

    AdjustPos(0.f, k_canvasPadding / 2);

    PopColor();
}

void MilanCanvasDrawer::DrawString(std::wstring_view const String, float const Scale)
{
    SetPosX(k_canvasPadding + Indent);
    DrawText(String, true, ETA_Left, Scale);
}

void MilanCanvasDrawer::DrawProperty(std::wstring_view const Name, std::wstring_view const String)
{
    FColor NameColor = Canvas->DrawColor;
    if (NameColor.A > 250) NameColor.A = static_cast<unsigned char>(NameColor.A * 0.8);

    PushColor(NameColor);

    SetPosX(k_canvasPadding + Indent);
    DrawText(Name, false);
    DrawText(L": ", false);

    PopColor();

    SetPosX(k_canvasPadding + 220.f);
    DrawText(String, true);
}

void MilanCanvasDrawer::DrawSeparator()
{
    float const LineHeight = GetTextSize(L"---", 1.f).Y;
    AdjustPos(0.f, LineHeight);
}

FString const& MilanCanvasDrawer::FormatObjectName(UObject* const Object)
{
    MILAN_CHECKW(Object != nullptr, L"");
    TargetName.Clear();

    if (Object->Class != nullptr)
    {
        ::LESDK::AppendObjectName(Object->Class, TargetName, SFXName::k_formatBasic);
        TargetName.Append(L" ");

        if (Object->Outer != nullptr)
        {
            if (Object->Outer->Outer != nullptr)
            {
                ::LESDK::AppendObjectName(Object->Outer->Outer, TargetName, SFXName::k_formatBasic);
                TargetName.Append(L".");
            }

            ::LESDK::AppendObjectName(Object->Outer, TargetName, SFXName::k_formatBasic);
            TargetName.Append(L".");
        }

        ::LESDK::AppendObjectName(Object, TargetName, SFXName::k_formatInstanced);
    }
    else
    {
        TargetName.Append(L"(none)");
    }

    return TargetName;
}
