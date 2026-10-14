#include "check.h"
#include "soh/soh/Enhancements/randomizer/map_tracker_pack_schema.h"

int main() {
    using namespace CheckTracker;
    using json = nlohmann::json;
    std::vector<std::string> warnings;
    bool invalid = false;
    auto node = json::parse(R"({"map_locations":[{"map_id":" kokiri_forest ","x":"12.5","y":20},
        {"map_id":"overworld","x":50,"y":" 90 ","size":15}],"unknown_editor_field":true})");
    auto placements = ParseMapPlacements(node, warnings, invalid);
    CHECK(!invalid && warnings.empty() && placements.size() == 2);
    CHECK(placements[0].mapId == "kokiri_forest" && placements[0].size == 22);
    CHECK(placements[0].x == 12.5f && placements[1].size == 15);
    for (const auto& bad :
         { json(nullptr), json::parse(R"({"map_id":"","x":1,"y":2})"), json::parse(R"({"map_id":"x","x":"nan","y":2})"),
           json::parse(R"({"map_id":"x","x":"3junk","y":2})"),
           json::parse(R"({"map_id":"x","x":1,"y":2,"size":0})") }) {
        node["map_locations"].push_back(bad);
    }
    placements = ParseMapPlacements(node, warnings, invalid);
    CHECK(invalid && warnings.size() == 5 && placements.size() == 2);
    float coordinate = 0;
    CHECK(!ReadMapCoordinate(json::array(), coordinate));
    CHECK(!ReadMapCoordinate("inf", coordinate));
    CHECK(!ReadMapCoordinate(1e30, coordinate));
    CHECK(!ReadMapCoordinate("", coordinate));
    CHECK(ReadMapCoordinate(-300, coordinate) && coordinate == -300);
}
