#include "platform/pch.h"

#include "application/simulation/SimulationTrajectoryAdapter.h"

namespace
{
    using cadcam::simulation::SimulationMove;
    using cadcam::simulation::SimulationMoveKind;
    using cadcam::simulation::SimulationPose;

    SimulationMoveKind toSimulationMoveKind(cadcam::machine::MachineMoveKind kind)
    {
        using Kind = cadcam::machine::MachineMoveKind;
        switch (kind)
        {
        case Kind::Rapid: return SimulationMoveKind::Rapid;
        case Kind::Cutting: return SimulationMoveKind::Cutting;
        case Kind::CuttingConnection: return SimulationMoveKind::CuttingConnection;
        case Kind::Overcut: return SimulationMoveKind::Overcut;
        }
        return SimulationMoveKind::Rapid;
    }

    SimulationMoveKind toSimulationMoveKind(cadcam::nc::NcSourceMoveKind kind)
    {
        using Kind = cadcam::nc::NcSourceMoveKind;
        switch (kind)
        {
        case Kind::Rapid: return SimulationMoveKind::Rapid;
        case Kind::Cutting: return SimulationMoveKind::Cutting;
        case Kind::CuttingConnection: return SimulationMoveKind::CuttingConnection;
        case Kind::Overcut: return SimulationMoveKind::Overcut;
        }
        return SimulationMoveKind::Rapid;
    }

    // 追加一组运动：起点取下一条运动之前的已知位姿。
    void appendMoves
    (
        std::vector<SimulationMove>& moves,
        const std::vector<cadcam::machine::MachineMove>& sourceMoves,
        cadcam::geometry::EntityId entityId,
        int processGroupId,
        int processUnitIndex,
        SimulationPose& cursor
    )
    {
        for (const cadcam::machine::MachineMove& sourceMove : sourceMoves)
        {
            SimulationMove move;
            move.startPose = cursor;
            move.targetPose =
            {
                sourceMove.target.x,
                sourceMove.target.y,
                sourceMove.target.z,
                sourceMove.target.aDegrees
            };
            move.kind = toSimulationMoveKind(sourceMove.kind);
            move.entityId = sourceMove.entityId != 0U ? sourceMove.entityId : entityId;
            move.processGroupId = sourceMove.processGroupId >= 0
                ? sourceMove.processGroupId : processGroupId;
            move.processUnitIndex = processUnitIndex;
            cursor = move.targetPose;
            moves.push_back(std::move(move));
        }
    }
}

cadcam::simulation::SimulationTrajectory SimulationTrajectoryAdapter::fromMachineTrajectory
(
    const cadcam::machine::MachineTrajectory& trajectory
)
{
    cadcam::simulation::SimulationTrajectory result;
    result.contentRevision = trajectory.contentRevision;
    result.processStateRevision = trajectory.processStateRevision;
    result.sourceMode = cadcam::simulation::SimulationSourceMode::Rotary4Axis;

    // MachineMove 只保存绝对目标位姿，因此起点依次取前一条运动的目标；
    // 首条运动之前的位置由 initialPose 表示。
    SimulationPose cursor;
    bool hasCursor = false;

    for (const cadcam::machine::EntityTrajectory& entity : trajectory.entities)
    {
        appendMoves(result.moves, entity.approachMoves, entity.entityId,
            entity.processGroupId, entity.processUnitIndex, cursor);
        appendMoves(result.moves, entity.cuttingMoves, entity.entityId,
            entity.processGroupId, entity.processUnitIndex, cursor);
        appendMoves(result.moves, entity.overcutMoves, entity.entityId,
            entity.processGroupId, entity.processUnitIndex, cursor);
        hasCursor = true;
    }

    if (!result.moves.empty())
    {
        result.initialPose = result.moves.front().startPose;
    }
    else if (hasCursor)
    {
        result.initialPose = cursor;
    }

    return result;
}

cadcam::simulation::SimulationTrajectory SimulationTrajectoryAdapter::fromNcProgram
(
    const cadcam::nc::NcProgram& program
)
{
    using cadcam::simulation::SimulationSourceMode;

    cadcam::simulation::SimulationTrajectory result;
    result.contentRevision = program.contentRevision;
    result.processStateRevision = program.processStateRevision;
    result.sourceMode = SimulationSourceMode::Planar3Axis;

    SimulationPose cursor;
    bool hasCursor = false;
    bool hasAxisAngle = false;

    for (const cadcam::nc::NcEntityBlock& block : program.entities)
    {
        for (const cadcam::nc::NcMotion& motion : block.motions)
        {
            SimulationMove move;
            move.entityId = motion.entityId != 0U ? motion.entityId : block.metadata.entityId;
            move.processGroupId = motion.processGroupId >= 0
                ? motion.processGroupId : block.metadata.processGroupId;
            move.processUnitIndex = block.processUnitIndex;
            move.kind = toSimulationMoveKind(motion.sourceKind);

            SimulationPose target = cursor;
            if (motion.axes.x.has_value()) target.x = *motion.axes.x;
            if (motion.axes.y.has_value()) target.y = *motion.axes.y;
            if (motion.axes.z.has_value()) target.z = *motion.axes.z;
            if (motion.axes.a.has_value())
            {
                target.aDegrees = *motion.axes.a;
                hasAxisAngle = true;
            }
            else if (hasAxisAngle)
            {
                target.aDegrees = cursor.aDegrees;
            }

            if (!hasCursor)
            {
                // 第一条运动没有已知前置位姿，用其目标作为起点，避免从原点产生假位移。
                cursor = target;
                result.initialPose = cursor;
                hasCursor = true;
            }

            move.startPose = cursor;
            move.targetPose = target;
            cursor = target;
            result.moves.push_back(std::move(move));
        }
    }

    return result;
}
