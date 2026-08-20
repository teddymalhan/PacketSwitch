#include "wirelab/session.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "session_detail.hpp"
#include "wirelab/benchmark.hpp"

#ifdef WIRELAB_HAS_CUDA
#include "wirelab/cuda_packet_parser.hpp"
#endif
#ifdef WIRELAB_HAS_METAL
#include "wirelab/metal_packet_parser.hpp"
#endif

namespace wirelab
{
  void Session::reset_simulation()
  {
    traffic_generator_.reset();
    traffic_analyzer_.reset();
    // The controller's faults are rebuilt by commit_topology, so leases are
    // dropped rather than restored onto a topology that may no longer have the
    // port they referenced.
    analysis_pipeline_.reset();
    simulation_start_ = std::chrono::steady_clock::now();
    tick_sequence_ = 0;
    total_packets_ = 0;
    total_bytes_ = 0;
    total_dropped_ = 0;
    port_counters_.clear();
    learned_mac_ports_.clear();
    metrics_history_.clear();
    mac_table_.clear();
    packet_rows_.clear();
    anomaly_rows_.clear();
    policy_actions_.clear();
    enforced_ports_.clear();
    for (const auto& node : topology_configuration_.nodes)
      if (node.type == TopologyNodeType::Host)
        port_counters_.try_emplace(node.id);
    port_states_.clear();
    traffic_result_ = "Ready. Configure traffic and press Start.";
    mark(SessionDirty::Telemetry);
  }

  void Session::start_traffic(
      const std::string& scenario,
      int32_t packets_per_tick,
      int32_t frame_size,
      uint64_t seed,
      const std::string& backend)
  {
    if (!has_topology())
    {
      set_status("Load or create a topology before starting traffic.");
      return;
    }
    const auto parsed_scenario = traffic_scenario_from_string(scenario);
    if (!parsed_scenario || packets_per_tick <= 0 || frame_size < 14)
    {
      set_status("Traffic settings are invalid.");
      return;
    }
    const auto& backends = available_backends();
    if (std::find(backends.begin(), backends.end(), backend) == backends.end())
    {
      set_status(backend + " is not available in this build or on this system.");
      return;
    }
    traffic_scenario_ = parsed_scenario.value();
    packets_per_tick_ = packets_per_tick;
    frame_size_ = frame_size;
    traffic_seed_ = seed;
    active_backend_ = backend;
    const auto host_count = static_cast<uint32_t>(std::max<size_t>(
        1,
        static_cast<size_t>(std::count_if(
            topology_configuration_.nodes.begin(),
            topology_configuration_.nodes.end(),
            [](const TopologyNode& node) { return node.type == TopologyNodeType::Host; }))));
    traffic_generator_ = std::make_unique<DeterministicTrafficGenerator>(
        TrafficGeneratorConfig{ traffic_scenario_, traffic_seed_, static_cast<size_t>(frame_size_), host_count });
    traffic_analyzer_ = std::make_unique<CpuPacketAnalyzer>();
#ifdef WIRELAB_HAS_CUDA
    if (active_backend_ == "CUDA")
      traffic_analyzer_ = std::make_unique<CudaPacketAnalyzer>();
#endif
#ifdef WIRELAB_HAS_METAL
    if (active_backend_ == "Metal")
      traffic_analyzer_ = std::make_unique<MetalPacketAnalyzer>();
    if (active_backend_ == "Metal (live)")
      traffic_analyzer_ = std::make_unique<MetalStreamingAnalyzer>();
#endif
    traffic_running_ = true;
    set_status("Traffic running on " + active_backend_);
    mark(SessionDirty::TrafficState);
  }

  void Session::stop_traffic()
  {
    if (!traffic_running_)
      return;
    traffic_running_ = false;
    set_status("Traffic stopped.");
    mark(SessionDirty::TrafficState);
  }

