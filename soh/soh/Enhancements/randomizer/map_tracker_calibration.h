#pragma once

#include "map_tracker_projection.h"
#include <array>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

namespace MapTracker {

struct CalibrationMesh {
    struct Triangle {
        std::array<size_t, 3> vertices;
        PositionProjection projection;
    };
    std::vector<PositionAnchor> anchors;
    std::vector<Triangle> triangles;
    std::optional<PositionProjection> affine;
    bool piecewise = true;
    bool hideOutside = true;

    static double Cross(const PositionAnchor& a, const PositionAnchor& b, const PositionAnchor& c) {
        return (b.sourceX - a.sourceX) * (c.sourceY - a.sourceY) -
               (b.sourceY - a.sourceY) * (c.sourceX - a.sourceX);
    }

    std::optional<PositionProjection> ProjectionAt(double x, double y) const {
        const PositionAnchor point{ x, y, 0, 0 };
        for (const auto& triangle : triangles) {
            const auto& a = anchors[triangle.vertices[0]];
            const auto& b = anchors[triangle.vertices[1]];
            const auto& c = anchors[triangle.vertices[2]];
            const double area = Cross(a, b, c);
            const double u = Cross(point, b, c) / area;
            const double v = Cross(a, point, c) / area;
            if (u >= -1e-8 && v >= -1e-8 && u + v <= 1.0 + 1e-8) {
                return piecewise ? std::optional(triangle.projection) : affine;
            }
        }
        return hideOutside ? std::nullopt : affine;
    }

    bool Build() {
        triangles.clear();
        affine.reset();
        if (anchors.size() < 3) {
            return false;
        }
        for (const auto& a : anchors) {
            if (!std::isfinite(a.sourceX) || !std::isfinite(a.sourceY) || !std::isfinite(a.mapX) ||
                !std::isfinite(a.mapY)) {
                return false;
            }
        }
        std::sort(anchors.begin(), anchors.end(), [](const auto& a, const auto& b) {
            return a.sourceX != b.sourceX ? a.sourceX < b.sourceX : a.sourceY < b.sourceY;
        });
        double minX = anchors.front().sourceX, maxX = minX, minY = anchors.front().sourceY, maxY = minY;
        for (size_t i = 0; i < anchors.size(); ++i) {
            const auto& a = anchors[i];
            if (!std::isfinite(a.sourceX) || !std::isfinite(a.sourceY) || !std::isfinite(a.mapX) ||
                !std::isfinite(a.mapY) || (i && a.sourceX == anchors[i - 1].sourceX && a.sourceY == anchors[i - 1].sourceY)) {
                return false;
            }
            minX = std::min(minX, a.sourceX); maxX = std::max(maxX, a.sourceX);
            minY = std::min(minY, a.sourceY); maxY = std::max(maxY, a.sourceY);
        }
        const double span = std::max(maxX - minX, maxY - minY);
        if (!std::isfinite(span) || span <= 1e-8) {
            return false;
        }
        // Normalize before Bowyer-Watson to keep circumcircle tests stable across game coordinate scales.
        std::vector<PositionAnchor> points;
        for (const auto& a : anchors) {
            points.push_back({ (a.sourceX - minX) / span, (a.sourceY - minY) / span, 0, 0 });
        }
        const size_t count = points.size();
        points.insert(points.end(), { { -16, -16, 0, 0 }, { 32, -16, 0, 0 }, { -16, 32, 0, 0 } });
        using Indices = std::array<size_t, 3>;
        std::vector<Indices> mesh{ { count, count + 1, count + 2 } };
        for (size_t index = 0; index < count; ++index) {
            std::vector<std::array<size_t, 2>> boundary;
            std::vector<Indices> remaining;
            for (const auto& triangle : mesh) {
                const auto& p = points[index];
                const auto& a = points[triangle[0]]; const auto& b = points[triangle[1]]; const auto& c = points[triangle[2]];
                const double ax = a.sourceX - p.sourceX, ay = a.sourceY - p.sourceY;
                const double bx = b.sourceX - p.sourceX, by = b.sourceY - p.sourceY;
                const double cx = c.sourceX - p.sourceX, cy = c.sourceY - p.sourceY;
                const double determinant = (ax * ax + ay * ay) * (bx * cy - by * cx) -
                    (bx * bx + by * by) * (ax * cy - ay * cx) + (cx * cx + cy * cy) * (ax * by - ay * bx);
                if (determinant < -1e-12) {
                    remaining.push_back(triangle);
                    continue;
                }
                for (int edge = 0; edge < 3; ++edge) {
                    std::array<size_t, 2> pair{ triangle[edge], triangle[(edge + 1) % 3] };
                    auto duplicate = std::find_if(boundary.begin(), boundary.end(), [&](const auto& other) {
                        return other[0] == pair[1] && other[1] == pair[0];
                    });
                    if (duplicate == boundary.end()) boundary.push_back(pair);
                    else boundary.erase(duplicate);
                }
            }
            for (auto edge : boundary) {
                const double area = Cross(points[edge[0]], points[edge[1]], points[index]);
                if (std::abs(area) <= 1e-12) continue;
                if (area < 0) std::swap(edge[0], edge[1]);
                remaining.push_back({ edge[0], edge[1], index });
            }
            mesh = std::move(remaining);
        }
        int orientation = 0;
        for (const auto& triangle : mesh) {
            if (triangle[0] >= count || triangle[1] >= count || triangle[2] >= count) continue;
            const auto& a = anchors[triangle[0]];
            const auto& b = anchors[triangle[1]];
            const auto& c = anchors[triangle[2]];
            const double area = Cross(a, b, c);
            PositionProjection fit{
                ((b.mapX - a.mapX) * (c.sourceY - a.sourceY) - (c.mapX - a.mapX) * (b.sourceY - a.sourceY)) / area,
                ((c.mapX - a.mapX) * (b.sourceX - a.sourceX) - (b.mapX - a.mapX) * (c.sourceX - a.sourceX)) / area,
                ((b.mapY - a.mapY) * (c.sourceY - a.sourceY) - (c.mapY - a.mapY) * (b.sourceY - a.sourceY)) / area,
                ((c.mapY - a.mapY) * (b.sourceX - a.sourceX) - (b.mapY - a.mapY) * (c.sourceX - a.sourceX)) / area,
                0, 0 };
            fit.offsetX = a.mapX - fit.xx * a.sourceX - fit.xz * a.sourceY;
            fit.offsetY = a.mapY - fit.zx * a.sourceX - fit.zz * a.sourceY;
            const double determinant = fit.xx * fit.zz - fit.xz * fit.zx;
            const double scale = fit.xx * fit.xx + fit.xz * fit.xz + fit.zx * fit.zx + fit.zz * fit.zz;
            if (!std::isfinite(scale) || !std::isfinite(fit.offsetX) || !std::isfinite(fit.offsetY) ||
                std::abs(determinant) <= scale * 1e-10) return false;
            const int sign = determinant > 0 ? 1 : -1;
            if (orientation && orientation != sign) return false; // Folded target geometry is not a valid atlas.
            orientation = sign;
            triangles.push_back({ triangle, fit });
        }
        affine = FitPositionProjection(anchors);
        return !triangles.empty() && (piecewise || affine.has_value()) && (hideOutside || affine.has_value());
    }
};

struct CalibrationProfile {
    std::string id;
    int scene = -1;
    std::optional<int> room;
    std::vector<int> setups;
    std::string layout = "any";
    CalibrationMesh mesh;

