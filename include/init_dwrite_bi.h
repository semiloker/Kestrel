#ifndef INIT_DWRITE_H
#define INIT_DWRITE_H

#include "dwrite.h"

class init_dwrite_bi
{
public:
    IDWriteFactory* pDWriteFactory = nullptr;

    IDWriteTextFormat* pTextFormatDisplay = nullptr;
    IDWriteTextFormat* pTextFormatTitle = nullptr;
    IDWriteTextFormat* pTextFormatHeader = nullptr;
    IDWriteTextFormat* pTextFormatStrong = nullptr;
    IDWriteTextFormat* pTextFormatValue = nullptr;
    IDWriteTextFormat* pTextFormatSmall = nullptr;
    IDWriteTextFormat* pTextFormatLabel = nullptr;
    IDWriteTextFormat* pTextFormatMicro = nullptr;
    IDWriteTextFormat* pTextFormatMono = nullptr;

    int overlay_pos_x = 20;
    int overlay_pos_y = 20;

    IDWriteTextFormat* pTextFormatPanel = nullptr;
    float panelGlyphTopOffset = 0.0f;
    float panelCharAdvance = 0.0f;

    void InitGraph();
    void InitDirectWrite();
    void CleanupDirectWrite();

    bool ensurePanelFormat(float charAdvance, float lineHeight);

private:
    IDWriteTextFormat* makeFormat(float size, DWRITE_FONT_WEIGHT weight,
                                  const wchar_t* family = L"Segoe UI");
};

#endif
