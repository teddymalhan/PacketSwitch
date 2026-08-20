#include "wirelab/wirelab_view_model.hpp"

#include <QString>

#include "wirelab/policy_enforcer.hpp"
#include "wirelab/topology.hpp"

namespace wirelab
{
  namespace
  {
    QString qstring(const std::string& text)
    {
      return QString::fromStdString(text);
    }

    QStringList qstringlist(const std::vector<std::string>& values)
    {
      QStringList list;
      list.reserve(static_cast<qsizetype>(values.size()));
      for (const auto& value : values)
        list.push_back(qstring(value));
      return list;
    }

    QString fault_target(const FaultRow& fault)
    {
      return fault.is_link() ? QStringLiteral("%1 ↔ %2").arg(qstring(fault.first), qstring(fault.second))
                             : qstring(fault.first);
    }
  }  // namespace

  WireLabViewModel::WireLabViewModel(QObject* parent) : QObject(parent)
  {
    publish();
  }

  void WireLabViewModel::publish()
  {
    const uint32_t dirty = session_.take_dirty();
    if (dirty == 0U)
      return;

    if (dirty & dirty_bit(SessionDirty::Topology))
    {
      topologyNodes_.clear();
      for (const auto& node : session_.topology_nodes())
        topologyNodes_.append(QVariantMap{ { "id", qstring(node.id) },
                                           { "type", QString::fromLatin1(to_string(node.type)) },
                                           { "x", node.x },
                                           { "y", node.y } });
      topologyLinks_.clear();
      for (const auto& link : session_.topology_links())
        topologyLinks_.append(QVariantMap{ { "from", qstring(link.from) },
                                           { "to", qstring(link.to) },
                                           { "latencyMs", static_cast<qlonglong>(link.latency_ms) } });
    }

    if (dirty & dirty_bit(SessionDirty::Faults))
    {
      activeFaults_.clear();
      for (const auto& fault : session_.active_faults())
        activeFaults_.append(QVariantMap{ { "first", qstring(fault.first) },
                                          { "second", qstring(fault.second) },
                                          { "target", fault_target(fault) },
                                          { "kind", fault.is_link() ? QStringLiteral("Link") : QStringLiteral("Port") },
                                          { "latencyMs", static_cast<qlonglong>(fault.latency_ms) },
                                          { "lossPercent", fault.loss_percent },
                                          { "blackhole", fault.blackhole } });
    }

    if (dirty & dirty_bit(SessionDirty::Policies))
    {
      policyRules_.clear();
      for (const auto& rule : session_.policy_rules())
        policyRules_.append(QVariantMap{ { "name", qstring(rule.name) },
                                         { "anomaly", QString::fromUtf8(display_name(rule.anomaly_type)) },
                                         { "action", QString::fromUtf8(display_name(rule.action)) },
                                         { "enabled", rule.enabled },
                                         { "rateLimit", static_cast<qulonglong>(rule.rate_limit_packets_per_second) },
                                         { "hits", static_cast<qulonglong>(rule.hits) } });
      enforcedPorts_.clear();
      for (const auto& port : session_.enforced_ports())
        enforcedPorts_.append(QVariantMap{ { "port", qstring(port.port) },
                                           { "rule", qstring(port.rule) },
                                           { "kind", QString::fromUtf8(to_string(port.kind)) },
                                           { "summary", qstring(port.summary) } });
    }

    if (dirty & dirty_bit(SessionDirty::Telemetry))
    {
      metricsHistory_.clear();
      for (const auto& sample : session_.metrics_history())
        metricsHistory_.append(QVariantMap{ { "sequence", static_cast<qulonglong>(sample.sequence) },
                                            { "throughputMbps", sample.throughput_mbps },
                                            { "latencyMs", sample.latency_ms },
                                            { "lossPercent", sample.loss_percent } });
      macTable_.clear();
      for (const auto& entry : session_.mac_table())
        macTable_.append(QVariantMap{ { "mac", qstring(entry.mac) }, { "port", qstring(entry.port) } });
      portStates_.clear();
      for (const auto& port : session_.port_states())
        portStates_.append(QVariantMap{ { "id", qstring(port.id) },
                                        { "state", port.enforced ? QStringLiteral("ENFORCED") : QStringLiteral("UP") },
                                        { "received", static_cast<qulonglong>(port.received) },
                                        { "forwarded", static_cast<qulonglong>(port.forwarded) },
                                        { "dropped", static_cast<qulonglong>(port.dropped) } });
      packetRows_.clear();
      for (const auto& packet : session_.packet_rows())
        packetRows_.append(QVariantMap{ { "source", qstring(packet.source_mac) },
                                        { "destination", qstring(packet.destination_mac) },
                                        { "sourceIp", qstring(packet.source_ip) },
                                        { "destinationIp", qstring(packet.destination_ip) },
                                        { "protocol", packet.protocol },
                                        { "destinationPort", packet.destination_port },
                                        { "bytes", packet.bytes },
                                        { "ingress", qstring(packet.ingress) },
                                        { "classification", QString::fromUtf8(display_name(packet.classification)) },
                                        { "validity", QString::fromUtf8(display_name(packet.validity)) } });
      anomalyRows_.clear();
      for (const auto& anomaly : session_.anomaly_rows())
        anomalyRows_.append(QVariantMap{ { "type", QString::fromUtf8(display_name(anomaly.type)) },
                                         { "source", qstring(anomaly.source_mac) },
                                         { "sourceIp", qstring(anomaly.source_ip) },
                                         { "port", anomaly.ingress_port },
                                         { "observed", static_cast<qulonglong>(anomaly.observed) },
                                         { "threshold", static_cast<qulonglong>(anomaly.threshold) } });
      policyActions_.clear();
      for (const auto& action : session_.policy_actions())
        policyActions_.append(QVariantMap{ { "sequence", static_cast<qulonglong>(action.sequence) },
                                           { "rule", qstring(action.rule) },
                                           { "anomaly", QString::fromUtf8(display_name(action.anomaly_type)) },
                                           { "action", QString::fromUtf8(display_name(action.action)) },
                                           { "port", qstring(action.port) },
                                           { "outcome", QString::fromUtf8(to_string(action.outcome)) },
                                           { "detail", qstring(action.detail) } });
    }

    if (dirty & dirty_bit(SessionDirty::Report))
    {
      reportRows_.clear();
      for (const auto& row : session_.report_rows())
        reportRows_.append(QVariantMap{ { "backend", qstring(row.backend_label) },
                                        { "backendId", qstring(row.backend_id) },
                                        { "scenario", qstring(row.scenario) },
                                        { "packets", static_cast<qulonglong>(row.packets) },
                                        { "elapsedNs", static_cast<qulonglong>(row.elapsed_ns) },
                                        { "packetsPerSecond", row.packets_per_second },
                                        { "goodputBitsPerSecond", row.goodput_bits_per_second },
                                        { "lossPercent", row.loss_percent },
                                        { "latencyP50Ns", static_cast<qulonglong>(row.latency_p50_ns) },
                                        { "latencyP95Ns", static_cast<qulonglong>(row.latency_p95_ns) },
                                        { "latencyP99Ns", static_cast<qulonglong>(row.latency_p99_ns) },
                                        { "hostToDeviceNs", static_cast<qulonglong>(row.host_to_device_ns) },
                                        { "kernelNs", static_cast<qulonglong>(row.kernel_ns) },
                                        { "deviceToHostNs", static_cast<qulonglong>(row.device_to_host_ns) },
                                        { "transferInclusiveNs", static_cast<qulonglong>(row.transfer_inclusive_ns) },
                                        { "queueWaitNs", static_cast<qulonglong>(row.queue_wait_ns) },
                                        { "speedup", row.speedup } });
      const auto& provenance = session_.report_provenance();
      reportProvenance_ = QVariantMap{ { "scenario", qstring(provenance.scenario) },
                                       { "seed", static_cast<qulonglong>(provenance.seed) },
                                       { "packets", static_cast<qulonglong>(provenance.packets) },
                                       { "batchSize", static_cast<qulonglong>(provenance.batch_size) },
                                       { "frameSize", static_cast<qulonglong>(provenance.frame_size) },
                                       { "hostCount", static_cast<qulonglong>(provenance.host_count) },
                                       { "generator", qstring(provenance.generator) },
                                       { "version", qstring(provenance.version) },
                                       { "buildType", qstring(provenance.build_type) },
                                       { "backendsCompiledIn", qstringlist(provenance.backends_compiled_in) },
                                       { "backendsPresent", qstringlist(provenance.backends_present) },
                                       { "generatedAt", qstring(provenance.generated_at) } };
    }

    if (dirty & dirty_bit(SessionDirty::Topology))
      emit topologyChanged();
    if (dirty & dirty_bit(SessionDirty::Selection))
      emit selectionChanged();
    if (dirty & dirty_bit(SessionDirty::Status))
      emit statusMessageChanged();
    if (dirty & dirty_bit(SessionDirty::TrafficState))
      emit trafficStateChanged();
    if (dirty & dirty_bit(SessionDirty::Faults))
      emit faultsChanged();
    if (dirty & dirty_bit(SessionDirty::Policies))
    {
      emit policiesChanged();
      // enforcedPorts is bound to telemetryChanged, and a policy change is the
      // only thing that rebuilds it outside a traffic tick.
      emit telemetryChanged();
    }
    else if (dirty & dirty_bit(SessionDirty::Telemetry))
    {
      emit telemetryChanged();
    }
    if (dirty & dirty_bit(SessionDirty::Report))
      emit reportChanged();
  }

