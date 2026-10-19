/*****************************************************************************
 * kfw_main.cpp: Kishan Forensic Workbench — application entry point
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *****************************************************************************/

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTextStream>

#include "kfw_shell.hpp"

namespace
{
void kfw_file_logger(QtMsgType type, const QMessageLogContext &context,
                     const QString &message)
{
    static QFile logFile(qEnvironmentVariable("KFW_LOGFILE"));
    if (!logFile.isOpen())
    {
        if (logFile.fileName().isEmpty())
            return;
        logFile.open(QIODevice::WriteOnly | QIODevice::Append
                     | QIODevice::Text);
    }
    const char *level = type == QtDebugMsg   ? "debug"
                      : type == QtInfoMsg    ? "info"
                      : type == QtWarningMsg ? "warning"
                      : type == QtCriticalMsg ? "critical"
                      : "fatal";
    QTextStream(&logFile) << level << ": " << message << '\n';
    logFile.flush();
}
} // namespace

int main(int argc, char *argv[])
{
    /* High-DPI support */
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    /* KFW's theme paints every control itself; Basic honors the custom
     * slider/scrollbar delegates (the native Windows style refuses
     * customization and misbehaves inside ScrollViews). Must be set
     * before the QML engine loads. */
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    if (!qEnvironmentVariable("KFW_LOGFILE").isEmpty())
        qInstallMessageHandler(kfw_file_logger);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("KishanForensicWorkbench"));
    app.setApplicationDisplayName(QStringLiteral("Kishan Forensic Workbench"));
    app.setApplicationVersion(QStringLiteral("0.1.0-alpha"));
    app.setOrganizationName(QStringLiteral("KishFW"));
    app.setOrganizationDomain(QStringLiteral("kfw.local"));
    app.setWindowIcon(QIcon(QStringLiteral(":/ui/kfw.ico")));

    /* libVLC must find its plugin tree. The engine normally discovers it
     * relative to libvlc.dll, but that breaks whenever the player is loaded
     * from an alien directory (ctest, unusual deploys) — pin it explicitly
     * unless the caller already preset one. */
    {
        const QString pluginDir =
            app.applicationDirPath() + QStringLiteral("/plugins");
        if (QFileInfo::exists(pluginDir)
            && qEnvironmentVariableIsEmpty("VLC_PLUGIN_PATH"))
        {
            qputenv("VLC_PLUGIN_PATH",
                    QDir::toNativeSeparators(pluginDir).toUtf8());
        }
    }

    KFWShell shell;
    if (!shell.init())
    {
        qCritical("KFW: failed to initialise the QML application shell");
        return 1;
    }

    return app.exec();
}
