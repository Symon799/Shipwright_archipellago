#pragma once

#include <cmath>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace CheckTracker {
struct MapPlacement {
    std::string mapId;
    float x = 0.0f;
    float y = 0.0f;
    float size = 22.0f;
};

inline std::string TrimPackString(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    return value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1);
}

inline bool ReadMapCoordinate(const nlohmann::json& value, float& output) {
    try {
        if (value.is_number()) {
            output = value.get<float>();
        } else if (value.is_string()) {
            const auto text = TrimPackString(value.get<std::string>());
            size_t consumed = 0;
            output = std::stof(text, &consumed);
            if (consumed != text.size())
                return false;
        } else {
            return false;
        }
        // Render grouping quantizes pixels by 100 into an int; reject overflow-sized coordinates.
        return std::isfinite(output) && std::abs(output) <= 10000000.0f;
    } catch (const std::exception&) { return false; }
}

inline std::vector<MapPlacement> ParseMapPlacements(const nlohmann::json& node, std::vector<std::string>& warnings,
                                                    bool& invalid) {
    std::vector<MapPlacement> placements;
    if (!node.contains("map_locations") || !node["map_locations"].is_array())
        return placements;
    for (size_t index = 0; index < node["map_locations"].size(); ++index) {
        const auto& entry = node["map_locations"][index];
        MapPlacement placement;
        const bool validId = entry.is_object() && entry.contains("map_id") && entry["map_id"].is_string();
        if (validId)
            placement.mapId = TrimPackString(entry["map_id"].get<std::string>());
        if (!validId || placement.mapId.empty() || !entry.contains("x") || !entry.contains("y") ||
            !ReadMapCoordinate(entry["x"], placement.x) || !ReadMapCoordinate(entry["y"], placement.y) ||
            (entry.contains("size") && !ReadMapCoordinate(entry["size"], placement.size)) || placement.size <= 0) {
            invalid = true;
            warnings.push_back("Invalid map_id/x/y/size at map_locations[" + std::to_string(index) + "]");
            continue;
        }
        placements.push_back(std::move(placement));
    }
    return placements;
}
} // namespace CheckTracker
