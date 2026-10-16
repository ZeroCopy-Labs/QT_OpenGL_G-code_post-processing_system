#pragma once

// CAD 图元几何细节（内部使用）：由 CadItem / DRW 实体推导点、切线、法向和锚点。
// CAD 选择可视化与 CAM 可视化共用；只依赖 CAD 图元与几何数学，不涉及加工状态。

#include <QVector>
#include <QVector3D>

#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include "cad/geometry/CadOcsGeometry.h"
#include "cad/items/CadItem.h"
#include "cad/view/rendering/CadSelectionHandle.h"
#include "drw_entities.h"

namespace cadview::geometrydetail
{

    constexpr double kVisualEpsilon = 1.0e-9;
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 6.28318530717958647692;

    inline QVector3D normalizeOrZero(QVector3D vector)
    {
        if (vector.lengthSquared() <= kVisualEpsilon)
        {
            return QVector3D();
        }

        vector.normalize();
        return vector;
    }

    inline QVector3D leftPerpendicular(const QVector3D& vector)
    {
        return QVector3D(-vector.y(), vector.x(), 0.0f);
    }

    inline QVector3D mapPlaneVectorToWorld(const QVector3D& axisU, const QVector3D& axisV, double u, double v)
    {
        return normalizeOrZero
        (
            axisU * static_cast<float>(u)
            + axisV * static_cast<float>(v)
        );
    }

    inline QVector3D bulgeArcCenter(const QVector3D& startPoint, const QVector3D& endPoint, double bulge, bool* valid = nullptr)
    {
        const QVector3D chord = endPoint - startPoint;
        const double chordLength = chord.length();

        if (valid != nullptr)
        {
            *valid = false;
        }

        if (chordLength <= kVisualEpsilon || std::abs(bulge) < 1.0e-8)
        {
            return QVector3D();
        }

        const QVector3D midpoint = (startPoint + endPoint) * 0.5f;
        const double centerOffset = chordLength * (1.0 / bulge - bulge) * 0.25;
        const QVector3D leftNormal
        (
            static_cast<float>(-chord.y() / chordLength),
            static_cast<float>(chord.x() / chordLength),
            0.0f
        );

        if (valid != nullptr)
        {
            *valid = true;
        }

        return midpoint + leftNormal * static_cast<float>(centerOffset);
    }

    inline QVector3D bulgeSegmentTangentAtStart(const QVector3D& startPoint, const QVector3D& endPoint, double bulge)
    {
        if (std::abs(bulge) < 1.0e-8)
        {
            return normalizeOrZero(endPoint - startPoint);
        }

        bool valid = false;
        const QVector3D center = bulgeArcCenter(startPoint, endPoint, bulge, &valid);

        if (!valid)
        {
            return normalizeOrZero(endPoint - startPoint);
        }

        const QVector3D radiusVector = startPoint - center;
        const QVector3D tangent = bulge > 0.0
            ? leftPerpendicular(radiusVector)
            : -leftPerpendicular(radiusVector);

        return normalizeOrZero(tangent);
    }

    inline QVector3D bulgeSegmentTangentAtEnd(const QVector3D& startPoint, const QVector3D& endPoint, double bulge)
    {
        if (std::abs(bulge) < 1.0e-8)
        {
            return normalizeOrZero(endPoint - startPoint);
        }

        bool valid = false;
        const QVector3D center = bulgeArcCenter(startPoint, endPoint, bulge, &valid);

        if (!valid)
        {
            return normalizeOrZero(endPoint - startPoint);
        }

        const QVector3D radiusVector = endPoint - center;
        const QVector3D tangent = bulge > 0.0
            ? leftPerpendicular(radiusVector)
            : -leftPerpendicular(radiusVector);

        return normalizeOrZero(tangent);
    }

