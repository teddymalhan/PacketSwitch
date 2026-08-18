#include "wirelab/wirelab_view_model.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include "wirelab/accelerated_backends.hpp"
#include "wirelab/version.hpp"

#ifdef WIRELAB_HAS_CUDA
#include "wirelab/cuda_packet_parser.hpp"
#endif
#ifdef WIRELAB_HAS_METAL
#include "wirelab/metal_packet_parser.hpp"
#endif

namespace wirelab
{
  namespace
  {
    bool scenario_from_name(const QString& name, TrafficScenario& scenario)
    {
      if (name == "known-unicast")
        scenario = TrafficScenario::KnownUnicast;
      else if (name == "broadcast")
        scenario = TrafficScenario::Broadcast;
      else if (name == "unknown-unicast")
        scenario = TrafficScenario::UnknownUnicast;
      else if (name == "mixed-traffic")
        scenario = TrafficScenario::Mixed;
      else if (name == "udp-flood")
        scenario = TrafficScenario::UdpFlood;
      else if (name == "port-scan")
        scenario = TrafficScenario::PortScan;
      else if (name == "broadcast-storm")
        scenario = TrafficScenario::BroadcastStorm;
      else
        return false;
      return true;
    }

    AnomalyDetectorConfig gui_anomaly_config()
    {
      AnomalyDetectorConfig config;
      config.window_duration_ns = 1'000'000'000;
      config.broadcast_packets_threshold = 100;
      config.unknown_unicast_packets_threshold = 100;
      config.udp_packets_threshold = 200;
      config.port_scan_destinations_threshold = 20;
      config.hot_talker_packets_threshold = 200;
      config.malformed_frames_threshold = 1;
      return config;
    }

    QString classification_name(PacketClassification classification)
    {
      switch (classification)
      {
        case PacketClassification::Broadcast: return QStringLiteral("Broadcast");
        case PacketClassification::UnknownUnicast: return QStringLiteral("Unknown unicast");
        case PacketClassification::KnownUnicast: return QStringLiteral("Known unicast");
        case PacketClassification::Malformed: return QStringLiteral("Malformed");
      }
      return QStringLiteral("Unknown");
    }

    QString validity_name(PacketValidity validity)
    {
      switch (validity)
      {
        case PacketValidity::Valid: return QStringLiteral("Valid");
        case PacketValidity::MalformedEthernet: return QStringLiteral("Malformed Ethernet");
        case PacketValidity::MalformedIpv4: return QStringLiteral("Malformed IPv4");
        case PacketValidity::MalformedTransport: return QStringLiteral("Malformed transport");
      }
      return QStringLiteral("Unknown");
    }

    QString anomaly_name(AnomalyType type)
    {
      switch (type)
      {
        case AnomalyType::BroadcastStorm: return QStringLiteral("Broadcast storm");
        case AnomalyType::MacFlap: return QStringLiteral("MAC flap");
        case AnomalyType::UnknownUnicastFlood: return QStringLiteral("Unknown-unicast flood");
        case AnomalyType::UdpFlood: return QStringLiteral("UDP flood");
        case AnomalyType::PortScan: return QStringLiteral("Port scan");
        case AnomalyType::HotTalker: return QStringLiteral("Hot talker");
        case AnomalyType::MalformedFrame: return QStringLiteral("Malformed frame");
      }
      return QStringLiteral("Unknown anomaly");
    }

    bool anomaly_from_name(const QString& name, AnomalyType& type)
    {
      if (name == "Broadcast storm")
        type = AnomalyType::BroadcastStorm;
      else if (name == "MAC flap")
        type = AnomalyType::MacFlap;
      else if (name == "Unknown-unicast flood")
        type = AnomalyType::UnknownUnicastFlood;
      else if (name == "UDP flood")
        type = AnomalyType::UdpFlood;
      else if (name == "Port scan")
        type = AnomalyType::PortScan;
      else if (name == "Hot talker")
        type = AnomalyType::HotTalker;
      else if (name == "Malformed frame")
        type = AnomalyType::MalformedFrame;
      else
        return false;
      return true;
    }

    QString policy_action_name(PolicyAction action)
    {
      switch (action)
      {
        case PolicyAction::Allow: return QStringLiteral("Allow");
        case PolicyAction::Drop: return QStringLiteral("Drop");
        case PolicyAction::Mirror: return QStringLiteral("Mirror");
        case PolicyAction::RateLimit: return QStringLiteral("Rate limit");
        case PolicyAction::Quarantine: return QStringLiteral("Quarantine");
        case PolicyAction::AlertOnly: return QStringLiteral("Alert only");
      }
      return QStringLiteral("Unknown");
    }

    bool policy_action_from_name(const QString& name, PolicyAction& action)
    {
      if (name == "Allow")
        action = PolicyAction::Allow;
      else if (name == "Drop")
        action = PolicyAction::Drop;
      else if (name == "Mirror")
        action = PolicyAction::Mirror;
      else if (name == "Rate limit")
        action = PolicyAction::RateLimit;
      else if (name == "Quarantine")
        action = PolicyAction::Quarantine;
      else if (name == "Alert only")
        action = PolicyAction::AlertOnly;
      else
        return false;
      return true;
    }

    QString enforcement_summary(const EnforcementAction& action)
    {
      switch (action.kind)
      {
        case EnforcementKind::Blackhole: return QStringLiteral("Dropping all frames");
        case EnforcementKind::Isolate: return QStringLiteral("Port quarantined");
        case EnforcementKind::RateLimit:
          return QStringLiteral("Capped at %1 kbit/s").arg(action.rate_limit_bits_per_second / 1000);
        case EnforcementKind::None: return QStringLiteral("Recorded only");
      }
      return QStringLiteral("Unknown");
    }

    QString ipv4_string(uint32_t address)
    {
      return QStringLiteral("%1.%2.%3.%4")
          .arg((address >> 24U) & 0xffU)
          .arg((address >> 16U) & 0xffU)
          .arg((address >> 8U) & 0xffU)
          .arg(address & 0xffU);
    }