  void Session::run_traffic_step()
  {
    if (!traffic_running_ || !traffic_generator_)
      return;
    std::vector<std::string> hosts;
    std::string switch_id;
    for (const auto& node : topology_configuration_.nodes)
    {
      if (node.type == TopologyNodeType::Host)
        hosts.push_back(node.id);
      else
        switch_id = node.id;
    }
    if (hosts.empty() || switch_id.empty())
    {
      stop_traffic();
      set_status("Traffic requires at least one host and one switch.");
      return;
    }

    auto frames = traffic_generator_->generate(static_cast<size_t>(packets_per_tick_));
    std::vector<PacketView> delivered;
    delivered.reserve(frames.size());
    uint64_t tick_bytes = 0;
    uint64_t tick_dropped = 0;
    std::chrono::nanoseconds total_latency{ 0 };
    uint64_t latency_samples = 0;
    const auto arrival = std::chrono::steady_clock::now();
    for (size_t index = 0; index < frames.size(); ++index)
    {
      const auto host_index = (tick_sequence_ * static_cast<uint64_t>(packets_per_tick_) + index) % hosts.size();
      const auto& host = hosts[host_index];
      auto& counters = port_counters_[host];
      ++counters.received;
      const auto port_decision = topology_controller_.evaluate_port(host, frames[index].size(), arrival);
      if (!port_decision || port_decision.value().dropped)
      {
        ++counters.dropped;
        ++tick_dropped;
        continue;
      }
      const auto link = std::find_if(
          topology_configuration_.links.begin(),
          topology_configuration_.links.end(),
          [&host, &switch_id](const TopologyLink& candidate)
          {
            return (candidate.from == host && candidate.to == switch_id) ||
                   (candidate.from == switch_id && candidate.to == host);
          });
      if (link == topology_configuration_.links.end())
      {
        ++counters.dropped;
        ++tick_dropped;
        continue;
      }
      const auto port_arrival = port_decision.value().delivery_times[0];
      const auto link_decision =
          topology_controller_.evaluate_link(link->from, link->to, frames[index].size(), port_arrival);
      if (!link_decision || link_decision.value().dropped)
      {
        ++counters.dropped;
        ++tick_dropped;
        continue;
      }
      // A duplication fault yields more than one delivery; emitting the frame
      // once per delivery is what makes the duplication control observable.
      const auto deliveries = static_cast<size_t>(port_decision.value().delivery_count) *
                              static_cast<size_t>(link_decision.value().delivery_count);
      if (deliveries == 0)
      {
        ++counters.dropped;
        ++tick_dropped;
        continue;
      }
      for (size_t copy = 0; copy < deliveries; ++copy)
      {
        ++counters.forwarded;
        tick_bytes += frames[index].size();
        delivered.push_back({ frames[index].data(), frames[index].size(), static_cast<uint32_t>(host_index) });
      }
      total_latency += link_decision.value().delivery_times[0] - arrival;
      ++latency_samples;
    }

    AnalysisBatch analysis;
    if (!delivered.empty() && traffic_analyzer_)
      analysis = traffic_analyzer_->analyze(delivered.data(), delivered.size());
    total_packets_ += frames.size();
    total_bytes_ += tick_bytes;
    total_dropped_ += tick_dropped;
    ++tick_sequence_;
    const double throughput_mbps = static_cast<double>(tick_bytes) * 16.0 / 1'000'000.0;
    const double average_latency_ms =
        latency_samples == 0 ? 0.0 : static_cast<double>(total_latency.count()) / 1'000'000.0 / latency_samples;
    rebuild_telemetry_rows(analysis, tick_dropped, throughput_mbps, average_latency_ms);
  }

