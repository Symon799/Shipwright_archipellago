#include "check.h"
#include "soh/soh/Network/Archipelago/ArchipelagoSession.h"
#include "soh/soh/Enhancements/randomizer/archipelago_check_visibility.h"
#include "soh/soh/Enhancements/randomizer/map_tracker_totals.h"

int main() {
    using namespace Archipelago;
    using namespace CheckTracker;
    CHECK(WorldVersion("1.4.0")->first == "1" && WorldVersion("1.4.0")->second == "4");
    CHECK(WorldVersion("1.4.9")->second == "4");
    CHECK(!WorldVersion("1") && !WorldVersion("1.") && !WorldVersion("bad.version"));
    CHECK(SameSlot("seed-a", "Link", "seed-a", "Link")); // Address/port is not the save identity.
    CHECK(!SameSlot("seed-a", "Link", "seed-b", "Link"));
    CHECK(!SameSlot("seed-a", "Link", "seed-a", "Zelda"));
    CHECK(ConnectionProfileKey("a.b", "c") != ConnectionProfileKey("a", "b.c"));
    CHECK(ConnectionProfileKey("ab", "c") != ConnectionProfileKey("a", "bc"));

    ReceivedItemQueue queue;
    Item arrow;
    arrow.itemName = "Light Arrows";
    arrow.index = 1;
    CHECK(!queue.Push(arrow, false, 0));
    CHECK(queue.Push(arrow, true, 0));
    CHECK(!queue.TakeNext(0)); // Wait for the missing earlier event.
    Item sword;
    sword.itemName = "Kokiri Sword";
    CHECK(queue.Push(sword, true, 0));
    CHECK(!queue.Push(sword, true, 0));
    CHECK(queue.TakeNext(0)->itemName == "Kokiri Sword");
    CHECK(!queue.Push(sword, true, 0)); // Duplicate while the game grants the active item.
    CHECK(!queue.TakeNext(0));
    CHECK(queue.TakeNext(1)->itemName == "Light Arrows");
    CHECK(!queue.Push(sword, true, 2)); // Replay after saving.
    CHECK(!queue.TakeNext(2));
    queue.Clear(); // Reconnect/sync restores the saved index, not an arbitrary network offset.
    CHECK(queue.Push(arrow, true, 1));
    CHECK(queue.TakeNext(1)->itemName == "Light Arrows");

    ScoutedCheckIndex<int> scouts;
    auto resolve = [](const std::string& name) -> std::optional<int> {
        if (name == "Chest")
            return 10;
        if (name == "Shop")
            return 11;
        return std::nullopt;
    };
    std::vector<Item> locations(3);
    locations[0].locationName = "Chest";
    locations[1].locationName = "Chest";
    locations[2].locationName = "Unknown location";
    scouts.Update(locations, resolve);
    CHECK(scouts.checks.size() == 1 && scouts.unresolvedNames.size() == 1);
    CHECK(ResolveArchipelagoVisibility(false, false, false) == ArchipelagoVisibility::SeedFallback);
    CHECK(ResolveArchipelagoVisibility(false, true, true) == ArchipelagoVisibility::Visible);
    CHECK(ResolveArchipelagoVisibility(true, true, true) == ArchipelagoVisibility::Hidden);
    CHECK(ResolveArchipelagoVisibility(false, false, true) == ArchipelagoVisibility::Hidden);
    scouts.Update(std::vector<Item>{}, resolve);
    CHECK(scouts.checks.empty() && scouts.unresolvedNames.empty());
    CHECK(ResolveArchipelagoVisibility(false, false, true) == ArchipelagoVisibility::Hidden);

    MapCheckTotals totals;
    for (int check : { 10, 11, 12 }) {
        const auto decision = ResolveArchipelagoVisibility(false, check != 12, true);
        if (decision == ArchipelagoVisibility::Visible)
            totals.AddVisibleCheck(check == 10, check == 11);
    }
    CHECK(totals.accessible == 1 && totals.checked == 1 && totals.total == 2);
}