    std::string yaml_quote(const std::string& text)
    {
      std::string result = "\"";
      result.reserve(text.size() + 2);
      for (const char character : text)
      {
        if (character == '\\' || character == '"')
          result.push_back('\\');
        result.push_back(character);
      }
      result.push_back('"');
      return result;
    }

    // The GUI labels a backend the way a person reads it and the benchmark
    // engine names it the way the CLI spells it; these two are the only place
    // the spellings meet.
    std::string benchmark_backend_id(const QString& label)
    {
      return label.toLower().toStdString();
    }

    QString benchmark_backend_label(const std::string& id)
    {
      if (id == "cuda")
        return QStringLiteral("CUDA");
      if (id == "metal")
        return QStringLiteral("Metal");
      return QStringLiteral("CPU");
    }
  }  // namespace

  WireLabViewModel::WireLabViewModel(QObject* parent)
      : QObject(parent),
        analysisPipeline_(gui_anomaly_config(), topologyController_),
        simulationStart_(std::chrono::steady_clock::now())
  {
    rebuildPolicyModel();
  }

  bool WireLabViewModel::hasTopology() const noexcept
  {
    return topologyController_.has_topology();
  }
  QString WireLabViewModel::topologyName() const
  {
    return QString::fromStdString(topologyConfiguration_.name);
  }
  QVariantList WireLabViewModel::topologyNodes() const
  {
    return topologyNodes_;
  }
  QVariantList WireLabViewModel::topologyLinks() const
  {
    return topologyLinks_;
  }
  QString WireLabViewModel::selectedType() const
  {
    return selectedType_;
  }
  QString WireLabViewModel::selectedId() const
  {
    return selectedId_;
  }
  QString WireLabViewModel::selectedSummary() const
  {
    return selectedSummary_;
  }
  QString WireLabViewModel::statusMessage() const
  {
    return statusMessage_;
  }
  bool WireLabViewModel::trafficRunning() const noexcept
  {
    return trafficRunning_;
  }
  QString WireLabViewModel::activeBackend() const
  {
    return activeBackend_;
  }
  QString WireLabViewModel::trafficResult() const
  {
    return trafficResult_;
  }
  QVariantList WireLabViewModel::metricsHistory() const
  {
    return metricsHistory_;
  }
  QVariantList WireLabViewModel::macTable() const
  {
    return macTable_;
  }
  QVariantList WireLabViewModel::portStates() const
  {
    return portStates_;
  }
  QVariantList WireLabViewModel::packetRows() const
  {
    return packetRows_;
  }
  QVariantList WireLabViewModel::anomalyRows() const
  {
    return anomalyRows_;
  }
  QVariantList WireLabViewModel::activeFaults() const
  {
    return activeFaults_;
  }
  QVariantList WireLabViewModel::policyRules() const
  {
    return policyRules_;
  }
  QVariantList WireLabViewModel::policyActions() const
  {
    return policyActions_;
  }
  QVariantList WireLabViewModel::enforcedPorts() const
  {
    return enforcedPorts_;
  }

  QStringList WireLabViewModel::anomalyTypeNames() const
  {
    return { anomaly_name(AnomalyType::BroadcastStorm),
             anomaly_name(AnomalyType::MacFlap),
             anomaly_name(AnomalyType::UnknownUnicastFlood),
             anomaly_name(AnomalyType::UdpFlood),
             anomaly_name(AnomalyType::PortScan),
             anomaly_name(AnomalyType::HotTalker),
             anomaly_name(AnomalyType::MalformedFrame) };
  }

  QStringList WireLabViewModel::policyActionNames() const
  {
    return { policy_action_name(PolicyAction::AlertOnly),  policy_action_name(PolicyAction::Mirror),
             policy_action_name(PolicyAction::RateLimit),  policy_action_name(PolicyAction::Drop),
             policy_action_name(PolicyAction::Quarantine), policy_action_name(PolicyAction::Allow) };
  }

  void WireLabViewModel::addPolicy(
      const QString& name,
      const QString& anomalyType,
      const QString& action,
      qulonglong rateLimitPacketsPerSecond)
  {
    PolicyRule rule;
    rule.name = name.trimmed().toStdString();
    rule.rate_limit_packets_per_second = rateLimitPacketsPerSecond;
    if (!anomaly_from_name(anomalyType, rule.anomaly_type) || !policy_action_from_name(action, rule.action))
    {
      setStatus(QStringLiteral("Unknown anomaly type or policy action."));
      return;
    }
    const auto result = analysisPipeline_.policies().add_rule(std::move(rule));
    if (!result)
    {
      setStatus(QStringLiteral("Policy rejected: %1").arg(to_string(result.error())));
      return;
    }
    setStatus(QStringLiteral("Policy %1 added.").arg(name));
    rebuildPolicyModel();
  }

  void WireLabViewModel::removePolicy(const QString& name)
  {
    if (!analysisPipeline_.policies().remove_rule(name.toStdString()))
    {
      setStatus(QStringLiteral("No policy named %1.").arg(name));
      return;
    }
    setStatus(QStringLiteral("Policy %1 removed.").arg(name));
    rebuildPolicyModel();
  }

  void WireLabViewModel::setPolicyEnabled(const QString& name, bool enabled)
  {
    if (!analysisPipeline_.policies().set_enabled(name.toStdString(), enabled))
    {
      setStatus(QStringLiteral("No policy named %1.").arg(name));
      return;
    }
    setStatus(QStringLiteral("Policy %1 %2.").arg(name, enabled ? QStringLiteral("enabled") : QStringLiteral("disabled")));
    rebuildPolicyModel();
  }

  void WireLabViewModel::releaseEnforcement(const QString& portId)
  {
    if (!analysisPipeline_.release(portId.toStdString()))
    {
      setStatus(QStringLiteral("Port %1 is not under enforcement.").arg(portId));
      return;
    }
    setStatus(QStringLiteral("Released enforcement on %1.").arg(portId));
    rebuildFaultModel();
    rebuildPolicyModel();
    emit telemetryChanged();
  }