  void Session::rebuild_telemetry_rows(
      const AnalysisBatch& analysis,
      uint64_t tick_dropped,
      double throughput_mbps,
      double average_latency_ms)
  {
    const double loss_percent =
        packets_per_tick_ == 0 ? 0.0 : 100.0 * static_cast<double>(tick_dropped) / packets_per_tick_;
    metrics_history_.push_back(MetricSample{ tick_sequence_, throughput_mbps, average_latency_ms, loss_percent });
    // The chart plots a fixed window of recent ticks; older samples are dropped.
    while (metrics_history_.size() > 60)
      metrics_history_.erase(metrics_history_.begin());

    std::vector<std::string> hosts;
    for (const auto& node : topology_configuration_.nodes)
      if (node.type == TopologyNodeType::Host)
        hosts.push_back(node.id);
    packet_rows_.clear();
    const auto packet_limit = std::min<size_t>(analysis.packets.size(), 100);
    for (size_t index = 0; index < packet_limit; ++index)
    {
      const auto& packet = analysis.packets[index];
      std::string ingress = std::to_string(packet.ingress_port);

      if (packet.ingress_port < hosts.size())
        ingress = hosts[packet.ingress_port];
      const auto source_mac = packet.source_mac.to_string();
      learned_mac_ports_[source_mac] = ingress;
      PacketRow row;
      row.source_mac = source_mac;
      row.destination_mac = packet.destination_mac.to_string();
      row.source_ip = session_detail::ipv4_string(packet.source_ipv4);
      row.destination_ip = session_detail::ipv4_string(packet.destination_ipv4);
      row.ingress = std::move(ingress);
      row.protocol = packet.protocol;
      row.destination_port = packet.destination_port;
      row.bytes = packet.frame_length;
      row.classification = packet.classification;
      row.validity = packet.validity;
      packet_rows_.push_back(std::move(row));
    }

    std::vector<std::pair<std::string, std::string>> learned(learned_mac_ports_.begin(), learned_mac_ports_.end());
    std::sort(learned.begin(), learned.end());
    mac_table_.clear();
    for (const auto& [mac, port] : learned)
      mac_table_.push_back(MacTableRow{ mac, port });

    const auto now = std::chrono::steady_clock::now();
    // One monotonic source: the detector's window clock and the enforcer's lease
    // clock are the same steady_clock, measured from the start of the run.
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - simulation_start_).count();
    const auto outcome = analysis_pipeline_.evaluate(analysis, static_cast<uint64_t>(std::max<int64_t>(elapsed, 0)), now);
    const auto& anomalies = outcome.anomalies;
    anomaly_rows_.clear();
    for (const auto& anomaly : anomalies)
      anomaly_rows_.push_back(AnomalyRow{ anomaly.type,
                                          anomaly.source_mac.to_string(),
                                          session_detail::ipv4_string(anomaly.source_ipv4),
                                          anomaly.ingress_port,
                                          anomaly.observed_packets,
                                          anomaly.threshold });

    const auto& released = outcome.released;
    const auto& enforced = outcome.enforced;
    for (const auto& action : released)
      policy_actions_.push_back(PolicyActionRow{ tick_sequence_,
                                                 action.rule_name,
                                                 action.anomaly_type,
                                                 action.action,
                                                 action.port_id,
                                                 action.outcome,
                                                 "Lease expired, fault restored" });
    for (const auto& action : enforced)
      policy_actions_.push_back(PolicyActionRow{ tick_sequence_,
                                                 action.rule_name,
                                                 action.anomaly_type,
                                                 action.action,
                                                 action.port_id,
                                                 action.outcome,
                                                 session_detail::enforcement_summary(action) });
    while (policy_actions_.size() > 200)
      policy_actions_.erase(policy_actions_.begin());
    if (!released.empty() || !enforced.empty())
    {
      rebuild_fault_rows();
      rebuild_policy_rows();
    }

    port_states_.clear();
    for (const auto& node : topology_configuration_.nodes)
    {
      if (node.type != TopologyNodeType::Host)
        continue;
      const auto& counters = port_counters_[node.id];
      port_states_.push_back(PortStateRow{ node.id,
                                           analysis_pipeline_.enforcer().is_enforced(node.id),
                                           counters.received,
                                           counters.forwarded,
                                           counters.dropped });
    }

    traffic_result_ = std::to_string(total_packets_) + " packets generated · " + std::to_string(total_bytes_) +
                      " delivered bytes · " + std::to_string(total_dropped_) + " dropped · " +
                      session_detail::format_fixed(throughput_mbps, 3) + " Mbps · " +
                      session_detail::format_fixed(average_latency_ms, 2) + " ms average";
    mark(SessionDirty::Telemetry);
  }
}  // namespace wirelab
