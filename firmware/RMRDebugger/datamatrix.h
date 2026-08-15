#pragma once
#include <QString>
#include <QVector>
#include <QImage>

/* DataMatrix ECC200 编码器 (ZXing 移植, Apache-2.0, ISO/IEC 16022)
 * 仅实现方形符号 + ASCII 编码子集, 覆盖设备 UUID 等 ASCII 文本。 */
namespace DataMatrix {
    bool encodeModules(const QString& text, QVector<QVector<bool>>& modules);
    QImage renderImage(const QString& text, int scale = 10, int quietModules = 4);
}