  void WireLabViewModel::rebuildPolicyModel()
  {
    policyRules_.clear();
    for (const auto& rule : analysisPipeline_.policies().rules())
    {
      policyRules_.append(
          QVariantMap{ { "name", QString::fromStdString(rule.name) },
                       { "anomaly", anomaly_name(rule.anomaly_type) },
                       { "action", policy_action_name(rule.action) },
                       { "enabled", rule.enabled },
                       { "rateLimit", static_cast<qulonglong>(rule.rate_limit_packets_per_second) },
                       { "hits", static_cast<qulonglong>(analysisPipeline_.policies().hit_count(rule.name)) } });
    }
    enforcedPorts_.clear();
    for (const auto& action : analysisPipeline_.enforcer().active())
    {
      enforcedPorts_.append(
          QVariantMap{ { "port", QString::fromStdString(action.port_id) },
                       { "rule", QString::fromStdString(action.rule_name) },
                       { "kind", QString::fromUtf8(to_string(action.kind)) },
                       { "summary", enforcement_summary(action) } });
    }
    emit policiesChanged();
  }

  QStringList WireLabViewModel::availableBackends() const
  {
    QStringList backends{ QStringLiteral("CPU") };
#ifdef WIRELAB_HAS_CUDA
    if (CudaPacketParser::is_available())
    {
      backends.push_back(QStringLiteral("CUDA"));
    }
#endif
#ifdef WIRELAB_HAS_METAL
    if (MetalPacketParser::is_available())
    {
      backends.push_back(QStringLiteral("Metal"));
    }
#endif
    return backends;
  }

  void WireLabViewModel::setStatus(QString message)
  {
    statusMessage_ = std::move(message);
    emit statusMessageChanged();
  }

  bool WireLabViewModel::commitTopology(TopologyConfiguration configuration, const QString& successMessage)
  {
    auto topology = Topology::create(configuration);
    if (!topology)
    {
      setStatus(QStringLiteral("Topology validation failed: %1").arg(to_string(topology.error())));
      return false;
    }
    stopTraffic();
    topologyConfiguration_ = std::move(configuration);
    topologyController_.load(std::move(topology.value()));
    clearSelection();
    resetSimulation();
    rebuildTopologyModels();
    rebuildFaultModel();
    setStatus(successMessage);
    emit topologyChanged();
    return true;
  }

  void WireLabViewModel::openTopology(const QString& path)
  {
    QString localPath = path;
    if (localPath.startsWith(QStringLiteral("file://")))
    {
      localPath = QUrl(localPath).toLocalFile();
    }
    const auto configuration = topology_configuration_from_yaml_file(localPath.toStdString());
    if (!configuration)
    {
      setStatus(QStringLiteral("Open topology failed: %1").arg(to_string(configuration.error())));
      return;
    }
    topologyPath_ = localPath;
    const auto name = QString::fromStdString(configuration.value().name);
    (void)commitTopology(configuration.value(), QStringLiteral("Loaded %1").arg(name));
  }

  void WireLabViewModel::saveTopology(const QString& path)
  {
    if (!hasTopology())
    {
      setStatus(QStringLiteral("There is no topology to save."));
      return;
    }
    QString localPath = path;
    if (localPath.startsWith(QStringLiteral("file://")))
    {
      localPath = QUrl(localPath).toLocalFile();
    }
    std::ofstream output(localPath.toStdString(), std::ios::trunc);
    if (!output)
    {
      setStatus(QStringLiteral("Could not open %1 for writing.").arg(path));
      return;
    }
    output << "network:\n  name: " << yaml_quote(topologyConfiguration_.name) << "\n\nnodes:\n";
    for (const auto& node : topologyConfiguration_.nodes)
      output << "  - { id: " << yaml_quote(node.id)
             << ", type: " << (node.type == TopologyNodeType::Switch ? "switch" : "host") << " }\n";
    output << "\nlinks:\n";
    for (const auto& link : topologyConfiguration_.links)
      output << "  - { from: " << yaml_quote(link.from) << ", to: " << yaml_quote(link.to)
             << ", latency_ms: " << link.latency.count() << " }\n";
    if (!output)
    {
      setStatus(QStringLiteral("Writing %1 failed.").arg(localPath));
      return;
    }
    topologyPath_ = localPath;
    setStatus(QStringLiteral("Saved topology to %1").arg(QFileInfo(localPath).fileName()));
  }

  void WireLabViewModel::rebuildTopologyModels()
  {
    topologyNodes_.clear();
    topologyLinks_.clear();
    size_t hostIndex = 0;
    const auto hostCount = static_cast<size_t>(std::count_if(
        topologyConfiguration_.nodes.begin(),
        topologyConfiguration_.nodes.end(),
        [](const TopologyNode& node) { return node.type == TopologyNodeType::Host; }));
    constexpr double pi = 3.14159265358979323846;
    for (const auto& node : topologyConfiguration_.nodes)
    {
      double x = 0.5;
      double y = 0.5;
      if (node.type == TopologyNodeType::Host && hostCount != 0)
      {
        const double angle = (2.0 * pi * static_cast<double>(hostIndex) / static_cast<double>(hostCount)) - pi / 2.0;
        x = 0.5 + 0.38 * std::cos(angle);
        y = 0.5 + 0.38 * std::sin(angle);
        ++hostIndex;
      }
      topologyNodes_.append(
          QVariantMap{ { "id", QString::fromStdString(node.id) },
                       { "type", QString::fromLatin1(to_string(node.type)) },
                       { "x", x },
                       { "y", y } });
    }
    for (const auto& link : topologyConfiguration_.links)
      topologyLinks_.append(
          QVariantMap{ { "from", QString::fromStdString(link.from) },
                       { "to", QString::fromStdString(link.to) },
                       { "latencyMs", static_cast<qlonglong>(link.latency.count()) } });
  }

