#include "wirelab/wirelab_view_model.hpp"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <filesystem>
#include <string>

#include "wirelab/version.hpp"

namespace wirelab
{
  namespace
  {
    std::string scenario_path()
    {
      return (std::filesystem::path(PROJECT_SOURCE_DIRECTORY) / "scenarios" / "security-lab.yaml").string();
    }

    void run_report_to_completion(WireLabViewModel& model)
    {
      // The GUI drives the report from a QML tick; a test drives the same step
      // by hand so the run stays deterministic and needs no event loop.
      int steps = 0;
      while (model.reportRunning() && steps < 10000)
      {
        model.runReportStep();
        ++steps;
      }
    }
  }  // namespace

  TEST(WireLabViewModelTest, ExercisesTopologyTrafficTelemetryAndFaultWorkflow)
  {
    WireLabViewModel model;
    model.openTopology(QString::fromStdString(scenario_path()));

    ASSERT_TRUE(model.hasTopology());
    EXPECT_EQ(model.topologyNodes().size(), 4);
    EXPECT_EQ(model.topologyLinks().size(), 3);

    model.selectNode(QStringLiteral("client-a"));
    EXPECT_EQ(model.selectedType(), QStringLiteral("node"));
    model.applySelectedFault(25, 0.0, false);
    ASSERT_EQ(model.activeFaults().size(), 1);

    model.startTraffic(QStringLiteral("mixed-traffic"), 64, 128, 42, QStringLiteral("CPU"));
    ASSERT_TRUE(model.trafficRunning());
    model.runTrafficStep();
    model.stopTraffic();

    ASSERT_EQ(model.metricsHistory().size(), 1);
    EXPECT_FALSE(model.packetRows().isEmpty());
    EXPECT_FALSE(model.macTable().isEmpty());
    ASSERT_EQ(model.portStates().size(), 3);
    EXPECT_TRUE(model.trafficResult().contains(QStringLiteral("64 packets generated")));

    model.clearFault(QStringLiteral("client-a"), QString());
    EXPECT_TRUE(model.activeFaults().isEmpty());
  }

  TEST(WireLabViewModelTest, RunsTrafficOnMetalBackendWhenAvailable)
  {
    WireLabViewModel model;
    if (!model.availableBackends().contains(QStringLiteral("Metal")))
    {
      GTEST_SKIP() << "Metal backend not built or no Metal device";
    }
    model.openTopology(QString::fromStdString(scenario_path()));
    ASSERT_TRUE(model.hasTopology());

    EXPECT_TRUE(model.availableBackends().contains(QStringLiteral("CPU")));
    EXPECT_TRUE(model.availableBackends().contains(QStringLiteral("Metal")));

    model.startTraffic(QStringLiteral("mixed-traffic"), 64, 128, 42, QStringLiteral("Metal"));
    ASSERT_TRUE(model.trafficRunning());
    EXPECT_EQ(model.activeBackend(), QStringLiteral("Metal"));
    model.runTrafficStep();
    EXPECT_FALSE(model.packetRows().isEmpty());
    model.stopTraffic();

    model.startTraffic(QStringLiteral("mixed-traffic"), 16, 128, 7, QStringLiteral("DoesNotExist"));
    EXPECT_FALSE(model.trafficRunning());
    EXPECT_TRUE(model.statusMessage().contains(QStringLiteral("not available")));
  }

  TEST(WireLabViewModelTest, OpensTopologyFromFileUrlString)
  {
    WireLabViewModel model;
    model.openTopology(QStringLiteral("file://") + QString::fromStdString(scenario_path()));
    EXPECT_TRUE(model.hasTopology());
    EXPECT_EQ(model.topologyName(), QStringLiteral("security-lab"));
  }

