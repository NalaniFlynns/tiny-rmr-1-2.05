#include "datamatrix.h"
#include <QByteArray>
#include <QtGlobal>
#include <algorithm>

/* ============================================================================
 * DataMatrix ECC200 encoder.
 * Ported from ZXing (Apache License 2.0):
 *   - SymbolInfo / ErrorCorrection / DefaultPlacement / DataMatrixWriter
 *     (com.google.zxing.datamatrix.encoder, Copyright 2006 Jeremias Maerki)
 *   - ASCII subset of HighLevelEncoder
 * Algorithm per ISO/IEC 16022 (ECC200). Square symbols only.
 * ========================================================================== */

namespace {

struct SymbolInfo {
    int dataCapacity;
    int errorCodewords;
    int matrixWidth;   /* per-region data width, excluding finder borders */
    int matrixHeight;  /* per-region data height */
    int dataRegions;   /* total data region count (1/4/16/36) */
    int rsBlockData;   /* -1 => custom 144 logic */
    int rsBlockError;
};

/* PROD_SYMBOLS filtered to square symbols (ZXing table) */
const SymbolInfo kSymbols[] = {
    {   3,   5,  8,  8,  1,   3,   5 },
    {   5,   7, 10, 10,  1,   5,   7 },
    {   8,  10, 12, 12,  1,   8,  10 },
    {  12,  12, 14, 14,  1,  12,  12 },
    {  18,  14, 16, 16,  1,  18,  14 },
    {  22,  18, 18, 18,  1,  22,  18 },
    {  30,  20, 20, 20,  1,  30,  20 },
    {  36,  24, 22, 22,  1,  36,  24 },
    {  44,  28, 24, 24,  1,  44,  28 },
    {  62,  36, 14, 14,  4,  62,  36 },
    {  86,  42, 16, 16,  4,  86,  42 },
    { 114,  48, 18, 18,  4, 114,  48 },
    { 144,  56, 20, 20,  4, 144,  56 },
    { 174,  68, 22, 22,  4, 174,  68 },
    { 204,  84, 24, 24,  4, 102,  42 },
    { 280, 112, 14, 14, 16, 140,  56 },
    { 368, 144, 16, 16, 16,  92,  36 },
    { 456, 192, 18, 18, 16, 114,  48 },
    { 576, 224, 20, 20, 16, 144,  56 },
    { 696, 272, 22, 22, 16, 174,  68 },
    { 816, 336, 24, 24, 16, 136,  56 },
    {1050, 408, 18, 18, 36, 175,  68 },
    {1304, 496, 20, 20, 36, 163,  62 },
    {1558, 620, 22, 22, 36,  -1,  62 },  /* 144x144: custom blocks */
};

int horizontalRegions(int dataRegions) {
    switch (dataRegions) {
        case 1:  return 1;
        case 2:  /* not used (square only) */
        case 4:  return 2;
        case 16: return 4;
        case 36: return 6;
        default: return 1;
    }
}
int verticalRegions(int dataRegions) {
    switch (dataRegions) {
        case 1:  return 1;
        case 4:  return 2;
        case 16: return 4;
        case 36: return 6;
        default: return 1;
    }
}

const SymbolInfo* lookupSymbol(int dataCodewords) {
    for (const SymbolInfo& s : kSymbols) {
        if (dataCodewords <= s.dataCapacity) return &s;
    }
    return nullptr;
}

/* ---- GF(256) log/antilog (poly 0x12D) ---- */
int kLog[256];
int kAlog[255];
bool kTablesReady = false;
void ensureTables() {
    if (kTablesReady) return;
    int p = 1;
    for (int i = 0; i < 255; i++) {
        kAlog[i] = p;
        kLog[p] = i;
        p <<= 1;
        if (p >= 256) p ^= 0x12D;
    }
    kTablesReady = true;
}
inline int gfMul(int a, int b) {
    if (a == 0 || b == 0) return 0;
    return kAlog[(kLog[a] + kLog[b]) % 255];
}

/* ---- ECC200 error correction (ZXing ErrorCorrection) ---- */
const int kFactorSets[] = {5, 7, 10, 11, 12, 14, 18, 20, 24, 28, 36, 42, 48, 56, 62, 68};
const int kFactors[][68] = {
    {228, 48, 15, 111, 62},
    {23, 68, 144, 134, 240, 92, 254},
    {28, 24, 185, 166, 223, 248, 116, 255, 110, 61},
    {175, 138, 205, 12, 194, 168, 39, 245, 60, 97, 120},
    {41, 153, 158, 91, 61, 42, 142, 213, 97, 178, 100, 242},
    {156, 97, 192, 252, 95, 9, 157, 119, 138, 45, 18, 186, 83, 185},
    {83, 195, 100, 39, 188, 75, 66, 61, 241, 213, 109, 129, 94, 254, 225, 48, 90, 188},
    {15, 195, 244, 9, 233, 71, 168, 2, 188, 160, 153, 145, 253, 79, 108, 82, 27, 174, 186, 172},
    {52, 190, 88, 205, 109, 39, 176, 21, 155, 197, 251, 223, 155, 21, 5, 172, 254, 124, 12, 181, 184, 96, 50, 193},
    {211, 231, 43, 97, 71, 96, 103, 174, 37, 151, 170, 53, 75, 34, 249, 121, 17, 138, 110, 213, 141, 136, 120, 151, 233, 168, 93, 255},
    {245, 127, 242, 218, 130, 250, 162, 181, 102, 120, 84, 179, 220, 251, 80, 182, 229, 18, 2, 4, 68, 33, 101, 137, 95, 119, 115, 44, 175, 184, 59, 25, 225, 98, 81, 112},
    {77, 193, 137, 31, 19, 38, 22, 153, 247, 105, 122, 2, 245, 133, 242, 8, 175, 95, 100, 9, 167, 105, 214, 111, 57, 121, 21, 1, 253, 57, 54, 101, 248, 202, 69, 50, 150, 177, 226, 5, 9, 5},
    {245, 132, 172, 223, 96, 32, 117, 22, 238, 133, 238, 231, 205, 188, 237, 87, 191, 106, 16, 147, 118, 23, 37, 90, 170, 205, 131, 88, 120, 100, 66, 138, 186, 240, 82, 44, 176, 87, 187, 147, 160, 175, 69, 213, 92, 253, 225, 19},
    {175, 9, 223, 238, 12, 17, 220, 208, 100, 29, 175, 170, 230, 192, 215, 235, 150, 159, 36, 223, 38, 200, 132, 54, 228, 146, 218, 234, 117, 203, 29, 232, 144, 238, 22, 150, 201, 117, 62, 207, 164, 13, 137, 245, 127, 67, 247, 28, 155, 43, 203, 107, 233, 53, 143, 46},
    {242, 93, 169, 50, 144, 210, 39, 118, 202, 188, 201, 189, 143, 108, 196, 37, 185, 112, 134, 230, 245, 63, 197, 190, 250, 106, 185, 221, 175, 64, 114, 71, 161, 44, 147, 6, 27, 218, 51, 63, 87, 10, 40, 130, 188, 17, 163, 31, 176, 170, 4, 107, 232, 7, 94, 166, 224, 124, 86, 47, 11, 204},
    {220, 228, 173, 89, 251, 149, 159, 56, 89, 33, 147, 244, 154, 36, 73, 127, 213, 136, 248, 180, 234, 197, 158, 177, 68, 122, 93, 213, 15, 160, 227, 236, 66, 139, 153, 185, 202, 167, 179, 25, 220, 232, 96, 210, 231, 136, 223, 239, 181, 241, 59, 52, 172, 25, 49, 232, 211, 189, 64, 54, 108, 153, 132, 63, 96, 103, 82, 186}
};

QByteArray createEccBlock(const QByteArray& codewords, int numEcWords) {
    int table = -1;
    for (int i = 0; i < (int)(sizeof(kFactorSets) / sizeof(kFactorSets[0])); i++) {
        if (kFactorSets[i] == numEcWords) { table = i; break; }
    }
    if (table < 0) return QByteArray();
    const int* poly = kFactors[table];
    QByteArray ecc(numEcWords, 0);
    for (int i = 0; i < codewords.size(); i++) {
        int m = (uchar)ecc[numEcWords - 1] ^ (uchar)codewords.at(i);
        for (int k = numEcWords - 1; k > 0; k--) {
            if (m != 0 && poly[k] != 0) ecc[k] = (char)((uchar)ecc[k - 1] ^ kAlog[(kLog[m] + kLog[poly[k]]) % 255]);
            else ecc[k] = ecc[k - 1];
        }
        if (m != 0 && poly[0] != 0) ecc[0] = (char)kAlog[(kLog[m] + kLog[poly[0]]) % 255];
        else ecc[0] = 0;
    }
    /* reverse */
    for (int i = 0; i < numEcWords / 2; i++) qSwap(ecc[i], ecc[numEcWords - 1 - i]);
    return ecc;
}

QByteArray encodeEcc200(const QByteArray& data, const SymbolInfo& si) {
    QByteArray all(data);
    all.resize(si.dataCapacity + si.errorCodewords);
    int blockCount = (si.rsBlockData == -1) ? 10 : (si.dataCapacity / si.rsBlockData);
    if (blockCount <= 1) {
        QByteArray ecc = createEccBlock(data, si.errorCodewords);
        all.replace(si.dataCapacity, si.errorCodewords, ecc);
    } else {
        for (int block = 0; block < blockCount; block++) {
            int dataSize = (si.rsBlockData == -1)
                ? (block <= 8 ? 156 : 155)
                : si.rsBlockData;
            int errorSize = (si.rsBlockData == -1) ? 62 : si.rsBlockError;
            QByteArray temp;
            temp.reserve(dataSize);
            for (int d = block; d < si.dataCapacity; d += blockCount) temp.append(data.at(d));
            QByteArray ecc = createEccBlock(temp, errorSize);
            int pos = 0;
            for (int e = block; e < errorSize * blockCount; e += blockCount) {
                all[si.dataCapacity + e] = ecc.at(pos++);
            }
        }
    }
    return all;
}

/* ---- ASCII high-level encoding (ZXing ASCIIEncoder subset) ---- */
bool isDigit(QChar c) { return c >= '0' && c <= '9'; }

QByteArray encodeAscii(const QString& text) {
    QByteArray cw;
    int pos = 0;
    const int len = text.size();
    while (pos < len) {
        QChar c = text.at(pos);
        if (isDigit(c) && pos + 1 < len && isDigit(text.at(pos + 1))) {
            int num = (c.unicode() - '0') * 10 + (text.at(pos + 1).unicode() - '0');
            cw.append((char)(num + 130));
            pos += 2;
        } else if (c.unicode() <= 127) {
            cw.append((char)(c.unicode() + 1));
            pos++;
        } else {
            cw.append((char)235);                       /* upper shift */
            cw.append((char)(c.unicode() - 128 + 1));
            pos++;
        }
    }
    return cw;
}

int randomize253State(int codewordPosition) {
    int pseudoRandom = ((149 * codewordPosition) % 253) + 1;
    int temp = 129 + pseudoRandom;
    if (temp > 254) temp -= 254;
    return temp;
}

/* ---- DefaultPlacement (ZXing) ---- */
class Placement {
public:
    Placement(const QByteArray& codewords, int numcols, int numrows)
        : m_cw(codewords), m_numcols(numcols), m_numrows(numrows) {
        m_bits.fill(-1, numcols * numrows);
    }
    bool getBit(int col, int row) const { return m_bits[row * m_numcols + col] == 1; }
    void place() {
        int pos = 0, row = 4, col = 0;
        do {
            if ((row == m_numrows) && (col == 0)) corner1(pos++);
            if ((row == m_numrows - 2) && (col == 0) && ((m_numcols % 4) != 0)) corner2(pos++);
            if ((row == m_numrows - 2) && (col == 0) && (m_numcols % 8 == 4)) corner3(pos++);
            if ((row == m_numrows + 4) && (col == 2) && ((m_numcols % 8) == 0)) corner4(pos++);
            do {
                if ((row < m_numrows) && (col >= 0) && noBit(col, row)) utah(row, col, pos++);
                row -= 2; col += 2;
            } while (row >= 0 && col < m_numcols);
            row++; col += 3;
            do {
                if ((row >= 0) && (col < m_numcols) && noBit(col, row)) utah(row, col, pos++);
                row += 2; col -= 2;
            } while (row < m_numrows && col >= 0);
            row += 3; col++;
        } while (row < m_numrows || col < m_numcols);
        if (noBit(m_numcols - 1, m_numrows - 1)) {
            setBit(m_numcols - 1, m_numrows - 1, true);
            setBit(m_numcols - 2, m_numrows - 2, true);
        }
    }
private:
    QByteArray m_cw;
    int m_numrows, m_numcols;
    QVector<int> m_bits;
    void setBit(int col, int row, bool bit) { m_bits[row * m_numcols + col] = bit ? 1 : 0; }
    bool noBit(int col, int row) const { return m_bits[row * m_numcols + col] < 0; }
    void module(int row, int col, int pos, int bit) {
        if (row < 0) { row += m_numrows; col += 4 - ((m_numrows + 4) % 8); }
        if (col < 0) { col += m_numcols; row += 4 - ((m_numcols + 4) % 8); }
        int v = (uchar)m_cw.at(pos);
        v &= 1 << (8 - bit);
        setBit(col, row, v != 0);
    }
    void utah(int row, int col, int pos) {
        module(row - 2, col - 2, pos, 1);
        module(row - 2, col - 1, pos, 2);
        module(row - 1, col - 2, pos, 3);
        module(row - 1, col - 1, pos, 4);
        module(row - 1, col, pos, 5);
        module(row, col - 2, pos, 6);
        module(row, col - 1, pos, 7);
        module(row, col, pos, 8);
    }
    void corner1(int pos) {
        module(m_numrows - 1, 0, pos, 1);
        module(m_numrows - 1, 1, pos, 2);
        module(m_numrows - 1, 2, pos, 3);
        module(0, m_numcols - 2, pos, 4);
        module(0, m_numcols - 1, pos, 5);
        module(1, m_numcols - 1, pos, 6);
        module(2, m_numcols - 1, pos, 7);
        module(3, m_numcols - 1, pos, 8);
    }
    void corner2(int pos) {
        module(m_numrows - 3, 0, pos, 1);
        module(m_numrows - 2, 0, pos, 2);
        module(m_numrows - 1, 0, pos, 3);
        module(0, m_numcols - 4, pos, 4);
        module(0, m_numcols - 3, pos, 5);
        module(0, m_numcols - 2, pos, 6);
        module(0, m_numcols - 1, pos, 7);
        module(1, m_numcols - 1, pos, 8);
    }
    void corner3(int pos) {
        module(m_numrows - 3, 0, pos, 1);
        module(m_numrows - 2, 0, pos, 2);
        module(m_numrows - 1, 0, pos, 3);
        module(0, m_numcols - 2, pos, 4);
        module(0, m_numcols - 1, pos, 5);
        module(1, m_numcols - 1, pos, 6);
        module(2, m_numcols - 1, pos, 7);
        module(3, m_numcols - 1, pos, 8);
    }
    void corner4(int pos) {
        module(m_numrows - 1, 0, pos, 1);
        module(m_numrows - 1, m_numcols - 1, pos, 2);
        module(0, m_numcols - 3, pos, 3);
        module(0, m_numcols - 2, pos, 4);
        module(0, m_numcols - 1, pos, 5);
        module(1, m_numcols - 3, pos, 6);
        module(1, m_numcols - 2, pos, 7);
        module(1, m_numcols - 1, pos, 8);
    }
};

/* ---- encodeLowLevel (ZXing DataMatrixWriter) ---- */
void buildSymbol(const Placement& placement, const SymbolInfo& si, QVector<QVector<bool>>& modules) {
    int symbolWidth = si.matrixWidth * horizontalRegions(si.dataRegions);
    int symbolHeight = si.matrixHeight * verticalRegions(si.dataRegions);
    int totalW = symbolWidth + horizontalRegions(si.dataRegions) * 2;
    int totalH = symbolHeight + verticalRegions(si.dataRegions) * 2;
    modules.clear();
    modules.resize(totalH, QVector<bool>(totalW, false));

    int matrixY = 0;
    for (int y = 0; y < symbolHeight; y++) {
        int matrixX;
        if ((y % si.matrixHeight) == 0) {
            matrixX = 0;
            for (int x = 0; x < totalW; x++) { modules[matrixY][matrixX] = (x % 2) == 0; matrixX++; }
            matrixY++;
        }
        matrixX = 0;
        for (int x = 0; x < symbolWidth; x++) {
            if ((x % si.matrixWidth) == 0) { modules[matrixY][matrixX] = true; matrixX++; }
            modules[matrixY][matrixX] = placement.getBit(x, y);
            matrixX++;
            if ((x % si.matrixWidth) == si.matrixWidth - 1) {
                modules[matrixY][matrixX] = (y % 2) == 0;
                matrixX++;
            }
        }
        matrixY++;
        if ((y % si.matrixHeight) == si.matrixHeight - 1) {
            matrixX = 0;
            for (int x = 0; x < totalW; x++) { modules[matrixY][matrixX] = true; matrixX++; }
            matrixY++;
        }
    }
}

} // namespace

