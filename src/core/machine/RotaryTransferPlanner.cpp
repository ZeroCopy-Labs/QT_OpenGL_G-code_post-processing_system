#include "core/machine/RotaryTransferPlanner.h"

#include <algorithm>
#include <cmath>

namespace cadcam::machine
{
    namespace
    {
        double linearDistance
        (
            const MachinePose4D& left,
            const MachinePose4D& right
        )
        {
            return std::sqrt
            (
                (left.x - right.x) * (left.x - right.x)
                + (left.y - right.y) * (left.y - right.y)
                + (left.z - right.z) * (left.z - right.z)
            );
        }

        bool finitePose(const MachinePose4D& pose)
        {
            return std::isfinite(pose.x)
                && std::isfinite(pose.y)
                && std::isfinite(pose.z)
                && std::isfinite(pose.aDegrees);
        }

        MachinePose4D interpolate
        (
            const MachinePose4D& start,
            const MachinePose4D& end,
            double factor
        )
        {
            return
            {
                start.x + (end.x - start.x) * factor,
                start.y + (end.y - start.y) * factor,
                start.z + (end.z - start.z) * factor,
                start.aDegrees + (end.aDegrees - start.aDegrees) * factor
            };
        }


        TransferMotionKind classifyTransfer
        (
            const RotaryTransferRequest& request
        )
        {
            if (request.previousProcessUnitIndex < 0)
                return TransferMotionKind::InitialApproach;
            const bool ownerMissing =
                !request.previousOwnerZone.has_value()
                || !request.nextOwnerZone.has_value();
            const bool zoneChanged = ownerMissing
                || request.previousOwnerZone != request.nextOwnerZone;
            const bool rotationRequired =
                std::abs(request.nextCutStart.aDegrees
                    - request.previousCutEnd.aDegrees)
                > request.numericalEpsilon;
            if (zoneChanged || rotationRequired)
                return TransferMotionKind::CrossZoneRotaryTransfer;
            return request.policy.sameZone.totalHeight(request.policy.sameZoneTransferClearance)
                    <= request.numericalEpsilon
                ? TransferMotionKind::SameZoneSurfaceTransfer
                : TransferMotionKind::SameZoneClearanceTransfer;
        }

        void appendDistinct
        (
            RotaryTransferPreview& preview,
            const MachinePose4D& start,
            const MachinePose4D& target,
            TransferMotionPhase phase,
            double tolerance
        )
        {
            const MachinePose4D& previous = preview.targets.empty()
                ? start : preview.targets.back();
            if (linearDistance(previous, target) <= tolerance
                && std::abs(previous.aDegrees - target.aDegrees) <= tolerance)
            {
                return;
            }
            preview.targets.push_back(target);
            preview.phases.push_back(phase);
        }

        void calculateCost
        (
            RotaryTransferPreview& preview,
            const MachinePose4D& start
        )
        {
            MachinePose4D previous = start;
            int previousDirection = 0;
            for (const MachinePose4D& target : preview.targets)
            {
                preview.cost.linearDistance += linearDistance(previous, target);
                const double deltaA = target.aDegrees - previous.aDegrees;
                preview.cost.rotaryDegrees += std::abs(deltaA);
                const int direction = deltaA > 0.0 ? 1 : deltaA < 0.0 ? -1 : 0;
                if (direction != 0 && previousDirection != 0
                    && direction != previousDirection)
                {
                    ++preview.cost.rotaryDirectionChanges;
                }
                if (direction != 0) previousDirection = direction;
                previous = target;
            }
        }

        Diagnostic transferDiagnostic
        (
            const OperationContext& context,
            const QString& message
        )
        {
            Diagnostic diagnostic;
            diagnostic.code =
                DiagnosticCode::MachineTrajectoryTransferSafetyViolation;
            diagnostic.severity = DiagnosticSeverity::Error;
            diagnostic.component = QStringLiteral("RotaryTransferPlanner");
            diagnostic.operation = context.operationName;
            diagnostic.stage = QStringLiteral("preview-rotary-transfer");
            diagnostic.userMessage = message;
            diagnostic.correlationId = context.correlationId;
            return diagnostic;
        }
    }

