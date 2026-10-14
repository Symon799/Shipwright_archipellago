#pragma once

#include <cstdint>
#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Archipelago {
inline std::optional<std::pair<std::string, std::string>> WorldVersion(std::string_view version) {
    const auto first = version.find('.');
    if (first == std::string_view::npos)
        return std::nullopt;
    const auto second = version.find('.', first + 1);
    const auto major = version.substr(0, first);
    const auto minor = version.substr(first + 1, second == std::string_view::npos ? second : second - first - 1);
    const auto numeric = [](std::string_view value) {
        return !value.empty() &&
               std::all_of(value.begin(), value.end(), [](char digit) { return digit >= '0' && digit <= '9'; });
    };
    if (!numeric(major) || !numeric(minor))
        return std::nullopt;
    return std::pair{ std::string(major), std::string(minor) };
}

struct Item {
    std::string itemName;
    std::string hintName;
    std::string locationName;
    std::string playerName;
    int playerNumber = 0;
    unsigned int flags = 0;
    uint64_t index = 0;
};

inline std::string ConnectionProfileKey(const std::string& seed, const std::string& slot) {
    constexpr char hex[] = "0123456789abcdef";
    const std::string identity = seed + '\0' + slot;
    std::string key = "Archipelago.Connections.";
    for (unsigned char byte : identity) {
        key += hex[byte >> 4];
        key += hex[byte & 15];
    }
    return key;
}

inline bool SameSlot(std::string_view seed, std::string_view slot, std::string_view savedSeed,
                     std::string_view savedSlot) {
    return seed == savedSeed && slot == savedSlot;
}

template <class Client, class NetworkItem>
Item DecodeReceivedItem(const Client& client, const NetworkItem& item, const std::string& game) {
    Item result;
    result.itemName = client.get_item_name(item.item, game);
    result.locationName = client.get_location_name(item.location, client.get_player_game(item.player));
    result.playerName = client.get_player_alias(item.player);
    result.playerNumber = item.player;
    result.flags = item.flags;
    result.index = item.index;
    return result;
}

// Network replays and out-of-order batches must not grant the same item twice.
// The save's index advances only after the game has actually granted the item.
class ReceivedItemQueue {
  public:
    bool Push(const Item& item, bool saveLoaded, uint64_t nextIndex) {
        if (!saveLoaded || item.index < nextIndex || (active && active->index == item.index)) {
            return false;
        }
        return pending.emplace(item.index, item).second;
    }

    std::optional<Item> TakeNext(uint64_t nextIndex) {
        pending.erase(pending.begin(), pending.lower_bound(nextIndex));
        if (active && active->index < nextIndex) {
            active.reset();
        }
        if (active) {
            return std::nullopt;
        }
        auto found = pending.find(nextIndex);
        if (found == pending.end()) {
            return std::nullopt;
        }
        active = std::move(found->second);
        pending.erase(found);
        return active;
    }

    void Clear() {
        pending.clear();
        active.reset();
    }

  private:
    std::map<uint64_t, Item> pending;
    std::optional<Item> active;
};
} // namespace Archipelago
