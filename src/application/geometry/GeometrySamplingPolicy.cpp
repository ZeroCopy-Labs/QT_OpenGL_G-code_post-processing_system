#include "platform/pch.h"

#include "application/geometry/GeometrySamplingPolicy.h"

#include "drw_entities.h"

namespace cadcam::geometry
{
    SamplingPolicy productionGeometrySamplingPolicy(int originalDxfType)
    {
        SamplingPolicy policy;
        policy.chordTolerance = 0.0;
        switch (static_cast<DRW::ETYPE>(originalDxfType))
        {
        case DRW::ETYPE::CIRCLE:
            policy.minimumSegments = 128;
            policy.fullTurnSegments = 128;
            break;
        case DRW::ETYPE::ARC:
            policy.minimumSegments = 16;
            policy.fullTurnSegments = 128;
            break;
        case DRW::ETYPE::ELLIPSE:
            policy.minimumSegments = 16;
            policy.fullTurnSegments = 128;
            break;
        case DRW::ETYPE::POLYLINE:
        case DRW::ETYPE::LWPOLYLINE:
            policy.minimumSegments = 1;
            policy.minimumBulgeSegments = 4;
            policy.fullTurnSegments = 128;
            break;
        default:
            policy.minimumSegments = 1;
            break;
        }
        return policy;
    }

    SamplingPolicy trajectoryGeometrySamplingPolicy(int originalDxfType)
    {
        SamplingPolicy policy = productionGeometrySamplingPolicy(originalDxfType);
        if (static_cast<DRW::ETYPE>(originalDxfType) == DRW::ETYPE::ARC)
        {
            policy.minimumSegments = 8;
            policy.maximumAngularStep = 5.0 * 6.28318530717958647692 / 360.0;
        }
        return policy;
    }
}