    bool Matches(int sceneId, int roomId, int setupId, bool mq) const {
        return scene == sceneId && (!room || *room == roomId) &&
               (setups.empty() || std::find(setups.begin(), setups.end(), setupId) != setups.end()) &&
               (layout == "any" || layout == (mq ? "mq" : "vanilla"));
    }
};

inline std::vector<CalibrationProfile> ParsePlayerCalibrations(const nlohmann::json& data,
                                                              std::vector<std::string>& warnings) {
    std::vector<CalibrationProfile> result;
    if (!data.is_object() || !data.contains("version") || !data["version"].is_number_integer() ||
        data["version"] != 1 || !data.contains("calibrations") || !data["calibrations"].is_array()) {
        warnings.push_back("Unsupported player_position schema; using automatic link positioning.");
        return result;
    }
    for (const auto& entry : data["calibrations"]) {
        try {
            CalibrationProfile profile;
            profile.id = entry.at("id").get<std::string>();
            if (!entry.at("scene_id").is_number_integer()) throw std::runtime_error("scene_id must be an integer");
            profile.scene = entry.at("scene_id").get<int>();
            if (profile.scene < 0 || profile.scene > 255 || profile.id.empty()) throw std::runtime_error("invalid scene/id");
            if (entry.contains("room_id") && !entry["room_id"].is_null()) {
                if (!entry["room_id"].is_number_integer()) throw std::runtime_error("room_id must be an integer");
                profile.room = entry["room_id"].get<int>();
                if (*profile.room < 0 || *profile.room > 255) throw std::runtime_error("invalid room");
            }
            if (entry.contains("setup_ids") && !entry["setup_ids"].is_null()) {
                if (!entry["setup_ids"].is_array() || entry["setup_ids"].empty()) throw std::runtime_error("invalid setups");
                for (const auto& setup : entry["setup_ids"]) {
                    if (!setup.is_number_integer() || setup.get<int>() < 0 || setup.get<int>() > 255)
                        throw std::runtime_error("invalid setup");
                    profile.setups.push_back(setup.get<int>());
                }
            }
            profile.layout = entry.value("layout", "any");
            const std::string method = entry.value("method", "piecewise_affine");
            const std::string outside = entry.value("outside_hull", "hide");
            if ((profile.layout != "any" && profile.layout != "vanilla" && profile.layout != "mq") ||
                (method != "affine" && method != "piecewise_affine") || (outside != "hide" && outside != "affine"))
                throw std::runtime_error("unsupported calibration mode");
            profile.mesh.piecewise = method == "piecewise_affine";
            profile.mesh.hideOutside = outside == "hide";
            if (!entry.at("anchors").is_array()) throw std::runtime_error("anchors must be an array");
            for (const auto& anchor : entry["anchors"]) {
                if (!anchor.at("world").at("x").is_number() || !anchor.at("world").at("z").is_number() ||
                    !anchor.at("map").at("x").is_number() || !anchor.at("map").at("y").is_number())
                    throw std::runtime_error("anchor coordinates must be numbers");
                profile.mesh.anchors.push_back({ anchor.at("world").at("x").get<double>(),
                    anchor.at("world").at("z").get<double>(), anchor.at("map").at("x").get<double>(),
                    anchor.at("map").at("y").get<double>() });
            }
            if (!profile.mesh.Build()) throw std::runtime_error("insufficient, duplicate, degenerate or folded anchors");
            result.push_back(std::move(profile));
        } catch (const std::exception& error) {
            warnings.push_back(std::string("Invalid player calibration; using link fallback: ") + error.what());
        }
    }
    return result;
}

} // namespace MapTracker