  bool WireLabViewModel::hasTopology() const noexcept
  {
    return session_.has_topology();
  }
  QString WireLabViewModel::topologyName() const
  {
    return qstring(session_.topology_name());
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
    switch (session_.selection_kind())
    {
      case SelectionKind::Node: return QStringLiteral("node");
      case SelectionKind::Link: return QStringLiteral("link");
      case SelectionKind::None: break;
    }
    return QString();
  }
  QString WireLabViewModel::selectedId() const
  {
    return qstring(session_.selected_id());
  }
  QString WireLabViewModel::selectedSummary() const
  {
    return qstring(session_.selected_summary());
  }
  QString WireLabViewModel::statusMessage() const
  {
    return qstring(session_.status_message());
  }
  bool WireLabViewModel::trafficRunning() const noexcept
  {
    return session_.traffic_running();
  }
  QString WireLabViewModel::activeBackend() const
  {
    return qstring(session_.active_backend());
  }
  QStringList WireLabViewModel::availableBackends() const
  {
    return qstringlist(Session::available_backends());
  }
  QString WireLabViewModel::trafficResult() const
  {
    return qstring(session_.traffic_result());
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
    return { QString::fromUtf8(display_name(AnomalyType::BroadcastStorm)),
             QString::fromUtf8(display_name(AnomalyType::MacFlap)),
             QString::fromUtf8(display_name(AnomalyType::UnknownUnicastFlood)),
             QString::fromUtf8(display_name(AnomalyType::UdpFlood)),
             QString::fromUtf8(display_name(AnomalyType::PortScan)),
             QString::fromUtf8(display_name(AnomalyType::HotTalker)),
             QString::fromUtf8(display_name(AnomalyType::MalformedFrame)) };
  }

