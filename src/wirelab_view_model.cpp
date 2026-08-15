#include "project/wirelab_view_model.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QVariantMap>

#include "project/packet_analyzer.hpp"
#include "project/traffic_generator.hpp"

namespace project
{
  namespace
  {
    bool scenario_from_name(const QString& name, TrafficScenario& scenario)
    {
      if (name == "known-unicast") scenario = TrafficScenario::KnownUnicast;
      else if (name == "broadcast") scenario = TrafficScenario::Broadcast;
      else if (name == "unknown-unicast") scenario = TrafficScenario::UnknownUnicast;
      else if (name == "mixed-traffic") scenario = TrafficScenario::Mixed;
      else return false;
      return true;
    }
  }

  WireLabViewModel::WireLabViewModel(QObject* parent) : QObject(parent) {}
  QString WireLabViewModel::topologyName() const { return topologyName_; }
  QVariantList WireLabViewModel::topologyNodes() const { return topologyNodes_; }
  QVariantList WireLabViewModel::topologyLinks() const { return topologyLinks_; }
  QString WireLabViewModel::statusMessage() const { return statusMessage_; }
  QString WireLabViewModel::trafficResult() const { return trafficResult_; }
  QString WireLabViewModel::faultSummary() const { return faultSummary_; }

  void WireLabViewModel::openTopology(const QString& path)
  {
    const auto configuration = topology_configuration_from_yaml_file(path.toStdString());
    if (!configuration)
    {
      statusMessage_ = QStringLiteral("Open topology failed: %1").arg(to_string(configuration.error()));
      emit statusMessageChanged();
      return;
    }
    const auto topology = Topology::create(configuration.value());
    if (!topology)
    {
      statusMessage_ = QStringLiteral("Topology validation failed: %1").arg(to_string(topology.error()));
      emit statusMessageChanged();
      return;
    }
    topologyController_.load(topology.value());
    topologyName_ = QString::fromStdString(topology.value().name());
    topologyNodes_.clear();
    topologyLinks_.clear();
    for (const auto& node : topology.value().nodes())
      topologyNodes_.append(QVariantMap{{ "id", QString::fromStdString(node.id) },
                                        { "type", QString::fromLatin1(to_string(node.type)) }});
    for (const auto& link : topology.value().links())
      topologyLinks_.append(QVariantMap{{ "from", QString::fromStdString(link.from) },
                                        { "to", QString::fromStdString(link.to) },
                                        { "latencyMs", static_cast<qlonglong>(link.latency.count()) }});
    statusMessage_ = QStringLiteral("Loaded %1 (revision %2)").arg(topologyName_).arg(topologyController_.topology_revision());
    emit topologyChanged();
    emit statusMessageChanged();
    updateFaultSummary();
  }

  void WireLabViewModel::runTrafficPreview(const QString& scenarioName, int packetCount, int batchSize, int frameSize,
                                            qulonglong seed)
  {
    TrafficScenario scenario;
    if (!scenario_from_name(scenarioName, scenario) || packetCount <= 0 || batchSize <= 0 || frameSize < 14)
    {
      trafficResult_ = QStringLiteral("Traffic preview requires a supported scenario, positive packet and batch counts, and a frame size of at least 14 bytes.");
      emit trafficResultChanged();
      return;
    }
    DeterministicTrafficGenerator generator({ scenario, static_cast<uint64_t>(seed), static_cast<size_t>(frameSize), 16 });
    CpuPacketAnalyzer analyzer;
    uint64_t bytes = 0;
    uint64_t broadcast = 0;
    for (int generated = 0; generated < packetCount;)
    {
      const size_t count = std::min(static_cast<size_t>(batchSize), static_cast<size_t>(packetCount - generated));
      const auto frames = generator.generate(count);
      std::vector<PacketView> packets;
      packets.reserve(frames.size());
      for (const auto& frame : frames) packets.push_back({ frame.data(), frame.size(), 0 });
      const auto result = analyzer.analyze(packets.data(), packets.size());
      bytes += result.received_bytes;
      broadcast += result.broadcast_packets;
      generated += static_cast<int>(count);
    }
    trafficResult_ = QStringLiteral("CPU preview: %1 packets, %2 bytes, %3 broadcast packets; seed %4.")
                         .arg(packetCount).arg(bytes).arg(broadcast).arg(seed);
    emit trafficResultChanged();
  }

  void WireLabViewModel::applyPortFault(const QString& portId, int latencyMs, double lossPercent, bool blackhole)
  {
    if (portId.isEmpty() || latencyMs < 0 || lossPercent < 0.0 || lossPercent > 100.0)
    {
      statusMessage_ = QStringLiteral("Fault requires a topology port, non-negative latency, and loss from 0 to 100 percent.");
      emit statusMessageChanged();
      return;
    }
    FaultConfiguration configuration;
    configuration.latency = std::chrono::milliseconds(latencyMs);
    configuration.loss_basis_points = static_cast<uint32_t>(std::lround(lossPercent * 100.0));
    configuration.blackhole = blackhole;
    const auto result = topologyController_.set_port_fault(portId.toStdString(), configuration);
    statusMessage_ = result ? QStringLiteral("Applied fault to %1.").arg(portId)
                            : QStringLiteral("Apply fault failed: %1").arg(to_string(result.error()));
    emit statusMessageChanged();
    updateFaultSummary();
  }

  void WireLabViewModel::clearPortFault(const QString& portId)
  {
    statusMessage_ = topologyController_.clear_port_fault(portId.toStdString())
                         ? QStringLiteral("Cleared fault on %1.").arg(portId)
                         : QStringLiteral("No active fault on %1.").arg(portId);
    emit statusMessageChanged();
    updateFaultSummary();
  }

  void WireLabViewModel::updateFaultSummary()
  {
    const auto faults = topologyController_.active_faults();
    faultSummary_ = faults ? QStringLiteral("%1 active fault(s)").arg(faults.value().size())
                           : QStringLiteral("No topology loaded");
    emit faultSummaryChanged();
  }
}
