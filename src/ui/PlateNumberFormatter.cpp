#include "ui/PlateNumberFormatter.h"

QString formatPlateNumberForDisplay(const QString &plateNumber)
{
    static const QString provincePrefixes =
        QStringLiteral("京沪津渝冀晋蒙辽吉黑苏浙皖闽赣鲁豫鄂湘粤桂琼川贵云藏陕甘青宁新");
    if (plateNumber.size() < 3 || plateNumber.contains(QChar(0x00B7))
        || !provincePrefixes.contains(plateNumber.at(0))) {
        return plateNumber;
    }

    const QChar cityCode = plateNumber.at(1);
    if (cityCode < QLatin1Char('A') || cityCode > QLatin1Char('Z')) {
        return plateNumber;
    }

    QString formatted = plateNumber;
    formatted.insert(2, QChar(0x00B7));
    return formatted;
}