  TEST(WireLabViewModelTest, EditsValidTopologyAndRejectsInvalidChanges)
  {
    WireLabViewModel model;
    model.openTopology(QString::fromStdString(scenario_path()));

    model.addNode(QStringLiteral("client-c"), QStringLiteral("host"));
    ASSERT_EQ(model.topologyNodes().size(), 5);
    model.addLink(QStringLiteral("client-c"), QStringLiteral("core-switch"), 3);
    EXPECT_EQ(model.topologyLinks().size(), 4);

    model.addNode(QStringLiteral("core-switch"), QStringLiteral("switch"));
    EXPECT_EQ(model.topologyNodes().size(), 5);
    EXPECT_TRUE(model.statusMessage().contains(QStringLiteral("validation failed"), Qt::CaseInsensitive));

    model.selectNode(QStringLiteral("client-c"));
    model.removeSelected();
    EXPECT_EQ(model.topologyNodes().size(), 4);
    EXPECT_EQ(model.topologyLinks().size(), 3);
  }

  TEST(WireLabViewModelTest, ReportsBenchmarkRowsForEveryAvailableBackend)
  {
    WireLabViewModel model;
    EXPECT_FALSE(model.reportRunning());
    EXPECT_TRUE(model.reportScenarioNames().contains(QStringLiteral("mixed-traffic")));

    model.runBenchmarkReport(QStringLiteral("mixed-traffic"), 512, 64, 128, 7);
    ASSERT_TRUE(model.reportRunning());
    run_report_to_completion(model);
    ASSERT_FALSE(model.reportRunning());
    EXPECT_DOUBLE_EQ(model.reportProgress(), 1.0);

    const auto rows = model.reportRows();
    // Every backend the view model offers has to end up measured, so an
    // accelerator that is listed but never runs is a failure, not a gap.
    ASSERT_EQ(rows.size(), model.availableBackends().size());
    const auto cpu = rows.front().toMap();
    EXPECT_EQ(cpu.value(QStringLiteral("backend")).toString(), QStringLiteral("CPU"));
    EXPECT_EQ(cpu.value(QStringLiteral("packets")).toULongLong(), 512U);
    EXPECT_GT(cpu.value(QStringLiteral("packetsPerSecond")).toDouble(), 0.0);
    EXPECT_GT(cpu.value(QStringLiteral("goodputBitsPerSecond")).toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(cpu.value(QStringLiteral("speedup")).toDouble(), 1.0);
    EXPECT_GE(
        cpu.value(QStringLiteral("latencyP95Ns")).toULongLong(), cpu.value(QStringLiteral("latencyP50Ns")).toULongLong());
    EXPECT_GE(
        cpu.value(QStringLiteral("latencyP99Ns")).toULongLong(), cpu.value(QStringLiteral("latencyP95Ns")).toULongLong());

    // Rows follow the backend order the view model advertises, so CPU is first
    // and every accelerator row is measured against it.
    const auto backends = model.availableBackends();
    int previousPosition = -1;
    for (const auto& row : rows)
    {
      const auto fields = row.toMap();
      const int position = backends.indexOf(fields.value(QStringLiteral("backend")).toString());
      EXPECT_GT(position, previousPosition);
      previousPosition = position;
      EXPECT_GT(fields.value(QStringLiteral("speedup")).toDouble(), 0.0);
    }
  }

  TEST(WireLabViewModelTest, ReportProvenanceNamesTheRunAndTheBuild)
  {
    WireLabViewModel model;
    model.runBenchmarkReport(QStringLiteral("broadcast"), 256, 32, 64, 11);
    run_report_to_completion(model);

    const auto provenance = model.reportProvenance();
    EXPECT_EQ(provenance.value(QStringLiteral("scenario")).toString(), QStringLiteral("broadcast"));
    EXPECT_EQ(provenance.value(QStringLiteral("seed")).toULongLong(), 11U);
    EXPECT_EQ(provenance.value(QStringLiteral("packets")).toULongLong(), 256U);
    EXPECT_EQ(provenance.value(QStringLiteral("batchSize")).toULongLong(), 32U);
    EXPECT_EQ(provenance.value(QStringLiteral("frameSize")).toULongLong(), 64U);
    EXPECT_EQ(provenance.value(QStringLiteral("version")).toString(), QStringLiteral(WIRELAB_VERSION));
    EXPECT_FALSE(provenance.value(QStringLiteral("buildType")).toString().isEmpty());
    EXPECT_FALSE(provenance.value(QStringLiteral("generatedAt")).toString().isEmpty());
    EXPECT_TRUE(provenance.value(QStringLiteral("backendsCompiledIn")).toStringList().contains(QStringLiteral("CPU")));
    EXPECT_EQ(provenance.value(QStringLiteral("backendsPresent")).toStringList(), model.availableBackends());
    // A backend can be compiled in without the machine having the device, never
    // the other way round.
    for (const auto& present : model.availableBackends())
      EXPECT_TRUE(provenance.value(QStringLiteral("backendsCompiledIn")).toStringList().contains(present));
  }

  TEST(WireLabViewModelTest, ExportsReportAsJsonAndCsv)
  {
    WireLabViewModel model;
    EXPECT_FALSE(model.exportReport(QStringLiteral("/tmp/wirelab-never-written.json")));
    EXPECT_TRUE(model.reportExportPath().contains(QStringLiteral("before exporting")));

    model.runBenchmarkReport(QStringLiteral("port-scan"), 256, 32, 64, 3);
    run_report_to_completion(model);
    const auto rows = model.reportRows();
    ASSERT_FALSE(rows.isEmpty());

    const auto base = std::filesystem::temp_directory_path() / "wirelab-view-model-report";
    const auto jsonPath = QString::fromStdString(base.string() + ".json");
    const auto csvPath = QString::fromStdString(base.string() + ".csv");
    std::filesystem::remove(jsonPath.toStdString());
    std::filesystem::remove(csvPath.toStdString());

    ASSERT_TRUE(model.exportReport(jsonPath));
    EXPECT_EQ(model.reportExportPath(), jsonPath);

    QFile jsonFile(jsonPath);
    ASSERT_TRUE(jsonFile.open(QIODevice::ReadOnly));
    const auto document = QJsonDocument::fromJson(jsonFile.readAll());
    ASSERT_TRUE(document.isObject());
    const auto object = document.object();
    EXPECT_EQ(
        object.value(QStringLiteral("provenance")).toObject().value(QStringLiteral("scenario")).toString(),
        QStringLiteral("port-scan"));
    const auto results = object.value(QStringLiteral("results")).toArray();
    ASSERT_EQ(results.size(), rows.size());
    EXPECT_EQ(results.at(0).toObject().value(QStringLiteral("backend")).toString(), QStringLiteral("CPU"));

    QFile csvFile(csvPath);
    ASSERT_TRUE(csvFile.open(QIODevice::ReadOnly));
    const auto csv = QString::fromUtf8(csvFile.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    ASSERT_EQ(csv.size(), rows.size() + 1);
    EXPECT_TRUE(csv.front().startsWith(QStringLiteral("backend,scenario,packets")));
    EXPECT_TRUE(csv.at(1).startsWith(QStringLiteral("CPU,port-scan,256")));

    std::filesystem::remove(jsonPath.toStdString());
    std::filesystem::remove(csvPath.toStdString());
  }

  TEST(WireLabViewModelTest, RejectsInvalidReportSettings)
  {
    WireLabViewModel model;
    model.runBenchmarkReport(QStringLiteral("no-such-scenario"), 256, 32, 64, 1);
    EXPECT_FALSE(model.reportRunning());
    EXPECT_TRUE(model.statusMessage().contains(QStringLiteral("invalid")));

    model.runBenchmarkReport(QStringLiteral("mixed-traffic"), 256, 32, 8, 1);
    EXPECT_FALSE(model.reportRunning());
    EXPECT_TRUE(model.reportRows().isEmpty());
  }
}  // namespace wirelab