  void WireLabViewModel::selectNode(const QString& id)
  {
    const auto found = std::find_if(
        topologyConfiguration_.nodes.begin(),
        topologyConfiguration_.nodes.end(),
        [&id](const TopologyNode& node) { return node.id == id.toStdString(); });
    if (found == topologyConfiguration_.nodes.end())
      return;
    selectedType_ = QStringLiteral("node");
    selectedId_ = id;
    selectedFirst_ = id;
    selectedSecond_.clear();
    selectedSummary_ =
        QStringLiteral("%1 node · %2 connected link(s)")
            .arg(QString::fromLatin1(to_string(found->type)))
            .arg(
                std::count_if(
                    topologyConfiguration_.links.begin(),
                    topologyConfiguration_.links.end(),
                    [&found](const TopologyLink& link) { return link.from == found->id || link.to == found->id; }));
    emit selectionChanged();
  }

  void WireLabViewModel::selectLink(const QString& from, const QString& to)
  {
    const auto found = std::find_if(
        topologyConfiguration_.links.begin(),
        topologyConfiguration_.links.end(),
        [&from, &to](const TopologyLink& link)
        {
          return (link.from == from.toStdString() && link.to == to.toStdString()) ||
                 (link.from == to.toStdString() && link.to == from.toStdString());
        });
    if (found == topologyConfiguration_.links.end())
      return;
    selectedType_ = QStringLiteral("link");
    selectedId_ = QStringLiteral("%1 ↔ %2").arg(from, to);
    selectedFirst_ = QString::fromStdString(found->from);
    selectedSecond_ = QString::fromStdString(found->to);
    selectedSummary_ = QStringLiteral("%1 ms base latency").arg(found->latency.count());
    emit selectionChanged();
  }

  void WireLabViewModel::clearSelection()
  {
    selectedType_.clear();
    selectedId_.clear();
    selectedFirst_.clear();
    selectedSecond_.clear();
    selectedSummary_.clear();
    emit selectionChanged();
  }

  void WireLabViewModel::addNode(const QString& id, const QString& type)
  {
    TopologyConfiguration edited = topologyConfiguration_;
    if (edited.name.empty())
      edited.name = "untitled-lab";
    edited.nodes.push_back(
        { id.trimmed().toStdString(),
          type.compare(QStringLiteral("switch"), Qt::CaseInsensitive) == 0 ? TopologyNodeType::Switch
                                                                           : TopologyNodeType::Host });
    (void)commitTopology(std::move(edited), QStringLiteral("Added node %1").arg(id.trimmed()));
  }

  void WireLabViewModel::addLink(const QString& from, const QString& to, int latencyMs)
  {
    TopologyConfiguration edited = topologyConfiguration_;
    edited.links.push_back(
        { from.trimmed().toStdString(), to.trimmed().toStdString(), std::chrono::milliseconds(latencyMs) });
    (void)commitTopology(std::move(edited), QStringLiteral("Added link %1 ↔ %2").arg(from.trimmed(), to.trimmed()));
  }

  void WireLabViewModel::removeSelected()
  {
    if (selectedType_.isEmpty())
      return;
    TopologyConfiguration edited = topologyConfiguration_;
    if (selectedType_ == QStringLiteral("node"))
    {
      const auto id = selectedFirst_.toStdString();
      edited.nodes.erase(
          std::remove_if(
              edited.nodes.begin(), edited.nodes.end(), [&id](const TopologyNode& node) { return node.id == id; }),
          edited.nodes.end());
      edited.links.erase(
          std::remove_if(
              edited.links.begin(),
              edited.links.end(),
              [&id](const TopologyLink& link) { return link.from == id || link.to == id; }),
          edited.links.end());
    }
    else
    {
      const auto first = selectedFirst_.toStdString();
      const auto second = selectedSecond_.toStdString();
      edited.links.erase(
          std::remove_if(
              edited.links.begin(),
              edited.links.end(),
              [&first, &second](const TopologyLink& link)
              { return (link.from == first && link.to == second) || (link.from == second && link.to == first); }),
          edited.links.end());
    }
    (void)commitTopology(std::move(edited), QStringLiteral("Removed %1").arg(selectedId_));
  }

  void WireLabViewModel::resetSimulation()
  {
    trafficGenerator_.reset();
    trafficAnalyzer_.reset();
    // The controller's faults are rebuilt by commitTopology, so leases are
    // dropped rather than restored onto a topology that may no longer have the
    // port they referenced.
    analysisPipeline_.reset();
    simulationStart_ = std::chrono::steady_clock::now();
    tickSequence_ = 0;
    totalPackets_ = 0;
    totalBytes_ = 0;
    totalDropped_ = 0;
    portCounters_.clear();
    learnedMacPorts_.clear();
    metricsHistory_.clear();
    macTable_.clear();
    packetRows_.clear();
    anomalyRows_.clear();
    policyActions_.clear();
    enforcedPorts_.clear();
    for (const auto& node : topologyConfiguration_.nodes)
      if (node.type == TopologyNodeType::Host)
        portCounters_.try_emplace(node.id);
    portStates_.clear();
    trafficResult_ = QStringLiteral("Ready. Configure traffic and press Start.");
    emit telemetryChanged();
  }

  void WireLabViewModel::startTraffic(
      const QString& scenario,
      int packetsPerTick,
      int frameSize,
      qulonglong seed,
      const QString& backend)
  {
    if (!hasTopology())
    {
      setStatus(QStringLiteral("Load or create a topology before starting traffic."));
      return;
    }
    TrafficScenario parsedScenario;
    if (!scenario_from_name(scenario, parsedScenario) || packetsPerTick <= 0 || frameSize < 14)
    {
      setStatus(QStringLiteral("Traffic settings are invalid."));
      return;
    }
    if (!availableBackends().contains(backend))
    {
      setStatus(QStringLiteral("%1 is not available in this build or on this system.").arg(backend));
      return;
    }
    trafficScenario_ = parsedScenario;
    packetsPerTick_ = packetsPerTick;
    frameSize_ = frameSize;
    trafficSeed_ = seed;
    activeBackend_ = backend;
    const auto hostCount = static_cast<uint32_t>(std::max<size_t>(
        1,
        std::count_if(
            topologyConfiguration_.nodes.begin(),
            topologyConfiguration_.nodes.end(),
            [](const TopologyNode& node) { return node.type == TopologyNodeType::Host; })));
    trafficGenerator_ = std::make_unique<DeterministicTrafficGenerator>(
        TrafficGeneratorConfig{ trafficScenario_, trafficSeed_, static_cast<size_t>(frameSize_), hostCount });
    trafficAnalyzer_ = std::make_unique<CpuPacketAnalyzer>();
#ifdef WIRELAB_HAS_CUDA
    if (activeBackend_ == QStringLiteral("CUDA"))
      trafficAnalyzer_ = std::make_unique<CudaPacketAnalyzer>();
#endif
#ifdef WIRELAB_HAS_METAL
    if (activeBackend_ == QStringLiteral("Metal"))
      trafficAnalyzer_ = std::make_unique<MetalPacketAnalyzer>();
#endif
    trafficRunning_ = true;
    setStatus(QStringLiteral("Traffic running on %1").arg(activeBackend_));
    emit trafficStateChanged();
  }