namespace DataMatrix {

bool encodeModules(const QString& text, QVector<QVector<bool>>& modules) {
    ensureTables();
    QByteArray cw = encodeAscii(text);
    const SymbolInfo* si = lookupSymbol(cw.size());
    if (!si) return false;
    /* pad (ZXing HighLevelEncoder): PAD then 253-randomize to capacity */
    if (cw.size() < si->dataCapacity) cw.append((char)129);
    while (cw.size() < si->dataCapacity) {
        cw.append((char)randomize253State(cw.size() + 1));
    }
    QByteArray all = encodeEcc200(cw, *si);
    int dataW = si->matrixWidth * horizontalRegions(si->dataRegions);
    int dataH = si->matrixHeight * verticalRegions(si->dataRegions);
    Placement placement(all, dataW, dataH);
    placement.place();
    buildSymbol(placement, *si, modules);
    return true;
}

QImage renderImage(const QString& text, int scale, int quietModules) {
    QVector<QVector<bool>> modules;
    if (!encodeModules(text, modules) || modules.isEmpty()) return QImage();
    int mh = modules.size();
    int mw = modules[0].size();
    int w = (mw + quietModules * 2) * scale;
    int h = (mh + quietModules * 2) * scale;
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::white);
    for (int r = 0; r < mh; r++) {
        for (int c = 0; c < mw; c++) {
            if (!modules[r][c]) continue;
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    img.setPixel((quietModules + c) * scale + dx, (quietModules + r) * scale + dy, qRgb(0, 0, 0));
                }
            }
        }
    }
    return img;
}

} // namespace DataMatrix
