#include "check.h"
#include "soh/soh/Enhancements/randomizer/map_tracker_calibration.h"
#include <iostream>

using namespace MapTracker;

int main() {
    CalibrationMesh mesh;
    mesh.anchors = {
        { 0, 0, 10, 20 }, { 100, 0, 210, 20 }, { 0, 100, 10, 220 }, { 100, 100, 210, 220 }, { 50, 50, 120, 130 }
    };
    CHECK(mesh.Build());
    CHECK(mesh.triangles.size() == 4);
    for (const auto& anchor : mesh.anchors) {
        auto fit = mesh.ProjectionAt(anchor.sourceX, anchor.sourceY);
        CHECK(fit);
        auto point = fit->Project(anchor.sourceX, anchor.sourceY);
        CHECK(std::abs(point.mapX - anchor.mapX) < 1e-5);
        CHECK(std::abs(point.mapY - anchor.mapY) < 1e-5);
    }
    CHECK(!mesh.ProjectionAt(-10, 50));
    mesh.hideOutside = false;
    CHECK(mesh.Build() && mesh.ProjectionAt(-10, 50));
    mesh.anchors.pop_back();
    CHECK(mesh.Build() && mesh.triangles.size() == 2); // Co-circular square.
    for (auto& anchor : mesh.anchors)
        anchor.mapX *= -1;
    CHECK(mesh.Build()); // Consistent mirrored orientation is valid.
    mesh.anchors.push_back(mesh.anchors.front());
    CHECK(!mesh.Build());
    mesh.anchors = { { 0, 0, 0, 0 }, { 1, 1, 1, 1 }, { 2, 2, 2, 2 } };
    CHECK(!mesh.Build());

    auto data = nlohmann::json::parse(R"({"version":1,"calibrations":[{
      "id":"test","scene_id":85,"room_id":null,"setup_ids":null,"layout":"any",
      "anchors":[{"world":{"x":0,"z":0},"map":{"x":10,"y":20}},
                 {"world":{"x":100,"z":0},"map":{"x":210,"y":20}},
                 {"world":{"x":0,"z":100},"map":{"x":10,"y":220}}]}]})");
    std::vector<std::string> warnings;
    auto profiles = ParsePlayerCalibrations(data, warnings);
    CHECK(profiles.size() == 1 && warnings.empty());
    CHECK(profiles[0].Matches(85, 3, 4, false));
    CHECK(!profiles[0].Matches(86, 3, 4, false));
    profiles[0].room = 1;
    profiles[0].setups = { 0, 1 };
    profiles[0].layout = "mq";
    CHECK(profiles[0].Matches(85, 1, 0, true));
    CHECK(!profiles[0].Matches(85, 2, 0, true));
    CHECK(!profiles[0].Matches(85, 1, 2, true));
    CHECK(!profiles[0].Matches(85, 1, 0, false));
    data["calibrations"][0]["anchors"].erase(0);
    CHECK(ParsePlayerCalibrations(data, warnings).empty() && !warnings.empty());
    data["version"] = 2;
    CHECK(ParsePlayerCalibrations(data, warnings).empty());
    std::cout << "PASS: piecewise interpolation, co-circular/mirrored meshes, "
                 "hull policy, "
                 "duplicates, collinearity, profile contexts and nonfatal "
                 "invalid schema\n";
}