  void WireLabViewModel::stopTraffic()
  {
    if (!trafficRunning_)
      return;
    trafficRunning_ = false;
    setStatus(QStringLiteral("Traffic stopped."));
    emit trafficStateChanged();
  }

  void WireLabViewModel::runTrafficStep()
  {
    if (!trafficRunning_ || !trafficGenerator_)
      return;
    std::vector<std::string> hosts;
    std::string switchId;
    for (const auto& node : topologyConfiguration_.nodes)
    {
      if (node.type == TopologyNodeType::Host)
        hosts.push_back(node.id);
      else
        switchId = node.id;
    }
    if (hosts.empty() || switchId.empty())
    {
      stopTraffic();
      setStatus(QStringLiteral("Traffic requires at least one host and one switch."));
      return;
    }

    auto frames = trafficGenerator_->generate(static_cast<size_t>(packetsPerTick_));
    std::vector<PacketView> delivered;
    delivered.reserve(frames.size());
    uint64_t tickBytes = 0;
    uint64_t tickDropped = 0;
    std::chrono::nanoseconds totalLatency{ 0 };
    uint64_t latencySamples = 0;
    const auto arrival = std::chrono::steady_clock::now();
    for (size_t index = 0; index < frames.size(); ++index)
    {
      const auto hostIndex = (tickSequence_ * static_cast<uint64_t>(packetsPerTick_) + index) % hosts.size();
      const auto& host = hosts[hostIndex];
      auto& counters = portCounters_[host];
      ++counters.received;
      const auto portDecision = topologyController_.evaluate_port(host, frames[index].size(), arrival);
      if (!portDecision || portDecision.value().dropped)
      {
        ++counters.dropped;
        ++tickDropped;
        continue;
      }
      const auto link = std::find_if(
          topologyConfiguration_.links.begin(),
          topologyConfiguration_.links.end(),
          [&host, &switchId](const TopologyLink& candidate)
          {
            return (candidate.from == host && candidate.to == switchId) ||
                   (candidate.from == switchId && candidate.to == host);
          });
      if (link == topologyConfiguration_.links.end())
      {
        ++counters.dropped;
        ++tickDropped;
        continue;
      }
      const auto portArrival = portDecision.value().delivery_times[0];
      const auto linkDecision = topologyController_.evaluate_link(link->from, link->to, frames[index].size(), portArrival);
      if (!linkDecision || linkDecision.value().dropped)
      {
        ++counters.dropped;
        ++tickDropped;
        continue;
      }
      // A duplication fault yields more than one delivery; emitting the frame
      // once per delivery is what makes the duplication control observable.
      const auto deliveries = static_cast<size_t>(portDecision.value().delivery_count) *
                              static_cast<size_t>(linkDecision.value().delivery_count);
      if (deliveries == 0)
      {
        ++counters.dropped;
        ++tickDropped;
        continue;
      }
      for (size_t copy = 0; copy < deliveries; ++copy)
      {
        ++counters.forwarded;
        tickBytes += frames[index].size();
        delivered.push_back({ frames[index].data(), frames[index].size(), static_cast<uint32_t>(hostIndex) });
      }
      totalLatency += linkDecision.value().delivery_times[0] - arrival;
      ++latencySamples;
    }

    AnalysisBatch analysis;
    if (!delivered.empty() && trafficAnalyzer_)
      analysis = trafficAnalyzer_->analyze(delivered.data(), delivered.size());
    totalPackets_ += frames.size();
    totalBytes_ += tickBytes;
    totalDropped_ += tickDropped;
    ++tickSequence_;
    const double throughputMbps = static_cast<double>(tickBytes) * 16.0 / 1'000'000.0;
    const double averageLatencyMs =
        latencySamples == 0 ? 0.0 : static_cast<double>(totalLatency.count()) / 1'000'000.0 / latencySamples;
    rebuildTelemetryModels(analysis, tickBytes, tickDropped, throughputMbps, averageLatencyMs);
  }

