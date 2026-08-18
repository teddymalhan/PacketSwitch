#ifndef PROJECT_WIRELAB_VIEW_MODEL_HPP_
#define PROJECT_WIRELAB_VIEW_MODEL_HPP_

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "wirelab/analysis_pipeline.hpp"
#include "wirelab/anomaly_detector.hpp"
#include "wirelab/benchmark.hpp"
#include "wirelab/packet_analyzer.hpp"
#include "wirelab/policy_enforcer.hpp"
#include "wirelab/topology_controller.hpp"
#include "wirelab/traffic_generator.hpp"

namespace wirelab
{
  class WireLabViewModel final : public QObject
  {
    Q_OBJECT
    Q_PROPERTY(bool hasTopology READ hasTopology NOTIFY topologyChanged)
    Q_PROPERTY(QString topologyName READ topologyName NOTIFY topologyChanged)
    Q_PROPERTY(QVariantList topologyNodes READ topologyNodes NOTIFY topologyChanged)
    Q_PROPERTY(QVariantList topologyLinks READ topologyLinks NOTIFY topologyChanged)
    Q_PROPERTY(QString selectedType READ selectedType NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedSummary READ selectedSummary NOTIFY selectionChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool trafficRunning READ trafficRunning NOTIFY trafficStateChanged)
    Q_PROPERTY(QString activeBackend READ activeBackend NOTIFY trafficStateChanged)
    Q_PROPERTY(QStringList availableBackends READ availableBackends CONSTANT)
    Q_PROPERTY(QString trafficResult READ trafficResult NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantList metricsHistory READ metricsHistory NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantList macTable READ macTable NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantList portStates READ portStates NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantList packetRows READ packetRows NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantList anomalyRows READ anomalyRows NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantList activeFaults READ activeFaults NOTIFY faultsChanged)
    Q_PROPERTY(QVariantList policyRules READ policyRules NOTIFY policiesChanged)
    Q_PROPERTY(QVariantList policyActions READ policyActions NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantList enforcedPorts READ enforcedPorts NOTIFY telemetryChanged)
    Q_PROPERTY(QStringList anomalyTypeNames READ anomalyTypeNames CONSTANT)
    Q_PROPERTY(QStringList policyActionNames READ policyActionNames CONSTANT)
    Q_PROPERTY(bool reportRunning READ reportRunning NOTIFY reportChanged)
    Q_PROPERTY(double reportProgress READ reportProgress NOTIFY reportChanged)
    Q_PROPERTY(QString reportStage READ reportStage NOTIFY reportChanged)
    Q_PROPERTY(QVariantList reportRows READ reportRows NOTIFY reportChanged)
    Q_PROPERTY(QVariantMap reportProvenance READ reportProvenance NOTIFY reportChanged)
    Q_PROPERTY(QString reportExportPath READ reportExportPath NOTIFY reportChanged)
    Q_PROPERTY(QStringList reportScenarioNames READ reportScenarioNames CONSTANT)

   public:
    explicit WireLabViewModel(QObject* parent = nullptr);

    [[nodiscard]] bool hasTopology() const noexcept;
    [[nodiscard]] QString topologyName() const;
    [[nodiscard]] QVariantList topologyNodes() const;
    [[nodiscard]] QVariantList topologyLinks() const;
    [[nodiscard]] QString selectedType() const;
    [[nodiscard]] QString selectedId() const;
    [[nodiscard]] QString selectedSummary() const;
    [[nodiscard]] QString statusMessage() const;
    [[nodiscard]] bool trafficRunning() const noexcept;
    [[nodiscard]] QString activeBackend() const;
    [[nodiscard]] QStringList availableBackends() const;
    [[nodiscard]] QString trafficResult() const;
    [[nodiscard]] QVariantList metricsHistory() const;
    [[nodiscard]] QVariantList macTable() const;
    [[nodiscard]] QVariantList portStates() const;
    [[nodiscard]] QVariantList packetRows() const;
    [[nodiscard]] QVariantList anomalyRows() const;
    [[nodiscard]] QVariantList activeFaults() const;
    [[nodiscard]] QVariantList policyRules() const;
    [[nodiscard]] QVariantList policyActions() const;
    [[nodiscard]] QVariantList enforcedPorts() const;
    [[nodiscard]] QStringList anomalyTypeNames() const;
    [[nodiscard]] QStringList policyActionNames() const;
    [[nodiscard]] bool reportRunning() const noexcept;
    [[nodiscard]] double reportProgress() const noexcept;
    [[nodiscard]] QString reportStage() const;
    [[nodiscard]] QVariantList reportRows() const;
    [[nodiscard]] QVariantMap reportProvenance() const;
    [[nodiscard]] QString reportExportPath() const;
    [[nodiscard]] QStringList reportScenarioNames() const;