    inline bool tryBuildEllipseAxes(const DRW_Ellipse* ellipse, QVector3D& majorAxis, QVector3D& minorAxis)
    {
        if (ellipse == nullptr)
        {
            return false;
        }

        majorAxis = QVector3D(ellipse->secPoint.x, ellipse->secPoint.y, ellipse->secPoint.z);

        if (majorAxis.lengthSquared() <= kVisualEpsilon || ellipse->ratio <= 0.0)
        {
            return false;
        }

        QVector3D normal(ellipse->extPoint.x, ellipse->extPoint.y, ellipse->extPoint.z);

        if (normal.lengthSquared() <= kVisualEpsilon)
        {
            normal = QVector3D(0.0f, 0.0f, 1.0f);
        }
        else
        {
            normal.normalize();
        }

        minorAxis = QVector3D::crossProduct(normal, majorAxis);

        if (minorAxis.lengthSquared() <= kVisualEpsilon)
        {
            return false;
        }

        minorAxis.normalize();
        minorAxis *= static_cast<float>(majorAxis.length() * ellipse->ratio);
        return true;
    }

    inline QVector3D resolveNormal(const DRW_Coord& extPoint)
    {
        QVector3D normal(extPoint.x, extPoint.y, extPoint.z);

        if (normal.lengthSquared() <= kVisualEpsilon)
        {
            return QVector3D(0.0f, 0.0f, 1.0f);
        }

        normal.normalize();
        return normal;
    }

    inline void buildPlaneBasis(const QVector3D& normal, QVector3D& axisX, QVector3D& axisY)
    {
        if (std::abs(normal.x()) <= 1.0e-6f && std::abs(normal.y()) <= 1.0e-6f)
        {
            axisX = QVector3D(1.0f, 0.0f, 0.0f);
            axisY = QVector3D::crossProduct(normal, axisX);

            if (axisY.lengthSquared() <= kVisualEpsilon)
            {
                axisY = QVector3D(0.0f, 1.0f, 0.0f);
            }
            else
            {
                axisY.normalize();
            }

            return;
        }

        const QVector3D helper = std::abs(normal.z()) < 0.999f
            ? QVector3D(0.0f, 0.0f, 1.0f)
            : QVector3D(0.0f, 1.0f, 0.0f);

        axisX = QVector3D::crossProduct(helper, normal);

        if (axisX.lengthSquared() <= kVisualEpsilon)
        {
            axisX = QVector3D(1.0f, 0.0f, 0.0f);
        }
        else
        {
            axisX.normalize();
        }

        axisY = QVector3D::crossProduct(normal, axisX);

        if (axisY.lengthSquared() <= kVisualEpsilon)
        {
            axisY = QVector3D(0.0f, 1.0f, 0.0f);
        }
        else
        {
            axisY.normalize();
        }
    }

    inline QVector3D circlePointAt(const DRW_Circle* circle, double parameter)
    {
        if (circle == nullptr || circle->radious <= 0.0)
        {
            return QVector3D();
        }

        return CadOcsGeometry::pointAt(circle, parameter);
    }

    inline QVector3D circleTangentAt(const DRW_Circle* circle, double parameter, bool reverseDirection)
    {
        if (circle == nullptr || circle->radious <= 0.0)
        {
            return QVector3D();
        }

        return CadOcsGeometry::tangentAt(circle, parameter, reverseDirection);
    }

    inline QVector3D arcPointAt(const DRW_Arc* arc, double angle)
    {
        if (arc == nullptr || arc->radious <= 0.0)
        {
            return QVector3D();
        }

        const QVector3D center = CadOcsGeometry::center(arc);
        QVector3D normal = CadOcsGeometry::normal(arc->extPoint);
        QVector3D axisX;
        QVector3D axisY;
        CadOcsGeometry::basis(arc->extPoint, axisX, axisY, normal);

        return center
            + axisX * static_cast<float>(std::cos(angle) * arc->radious)
            + axisY * static_cast<float>(std::sin(angle) * arc->radious);
    }

    inline QVector3D arcTangentAt(const DRW_Arc* arc, double angle, bool reverseDirection)
    {
        if (arc == nullptr || arc->radious <= 0.0)
        {
            return QVector3D();
        }

        QVector3D normal = CadOcsGeometry::normal(arc->extPoint);
        QVector3D axisX;
        QVector3D axisY;
        CadOcsGeometry::basis(arc->extPoint, axisX, axisY, normal);

        QVector3D tangent
        (
            axisX * static_cast<float>(-std::sin(angle))
            + axisY * static_cast<float>(std::cos(angle))
        );

        if (reverseDirection)
        {
            tangent = -tangent;
        }

        return normalizeOrZero(tangent);
    }