  QStringList WireLabViewModel::policyActionNames() const
  {
    return { QString::fromUtf8(display_name(PolicyAction::AlertOnly)),
             QString::fromUtf8(display_name(PolicyAction::Mirror)),
             QString::fromUtf8(display_name(PolicyAction::RateLimit)),
             QString::fromUtf8(display_name(PolicyAction::Drop)),
             QString::fromUtf8(display_name(PolicyAction::Quarantine)),
             QString::fromUtf8(display_name(PolicyAction::Allow)) };
  }

  bool WireLabViewModel::reportRunning() const noexcept
  {
    return session_.report_running();
  }
  double WireLabViewModel::reportProgress() const noexcept
  {
    return session_.report_progress();
  }
  QString WireLabViewModel::reportStage() const
  {
    return qstring(session_.report_stage());
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
    return qstring(session_.report_export_path());
  }
  QStringList WireLabViewModel::reportScenarioNames() const
  {
    return qstringlist(scenario_names());
  }

  void WireLabViewModel::openTopology(const QString& path)
  {
    session_.open_topology(path.toStdString());
    publish();
  }

  void WireLabViewModel::saveTopology(const QString& path)
  {
    session_.save_topology(path.toStdString());
    publish();
  }

  void WireLabViewModel::selectNode(const QString& id)
  {
    session_.select_node(id.toStdString());
    publish();
  }

