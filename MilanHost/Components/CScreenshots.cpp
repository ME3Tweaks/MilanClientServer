#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "fpng/fpng.h"

#include "Components/CCommands.hpp"
#include "Components/CScreenshots.hpp"
#include "Engine/TransparentRender.hpp"
#include "Shell/CmdArgs.hpp"


t_appCreateBitmap* appCreateBitmap_orig = nullptr;

namespace
{
    bool fpngInitialized = false;
    int cachedScreenshotIndex = 1;
    int maxScreenshotIndex = 99999;

    bool ShouldCopyImagesToClipboard()
    {
        return CmdArgs::HasFlag(L"-copyimages");
    }

    bool CopyImageToClipboard(int width, int height, std::vector<unsigned char> const& rgbData)
    {
        if (width <= 0 || height <= 0 || rgbData.empty())
        {
            return false;
        }

        int const bytesPerPixel = 4;
        int const rowBytes = ((width * bytesPerPixel) + 3) & ~3;
        size_t const pixelDataSize = static_cast<size_t>(rowBytes) * static_cast<size_t>(height);
        std::vector<unsigned char> dibData(pixelDataSize, 0);

        auto const rgbPixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        for (size_t i = 0; i < rgbPixelCount; ++i)
        {
            size_t const rgbOffset = i * 3;
            size_t const dibOffset = i * 4;
            if (rgbOffset + 2 >= rgbData.size())
            {
                break;
            }

            dibData[dibOffset] = rgbData[rgbOffset + 2];
            dibData[dibOffset + 1] = rgbData[rgbOffset + 1];
            dibData[dibOffset + 2] = rgbData[rgbOffset];
            dibData[dibOffset + 3] = 0xFF;
        }

        BITMAPINFOHEADER header{};
        header.biSize = sizeof(BITMAPINFOHEADER);
        header.biWidth = width;
        header.biHeight = -height;
        header.biPlanes = 1;
        header.biBitCount = 32;
        header.biCompression = BI_RGB;
        header.biSizeImage = static_cast<DWORD>(pixelDataSize);

        size_t const totalSize = sizeof(BITMAPINFOHEADER) + dibData.size();
        HGLOBAL clipboardData = GlobalAlloc(GMEM_MOVEABLE, totalSize);
        if (clipboardData == nullptr)
        {
            return false;
        }

        void* memory = GlobalLock(clipboardData);
        if (memory == nullptr)
        {
            GlobalFree(clipboardData);
            return false;
        }

        std::memcpy(memory, &header, sizeof(BITMAPINFOHEADER));
        std::memcpy(static_cast<unsigned char*>(memory) + sizeof(BITMAPINFOHEADER), dibData.data(), dibData.size());
        GlobalUnlock(clipboardData);

        bool clipboardOpened = false;
        for (int attempt = 0; attempt < 5 && !clipboardOpened; ++attempt)
        {
            clipboardOpened = OpenClipboard(nullptr);
            if (!clipboardOpened)
            {
                Sleep(10);
            }
        }

        if (!clipboardOpened)
        {
            GlobalFree(clipboardData);
            return false;
        }

        bool const success = EmptyClipboard() && (SetClipboardData(CF_DIB, clipboardData) != nullptr);
        CloseClipboard();

        if (!success)
        {
            GlobalFree(clipboardData);
        }

        return success;
    }
}


bool CScreenshots::Initialize(::LESDK::Initializer& Context)
{
    INSTALL_HOOK_ONE(appCreateBitmap, GLOBAL_APPCREATEBITMAP_RVA);
    if (appCreateBitmap_orig == nullptr)
    {
        MILAN_ERRORW(ELOG_Startup, L"failed to hook appCreateBitmap, PNG screenshots will be disabled");
    }
    else if (TransparentRender::IsTransparentRenderEnabled())
    {
        TransparentRender::RegisterCommands(COMPONENT(CCommands));
    }

    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CScreenshots::Terminate(::LESDK::Initializer& Context)
{
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);

    if (appCreateBitmap_orig != nullptr)
    {
        Context.UninstallHook("appCreateBitmap");
        appCreateBitmap_orig = nullptr;
    }
}


// data is an array of FColor structs (BGRA) of size width * height
void appCreateBitmap_hook(wchar_t* inputBaseName, int width, int height, FColor* data, void* fileManager)
{
    // Don't use this anymore
    MILAN_UNUSED(fileManager);

    if (!fpngInitialized)
    {
        // Initialize the library before use
        fpng::fpng_init();
        fpngInitialized = true;
    }

    long byteCount = width * height * 3;

    // Color order needs swapped around for FPNG to access data
    // since nothing supports BRGA.
    std::vector<unsigned char> newImageData(byteCount);
    int pixelIndex = 0; // Which pixel we are one
    long totalCount = width * height; // how many pixels
    long position = 0; // Where to place into vector
    for (long i = 0; i < totalCount; i++)
    {
        auto color = &data[pixelIndex];
        newImageData[position] = color->R;
        newImageData[position + 1L] = color->G;
        newImageData[position + 2L] = color->B;

        pixelIndex++;
        position = pixelIndex * 3;
    }

    if (TransparentRender::HandleCapture(inputBaseName, width, height, newImageData))
    {
        // Consumed as part of a transparent-render capture sequence
        return;
    }

    // Determine output filename.
    auto path = std::filesystem::path(inputBaseName);
    auto extension = path.extension();
    if (extension != ".png")
    {
        // Calculate a new name / output path inside a per-game subdirectory (e.g. "LE3")
        auto basePath = std::filesystem::path(inputBaseName);
        auto parentDir = basePath.parent_path(); // ScreenShots directory

        // Build subdirectory name like "LE3" using SDK_TARGET_GAME macro
        auto outDir = parentDir / L"LMP";

        // Ensure directory exists (ignore errors)
        std::error_code ec;
        std::filesystem::create_directories(outDir, ec);

        if (std::filesystem::exists(outDir) == false) {
            // Cannot create output folder!!
            return;
        }

        std::filesystem::path outPath;
        std::wstring newFname;
        while (cachedScreenshotIndex < maxScreenshotIndex)
        {
            newFname = std::format(L"ScreenShot{:05}.png", cachedScreenshotIndex);
            cachedScreenshotIndex++; // Increment the cached index immediately.

            // Test the game-specific folder...
            outPath = outDir / newFname;

            // Check if file exists
            if (!std::filesystem::exists(outPath))
            {
                // File doesn't exist. Use this one
                path = outPath;
                break;
            }
            // File exists, go to next one
        }

        if (cachedScreenshotIndex >= maxScreenshotIndex) {
            return; // Can't take any more screenshots
        }
    }

    // Save the image data using the fpng library.
    // Returns false if can't write, we don't really care though.
    const bool saved = fpng::fpng_encode_image_to_wfile(path.c_str(), newImageData.data(), width, height, 3, 0U); // 3bpp, no flags
    if (saved && ShouldCopyImagesToClipboard())
    {
        CopyImageToClipboard(width, height, newImageData);
    }
}