    inline QVector3D ellipsePointAt(const DRW_Ellipse* ellipse, double parameter)
    {
        if (ellipse == nullptr)
        {
            return QVector3D();
        }

        const QVector3D center(ellipse->basePoint.x, ellipse->basePoint.y, ellipse->basePoint.z);
        QVector3D majorAxis;
        QVector3D minorAxis;

        if (!tryBuildEllipseAxes(ellipse, majorAxis, minorAxis))
        {
            return QVector3D();
        }

        return center
            + majorAxis * static_cast<float>(std::cos(parameter))
            + minorAxis * static_cast<float>(std::sin(parameter));
    }

    inline double normalizeAnglePositive(double angle)
    {
        double normalized = std::fmod(angle, kTwoPi);

        if (normalized < 0.0)
        {
            normalized += kTwoPi;
        }

        return normalized;
    }

    inline bool isFullEllipsePath(const DRW_Ellipse* ellipse)
    {
        if (ellipse == nullptr)
        {
            return false;
        }

        const double span = ellipse->endparam - ellipse->staparam;
        return std::abs(span) < 1.0e-10
            || std::abs(std::abs(span) - kTwoPi) < 1.0e-10;
    }

    inline double effectiveCircleStartParameter(const std::optional<double>& startParameter)
    {
        if (startParameter.has_value()) return normalizeAnglePositive(*startParameter);

        return kPi * 0.5;
    }

    inline double effectiveClosedEllipseStartParameter
    (
        const std::optional<double>& startParameter,
        const DRW_Ellipse* ellipse
    )
    {
        if (startParameter.has_value()) return *startParameter;

        return ellipse != nullptr ? ellipse->staparam : 0.0;
    }

    inline QVector3D ellipseTangentAt(const DRW_Ellipse* ellipse, double parameter, bool reverseDirection)
    {
        QVector3D majorAxis;
        QVector3D minorAxis;

        if (!tryBuildEllipseAxes(ellipse, majorAxis, minorAxis))
        {
            return QVector3D();
        }

        QVector3D tangent
        (
            static_cast<float>(-std::sin(parameter)) * majorAxis
            + static_cast<float>(std::cos(parameter)) * minorAxis
        );

        if (reverseDirection)
        {
            tangent = -tangent;
        }

        return normalizeOrZero(tangent);
    }

    inline QVector3D resolvePolylineNormal(const DRW_Coord& extPoint)
    {
        QVector3D normal(extPoint.x, extPoint.y, extPoint.z);

        if (normal.lengthSquared() <= kVisualEpsilon)
        {
            return QVector3D();
        }

        normal.normalize();
        return normal;
    }

    inline bool is3DPolyline(const DRW_Polyline* polyline)
    {
        return polyline != nullptr && ((polyline->flags & 8) != 0);
    }

    inline QVector3D inferPolylinePlaneNormal(const std::vector<std::shared_ptr<DRW_Vertex>>& vertices)
    {
        if (vertices.size() < 3)
        {
            return QVector3D();
        }

        const QVector3D p0
        (
            static_cast<float>(vertices.front()->basePoint.x),
            static_cast<float>(vertices.front()->basePoint.y),
            static_cast<float>(vertices.front()->basePoint.z)
        );

        for (size_t index = 1; index + 1 < vertices.size(); ++index)
        {
            const QVector3D p1
            (
                static_cast<float>(vertices[index]->basePoint.x),
                static_cast<float>(vertices[index]->basePoint.y),
                static_cast<float>(vertices[index]->basePoint.z)
            );
            const QVector3D p2
            (
                static_cast<float>(vertices[index + 1]->basePoint.x),
                static_cast<float>(vertices[index + 1]->basePoint.y),
                static_cast<float>(vertices[index + 1]->basePoint.z)
            );

            QVector3D normal = QVector3D::crossProduct(p1 - p0, p2 - p0);

            if (normal.lengthSquared() > kVisualEpsilon)
            {
                normal.normalize();
                return normal;
            }
        }

        return QVector3D();
    }

