#ifndef PROJECT_SWITCH_METRICS_HPP_
#define PROJECT_SWITCH_METRICS_HPP_

#include <atomic>
#include <cstdint>

namespace wirelab
{
  struct SwitchMetricsSnapshot
  {
    uint64_t received_packets = 0;
    uint64_t received_bytes = 0;
    uint64_t forwarded_packets = 0;
    uint64_t forwarded_bytes = 0;
    uint64_t broadcast_packets = 0;
    uint64_t unknown_unicast_packets = 0;
    uint64_t dropped_packets = 0;
    uint64_t malformed_packets = 0;
    uint64_t learned_macs = 0;
  };

  class SwitchMetrics
  {
   public:
    SwitchMetrics() = default;
    SwitchMetrics(const SwitchMetrics&) = delete;
    SwitchMetrics& operator=(const SwitchMetrics&) = delete;
    SwitchMetrics(SwitchMetrics&& other) noexcept;
    SwitchMetrics& operator=(SwitchMetrics&& other) noexcept;

    void record_received(uint64_t bytes) noexcept;
    void record_forwarded(uint64_t bytes) noexcept;
    void record_broadcast() noexcept;
    void record_unknown_unicast() noexcept;
    void record_drop() noexcept;
    void record_malformed() noexcept;
    void record_mac_learned() noexcept;
    [[nodiscard]] SwitchMetricsSnapshot snapshot() const noexcept;

   private:
    std::atomic<uint64_t> received_packets_{ 0 };
    std::atomic<uint64_t> received_bytes_{ 0 };
    std::atomic<uint64_t> forwarded_packets_{ 0 };
    std::atomic<uint64_t> forwarded_bytes_{ 0 };
    std::atomic<uint64_t> broadcast_packets_{ 0 };
    std::atomic<uint64_t> unknown_unicast_packets_{ 0 };
    std::atomic<uint64_t> dropped_packets_{ 0 };
    std::atomic<uint64_t> malformed_packets_{ 0 };
    std::atomic<uint64_t> learned_macs_{ 0 };
  };
}

#endif