  void WireLabViewModel::selectLink(const QString& from, const QString& to)
  {
    session_.select_link(from.toStdString(), to.toStdString());
    publish();
  }

  void WireLabViewModel::clearSelection()
  {
    session_.clear_selection();
    publish();
  }

  void WireLabViewModel::addNode(const QString& id, const QString& type)
  {
    session_.add_node(id.toStdString(), type.toStdString());
    publish();
  }

  void WireLabViewModel::addLink(const QString& from, const QString& to, int latencyMs)
  {
    session_.add_link(from.toStdString(), to.toStdString(), latencyMs);
    publish();
  }

  void WireLabViewModel::removeSelected()
  {
    session_.remove_selected();
    publish();
  }

  void WireLabViewModel::startTraffic(
      const QString& scenario,
      int packetsPerTick,
      int frameSize,
      qulonglong seed,
      const QString& backend)
  {
    session_.start_traffic(scenario.toStdString(), packetsPerTick, frameSize, seed, backend.toStdString());
    publish();
  }

  void WireLabViewModel::stopTraffic()
  {
    session_.stop_traffic();
    publish();
  }

  void WireLabViewModel::runTrafficStep()
  {
    session_.run_traffic_step();
    publish();
  }

  void WireLabViewModel::applySelectedFault(int latencyMs, double lossPercent, bool blackhole)
  {
    session_.apply_selected_fault(latencyMs, lossPercent, blackhole);
    publish();
  }

  void WireLabViewModel::clearFault(const QString& firstEndpoint, const QString& secondEndpoint)
  {
    session_.clear_fault(firstEndpoint.toStdString(), secondEndpoint.toStdString());
    publish();
  }

  void WireLabViewModel::addPolicy(
      const QString& name,
      const QString& anomalyType,
      const QString& action,
      qulonglong rateLimitPacketsPerSecond)
  {
    session_.add_policy(name.toStdString(), anomalyType.toStdString(), action.toStdString(), rateLimitPacketsPerSecond);
    publish();
  }

  void WireLabViewModel::removePolicy(const QString& name)
  {
    session_.remove_policy(name.toStdString());
    publish();
  }

  void WireLabViewModel::setPolicyEnabled(const QString& name, bool enabled)
  {
    session_.set_policy_enabled(name.toStdString(), enabled);
    publish();
  }

  void WireLabViewModel::releaseEnforcement(const QString& portId)
  {
    session_.release_enforcement(portId.toStdString());
    publish();
  }

  void WireLabViewModel::runBenchmarkReport(const QString& scenario, int packets, int batchSize, int frameSize, int seed)
  {
    session_.run_benchmark_report(scenario.toStdString(), packets, batchSize, frameSize, seed);
    publish();
  }

  void WireLabViewModel::runReportStep()
  {
    session_.run_report_step();
    publish();
  }

  bool WireLabViewModel::exportReport(const QString& path)
  {
    const bool exported = session_.export_report(path.toStdString());
    publish();
    return exported;
  }
}  // namespace wirelab