    inline bool buildPolylinePlaneBasis
    (
        const DRW_Polyline* polyline,
        QVector3D& origin,
        QVector3D& axisU,
        QVector3D& axisV,
        QVector3D& normal
    )
    {
        if (polyline == nullptr || polyline->vertlist.empty())
        {
            return false;
        }

        if (is3DPolyline(polyline))
        {
            normal = inferPolylinePlaneNormal(polyline->vertlist);

            if (normal.lengthSquared() <= kVisualEpsilon)
            {
                return false;
            }

            origin = QVector3D
            (
                static_cast<float>(polyline->vertlist.front()->basePoint.x),
                static_cast<float>(polyline->vertlist.front()->basePoint.y),
                static_cast<float>(polyline->vertlist.front()->basePoint.z)
            );
            buildPlaneBasis(normal, axisU, axisV);
            return true;
        }

        normal = resolvePolylineNormal(polyline->extPoint);

        if (normal.lengthSquared() <= kVisualEpsilon)
        {
            normal = QVector3D(0.0f, 0.0f, 1.0f);
        }

        buildPlaneBasis(normal, axisU, axisV);
        origin = normal * static_cast<float>(polyline->basePoint.z);
        return true;
    }

    inline bool buildLWPolylinePlaneBasis
    (
        const DRW_LWPolyline* polyline,
        QVector3D& origin,
        QVector3D& axisU,
        QVector3D& axisV,
        QVector3D& normal
    )
    {
        if (polyline == nullptr)
        {
            return false;
        }

        normal = resolveNormal(polyline->extPoint);
        buildPlaneBasis(normal, axisU, axisV);
        origin = normal * static_cast<float>(polyline->elevation);
        return true;
    }

    inline QVector3D polylineVertexToWcs(const DRW_Polyline* polyline, const std::shared_ptr<DRW_Vertex>& vertex)
    {
        if (polyline == nullptr || vertex == nullptr)
        {
            return QVector3D();
        }

        if (is3DPolyline(polyline))
        {
            return QVector3D
            (
                static_cast<float>(vertex->basePoint.x),
                static_cast<float>(vertex->basePoint.y),
                static_cast<float>(vertex->basePoint.z)
            );
        }

        QVector3D origin;
        QVector3D axisU;
        QVector3D axisV;
        QVector3D normal;
        buildPolylinePlaneBasis(polyline, origin, axisU, axisV, normal);

        return origin
            + axisU * static_cast<float>(vertex->basePoint.x)
            + axisV * static_cast<float>(vertex->basePoint.y)
            + normal * static_cast<float>(vertex->basePoint.z);
    }

    inline QVector3D lwPolylineVertexToWcs(const DRW_LWPolyline* polyline, const std::shared_ptr<DRW_Vertex2D>& vertex)
    {
        if (polyline == nullptr || vertex == nullptr)
        {
            return QVector3D();
        }

        QVector3D origin;
        QVector3D axisU;
        QVector3D axisV;
        QVector3D normal;
        buildLWPolylinePlaneBasis(polyline, origin, axisU, axisV, normal);

        return origin
            + axisU * static_cast<float>(vertex->x)
            + axisV * static_cast<float>(vertex->y);
    }

    inline void projectPointToPlaneUV
    (
        const QVector3D& origin,
        const QVector3D& axisU,
        const QVector3D& axisV,
        const QVector3D& point,
        double& u,
        double& v
    )
    {
        const QVector3D delta = point - origin;
        u = QVector3D::dotProduct(delta, axisU);
        v = QVector3D::dotProduct(delta, axisV);
    }

