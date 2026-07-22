#ifndef INIT_DWRITE_H
#define INIT_DWRITE_H

#include "dwrite.h"

class init_dwrite_bi
{
public:
    IDWriteFactory* pDWriteFactory = nullptr;

    IDWriteTextFormat* pTextFormatHeader = nullptr;
    IDWriteTextFormat* pTextFormatLabel = nullptr;
    IDWriteTextFormat* pTextFormatValue = nullptr;

    int overlay_pos_x = 20;
    int overlay_pos_y = 20;

    void InitGraph();
    void InitDirectWrite();
    void CleanupDirectWrite();
};

#endif