  void WireLabViewModel::rebuildTelemetryModels(
      const AnalysisBatch& analysis,
      uint64_t tickBytes,
      uint64_t tickDropped,
      double throughputMbps,
      double averageLatencyMs)
  {
    const double lossPercent = packetsPerTick_ == 0 ? 0.0 : 100.0 * static_cast<double>(tickDropped) / packetsPerTick_;
    metricsHistory_.append(
        QVariantMap{ { "sequence", static_cast<qulonglong>(tickSequence_) },
                     { "throughputMbps", throughputMbps },
                     { "latencyMs", averageLatencyMs },
                     { "lossPercent", lossPercent } });
    while (metricsHistory_.size() > 60)
      metricsHistory_.removeFirst();

    std::vector<std::string> hosts;
    for (const auto& node : topologyConfiguration_.nodes)
      if (node.type == TopologyNodeType::Host)
        hosts.push_back(node.id);
    packetRows_.clear();
    const auto packetLimit = std::min<size_t>(analysis.packets.size(), 100);
    for (size_t index = 0; index < packetLimit; ++index)
    {
      const auto& packet = analysis.packets[index];
      QString ingress = QString::number(packet.ingress_port);

      if (packet.ingress_port < hosts.size())
        ingress = QString::fromStdString(hosts[packet.ingress_port]);
      const auto sourceMac = packet.source_mac.to_string();
      learnedMacPorts_[sourceMac] = ingress.toStdString();
      packetRows_.append(
          QVariantMap{ { "source", QString::fromStdString(sourceMac) },
                       { "destination", QString::fromStdString(packet.destination_mac.to_string()) },
                       { "sourceIp", ipv4_string(packet.source_ipv4) },
                       { "destinationIp", ipv4_string(packet.destination_ipv4) },
                       { "protocol", packet.protocol },
                       { "destinationPort", packet.destination_port },
                       { "bytes", packet.frame_length },
                       { "ingress", ingress },
                       { "classification", classification_name(packet.classification) },
                       { "validity", validity_name(packet.validity) } });
    }

    std::vector<std::pair<std::string, std::string>> learned(learnedMacPorts_.begin(), learnedMacPorts_.end());
    std::sort(learned.begin(), learned.end());
    macTable_.clear();
    for (const auto& [mac, port] : learned)
      macTable_.append(QVariantMap{ { "mac", QString::fromStdString(mac) }, { "port", QString::fromStdString(port) } });

    const auto now = std::chrono::steady_clock::now();
    // One monotonic source: the detector's window clock and the enforcer's lease
    // clock are the same steady_clock, measured from the start of the run.
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - simulationStart_).count();
    const auto outcome = analysisPipeline_.evaluate(analysis, static_cast<uint64_t>(std::max<int64_t>(elapsed, 0)), now);
    const auto& anomalies = outcome.anomalies;
    anomalyRows_.clear();
    for (const auto& anomaly : anomalies)
      anomalyRows_.append(
          QVariantMap{ { "type", anomaly_name(anomaly.type) },
                       { "source", QString::fromStdString(anomaly.source_mac.to_string()) },
                       { "sourceIp", ipv4_string(anomaly.source_ipv4) },
                       { "port", anomaly.ingress_port },
                       { "observed", static_cast<qulonglong>(anomaly.observed_packets) },
                       { "threshold", static_cast<qulonglong>(anomaly.threshold) } });

    const auto& released = outcome.released;
    const auto& enforced = outcome.enforced;
    for (const auto& action : released)
      policyActions_.append(
          QVariantMap{ { "sequence", static_cast<qulonglong>(tickSequence_) },
                       { "rule", QString::fromStdString(action.rule_name) },
                       { "anomaly", anomaly_name(action.anomaly_type) },
                       { "action", policy_action_name(action.action) },
                       { "port", QString::fromStdString(action.port_id) },
                       { "outcome", QString::fromUtf8(to_string(action.outcome)) },
                       { "detail", QStringLiteral("Lease expired, fault restored") } });
    for (const auto& action : enforced)
      policyActions_.append(
          QVariantMap{ { "sequence", static_cast<qulonglong>(tickSequence_) },
                       { "rule", QString::fromStdString(action.rule_name) },
                       { "anomaly", anomaly_name(action.anomaly_type) },
                       { "action", policy_action_name(action.action) },
                       { "port", QString::fromStdString(action.port_id) },
                       { "outcome", QString::fromUtf8(to_string(action.outcome)) },
                       { "detail", enforcement_summary(action) } });
    while (policyActions_.size() > 200)
      policyActions_.removeFirst();
    if (!released.empty() || !enforced.empty())
    {
      rebuildFaultModel();
      rebuildPolicyModel();
    }

    portStates_.clear();
    for (const auto& node : topologyConfiguration_.nodes)
    {
      if (node.type != TopologyNodeType::Host)
        continue;
      const auto& counters = portCounters_[node.id];
      portStates_.append(
          QVariantMap{
              { "id", QString::fromStdString(node.id) },
              { "state",
                analysisPipeline_.enforcer().is_enforced(node.id) ? QStringLiteral("ENFORCED") : QStringLiteral("UP") },
              { "received", static_cast<qulonglong>(counters.received) },
              { "forwarded", static_cast<qulonglong>(counters.forwarded) },
              { "dropped", static_cast<qulonglong>(counters.dropped) } });
    }