    inline QVector3D bulgeSegmentTangentAtStartOnPlane
    (
        const QVector3D& origin,
        const QVector3D& axisU,
        const QVector3D& axisV,
        const QVector3D& startPoint,
        const QVector3D& endPoint,
        double bulge
    )
    {
        if (std::abs(bulge) < 1.0e-8)
        {
            return normalizeOrZero(endPoint - startPoint);
        }

        double su = 0.0;
        double sv = 0.0;
        double eu = 0.0;
        double ev = 0.0;
        projectPointToPlaneUV(origin, axisU, axisV, startPoint, su, sv);
        projectPointToPlaneUV(origin, axisU, axisV, endPoint, eu, ev);

        const double dx = eu - su;
        const double dy = ev - sv;
        const double chordLength = std::sqrt(dx * dx + dy * dy);

        if (chordLength <= kVisualEpsilon)
        {
            return QVector3D();
        }

        const double midpointU = (su + eu) * 0.5;
        const double midpointV = (sv + ev) * 0.5;
        const double centerOffset = chordLength * (1.0 / bulge - bulge) * 0.25;
        const double centerU = midpointU - centerOffset * (dy / chordLength);
        const double centerV = midpointV + centerOffset * (dx / chordLength);
        const double radiusU = su - centerU;
        const double radiusV = sv - centerV;
        const double tangentU = bulge > 0.0 ? -radiusV : radiusV;
        const double tangentV = bulge > 0.0 ? radiusU : -radiusU;

        return mapPlaneVectorToWorld(axisU, axisV, tangentU, tangentV);
    }

    inline QVector3D bulgeSegmentTangentAtEndOnPlane
    (
        const QVector3D& origin,
        const QVector3D& axisU,
        const QVector3D& axisV,
        const QVector3D& startPoint,
        const QVector3D& endPoint,
        double bulge
    )
    {
        if (std::abs(bulge) < 1.0e-8)
        {
            return normalizeOrZero(endPoint - startPoint);
        }

        double su = 0.0;
        double sv = 0.0;
        double eu = 0.0;
        double ev = 0.0;
        projectPointToPlaneUV(origin, axisU, axisV, startPoint, su, sv);
        projectPointToPlaneUV(origin, axisU, axisV, endPoint, eu, ev);

        const double dx = eu - su;
        const double dy = ev - sv;
        const double chordLength = std::sqrt(dx * dx + dy * dy);

        if (chordLength <= kVisualEpsilon)
        {
            return QVector3D();
        }

        const double midpointU = (su + eu) * 0.5;
        const double midpointV = (sv + ev) * 0.5;
        const double centerOffset = chordLength * (1.0 / bulge - bulge) * 0.25;
        const double centerU = midpointU - centerOffset * (dy / chordLength);
        const double centerV = midpointV + centerOffset * (dx / chordLength);
        const double radiusU = eu - centerU;
        const double radiusV = ev - centerV;
        const double tangentU = bulge > 0.0 ? -radiusV : radiusV;
        const double tangentV = bulge > 0.0 ? radiusU : -radiusU;

        return mapPlaneVectorToWorld(axisU, axisV, tangentU, tangentV);
    }

    inline QVector3D polylineForwardStartTangent(const DRW_Polyline* polyline)
    {
        if (polyline == nullptr || polyline->vertlist.size() < 2)
        {
            return QVector3D();
        }

        QVector3D origin;
        QVector3D axisU;
        QVector3D axisV;
        QVector3D normal;
        const bool hasPlaneBasis = buildPolylinePlaneBasis(polyline, origin, axisU, axisV, normal);

        for (size_t index = 0; index + 1 < polyline->vertlist.size(); ++index)
        {
            const auto& current = polyline->vertlist.at(index);
            const auto& next = polyline->vertlist.at(index + 1);
            const QVector3D startPoint = polylineVertexToWcs(polyline, current);
            const QVector3D endPoint = polylineVertexToWcs(polyline, next);
            const QVector3D tangent = hasPlaneBasis && !is3DPolyline(polyline)
                ? bulgeSegmentTangentAtStartOnPlane(origin, axisU, axisV, startPoint, endPoint, current->bulge)
                : normalizeOrZero(endPoint - startPoint);

            if (tangent.lengthSquared() > kVisualEpsilon)
            {
                return tangent;
            }
        }

        if ((polyline->flags & 1) != 0)
        {
            const auto& current = polyline->vertlist.back();
            const auto& next = polyline->vertlist.front();
            const QVector3D startPoint = polylineVertexToWcs(polyline, current);
            const QVector3D endPoint = polylineVertexToWcs(polyline, next);
            return hasPlaneBasis && !is3DPolyline(polyline)
                ? bulgeSegmentTangentAtStartOnPlane(origin, axisU, axisV, startPoint, endPoint, current->bulge)
                : normalizeOrZero(endPoint - startPoint);
        }

        return QVector3D();
    }

