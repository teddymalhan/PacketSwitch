/**
 * @file mac_table.cpp
 * @brief Implementation of MAC address learning table
 */

#include "project/mac_table.hpp"

#include <algorithm>

namespace project
{
  MacTable::MacTable(MacTable&& other) noexcept : table_(std::move(other.table_))
  {
    // Move mutex is not possible, so we just move the table
    // The moved-from object will have an empty table
    other.table_.clear();
  }

  MacTable& MacTable::operator=(MacTable&& other) noexcept
  {
    if (this != &other)
    {
      std::unique_lock lock(mutex_);
      table_ = std::move(other.table_);
      other.table_.clear();
    }
    return *this;
  }

  bool MacTable::insert(const MacAddress& mac, const Endpoint& endpoint)
  {
    std::unique_lock lock(mutex_);

    bool is_new = (table_.find(mac) == table_.end());
    table_[mac] = endpoint;
    return is_new;
  }

  std::optional<Endpoint> MacTable::lookup(const MacAddress& mac) const
  {
    std::shared_lock lock(mutex_);

    auto it = table_.find(mac);
    if (it != table_.end())
    {
      return it->second;
    }

    return std::nullopt;
  }

  bool MacTable::remove(const MacAddress& mac)
  {
    std::unique_lock lock(mutex_);

    auto it = table_.find(mac);
    if (it != table_.end())
    {
      table_.erase(it);
      return true;
    }

    return false;
  }

  bool MacTable::contains(const MacAddress& mac) const
  {
    std::shared_lock lock(mutex_);
    return table_.find(mac) != table_.end();
  }

  std::vector<Endpoint> MacTable::get_all_endpoints() const
  {
    std::shared_lock lock(mutex_);

    std::vector<Endpoint> endpoints;
    endpoints.reserve(table_.size());

    for (const auto& [mac, endpoint] : table_)
    {
      endpoints.push_back(endpoint);
    }

    return endpoints;
  }

  std::vector<Endpoint> MacTable::get_all_endpoints_except(const MacAddress& exclude_mac) const
  {
    std::shared_lock lock(mutex_);

    std::vector<Endpoint> endpoints;

    for (const auto& [mac, endpoint] : table_)
    {
      if (mac != exclude_mac)
      {
        endpoints.push_back(endpoint);
      }
    }

    return endpoints;
  }

}  // namespace project
