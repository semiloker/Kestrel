#include "init_dwrite_bi.h"

void init_dwrite_bi::InitGraph()
{
    InitDirectWrite();
}

void init_dwrite_bi::InitDirectWrite()
{
    if (!pDWriteFactory)
    {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&pDWriteFactory));

        pDWriteFactory->CreateTextFormat(
            L"Segoe UI",
            NULL,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            20.0f,
            L"en-us",
            &pTextFormatHeader
        );

        pDWriteFactory->CreateTextFormat(
            L"Segoe UI",
            NULL,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f,
            L"en-us",
            &pTextFormatLabel
        );

        pDWriteFactory->CreateTextFormat(
            L"Segoe UI",
            NULL,
            DWRITE_FONT_WEIGHT_REGULAR,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            14.0f,
            L"en-us",
            &pTextFormatValue
        );
    }
}

void init_dwrite_bi::CleanupDirectWrite()
{
    if (pTextFormatHeader) { pTextFormatHeader->Release(); pTextFormatHeader = nullptr; }
    if (pTextFormatLabel) { pTextFormatLabel->Release(); pTextFormatLabel = nullptr; }
    if (pTextFormatValue) { pTextFormatValue->Release(); pTextFormatValue = nullptr; }

    if (pDWriteFactory) { pDWriteFactory->Release(); pDWriteFactory = nullptr; }
}