    inline QVector3D polylineForwardStartTangentAt(const DRW_Polyline* polyline, size_t startIndex)
    {
        if (polyline == nullptr || polyline->vertlist.size() < 2)
        {
            return QVector3D();
        }

        const size_t count = polyline->vertlist.size();
        const size_t nextIndex = (startIndex + 1) % count;
        const auto& current = polyline->vertlist.at(startIndex);
        const auto& next = polyline->vertlist.at(nextIndex);
        const QVector3D startPoint = polylineVertexToWcs(polyline, current);
        const QVector3D endPoint = polylineVertexToWcs(polyline, next);
        QVector3D origin;
        QVector3D axisU;
        QVector3D axisV;
        QVector3D normal;
        const bool hasPlaneBasis = buildPolylinePlaneBasis(polyline, origin, axisU, axisV, normal);
        return hasPlaneBasis && !is3DPolyline(polyline)
            ? bulgeSegmentTangentAtStartOnPlane(origin, axisU, axisV, startPoint, endPoint, current->bulge)
            : normalizeOrZero(endPoint - startPoint);
    }

    inline QVector3D polylineReverseStartTangent(const DRW_Polyline* polyline)
    {
        if (polyline == nullptr || polyline->vertlist.size() < 2)
        {
            return QVector3D();
        }

        QVector3D origin;
        QVector3D axisU;
        QVector3D axisV;
        QVector3D normal;
        const bool hasPlaneBasis = buildPolylinePlaneBasis(polyline, origin, axisU, axisV, normal);

        for (size_t index = polyline->vertlist.size() - 1; index > 0; --index)
        {
            const auto& current = polyline->vertlist.at(index);
            const auto& next = polyline->vertlist.at(index - 1);
            const QVector3D startPoint = polylineVertexToWcs(polyline, current);
            const QVector3D endPoint = polylineVertexToWcs(polyline, next);
            const QVector3D tangent = hasPlaneBasis && !is3DPolyline(polyline)
                ? bulgeSegmentTangentAtStartOnPlane(origin, axisU, axisV, startPoint, endPoint, -next->bulge)
                : normalizeOrZero(endPoint - startPoint);

            if (tangent.lengthSquared() > kVisualEpsilon)
            {
                return tangent;
            }
        }

        if ((polyline->flags & 1) != 0)
        {
            const auto& current = polyline->vertlist.front();
            const auto& next = polyline->vertlist.back();
            const QVector3D startPoint = polylineVertexToWcs(polyline, current);
            const QVector3D endPoint = polylineVertexToWcs(polyline, next);
            return hasPlaneBasis && !is3DPolyline(polyline)
                ? bulgeSegmentTangentAtStartOnPlane(origin, axisU, axisV, startPoint, endPoint, -next->bulge)
                : normalizeOrZero(endPoint - startPoint);
        }

        return QVector3D();
    }

    inline QVector3D polylineForwardEndTangentAt(const DRW_Polyline* polyline, size_t startIndex)
    {
        if (polyline == nullptr || polyline->vertlist.size() < 2)
        {
            return QVector3D();
        }

        const size_t count = polyline->vertlist.size();
        const size_t previousIndex = (startIndex + count - 1) % count;
        const auto& previous = polyline->vertlist.at(previousIndex);
        const auto& current = polyline->vertlist.at(startIndex);
        const QVector3D startPoint = polylineVertexToWcs(polyline, previous);
        const QVector3D endPoint = polylineVertexToWcs(polyline, current);
        QVector3D origin;
        QVector3D axisU;
        QVector3D axisV;
        QVector3D normal;
        const bool hasPlaneBasis = buildPolylinePlaneBasis(polyline, origin, axisU, axisV, normal);
        return hasPlaneBasis && !is3DPolyline(polyline)
            ? bulgeSegmentTangentAtEndOnPlane(origin, axisU, axisV, startPoint, endPoint, previous->bulge)
            : normalizeOrZero(endPoint - startPoint);
    }

