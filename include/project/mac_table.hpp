#ifndef PROJECT_MAC_TABLE_HPP_
#define PROJECT_MAC_TABLE_HPP_

#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

#include "project/ethernet_frame.hpp"
#include "project/udp_socket.hpp"

namespace project
{
  class MacTable
  {
   private:
    
    std::unordered_map<MacAddress, Endpoint> table_;
    mutable std::shared_mutex mutex_;
   public:
    MacTable() = default;
    MacTable(const MacTable&) = delete;
    MacTable& operator=(const MacTable&) = delete;
    MacTable(MacTable&& other) noexcept;
    MacTable& operator=(MacTable&& other) noexcept;
    bool insert(const MacAddress& mac, const Endpoint& endpoint);
    [[nodiscard]] std::optional<Endpoint> lookup(const MacAddress& mac) const;
    bool remove(const MacAddress& mac);
    [[nodiscard]] bool contains(const MacAddress& mac) const;
    [[nodiscard]] std::vector<Endpoint> get_all_endpoints() const;
    [[nodiscard]] std::vector<Endpoint> get_all_endpoints_except(const MacAddress& exclude_mac) const;
    [[nodiscard]] size_t size() const noexcept
    {
      std::shared_lock lock(mutex_);
      return table_.size();
    }
    [[nodiscard]] bool empty() const noexcept
    {
      std::shared_lock lock(mutex_);
      return table_.empty();
    }
    void clear() noexcept
    {
      std::unique_lock lock(mutex_);
      table_.clear();
    }
    [[nodiscard]] std::unordered_map<MacAddress, Endpoint> get_all_entries() const
    {
      std::shared_lock lock(mutex_);
      return table_;
    }
  };

}  

#endif  
