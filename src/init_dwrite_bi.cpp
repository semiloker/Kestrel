#include "init_dwrite_bi.h"

IDWriteTextFormat *init_dwrite_bi::makeFormat(float size, DWRITE_FONT_WEIGHT weight,
                                              const wchar_t *family)
{
    IDWriteTextFormat *format = nullptr;

    HRESULT hr = pDWriteFactory->CreateTextFormat(
        family,
        NULL,
        weight,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        size,
        L"en-us",
        &format);

    if (SUCCEEDED(hr) && format)
    {
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    return format;
}

void init_dwrite_bi::InitGraph()
{
    InitDirectWrite();
}

void init_dwrite_bi::InitDirectWrite()
{
    if (pDWriteFactory)
        return;

    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown **>(&pDWriteFactory));

    if (!pDWriteFactory)
        return;

    pTextFormatDisplay = makeFormat(34.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    pTextFormatTitle = makeFormat(24.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    pTextFormatHeader = makeFormat(20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    pTextFormatStrong = makeFormat(15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    pTextFormatValue = makeFormat(14.0f, DWRITE_FONT_WEIGHT_REGULAR);
    pTextFormatSmall = makeFormat(13.0f, DWRITE_FONT_WEIGHT_REGULAR);
    pTextFormatLabel = makeFormat(12.0f, DWRITE_FONT_WEIGHT_REGULAR);
    pTextFormatMicro = makeFormat(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    pTextFormatMono = makeFormat(11.0f, DWRITE_FONT_WEIGHT_REGULAR, L"Consolas");
}

bool init_dwrite_bi::ensurePanelFormat(float charAdvance, float lineHeight)
{
    if (pTextFormatPanel && panelCharAdvance == charAdvance)
        return true;

    if (!pDWriteFactory)
        return false;

    if (pTextFormatPanel)
    {
        pTextFormatPanel->Release();
        pTextFormatPanel = nullptr;
    }

    IDWriteFontCollection *collection = nullptr;
    if (FAILED(pDWriteFactory->GetSystemFontCollection(&collection, FALSE)) || !collection)
        return false;

    const wchar_t *candidates[] = {L"Cascadia Mono", L"Consolas", L"Courier New"};

    IDWriteFont *font = nullptr;
    const wchar_t *chosenFamily = nullptr;

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]) && !font; ++i)
    {
        UINT32 index = 0;
        BOOL exists = FALSE;

        if (FAILED(collection->FindFamilyName(candidates[i], &index, &exists)) || !exists)
            continue;

        IDWriteFontFamily *family = nullptr;
        if (FAILED(collection->GetFontFamily(index, &family)) || !family)
            continue;

        if (SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL,
                                                   DWRITE_FONT_STRETCH_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL,
                                                   &font)) &&
            font)
        {
            chosenFamily = candidates[i];
        }

        family->Release();
    }

    if (!font || !chosenFamily)
    {
        if (font)
            font->Release();
        collection->Release();
        return false;
    }

    float size = charAdvance / 0.55f;
    float ascentPx = 0.0f;
    panelGlyphTopOffset = 0.0f;

    IDWriteFontFace *face = nullptr;
    if (SUCCEEDED(font->CreateFontFace(&face)) && face)
    {
        DWRITE_FONT_METRICS fm;
        face->GetMetrics(&fm);

        UINT32 codepoint = (UINT32)L'0';
        UINT16 glyph = 0;
        DWRITE_GLYPH_METRICS gm;

        if (fm.designUnitsPerEm > 0 &&
            SUCCEEDED(face->GetGlyphIndices(&codepoint, 1, &glyph)) &&
            SUCCEEDED(face->GetDesignGlyphMetrics(&glyph, 1, &gm, FALSE)) &&
            gm.advanceWidth > 0)
        {
            float upem = (float)fm.designUnitsPerEm;
            size = charAdvance * upem / (float)gm.advanceWidth;

            panelGlyphTopOffset = ((float)fm.ascent - (float)fm.capHeight) * size / upem;
            ascentPx = (float)fm.ascent * size / upem;
        }

        face->Release();
    }

    HRESULT hr = pDWriteFactory->CreateTextFormat(
        chosenFamily, NULL,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        size, L"en-us", &pTextFormatPanel);

    font->Release();
    collection->Release();

    if (FAILED(hr) || !pTextFormatPanel)
    {
        pTextFormatPanel = nullptr;
        return false;
    }

    pTextFormatPanel->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    pTextFormatPanel->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    pTextFormatPanel->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    if (ascentPx > 0.0f)
        pTextFormatPanel->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                                         lineHeight, ascentPx);

    panelCharAdvance = charAdvance;
    return true;
}

void init_dwrite_bi::CleanupDirectWrite()
{
    auto release = [](IDWriteTextFormat *&f)
    { if (f) { f->Release(); f = nullptr; } };

    release(pTextFormatDisplay);
    release(pTextFormatTitle);
    release(pTextFormatHeader);
    release(pTextFormatStrong);
    release(pTextFormatValue);
    release(pTextFormatSmall);
    release(pTextFormatLabel);
    release(pTextFormatMicro);
    release(pTextFormatMono);
    release(pTextFormatPanel);

    if (pDWriteFactory) { pDWriteFactory->Release(); pDWriteFactory = nullptr; }
}
