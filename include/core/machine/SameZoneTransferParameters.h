#pragma once

#include <algorithm>
#include <cmath>

namespace cadcam::machine
{
    struct SameZoneTransferParameters
    {
        double minimumRetractHeight = 0.0;
        double minimumApproachHeight = 0.0;
        bool automaticTotalHeight = true;
        double departureRatio = 0.25;
        double approachRatio = 0.25;

        double totalHeight(double configuredHeight) const
        {
            return automaticTotalHeight
                ? std::max(minimumRetractHeight, minimumApproachHeight) : configuredHeight;
        }

        bool valid(double configuredHeight) const
        {
            const double height = totalHeight(configuredHeight);
            return std::isfinite(minimumRetractHeight) && minimumRetractHeight >= 0.0
                && std::isfinite(minimumApproachHeight) && minimumApproachHeight >= 0.0
                && std::isfinite(height)
                && height >= std::max(minimumRetractHeight, minimumApproachHeight)
                && std::isfinite(departureRatio) && departureRatio >= 0.0
                && departureRatio <= 1.0
                && std::isfinite(approachRatio) && approachRatio >= 0.0
                && approachRatio <= 1.0
                && departureRatio + approachRatio <= 1.0 + 1.0e-12;
        }
    };
}