    trafficResult_ = QStringLiteral("%1 packets generated · %2 delivered bytes · %3 dropped · %4 Mbps · %5 ms average")
                         .arg(totalPackets_)
                         .arg(totalBytes_)
                         .arg(totalDropped_)
                         .arg(throughputMbps, 0, 'f', 3)
                         .arg(averageLatencyMs, 0, 'f', 2);
    Q_UNUSED(tickBytes);
    emit telemetryChanged();
  }

  void WireLabViewModel::applySelectedFault(int latencyMs, double lossPercent, bool blackhole)
  {
    if (selectedType_.isEmpty())
    {
      setStatus(QStringLiteral("Select a host or link before applying a fault."));
      return;
    }
    FaultConfiguration configuration;
    configuration.latency = std::chrono::milliseconds(latencyMs);
    configuration.loss_basis_points = static_cast<uint32_t>(std::lround(lossPercent * 100.0));
    configuration.blackhole = blackhole;
    const auto result =
        selectedType_ == QStringLiteral("link")
            ? topologyController_.set_link_fault(selectedFirst_.toStdString(), selectedSecond_.toStdString(), configuration)
            : topologyController_.set_port_fault(selectedFirst_.toStdString(), configuration);
    setStatus(
        result ? QStringLiteral("Applied fault to %1").arg(selectedId_)
               : QStringLiteral("Apply fault failed: %1").arg(to_string(result.error())));
    rebuildFaultModel();
  }

  void WireLabViewModel::clearFault(const QString& firstEndpoint, const QString& secondEndpoint)
  {
    const bool cleared = secondEndpoint.isEmpty() ? topologyController_.clear_port_fault(firstEndpoint.toStdString())
                                                  : topologyController_.clear_link_fault(
                                                        firstEndpoint.toStdString(), secondEndpoint.toStdString());
    setStatus(cleared ? QStringLiteral("Cleared fault.") : QStringLiteral("The selected fault was no longer active."));
    rebuildFaultModel();
  }

  void WireLabViewModel::rebuildFaultModel()
  {
    activeFaults_.clear();
    const auto faults = topologyController_.active_faults();
    if (faults)
    {
      for (const auto& fault : faults.value())
      {
        const bool link = !fault.second_endpoint.empty();
        activeFaults_.append(
            QVariantMap{
                { "first", QString::fromStdString(fault.first_endpoint) },
                { "second", QString::fromStdString(fault.second_endpoint) },
                { "target",
                  link ? QStringLiteral("%1 ↔ %2").arg(
                             QString::fromStdString(fault.first_endpoint), QString::fromStdString(fault.second_endpoint))
                       : QString::fromStdString(fault.first_endpoint) },
                { "kind", link ? QStringLiteral("Link") : QStringLiteral("Port") },
                { "latencyMs",
                  static_cast<qlonglong>(
                      std::chrono::duration_cast<std::chrono::milliseconds>(fault.configuration.latency).count()) },
                { "lossPercent", static_cast<double>(fault.configuration.loss_basis_points) / 100.0 },
                { "blackhole", fault.configuration.blackhole } });
      }
    }
    emit faultsChanged();
  }

  bool WireLabViewModel::reportRunning() const noexcept
  {
    return reportRunning_;
  }
  double WireLabViewModel::reportProgress() const noexcept
  {
    return reportProgress_;
  }
  QString WireLabViewModel::reportStage() const
  {
    return reportStage_;
  }
  QVariantList WireLabViewModel::reportRows() const
  {
    return reportRows_;
  }
  QVariantMap WireLabViewModel::reportProvenance() const
  {
    return reportProvenance_;
  }
  QString WireLabViewModel::reportExportPath() const
  {
    return reportExportPath_;
  }

  QStringList WireLabViewModel::reportScenarioNames() const
  {
    // Asking the benchmark engine which names it parses keeps the Reports form
    // from offering a scenario the engine would reject.
    QStringList names;
    for (const char* candidate :
         { "known-unicast", "broadcast", "unknown-unicast", "mixed-traffic", "udp-flood", "port-scan", "broadcast-storm" })
    {
      if (traffic_scenario_from_string(candidate))
        names.push_back(QString::fromLatin1(candidate));
    }
    return names;
  }

  void WireLabViewModel::runBenchmarkReport(const QString& scenario, int packets, int batchSize, int frameSize, int seed)
  {
    if (reportRunning_)
    {
      setStatus(QStringLiteral("A benchmark report is already running."));
      return;
    }
    const auto parsedScenario = traffic_scenario_from_string(scenario.toStdString());
    if (!parsedScenario || packets <= 0 || batchSize <= 0 || seed < 0 ||
        frameSize < static_cast<int>(ETHERNET_HEADER_SIZE) || frameSize > static_cast<int>(MAX_BENCHMARK_FRAME_SIZE))
    {
      setStatus(QStringLiteral("Report settings are invalid."));
      return;
    }

    reportConfig_ = BenchmarkConfig{};
    reportConfig_.traffic.scenario = parsedScenario.value();
    reportConfig_.traffic.seed = static_cast<uint64_t>(seed);
    reportConfig_.traffic.frame_size = static_cast<size_t>(frameSize);
    reportConfig_.packet_count = static_cast<size_t>(packets);
    reportConfig_.batch_size = static_cast<size_t>(batchSize);
    reportQueue_ = availableBackends();
    reportIndex_ = 0;
    reportResults_.clear();
    reportRows_.clear();
    reportExportPath_.clear();
    reportCompletedPackets_ = 0;
    reportTotalPackets_ = reportConfig_.packet_count * static_cast<uint64_t>(reportQueue_.size());
    // Twenty slices per backend keeps the GUI responsive without paying for a
    // tick per batch; a slice never splits a batch, so the counters are the
    // counters of an unsliced run either way.
    reportSliceBudget_ = std::max<size_t>(reportConfig_.batch_size, (reportConfig_.packet_count + 19) / 20);
    reportRunning_ = true;
    reportProgress_ = 0.0;
    rebuildReportProvenance();
    if (!beginNextReportBackend())
    {
      finishReport();
      return;
    }
    setStatus(QStringLiteral("Benchmark report running on %1 backend(s).").arg(reportQueue_.size()));
    emit reportChanged();
  }

  bool WireLabViewModel::beginNextReportBackend()
  {
    while (reportIndex_ < reportQueue_.size())
    {
      BenchmarkConfig config = reportConfig_;
      config.backend = benchmark_backend_id(reportQueue_.at(reportIndex_));
      auto run = BenchmarkRun::create(config, accelerated_benchmark_backend_factory());
      if (run)
      {
        reportRun_.emplace(std::move(run.value()));
        reportStage_ = QStringLiteral("Measuring %1 (%2 of %3)")
                           .arg(reportQueue_.at(reportIndex_))
                           .arg(reportIndex_ + 1)
                           .arg(reportQueue_.size());
        return true;
      }
      // A device can vanish between listing the backends and measuring one; its
      // share of the work still counts as done so progress reaches the end.
      reportCompletedPackets_ += reportConfig_.packet_count;
      ++reportIndex_;
    }
    return false;
  }

  void WireLabViewModel::runReportStep()
  {
    if (!reportRunning_)
      return;
    if (!reportRun_ && !beginNextReportBackend())
    {
      finishReport();
      return;
    }
    const auto before = reportRun_->completed_packets();
    reportRun_->advance(reportSliceBudget_);
    reportCompletedPackets_ += reportRun_->completed_packets() - before;
    if (reportRun_->finished())
    {
      reportResults_.push_back(reportRun_->result());
      reportRun_.reset();
      ++reportIndex_;
      rebuildReportRows();
      if (!beginNextReportBackend())
      {
        finishReport();
        return;
      }
    }
    reportProgress_ = reportTotalPackets_ == 0
                          ? 1.0
                          : static_cast<double>(reportCompletedPackets_) / static_cast<double>(reportTotalPackets_);
    emit reportChanged();
  }

  void WireLabViewModel::finishReport()
  {
    reportRun_.reset();
    reportRunning_ = false;
    reportProgress_ = 1.0;
    reportStage_ = QStringLiteral("%1 backend(s) measured · %2 packets each")
                       .arg(reportRows_.size())
                       .arg(static_cast<qulonglong>(reportConfig_.packet_count));
    setStatus(QStringLiteral("Benchmark report complete."));
    emit reportChanged();
  }

  void WireLabViewModel::rebuildReportRows()
  {
    double cpuPacketsPerSecond = 0.0;
    for (const auto& result : reportResults_)
      if (result.backend == "cpu")
        cpuPacketsPerSecond = result.packets_per_second;
    reportRows_.clear();
    for (const auto& result : reportResults_)
    {
      const double speedup = cpuPacketsPerSecond <= 0.0 ? 0.0 : result.packets_per_second / cpuPacketsPerSecond;
      reportRows_.append(
          QVariantMap{ { "backend", benchmark_backend_label(result.backend) },
                       { "backendId", QString::fromStdString(result.backend) },
                       { "scenario", QString::fromStdString(result.scenario) },
                       { "packets", static_cast<qulonglong>(result.completed_packets) },
                       { "elapsedNs", static_cast<qulonglong>(result.elapsed_ns) },
                       { "packetsPerSecond", result.packets_per_second },
                       { "goodputBitsPerSecond", result.goodput_bits_per_second },
                       { "lossPercent", result.loss_percentage },
                       { "latencyP50Ns", static_cast<qulonglong>(result.batch_analysis_latency_p50_ns) },
                       { "latencyP95Ns", static_cast<qulonglong>(result.batch_analysis_latency_p95_ns) },
                       { "latencyP99Ns", static_cast<qulonglong>(result.batch_analysis_latency_p99_ns) },
                       { "hostToDeviceNs", static_cast<qulonglong>(result.timing.host_to_device_ns) },
                       { "kernelNs", static_cast<qulonglong>(result.timing.kernel_ns) },
                       { "deviceToHostNs", static_cast<qulonglong>(result.timing.device_to_host_ns) },
                       { "speedup", speedup } });
    }
  }

  void WireLabViewModel::rebuildReportProvenance()
  {
    QStringList compiledIn;
    for (const QString& label : { QStringLiteral("CPU"), QStringLiteral("CUDA"), QStringLiteral("Metal") })
      if (benchmark_backend_is_compiled_in(benchmark_backend_id(label)))
        compiledIn.push_back(label);
    reportProvenance_ = QVariantMap{ { "scenario", QString::fromLatin1(to_string(reportConfig_.traffic.scenario)) },
                                     { "seed", static_cast<qulonglong>(reportConfig_.traffic.seed) },
                                     { "packets", static_cast<qulonglong>(reportConfig_.packet_count) },
                                     { "batchSize", static_cast<qulonglong>(reportConfig_.batch_size) },
                                     { "frameSize", static_cast<qulonglong>(reportConfig_.traffic.frame_size) },
                                     { "hostCount", static_cast<qulonglong>(reportConfig_.traffic.host_count) },
                                     { "generator", QString::fromStdString(reportConfig_.generator) },
                                     { "version", QStringLiteral(WIRELAB_VERSION) },
                                     { "buildType", QStringLiteral(WIRELAB_BUILD_TYPE) },
                                     { "backendsCompiledIn", compiledIn },
                                     { "backendsPresent", availableBackends() },
                                     { "generatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate) } };
  }

  bool WireLabViewModel::exportReport(const QString& path)
  {
    const auto fail = [this](const QString& message)
    {
      reportExportPath_ = message;
      setStatus(message);
      emit reportChanged();
      return false;
    };
    if (reportRows_.isEmpty())
      return fail(QStringLiteral("Run a benchmark report before exporting."));

    QString localPath = path;
    if (localPath.startsWith(QStringLiteral("file://")))
      localPath = QUrl(localPath).toLocalFile();
    if (localPath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
      localPath.chop(5);
    if (localPath.isEmpty())
      return fail(QStringLiteral("Choose a file name for the report."));
    const QString jsonPath = localPath + QStringLiteral(".json");
    const QString csvPath = localPath + QStringLiteral(".csv");

    QJsonArray results;
    for (const auto& row : reportRows_)
      results.append(QJsonObject::fromVariantMap(row.toMap()));
    QJsonObject document;
    document.insert(QStringLiteral("provenance"), QJsonObject::fromVariantMap(reportProvenance_));
    document.insert(QStringLiteral("results"), results);

    QFile jsonFile(jsonPath);
    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        jsonFile.write(QJsonDocument(document).toJson(QJsonDocument::Indented)) < 0)
      return fail(QStringLiteral("Could not write %1").arg(jsonPath));
    jsonFile.close();

    // The CSV carries the same rows in a fixed column order so a spreadsheet
    // and the JSON never disagree about what a column means.
    static const char* const columns[] = { "backend",        "scenario",         "packets",
                                           "elapsedNs",      "packetsPerSecond", "goodputBitsPerSecond",
                                           "lossPercent",    "latencyP50Ns",     "latencyP95Ns",
                                           "latencyP99Ns",   "hostToDeviceNs",   "kernelNs",
                                           "deviceToHostNs", "speedup" };
    QString csv;
    for (size_t column = 0; column < std::size(columns); ++column)
      csv += QString::fromLatin1(columns[column]) +
             (column + 1 == std::size(columns) ? QStringLiteral("\n") : QStringLiteral(","));
    for (const auto& row : reportRows_)
    {
      const auto fields = row.toMap();
      for (size_t column = 0; column < std::size(columns); ++column)
        csv += fields.value(QString::fromLatin1(columns[column])).toString() +
               (column + 1 == std::size(columns) ? QStringLiteral("\n") : QStringLiteral(","));
    }
    QFile csvFile(csvPath);
    if (!csvFile.open(QIODevice::WriteOnly | QIODevice::Truncate) || csvFile.write(csv.toUtf8()) < 0)
      return fail(QStringLiteral("Could not write %1").arg(csvPath));
    csvFile.close();

    reportExportPath_ = jsonPath;
    setStatus(
        QStringLiteral("Exported report to %1 and %2").arg(QFileInfo(jsonPath).fileName(), QFileInfo(csvPath).fileName()));
    emit reportChanged();
    return true;
  }
}  // namespace wirelab
