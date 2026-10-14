#include "check.h"
#include "soh/soh/Network/Archipelago/ArchipelagoSession.h"
#include <apclient.hpp>
#include <chrono>
#include <thread>

int main(int argc, char** argv) {
    CHECK(argc == 4);
    for (int session = 0; session < 3; ++session) {
        APClient client("soh-validation-only", "Ship of Harkinian", std::string("ws://127.0.0.1:") + argv[session + 1]);
        bool connected = false;
        bool packageReady = false;
        bool scouted = false;
        bool checked = false;
        bool refused = false;
        size_t receivedCount = 0;
        std::string error;
        Archipelago::ReceivedItemQueue queue;
        client.set_socket_error_handler([&](const std::string& message) { error = message; });
        client.set_room_info_handler([&] { client.ConnectSlot("Validation", "", 0b0101, {}, { 0, 6, 7 }); });
        client.set_data_package_changed_handler([&](const nlohmann::json&) { packageReady = true; });
        client.set_slot_refused_handler([&](const std::list<std::string>&) { refused = true; });
        client.set_slot_connected_handler([&](const nlohmann::json& data) {
            CHECK(data.at("apworld_version") == "1.4.0");
            CHECK(Archipelago::WorldVersion(data.at("apworld_version").get<std::string>())->second == "4");
            connected = true;
            client.LocationScouts({ 60001, 60002 });
            client.LocationChecks({ 60001 });
            client.Sync();
        });
        client.set_items_received_handler([&](const std::list<APClient::NetworkItem>& items) {
            receivedCount += items.size();
            for (const auto& item : items)
                queue.Push(Archipelago::DecodeReceivedItem(client, item, "Ship of Harkinian"), true, 0);
        });
        client.set_location_info_handler([&](const std::list<APClient::NetworkItem>& items) {
            CHECK(items.size() == 2);
            CHECK(items.front().location == 60001);
            scouted = true;
        });
        client.set_location_checked_handler([&](const std::list<int64_t>& items) {
            if (std::find(items.begin(), items.end(), 60001) != items.end())
                checked = true;
        });
        client.set_room_update_handler([&] { checked = client.get_checked_locations().contains(60001); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
        while ((!connected || !packageReady || !scouted || !checked || receivedCount < 4) && error.empty() &&
               !refused && std::chrono::steady_clock::now() < deadline) {
            client.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!error.empty())
            std::cerr << error << '\n';
        std::cerr << "session=" << session << " connected=" << connected << " package=" << packageReady
                  << " scouts=" << scouted << " checked=" << checked << " received=" << receivedCount << '\n';
        CHECK(connected && packageReady && scouted && checked && receivedCount == 4 && error.empty() && !refused);
        CHECK(client.get_player_alias(1) == "Validation");
        CHECK(Archipelago::SameSlot(client.get_seed(), "Validation", "validation-seed-a", "Validation") ==
              (session < 2));
        CHECK(client.get_checked_locations().contains(60001));
        CHECK(!client.get_missing_locations().contains(60001));
        // The server replays the received batch. The shared queue must deliver each index once.
        auto sword = queue.TakeNext(0);
        CHECK(sword && sword->itemName == "Kokiri Sword" && sword->locationName == "Chest");
        CHECK(!queue.TakeNext(0));
        auto arrows = queue.TakeNext(1);
        CHECK(arrows && arrows->itemName == "Light Arrows" && arrows->flags == 1);
        CHECK(arrows->locationName == "Foreign Chest" && arrows->playerName == "Foreign");
        CHECK(!queue.TakeNext(2));
        client.reset();
    }
}