    inline QVector3D polylineReverseStartTangentAt(const DRW_Polyline* polyline, size_t startIndex)
    {
        return -polylineForwardEndTangentAt(polyline, startIndex);
    }

    inline QVector3D lwPolylineForwardStartTangent(const DRW_LWPolyline* polyline)
    {
        if (polyline == nullptr || polyline->vertlist.size() < 2)
        {
            return QVector3D();
        }

        QVector3D origin;
        QVector3D axisU;
        QVector3D axisV;
        QVector3D normal;
        const bool hasPlaneBasis = buildLWPolylinePlaneBasis(polyline, origin, axisU, axisV, normal);

        for (size_t index = 0; index + 1 < polyline->vertlist.size(); ++index)
        {
            const auto& current = polyline->vertlist.at(index);
            const auto& next = polyline->vertlist.at(index + 1);
            const QVector3D startPoint = lwPolylineVertexToWcs(polyline, current);
            const QVector3D endPoint = lwPolylineVertexToWcs(polyline, next);
            const QVector3D tangent = hasPlaneBasis
                ? bulgeSegmentTangentAtStartOnPlane(origin, axisU, axisV, startPoint, endPoint, current->bulge)
                : normalizeOrZero(endPoint - startPoint);

            if (tangent.lengthSquared() > kVisualEpsilon)
            {
                return tangent;
            }
        }

        if ((polyline->flags & 1) != 0)
        {
            const auto& current = polyline->vertlist.back();
            const auto& next = polyline->vertlist.front();
            const QVector3D startPoint = lwPolylineVertexToWcs(polyline, current);
            const QVector3D endPoint = lwPolylineVertexToWcs(polyline, next);
            return hasPlaneBasis
                ? bulgeSegmentTangentAtStartOnPlane(origin, axisU, axisV, startPoint, endPoint, current->bulge)
                : normalizeOrZero(endPoint - startPoint);
        }

        return QVector3D();
    }

    inline QVector3D lwPolylineForwardStartTangentAt(const DRW_LWPolyline* polyline, size_t startIndex)
    {
        if (polyline == nullptr || polyline->vertlist.size() < 2)
        {
            return QVector3D();
        }

        const size_t count = polyline->vertlist.size();
        const size_t nextIndex = (startIndex + 1) % count;
        const auto& current = polyline->vertlist.at(startIndex);
        const auto& next = polyline->vertlist.at(nextIndex);
        const QVector3D startPoint = lwPolylineVertexToWcs(polyline, current);
        const QVector3D endPoint = lwPolylineVertexToWcs(polyline, next);
        QVector3D origin;
        QVector3D axisU;
        QVector3D axisV;
        QVector3D normal;
        const bool hasPlaneBasis = buildLWPolylinePlaneBasis(polyline, origin, axisU, axisV, normal);
        return hasPlaneBasis
            ? bulgeSegmentTangentAtStartOnPlane(origin, axisU, axisV, startPoint, endPoint, current->bulge)
            : normalizeOrZero(endPoint - startPoint);
    }

    inline QVector3D lwPolylineReverseStartTangent(const DRW_LWPolyline* polyline)
    {
        if (polyline == nullptr || polyline->vertlist.size() < 2)
        {
            return QVector3D();
        }

        QVector3D origin;
        QVector3D axisU;
        QVector3D axisV;
        QVector3D normal;
        const bool hasPlaneBasis = buildLWPolylinePlaneBasis(polyline, origin, axisU, axisV, normal);

        for (size_t index = polyline->vertlist.size() - 1; index > 0; --index)
        {
            const auto& current = polyline->vertlist.at(index);
            const auto& next = polyline->vertlist.at(index - 1);
            const QVector3D startPoint = lwPolylineVertexToWcs(polyline, current);
            const QVector3D endPoint = lwPolylineVertexToWcs(polyline, next);
            const QVector3D tangent = hasPlaneBasis
                ? bulgeSegmentTangentAtStartOnPlane(origin, axisU, axisV, startPoint, endPoint, -next->bulge)
                : normalizeOrZero(endPoint - startPoint);

            if (tangent.lengthSquared() > kVisualEpsilon)
            {
                return tangent;
            }
        }

        if ((polyline->flags & 1) != 0)
        {
            const auto& current = polyline->vertlist.front();
            const auto& next = polyline->vertlist.back();
            const QVector3D startPoint = lwPolylineVertexToWcs(polyline, current);
            const QVector3D endPoint = lwPolylineVertexToWcs(polyline, next);
            return hasPlaneBasis
                ? bulgeSegmentTangentAtStartOnPlane(origin, axisU, axisV, startPoint, endPoint, -next->bulge)
                : normalizeOrZero(endPoint - startPoint);
        }

        return QVector3D();
    }

