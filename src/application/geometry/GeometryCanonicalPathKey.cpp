#include "platform/pch.h"

#include "application/geometry/GeometryCanonicalPathKey.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace
{
    QString pointToken(const cadcam::geometry::Vector3d& point, double tolerance)
    {
        const qint64 x = static_cast<qint64>(std::llround(point.x / tolerance));
        const qint64 y = static_cast<qint64>(std::llround(point.y / tolerance));
        const qint64 z = static_cast<qint64>(std::llround(point.z / tolerance));
        return QStringLiteral("%1,%2,%3").arg(x).arg(y).arg(z);
    }

    QString joinTokens(const std::vector<QString>& tokens, std::size_t startIndex, bool reverse)
    {
        QString key;
        key.reserve(static_cast<int>(tokens.size() * 24));

        for (std::size_t offset = 0; offset < tokens.size(); ++offset)
        {
            const std::size_t index = reverse
                ? (startIndex + tokens.size() - offset) % tokens.size()
                : (startIndex + offset) % tokens.size();

            if (!key.isEmpty())
            {
                key.append(QLatin1Char(';'));
            }

            key.append(tokens[index]);
        }

        return key;
    }
}

namespace cadcam::geometry
{
    QString canonicalGeometryPathKey
    (
        const std::vector<Vector3d>& points,
        int typeKey,
        unsigned int colorKey,
        double coordinateTolerance
    )
    {
        if (points.size() < 2U || !(coordinateTolerance > 0.0))
        {
            return QString();
        }

        std::vector<QString> tokens;
        tokens.reserve(points.size());

        for (const Vector3d& point : points)
        {
            const QString token = pointToken(point, coordinateTolerance);

            if (!tokens.empty() && tokens.back() == token)
            {
                continue;
            }

            tokens.push_back(token);
        }

        if (tokens.size() < 2U)
        {
            return QString();
        }

        bool closed = false;

        if (tokens.size() > 1U && tokens.front() == tokens.back())
        {
            tokens.pop_back();
            closed = true;
        }

        if (tokens.size() < 2U)
        {
            return QString();
        }

        QString bestKey;

        if (closed)
        {
            // 闭合环：在全部旋转起点和正反两个方向上取字典序最小的表达。
            for (std::size_t index = 0; index < tokens.size(); ++index)
            {
                const QString forwardKey = joinTokens(tokens, index, false);
                const QString reverseKey = joinTokens(tokens, index, true);

                if (bestKey.isEmpty() || forwardKey < bestKey)
                {
                    bestKey = forwardKey;
                }

                if (reverseKey < bestKey)
                {
                    bestKey = reverseKey;
                }
            }
        }
        else
        {
            const QString forwardKey = joinTokens(tokens, 0, false);
            const QString reverseKey = joinTokens(tokens, tokens.size() - 1U, true);
            bestKey = forwardKey < reverseKey ? forwardKey : reverseKey;
        }

        return QStringLiteral("%1|%2|%3").arg(typeKey).arg(colorKey).arg(bestKey);
    }
}
