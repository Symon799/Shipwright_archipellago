#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace MapTracker {

struct PositionAnchor {
    double sourceX, sourceY;
    double mapX, mapY;
};

struct PositionProjection {
    double xx, xz, zx, zz;
    double offsetX, offsetY;

    PositionAnchor Project(double x, double z) const {
        return { x, z, xx * x + xz * z + offsetX, zx * x + zz * z + offsetY };
    }
};

inline std::optional<PositionProjection> MakeMinimapProjection(double compassScaleX, double compassScaleY,
                                                              double pixelOffsetX, double pixelOffsetY) {
    if (compassScaleX <= 0 || compassScaleY <= 0) {
        return std::nullopt;
    }
    // Compass scales/offsets use tenths of a screen pixel. Keep floating-point precision instead of HUD rounding.
    return PositionProjection{ 1.0 / (10.0 * compassScaleX), 0, 0, 1.0 / (10.0 * compassScaleY),
                               pixelOffsetX, pixelOffsetY };
}

// Three independent anchors allow affine/isometric deformation; two determine rotation and uniform scale only.
inline std::optional<PositionProjection> FitPositionProjection(const std::vector<PositionAnchor>& anchors) {
    if (anchors.size() < 2) {
        return std::nullopt;
    }
    double worldX = 0, worldZ = 0, mapX = 0, mapY = 0;
    for (const auto& anchor : anchors) {
        worldX += anchor.sourceX;
        worldZ += anchor.sourceY;
        mapX += anchor.mapX;
        mapY += anchor.mapY;
    }
    worldX /= anchors.size();
    worldZ /= anchors.size();
    mapX /= anchors.size();
    mapY /= anchors.size();

    double xx = 0, xz = 0, zz = 0, txX = 0, txZ = 0, tyX = 0, tyZ = 0, mapVariance = 0;
    for (const auto& anchor : anchors) {
        const double x = anchor.sourceX - worldX, z = anchor.sourceY - worldZ;
        const double u = anchor.mapX - mapX, v = anchor.mapY - mapY;
        xx += x * x;
        xz += x * z;
        zz += z * z;
        txX += u * x;
        txZ += u * z;
        tyX += v * x;
        tyZ += v * z;
        mapVariance += u * u + v * v;
    }
    PositionProjection projection{};
    const double determinant = xx * zz - xz * xz;
    if (anchors.size() >= 3 && determinant > std::max(1.0, xx * zz) * 0.0001) {
        projection.xx = (txX * zz - txZ * xz) / determinant;
        projection.xz = (txZ * xx - txX * xz) / determinant;
        projection.zx = (tyX * zz - tyZ * xz) / determinant;
        projection.zz = (tyZ * xx - tyX * xz) / determinant;
    } else {
        const double spread = xx + zz;
        if (spread < 1.0) {
            return std::nullopt;
        }
        projection.xx = projection.zz = (txX + tyZ) / spread;
        projection.zx = (tyX - txZ) / spread;
        projection.xz = -projection.zx;
    }
    projection.offsetX = mapX - projection.xx * worldX - projection.xz * worldZ;
    projection.offsetY = mapY - projection.zx * worldX - projection.zz * worldZ;

    double error = 0;
    for (const auto& anchor : anchors) {
        const auto predicted = projection.Project(anchor.sourceX, anchor.sourceY);
        error += std::pow(predicted.mapX - anchor.mapX, 2) + std::pow(predicted.mapY - anchor.mapY, 2);
    }
    const double projectedArea = projection.xx * projection.zz - projection.xz * projection.zx;
    const double projectedScale = projection.xx * projection.xx + projection.xz * projection.xz +
                                  projection.zx * projection.zx + projection.zz * projection.zz;
    if (!std::isfinite(error) || mapVariance < 1.0 || error > mapVariance * 0.1225 ||
        std::abs(projectedArea) < projectedScale * 0.0001) {
        return std::nullopt;
    }
    return projection;
}

} // namespace MapTracker
