#include "wirelab/switch_metrics.hpp"
#include <utility>


namespace wirelab
{
  SwitchMetrics::SwitchMetrics(SwitchMetrics&& other) noexcept
  {
    *this = std::move(other);
  }

  SwitchMetrics& SwitchMetrics::operator=(SwitchMetrics&& other) noexcept
  {
    const auto values = other.snapshot();
    received_packets_.store(values.received_packets, std::memory_order_relaxed);
    received_bytes_.store(values.received_bytes, std::memory_order_relaxed);
    forwarded_packets_.store(values.forwarded_packets, std::memory_order_relaxed);
    forwarded_bytes_.store(values.forwarded_bytes, std::memory_order_relaxed);
    broadcast_packets_.store(values.broadcast_packets, std::memory_order_relaxed);
    unknown_unicast_packets_.store(values.unknown_unicast_packets, std::memory_order_relaxed);
    dropped_packets_.store(values.dropped_packets, std::memory_order_relaxed);
    malformed_packets_.store(values.malformed_packets, std::memory_order_relaxed);
    learned_macs_.store(values.learned_macs, std::memory_order_relaxed);
    return *this;
  }

  void SwitchMetrics::record_received(uint64_t bytes) noexcept
  {
    received_packets_.fetch_add(1, std::memory_order_relaxed);
    received_bytes_.fetch_add(bytes, std::memory_order_relaxed);
  }

  void SwitchMetrics::record_forwarded(uint64_t bytes) noexcept
  {
    forwarded_packets_.fetch_add(1, std::memory_order_relaxed);
    forwarded_bytes_.fetch_add(bytes, std::memory_order_relaxed);
  }

  void SwitchMetrics::record_broadcast() noexcept
  {
    broadcast_packets_.fetch_add(1, std::memory_order_relaxed);
  }

  void SwitchMetrics::record_unknown_unicast() noexcept
  {
    unknown_unicast_packets_.fetch_add(1, std::memory_order_relaxed);
  }

  void SwitchMetrics::record_drop() noexcept
  {
    dropped_packets_.fetch_add(1, std::memory_order_relaxed);
  }

  void SwitchMetrics::record_malformed() noexcept
  {
    malformed_packets_.fetch_add(1, std::memory_order_relaxed);
  }

  void SwitchMetrics::record_mac_learned() noexcept
  {
    learned_macs_.fetch_add(1, std::memory_order_relaxed);
  }

  SwitchMetricsSnapshot SwitchMetrics::snapshot() const noexcept
  {
    return { received_packets_.load(std::memory_order_relaxed),
             received_bytes_.load(std::memory_order_relaxed),
             forwarded_packets_.load(std::memory_order_relaxed),
             forwarded_bytes_.load(std::memory_order_relaxed),
             broadcast_packets_.load(std::memory_order_relaxed),
             unknown_unicast_packets_.load(std::memory_order_relaxed),
             dropped_packets_.load(std::memory_order_relaxed),
             malformed_packets_.load(std::memory_order_relaxed),
             learned_macs_.load(std::memory_order_relaxed) };
  }
}
