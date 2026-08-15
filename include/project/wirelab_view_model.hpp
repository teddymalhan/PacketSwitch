#ifndef PROJECT_WIRELAB_VIEW_MODEL_HPP_
#define PROJECT_WIRELAB_VIEW_MODEL_HPP_

#include <QObject>
#include <QString>
#include <QVariantList>

#include "project/topology_controller.hpp"

namespace project
{
  class WireLabViewModel final : public QObject
  {
    Q_OBJECT
    Q_PROPERTY(QString topologyName READ topologyName NOTIFY topologyChanged)
    Q_PROPERTY(QVariantList topologyNodes READ topologyNodes NOTIFY topologyChanged)
    Q_PROPERTY(QVariantList topologyLinks READ topologyLinks NOTIFY topologyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString trafficResult READ trafficResult NOTIFY trafficResultChanged)
    Q_PROPERTY(QString faultSummary READ faultSummary NOTIFY faultSummaryChanged)

   public:
    explicit WireLabViewModel(QObject* parent = nullptr);

    [[nodiscard]] QString topologyName() const;
    [[nodiscard]] QVariantList topologyNodes() const;
    [[nodiscard]] QVariantList topologyLinks() const;
    [[nodiscard]] QString statusMessage() const;
    [[nodiscard]] QString trafficResult() const;
    [[nodiscard]] QString faultSummary() const;

    Q_INVOKABLE void openTopology(const QString& path);
    Q_INVOKABLE void runTrafficPreview(const QString& scenario, int packetCount, int batchSize, int frameSize,
                                       qulonglong seed);
    Q_INVOKABLE void applyPortFault(const QString& portId, int latencyMs, double lossPercent, bool blackhole);
    Q_INVOKABLE void clearPortFault(const QString& portId);

   signals:
    void topologyChanged();
    void statusMessageChanged();
    void trafficResultChanged();
    void faultSummaryChanged();

   private:
    void updateFaultSummary();
    TopologyController topologyController_;
    QString topologyName_;
    QVariantList topologyNodes_;
    QVariantList topologyLinks_;
    QString statusMessage_;
    QString trafficResult_;
    QString faultSummary_;
  };
}

#endif