    OperationResult<RotaryTransferPreview> RotaryTransferPlanner::preview
    (
        const RotaryTransferRequest& request,
        const OperationContext& context
    )
    {
        OperationResult<RotaryTransferPreview> result;
        if (!finitePose(request.previousCutEnd)
            || !finitePose(request.nextCutStart)
            || !std::isfinite(request.rotationSafeMachineZ)
            || !std::isfinite(request.numericalEpsilon)
            || request.numericalEpsilon <= 0.0
            || !std::isfinite(request.policy.rotationSafetyClearance)
            || request.policy.rotationSafetyClearance <= 0.0
            || !std::isfinite(request.policy.sameZoneTransferClearance)
            || request.policy.sameZoneTransferClearance < 0.0
            || !request.policy.sameZone.valid(request.policy.sameZoneTransferClearance))
        {
            result.status = OperationStatus::InvalidInput;
            result.addDiagnostic(transferDiagnostic
                (context, QStringLiteral("动态转移预览输入无效。")));
            return result;
        }

        RotaryTransferPreview preview;
        preview.kind = classifyTransfer(request);
        preview.cutStart = request.nextCutStart;

        if (preview.kind == TransferMotionKind::SameZoneSurfaceTransfer)
        {
            MachinePose4D target = request.nextCutStart;
            target.aDegrees = request.previousCutEnd.aDegrees;
            preview.finalApproachOrigin = request.previousCutEnd;
            appendDistinct(preview, request.previousCutEnd, target,
                TransferMotionPhase::SurfaceTransfer,
                request.numericalEpsilon);
        }
        else if (preview.kind
            == TransferMotionKind::SameZoneClearanceTransfer)
        {
            const auto& parameters = request.policy.sameZone;
            const double height = parameters.totalHeight(request.policy.sameZoneTransferClearance);
            const double topZ = std::max(request.previousCutEnd.z, request.nextCutStart.z) + height;
            const bool linked = request.policy.coordinatedTransferEnabled;
            MachinePose4D retract = request.previousCutEnd;
            retract.z = linked ? retract.z + parameters.minimumRetractHeight : topZ;
            MachinePose4D landing = request.nextCutStart;
            landing.z = linked ? landing.z + parameters.minimumApproachHeight : topZ;
            landing.aDegrees = request.previousCutEnd.aDegrees;
            // 高度为零的联动段不占用水平行程。
            const double departureRatio = linked && topZ > retract.z + request.numericalEpsilon
                ? parameters.departureRatio : 0.0;
            const double approachRatio = linked && topZ > landing.z + request.numericalEpsilon
                ? std::min(parameters.approachRatio, 1.0 - departureRatio) : 0.0;
            MachinePose4D departure = interpolate(request.previousCutEnd, request.nextCutStart, departureRatio);
            departure.z = topZ;
            departure.aDegrees = request.previousCutEnd.aDegrees;
            MachinePose4D transfer = interpolate(request.previousCutEnd, request.nextCutStart, 1.0 - approachRatio);
            transfer.z = topZ;
            transfer.aDegrees = request.previousCutEnd.aDegrees;
            appendDistinct(preview, request.previousCutEnd, retract,
                TransferMotionPhase::CoordinatedDeparture, request.numericalEpsilon);
            appendDistinct(preview, request.previousCutEnd, departure,
                TransferMotionPhase::CoordinatedDeparture, request.numericalEpsilon);
            appendDistinct(preview, request.previousCutEnd, transfer,
                TransferMotionPhase::SurfaceTransfer, request.numericalEpsilon);
            appendDistinct(preview, request.previousCutEnd, landing,
                TransferMotionPhase::CoordinatedApproach, request.numericalEpsilon);
            appendDistinct(preview, request.previousCutEnd, request.nextCutStart,
                TransferMotionPhase::CoordinatedApproach, request.numericalEpsilon);
            preview.finalApproachOrigin = preview.targets.size() >= 2U
                ? preview.targets[preview.targets.size() - 2U] : request.previousCutEnd;
        }
        else
        {
            const double departureFactor =
                request.policy.coordinatedTransferEnabled ? 0.25 : 0.0;
            const double approachFactor =
                request.policy.coordinatedTransferEnabled ? 0.75 : 1.0;
            MachinePose4D departure = interpolate
                (request.previousCutEnd, request.nextCutStart,
                    departureFactor);
            departure.z = request.rotationSafeMachineZ;
            departure.aDegrees = request.previousCutEnd.aDegrees;
            MachinePose4D rotary = interpolate
                (request.previousCutEnd, request.nextCutStart,
                    approachFactor);
            rotary.z = request.rotationSafeMachineZ;
            rotary.aDegrees = request.nextCutStart.aDegrees;
            preview.finalApproachOrigin = rotary;
            appendDistinct(preview, request.previousCutEnd, departure,
                TransferMotionPhase::CoordinatedDeparture,
                request.numericalEpsilon);
            appendDistinct(preview, request.previousCutEnd, rotary,
                TransferMotionPhase::SafeRotaryTransfer,
                request.numericalEpsilon);
            appendDistinct(preview, request.previousCutEnd,
                request.nextCutStart,
                TransferMotionPhase::CoordinatedApproach,
                request.numericalEpsilon);
        }

        if (!finitePose(preview.finalApproachOrigin)
            || preview.targets.size() != preview.phases.size()
            || (!preview.targets.empty()
                && (linearDistance(preview.targets.back(),
                        request.nextCutStart) > request.numericalEpsilon
                    || std::abs(preview.targets.back().aDegrees
                        - request.nextCutStart.aDegrees)
                        > request.numericalEpsilon)))
        {
            result.status = OperationStatus::Failed;
            result.addDiagnostic(transferDiagnostic
                (context, QStringLiteral("动态转移预览未到达目标切削入口。")));
            return result;
        }
        calculateCost(preview, request.previousCutEnd);
        result.status = OperationStatus::Success;
        result.value = std::move(preview);
        return result;
    }
}