    Q_INVOKABLE void openTopology(const QString& path);
    Q_INVOKABLE void saveTopology(const QString& path);
    Q_INVOKABLE void selectNode(const QString& id);
    Q_INVOKABLE void selectLink(const QString& from, const QString& to);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void addNode(const QString& id, const QString& type);
    Q_INVOKABLE void addLink(const QString& from, const QString& to, int latencyMs);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void
    startTraffic(const QString& scenario, int packetsPerTick, int frameSize, qulonglong seed, const QString& backend);
    Q_INVOKABLE void stopTraffic();
    Q_INVOKABLE void runTrafficStep();
    Q_INVOKABLE void applySelectedFault(int latencyMs, double lossPercent, bool blackhole);
    Q_INVOKABLE void clearFault(const QString& firstEndpoint, const QString& secondEndpoint);
    Q_INVOKABLE void
    addPolicy(const QString& name, const QString& anomalyType, const QString& action, qulonglong rateLimitPacketsPerSecond);
    Q_INVOKABLE void removePolicy(const QString& name);
    Q_INVOKABLE void setPolicyEnabled(const QString& name, bool enabled);
    Q_INVOKABLE void releaseEnforcement(const QString& portId);
    Q_INVOKABLE void runBenchmarkReport(const QString& scenario, int packets, int batchSize, int frameSize, int seed);
    Q_INVOKABLE void runReportStep();
    Q_INVOKABLE bool exportReport(const QString& path);

   signals:
    void topologyChanged();
    void selectionChanged();
    void statusMessageChanged();
    void trafficStateChanged();
    void telemetryChanged();
    void faultsChanged();
    void policiesChanged();
    void reportChanged();

   private:
    struct PortCounters
    {
      uint64_t received = 0;
      uint64_t forwarded = 0;
      uint64_t dropped = 0;
    };

    [[nodiscard]] bool commitTopology(TopologyConfiguration configuration, const QString& successMessage);
    void rebuildTopologyModels();
    void rebuildFaultModel();
    void rebuildPolicyModel();
    void rebuildTelemetryModels(
        const AnalysisBatch& analysis,
        uint64_t tickBytes,
        uint64_t tickDropped,
        double throughputMbps,
        double averageLatencyMs);
    void resetSimulation();
    void setStatus(QString message);
    // The report advances one backend at a time; beginNextReportBackend() arms the
    // next queued backend and answers whether there was one left to arm.
    bool beginNextReportBackend();
    void finishReport();
    void rebuildReportRows();
    void rebuildReportProvenance();

    TopologyController topologyController_;
    TopologyConfiguration topologyConfiguration_;
    QString topologyPath_;
    QVariantList topologyNodes_;
    QVariantList topologyLinks_;
    QString selectedType_;
    QString selectedId_;
    QString selectedFirst_;
    QString selectedSecond_;
    QString selectedSummary_;
    QString statusMessage_;

    bool trafficRunning_ = false;
    QString activeBackend_ = QStringLiteral("CPU");
    TrafficScenario trafficScenario_ = TrafficScenario::Mixed;
    int packetsPerTick_ = 256;
    int frameSize_ = 64;
    uint64_t trafficSeed_ = 1;
    uint64_t tickSequence_ = 0;
    uint64_t totalPackets_ = 0;
    uint64_t totalBytes_ = 0;
    uint64_t totalDropped_ = 0;
    std::unique_ptr<DeterministicTrafficGenerator> trafficGenerator_;
    std::unique_ptr<PacketAnalyzer> trafficAnalyzer_;
    AnalysisPipeline analysisPipeline_;
    std::unordered_map<std::string, PortCounters> portCounters_;
    std::chrono::steady_clock::time_point simulationStart_{};
    std::unordered_map<std::string, std::string> learnedMacPorts_;

    QString trafficResult_;
    QVariantList metricsHistory_;
    QVariantList macTable_;
    QVariantList portStates_;
    QVariantList packetRows_;
    QVariantList anomalyRows_;
    QVariantList activeFaults_;
    QVariantList policyRules_;
    QVariantList policyActions_;
    QVariantList enforcedPorts_;

    bool reportRunning_ = false;
    double reportProgress_ = 0.0;
    QString reportStage_;
    QString reportExportPath_;
    BenchmarkConfig reportConfig_;
    QStringList reportQueue_;
    int reportIndex_ = 0;
    size_t reportSliceBudget_ = 0;
    uint64_t reportCompletedPackets_ = 0;
    uint64_t reportTotalPackets_ = 0;
    std::optional<BenchmarkRun> reportRun_;
    std::vector<BenchmarkResult> reportResults_;
    QVariantList reportRows_;
    QVariantMap reportProvenance_;
  };
}  // namespace wirelab

#endif
