#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "wirelab/wirelab_view_model.hpp"

int main(int argc, char* argv[])
{
  QGuiApplication application(argc, argv);
  QGuiApplication::setOrganizationName(QStringLiteral("WireLab"));
  QGuiApplication::setApplicationName(QStringLiteral("WireLab"));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("WireLab"));

#ifdef Q_OS_MACOS
  // Native macOS look: aqua controls, system fonts, automatic light/dark.
  QQuickStyle::setStyle(QStringLiteral("macOS"));
#endif

  QQmlApplicationEngine engine;
  wirelab::WireLabViewModel viewModel;
  engine.rootContext()->setContextProperty("wirelab", &viewModel);
  engine.loadFromModule("WireLab", "Main");
  if (engine.rootObjects().isEmpty()) return 1;
  return application.exec();
}
