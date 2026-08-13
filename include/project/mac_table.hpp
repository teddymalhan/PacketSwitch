/**
 * @file mac_table.hpp
 * @brief Thread-safe MAC address learning table
 *
 * Provides a learning table that maps MAC addresses to network endpoints.
 * This is the core data structure for a learning switch.
 */

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
  /**
   * @brief Thread-safe MAC address learning table
   *
   * Maintains a mapping from MAC addresses to endpoints (IP:port).
   * Uses a shared read/write lock for efficient concurrent access.
   *
   * This follows the learning switch pattern:
   * 1. When a frame arrives, learn the source MAC → endpoint mapping
   * 2. When forwarding, lookup destination MAC in the table
   * 3. If found, forward to that endpoint only (unicast)
   * 4. If not found or broadcast, forward to all endpoints (broadcast)
   *
   * Example:
   * @code
   * MacTable table;
   *
   * // Learn a MAC address
   * MacAddress mac({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
   * Endpoint ep("192.168.1.2", 8080);
   * table.insert(mac, ep);
   *
   * // Lookup
   * auto result = table.lookup(mac);
   * if (result) {
   *   std::cout << "Found endpoint: " << *result << std::endl;
   * }
   *
   * // Get all endpoints for broadcast
   * auto all_endpoints = table.get_all_endpoints();
   * @endcode
   */
  class MacTable
  {
   private:
    // Mapping from MAC address to endpoint
    std::unordered_map<MacAddress, Endpoint> table_;

    // Read/write mutex for thread-safe access
    // Uses shared_mutex for concurrent reads, exclusive writes
    mutable std::shared_mutex mutex_;

   public:
    /**
     * @brief Default constructor
     */
    MacTable() = default;

    /**
     * @brief Deleted copy constructor
     */
    MacTable(const MacTable&) = delete;

    /**
     * @brief Deleted copy assignment
     */
    MacTable& operator=(const MacTable&) = delete;

    /**
     * @brief Move constructor
     */
    MacTable(MacTable&& other) noexcept;

    /**
     * @brief Move assignment operator
     */
    MacTable& operator=(MacTable&& other) noexcept;

    /**
     * @brief Insert or update a MAC → endpoint mapping
     *
     * If the MAC address already exists, updates the endpoint.
     * If it doesn't exist, creates a new entry.
     *
     * @param mac The MAC address
     * @param endpoint The endpoint associated with this MAC
     * @return true if this is a new entry, false if it was updated
     */
    bool insert(const MacAddress& mac, const Endpoint& endpoint);

    /**
     * @brief Lookup endpoint for a MAC address
     *
     * @param mac The MAC address to look up
     * @return optional<Endpoint> The endpoint if found, empty if not found
     */
    [[nodiscard]] std::optional<Endpoint> lookup(const MacAddress& mac) const;

    /**
     * @brief Remove a MAC address from the table
     *
     * @param mac The MAC address to remove
     * @return true if an entry was removed, false if it didn't exist
     */
    bool remove(const MacAddress& mac);

    /**
     * @brief Check if a MAC address exists in the table
     *
     * @param mac The MAC address to check
     * @return true if the MAC address is in the table
     */
    [[nodiscard]] bool contains(const MacAddress& mac) const;

    /**
     * @brief Get all endpoints in the table
     *
     * Returns a vector of all known endpoints. This is useful for
     * broadcasting frames to all known endpoints.
     *
     * @return Vector of all endpoints
     */
    [[nodiscard]] std::vector<Endpoint> get_all_endpoints() const;

    /**
     * @brief Get all endpoints except one
     *
     * Useful for broadcasting to all endpoints except the source.
     *
     * @param exclude_mac MAC address to exclude from the result
     * @return Vector of endpoints (excluding the specified MAC)
     */
    [[nodiscard]] std::vector<Endpoint> get_all_endpoints_except(const MacAddress& exclude_mac) const;

    /**
     * @brief Get the number of entries in the table
     * @return Number of MAC → endpoint mappings
     */
    [[nodiscard]] size_t size() const noexcept
    {
      std::shared_lock lock(mutex_);
      return table_.size();
    }

    /**
     * @brief Check if the table is empty
     * @return true if the table has no entries
     */
    [[nodiscard]] bool empty() const noexcept
    {
      std::shared_lock lock(mutex_);
      return table_.empty();
    }

    /**
     * @brief Clear all entries from the table
     */
    void clear() noexcept
    {
      std::unique_lock lock(mutex_);
      table_.clear();
    }

    /**
     * @brief Get a copy of the entire table
     *
     * Returns a copy of the internal map. Useful for debugging
     * or iteration over all entries.
     *
     * @return Copy of the MAC → endpoint map
     */
    [[nodiscard]] std::unordered_map<MacAddress, Endpoint> get_all_entries() const
    {
      std::shared_lock lock(mutex_);
      return table_;
    }
  };

}  // namespace project

#endif  // PROJECT_MAC_TABLE_HPP_