    inline QVector3D lwPolylineForwardEndTangentAt(const DRW_LWPolyline* polyline, size_t startIndex)
    {
        if (polyline == nullptr || polyline->vertlist.size() < 2)
        {
            return QVector3D();
        }

        const size_t count = polyline->vertlist.size();
        const size_t previousIndex = (startIndex + count - 1) % count;
        const auto& previous = polyline->vertlist.at(previousIndex);
        const auto& current = polyline->vertlist.at(startIndex);
        const QVector3D startPoint = lwPolylineVertexToWcs(polyline, previous);
        const QVector3D endPoint = lwPolylineVertexToWcs(polyline, current);
        QVector3D origin;
        QVector3D axisU;
        QVector3D axisV;
        QVector3D normal;
        const bool hasPlaneBasis = buildLWPolylinePlaneBasis(polyline, origin, axisU, axisV, normal);
        return hasPlaneBasis
            ? bulgeSegmentTangentAtEndOnPlane(origin, axisU, axisV, startPoint, endPoint, previous->bulge)
            : normalizeOrZero(endPoint - startPoint);
    }

    inline QVector3D lwPolylineReverseStartTangentAt(const DRW_LWPolyline* polyline, size_t startIndex)
    {
        return -lwPolylineForwardEndTangentAt(polyline, startIndex);
    }

    inline size_t effectiveClosedPolylineStartIndex
    (
        const std::optional<double>& startParameter,
        size_t vertexCount
    )
    {
        if (vertexCount == 0)
        {
            return 0;
        }

        if (startParameter.has_value())
        {
            const int rawIndex = static_cast<int>(std::llround(*startParameter));
            const int normalized = ((rawIndex % static_cast<int>(vertexCount)) + static_cast<int>(vertexCount)) % static_cast<int>(vertexCount);
            return static_cast<size_t>(normalized);
        }

        return 0;
    }

    inline bool tryBuildGeometryAnchor(const CadItem* item, QVector3D& anchor)
    {
        if (item == nullptr || item->m_geometry.vertices.isEmpty())
        {
            return false;
        }

        QVector3D minPoint = item->m_geometry.vertices.front();
        QVector3D maxPoint = minPoint;

        for (const QVector3D& vertex : item->m_geometry.vertices)
        {
            minPoint.setX(std::min(minPoint.x(), vertex.x()));
            minPoint.setY(std::min(minPoint.y(), vertex.y()));
            minPoint.setZ(std::min(minPoint.z(), vertex.z()));
            maxPoint.setX(std::max(maxPoint.x(), vertex.x()));
            maxPoint.setY(std::max(maxPoint.y(), vertex.y()));
            maxPoint.setZ(std::max(maxPoint.z(), vertex.z()));
        }

        anchor = (minPoint + maxPoint) * 0.5f;
        return true;
    }

    inline void appendSelectionHandle
    (
        QVector<CadSelectionHandleInfo>& handles,
        const QVector3D& position,
        bool isBasePoint,
        bool editable,
        int pointIndex,
        CadSelectionHandleShape shape = CadSelectionHandleShape::RoundPoint,
        const QVector3D& direction = QVector3D()
    )
    {
        for (const CadSelectionHandleInfo& handle : handles)
        {
            if ((handle.position - position).lengthSquared() <= kVisualEpsilon)
            {
                return;
            }
        }

        CadSelectionHandleInfo handle;
        handle.position = position;
        handle.isBasePoint = isBasePoint;
        handle.editable = editable;
        handle.pointIndex = pointIndex;
        handle.shape = shape;
        handle.direction = normalizeOrZero(direction);
        handles.push_back(std::move(handle));
    }
}