/*****************************************************************************
 * kfw_shell.cpp: Kishan Forensic Workbench ??? Qt application shell
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * KFW is an independent project and is not affiliated with or endorsed by
 * VideoLAN.
 *****************************************************************************/

#include "kfw_shell.hpp"

#include "kfw_video_screen.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QThread>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>

// Forensic layer (C API, wrapped in extern "C" guards)
extern "C" {
#include "../cases/kfw_db.h"
#include "../detect/kfw_detect.h"
#include "../evidence/kfw_evidence.h"
#include "../integrity/kfw_integrity.h"
#include "../reporting/kfw_report.h"
#include "../export/kfw_package.h"
#include "../timeline/kfw_timeline.h"
#include "../media/kfw_media.h"
}

// ---- Global per-process case database --------------------------------------

static kfw_db_t *s_db = nullptr;

/* Only one media probe may touch the engine at a time (see kfw_media.c). */
static QMutex s_probeLock;

// ---- Display helpers ---------------------------------------------------------

static QString fmt_epoch(int64_t t, bool withTime = true)
{
    if (t <= 0)
        return QStringLiteral("???");
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(t, QTimeZone::UTC);
    return withTime ? dt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                    : dt.toString(QStringLiteral("yyyy-MM-dd"));
}

static QString fmt_ms(int64_t ms)
{
    if (ms < 0)
        return QStringLiteral("???");
    const int64_t h = ms / 3600000;
    const int64_t m = (ms % 3600000) / 60000;
    const int64_t s = (ms % 60000) / 1000;
    const int64_t f = ms % 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(f, 2, 10, QLatin1Char('0'));
}

static void fourcc_to_str(uint32_t cc, char out[5])
{
    out[0] = (char)((cc >> 0) & 0xff);
    out[1] = (char)((cc >> 8) & 0xff);
    out[2] = (char)((cc >> 16) & 0xff);
    out[3] = (char)((cc >> 24) & 0xff);
    out[4] = '\0';
    for (int i = 0; i < 4; ++i)
        if (out[i] < 0x20 || out[i] > 0x7e)
            out[i] = '?';
}

static QString codec_display(const kfw_track_t *t)
{
    if (t->description && t->description[0])
        return QString::fromUtf8(t->description);
    char cc[5];
    fourcc_to_str(t->codec, cc);
    return QString::fromLatin1(cc);
}

// Portable layout: when a `portable.ini` marker sits next to the executable,
// all user data (cases, evidence store, reports, exports) lives under
// `<appdir>/data` instead of the per-user AppData location.
static QString kfwDataDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    if (QFileInfo::exists(appDir + QStringLiteral("/portable.ini")))
        return appDir + QStringLiteral("/data");
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

// ---- Live VLC trampolines (decoder thread) ----------------------------------

static void vlc_frame_trampoline(void *user, int64_t ts_ms,
                                 unsigned w, unsigned h,
                                 const uint8_t *bgra, size_t pitch,
                                 size_t lines)
{
    static_cast<KFWShell *>(user)->onVlcFrame(ts_ms, w, h, bgra, pitch, lines);
}

/* ---- Construction / destruction ---------------------------------------------- */

KFWShell::KFWShell(QObject *parent)
    : QObject(parent)
    , m_engine(new QQmlApplicationEngine(this))
    , m_caseModel(new KFWCaseModel(this))
    , m_evidenceModel(new KFWEvidenceModel(this))
    , m_timelineModel(new KFWTimelineModel(this))
{
    m_pipelineTrace = !qEnvironmentVariable("KFW_PIPELINE_DEBUG").isEmpty();

    m_playPoll = new QTimer(this);
    m_playPoll->setInterval(40);
    connect(m_playPoll, &QTimer::timeout, this, &KFWShell::pollPlayback);

    const QString dataDir =
        kfwDataDir();
    QDir().mkpath(dataDir + QStringLiteral("/cases"));
    m_databasePath = dataDir + QStringLiteral("/cases/kfw.db");
    openOrCreateDatabase(m_databasePath);
}

KFWShell::~KFWShell()
{
    /* Destroy the engine FIRST: its bindings re-evaluate context properties
     * during teardown, which spams "Cannot read property of null" warnings
     * once the models exposed to QML are gone. */
    if (m_engine != nullptr)
    {
        delete m_engine;
        m_engine = nullptr;
    }

    teardownLivePlayback();

    /* Probe workers capture `this`; let them finish before we disappear. */
    QElapsedTimer waited;
    waited.start();
    while (m_probesRunning.load() > 0 && waited.elapsed() < 15000)
        QThread::msleep(20);

    kfw_media_shutdown();

    if (s_db)
    {
        kfw_db_close(s_db);
        s_db = nullptr;
    }
}

bool KFWShell::openOrCreateDatabase(const QString &dbPath)
{
    if (s_db)
    {
        kfw_db_close(s_db);
        s_db = nullptr;
    }
    s_db = kfw_db_open(dbPath.toUtf8().constData());
    if (!s_db)
    {
        emit errorOccurred(QStringLiteral("Failed to open case database at: ")
                           + dbPath);
        return false;
    }
    m_databasePath = dbPath;
    emit databasePathChanged();
    refreshAuditChain();
    return true;
}

bool KFWShell::init(const QString &qmlRootDir)
{
    Q_UNUSED(qmlRootDir)

    /* Deployed QML plugins (windeployqt layout) live in <appdir>/qml; add
     * it explicitly so imports resolve without a qt.conf next to the exe. */
    m_engine->addImportPath(
        QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    /* The app's own QML module is embedded at prefix /KFW; register the
     * resource root so the engine finds :/KFW/qmldir regardless of the
     * working directory (portable layouts). */
    m_engine->addImportPath(QStringLiteral(":/"));

    QQmlContext *ctx = m_engine->rootContext();
    ctx->setContextProperty(QStringLiteral("kfwShell"), this);
    ctx->setContextProperty(QStringLiteral("kfwCaseModel"),     m_caseModel);
    ctx->setContextProperty(QStringLiteral("kfwEvidenceModel"), m_evidenceModel);
    ctx->setContextProperty(QStringLiteral("kfwTimelineModel"), m_timelineModel);

    qmlRegisterType<KFWVideoScreen>("KFW.Video", 1, 0, "KFWVideoScreen");

    /* Expose the theme singleton through an unqualified context property so
     * QML files can write `theme.accent` (and friends) directly. */
    {
        QQmlComponent themeComponent(m_engine,
            QUrl(QStringLiteral("qrc:/KFW/ui/qml/KFWTheme.qml")));
        if (themeComponent.isReady())
        {
            QObject *themeInst = themeComponent.create();
            if (themeInst)
            {
                themeInst->setParent(this);
                ctx->setContextProperty(QStringLiteral("theme"), themeInst);
            }
        }
        else
        {
            qWarning("KFWShell: theme singleton failed to load: %s",
                     qPrintable(themeComponent.errorString()));
        }
    }

    /* Load the shell screen from the embedded QML module (URI "KFW") */
    m_engine->loadFromModule(QStringLiteral("KFW"), QStringLiteral("KFWAppShell"));

    if (m_engine->rootObjects().isEmpty())
    {
        qCritical("KFWShell: failed to load the QML application shell");
        return false;
    }

    reloadCases();

    /* Env-gated QA hook: KFW_SEED_DEMO=<media path> feeds the file through
     * the real evidence pipeline into a scratch case and opens it in the
     * workspace ??? lets runtime QA exercise playback + live analysis without
     * manual UI interaction. Dormant unless the variable is set. */
    const QString seedPath = qEnvironmentVariable("KFW_SEED_DEMO");
    if (!seedPath.isEmpty() && QFileInfo::exists(seedPath))
    {
        kfw_db_case_t c;
        memset(&c, 0, sizeof(c));
        snprintf(c.title, sizeof(c.title), "Playback QA");
        snprintf(c.examiner, sizeof(c.examiner), "%s",
                 m_examinerName.toUtf8().constData());
        snprintf(c.status, sizeof(c.status), "Open");

        const QString storeRoot =
            kfwDataDir()
            + QStringLiteral("/evidence");
        QDir().mkpath(storeRoot);

        kfw_evidence_ctx_t ectx = {
            s_db,
            storeRoot.toUtf8().constData(),
            m_examinerName.toUtf8().constData()
        };
        kfw_db_evidence_t ev;
        memset(&ev, 0, sizeof(ev));
        if (kfw_db_case_insert(s_db, &c) == 0
            && kfw_evidence_import(&ectx, c.case_id,
                                   seedPath.toUtf8().constData(),
                                   "Original", &ev) >= 0)
        {
            reloadCases();
            reloadTimeline(QString::fromUtf8(c.case_id));
            openEvidence(QString::fromUtf8(c.case_id),
                         QString::fromUtf8(ev.evidence_id));
        }
        refreshAuditChain();
    }

    /* Env-gated capture hook: KFW_SCREENSHOT=<file> renders one frame of the
     * shell (uses the persisted theme mode) and exits ??? used for theme QA.
     * KFW_SCREENSHOT_MS=<n> overrides the settle delay. */
    const QString shotPath = qEnvironmentVariable("KFW_SCREENSHOT");
    if (!shotPath.isEmpty())
    {
        QQuickWindow *win =
            qobject_cast<QQuickWindow *>(m_engine->rootObjects().constFirst());
        if (win)
        {
            win->show();
            int delay = qEnvironmentVariableIntValue("KFW_SCREENSHOT_MS");
            if (delay <= 0)
                delay = 1800;
            QTimer::singleShot(delay, win, [win, shotPath]()
            {
                const QImage img = win->grabWindow();
                img.save(shotPath);
                QCoreApplication::exit(img.isNull() ? 2 : 0);
            });
        }
    }
    return true;
}

// ---- Properties -----------------------------------------------------------------

void KFWShell::setExaminerName(const QString &name)
{
    if (m_examinerName == name)
        return;
    m_examinerName = name;
    emit examinerNameChanged();
}

QString KFWShell::engineVersion() const
{
    return QString::fromUtf8(kfw_media_engine_version());
}

void KFWShell::refreshAuditChain()
{
    const bool ok = s_db && (kfw_db_audit_verify_chain(s_db, 0) == 0);
    if (ok != m_auditChainOk)
    {
        m_auditChainOk = ok;
        emit auditChainChanged(ok);
    }
}

// ---- Data (re)loading -------------------------------------------------------------

void KFWShell::reloadCases()
{
    if (!s_db)
        return;

    QVariantList rows;
    kfw_db_case_t *arr = nullptr;
    int count = 0;
    if (kfw_db_case_list(s_db, &arr, &count) == 0)
    {
        for (int i = 0; i < count; ++i)
        {
            const kfw_db_case_t &c = arr[i];
            int evidence = 0, pending = 0;
            kfw_db_evidence_t *ev = nullptr;
            int evCount = 0;
            if (kfw_db_evidence_list(s_db, c.case_id, &ev, &evCount) == 0)
            {
                evidence = evCount;
                for (int j = 0; j < evCount; ++j)
                    if (QString::fromUtf8(ev[j].verify_status)
                            == QStringLiteral("Not checked"))
                        ++pending;
                free(ev);
            }

            QVariantMap row;
            row.insert(QStringLiteral("caseId"),        QString::fromUtf8(c.case_id));
            row.insert(QStringLiteral("title"),         QString::fromUtf8(c.title));
            row.insert(QStringLiteral("examiner"),      QString::fromUtf8(c.examiner));
            row.insert(QStringLiteral("status"),        QString::fromUtf8(c.status));
            row.insert(QStringLiteral("notes"),         QString::fromUtf8(c.notes));
            row.insert(QStringLiteral("created"),       fmt_epoch(c.created_utc));
            row.insert(QStringLiteral("modified"),      fmt_epoch(c.modified_utc));
            row.insert(QStringLiteral("evidenceCount"), evidence);
            row.insert(QStringLiteral("pendingVerify"), pending);
            rows.append(row);
        }
        free(arr);
    }

    m_caseModel->loadList(rows);
}

void KFWShell::reloadEvidence(const QString &caseId)
{
    if (!s_db || caseId.isEmpty())
    {
        m_evidenceModel->reload({});
        return;
    }

    QVariantList rows;
    kfw_db_evidence_t *arr = nullptr;
    int count = 0;
    if (kfw_db_evidence_list(s_db, caseId.toUtf8().constData(), &arr, &count) == 0)
    {
        for (int i = 0; i < count; ++i)
        {
            const kfw_db_evidence_t &e = arr[i];
            QVariantMap row;
            row.insert(QStringLiteral("evidenceId"), QString::fromUtf8(e.evidence_id));
            row.insert(QStringLiteral("filename"),   QString::fromUtf8(e.orig_filename));
            row.insert(QStringLiteral("fileType"),   QString::fromUtf8(e.file_type));
            row.insert(QStringLiteral("sizeBytes"),  (long long)e.size_bytes);
            row.insert(QStringLiteral("sha256"),     QString::fromUtf8(e.sha256));
            row.insert(QStringLiteral("storedPath"), QString::fromUtf8(e.stored_path));
            row.insert(QStringLiteral("origPath"),   QString::fromUtf8(e.orig_path));
            row.insert(QStringLiteral("provenance"), QString::fromUtf8(e.provenance));
            row.insert(QStringLiteral("verifyStatus"), QString::fromUtf8(e.verify_status));
            row.insert(QStringLiteral("duplicateOf"),  QString::fromUtf8(e.duplicate_of));
            row.insert(QStringLiteral("verifiedText"), fmt_epoch(e.verified_utc,
                                                                  true));
            row.insert(QStringLiteral("importedText"), fmt_epoch(e.imported_utc));
            rows.append(row);
        }
        free(arr);
    }

    m_evidenceModel->reload(rows);
}

void KFWShell::reloadTimeline(const QString &caseId)
{
    if (!s_db || caseId.isEmpty())
    {
        m_timelineModel->reload({});
        return;
    }

    QVariantList rows;
    kfw_db_event_t *arr = nullptr;
    int count = 0;
    if (kfw_db_event_list(s_db, caseId.toUtf8().constData(), &arr, &count) == 0)
    {
        for (int i = 0; i < count; ++i)
        {
            const kfw_db_event_t &e = arr[i];
            char ts[16];
            kfw_timeline_format_ts(e.ts_ms, ts, sizeof(ts));
            QVariantMap row;
            row.insert(QStringLiteral("eventId"),    QString::fromUtf8(e.event_id));
            row.insert(QStringLiteral("tsMs"),       (long long)e.ts_ms);
            row.insert(QStringLiteral("tsText"),     QString::fromUtf8(ts));
            row.insert(QStringLiteral("kind"),       QString::fromUtf8(e.kind));
            row.insert(QStringLiteral("label"),      QString::fromUtf8(e.label));
            row.insert(QStringLiteral("body"),       QString::fromUtf8(e.body));
            row.insert(QStringLiteral("evidenceId"), QString::fromUtf8(e.evidence_id));
            row.insert(QStringLiteral("created"),    fmt_epoch(e.created_utc));
            rows.append(row);
        }
        free(arr);
    }

    m_timelineModel->reload(rows);
}

// ---- Navigation --------------------------------------------------------------------

void KFWShell::showDashboard()
{
    if (m_currentScreen == QStringLiteral("Dashboard"))
        return;
    m_currentScreen = QStringLiteral("Dashboard");
    reloadCases();   /* refresh counts after workspace sessions */
    emit currentScreenChanged();
}

void KFWShell::openCase(const QString &caseId)
{
    ++m_switchGen;
    m_activeCaseId    = caseId;
    m_activeCaseTitle.clear();
    m_activeEvidenceId.clear();
    m_activeEvidenceName.clear();
    m_activeEvidenceSha.clear();
    m_activeEvidenceStatus = QStringLiteral("Not checked");
    m_activeEvidenceProvenance.clear();
    m_activePlayerUrl = QUrl();
    m_activeMediaInfo.clear();

    teardownLivePlayback();

    kfw_db_case_t c;
    if (s_db && kfw_db_case_get(s_db, caseId.toUtf8().constData(), &c) == 0)
        m_activeCaseTitle = QString::fromUtf8(c.title);

    if (s_db)
    {
        reloadEvidence(caseId);
        reloadTimeline(caseId);
    }

    emit activeCaseChanged();
    emit activeEvidenceChanged();
    emit mediaInfoChanged();

    m_currentScreen = QStringLiteral("Workspace");
    emit currentScreenChanged();
}

void KFWShell::openEvidence(const QString &caseId, const QString &evidenceId)
{
    /* Switching between evidence items of the case that is already open must
     * NOT rebuild the case: openCase() resets the evidence/timeline models,
     * which destroys the very list delegate that is handling the click, and
     * re-emits the screen change. Only swap the evidence and the player. */
    const bool sameCase = !caseId.isEmpty() && caseId == m_activeCaseId
                          && m_currentScreen == QStringLiteral("Workspace");

    if (!sameCase)
        openCase(caseId);
    if (evidenceId.isEmpty() || !s_db)
        return;
    if (sameCase && evidenceId == m_activeEvidenceId && m_player != nullptr)
        return;   /* already open and playing */

    kfw_db_evidence_t ev;
    if (kfw_db_evidence_get(s_db, evidenceId.toUtf8().constData(), &ev) != 0)
        return;

    if (sameCase)
    {
        ++m_switchGen;
        teardownLivePlayback();
        m_activeMediaInfo.clear();
        emit mediaInfoChanged();
    }

    m_activeEvidenceId         = evidenceId;
    m_activeEvidenceName       = QString::fromUtf8(ev.orig_filename);
    m_activeEvidenceSha        = QString::fromUtf8(ev.sha256);
    m_activeEvidenceStatus     = QString::fromUtf8(ev.verify_status);
    m_activeEvidenceProvenance = QString::fromUtf8(ev.provenance);
    const QString stored = QString::fromUtf8(ev.stored_path);
    m_activePlayerUrl = QFileInfo(stored).exists()
        ? QUrl::fromLocalFile(stored) : QUrl();

    emit activeEvidenceChanged();
    probeEvidenceMedia(evidenceId);
}

// ---- Case management -----------------------------------------------------------------

QString KFWShell::createCase(const QString &title, const QString &examiner)
{
    return createCaseEx(title, examiner, QString(), QString(), QString());
}

QString KFWShell::createCaseEx(const QString &title, const QString &examiner,
                               const QString &caseNumber, const QString &agency,
                               const QString &requester)
{
    if (!s_db || title.trimmed().isEmpty())
        return {};

    kfw_db_case_t c;
    memset(&c, 0, sizeof(c));
    snprintf(c.title,    sizeof(c.title),    "%s", title.toUtf8().constData());
    snprintf(c.examiner, sizeof(c.examiner), "%s",
             (examiner.isEmpty() ? m_examinerName : examiner).toUtf8().constData());
    snprintf(c.status,   sizeof(c.status),   "Open");
    snprintf(c.case_number, sizeof(c.case_number), "%s",
             caseNumber.trimmed().toUtf8().constData());
    snprintf(c.agency,      sizeof(c.agency),      "%s",
             agency.trimmed().toUtf8().constData());
    snprintf(c.requester,   sizeof(c.requester),   "%s",
             requester.trimmed().toUtf8().constData());

    if (kfw_db_case_insert(s_db, &c) != 0)
    {
        emit errorOccurred(QStringLiteral("Failed to create case in database."));
        return {};
    }

    const QString caseId = QString::fromUtf8(c.case_id);
    reloadCases();
    emit caseCreated(caseId);
    emit notice(QStringLiteral("Case %1 created").arg(caseId));
    refreshAuditChain();
    return caseId;
}

QVariantMap KFWShell::caseDetails(const QString &caseId)
{
    QVariantMap m;
    kfw_db_case_t c;
    if (!s_db || kfw_db_case_get(s_db, caseId.toUtf8().constData(), &c) != 0)
        return m;
    auto u = [](const char *p) { return QString::fromUtf8(p); };
    m.insert(QStringLiteral("caseId"),       u(c.case_id));
    m.insert(QStringLiteral("title"),        u(c.title));
    m.insert(QStringLiteral("examiner"),     u(c.examiner));
    m.insert(QStringLiteral("status"),       u(c.status));
    m.insert(QStringLiteral("caseNumber"),   u(c.case_number));
    m.insert(QStringLiteral("agency"),       u(c.agency));
    m.insert(QStringLiteral("requester"),    u(c.requester));
    m.insert(QStringLiteral("incidentDate"), u(c.incident_date));
    m.insert(QStringLiteral("location"),     u(c.location));
    m.insert(QStringLiteral("description"),  u(c.description));
    m.insert(QStringLiteral("created"),
             QDateTime::fromSecsSinceEpoch(c.created_utc, QTimeZone::UTC)
                 .toString(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'")));
    return m;
}

bool KFWShell::saveCaseDetails(const QString &caseId, const QVariantMap &d)
{
    if (!s_db)
        return false;
    kfw_db_case_t m;
    memset(&m, 0, sizeof(m));
    auto put = [&d](char *dst, size_t n, const char *key) {
        snprintf(dst, n, "%s",
                 d.value(QString::fromLatin1(key)).toString().trimmed().toUtf8().constData());
    };
    put(m.case_number,   sizeof(m.case_number),   "caseNumber");
    put(m.agency,        sizeof(m.agency),        "agency");
    put(m.requester,     sizeof(m.requester),     "requester");
    put(m.incident_date, sizeof(m.incident_date), "incidentDate");
    put(m.location,      sizeof(m.location),      "location");
    put(m.description,   sizeof(m.description),   "description");

    if (kfw_db_case_update_meta(s_db, caseId.toUtf8().constData(), &m,
                                m_examinerName.toUtf8().constData()) != 0)
    {
        emit errorOccurred(QStringLiteral("Could not save the case details."));
        return false;
    }
    reloadCases();
    refreshAuditChain();
    emit notice(QStringLiteral("Case details saved"));
    return true;
}

void KFWShell::setCaseStatus(const QString &caseId, const QString &status)
{
    if (!s_db)
        return;

    /* Preserve existing notes ??? case_update overwrites them. */
    kfw_db_case_t c;
    if (kfw_db_case_get(s_db, caseId.toUtf8().constData(), &c) != 0)
        return;

    if (kfw_db_case_update(s_db, caseId.toUtf8().constData(),
                            status.toUtf8().constData(), c.notes) != 0)
    {
        emit errorOccurred(QStringLiteral("Could not update case %1").arg(caseId));
        return;
    }

    m_caseModel->setStatus(caseId, status);
    refreshAuditChain();
    emit notice(QStringLiteral("%1 marked %2").arg(caseId, status));
}

// ---- Evidence import (worker thread) ---------------------------------------------------

void KFWShell::importEvidence(const QString &caseId, const QString &filePath)
{
    if (!s_db)
    {
        emit errorOccurred(QStringLiteral("No case database open."));
        return;
    }

    const QByteArray caseIdBytes  = caseId.toUtf8();
    const QByteArray filePathBytes = filePath.toUtf8();
    const QByteArray examinerBytes = m_examinerName.toUtf8();
    const QString    storeRoot =
        kfwDataDir()
        + QStringLiteral("/evidence");
    QDir().mkpath(storeRoot);
    const QByteArray storeRootBytes = storeRoot.toUtf8();

    QThread *worker = QThread::create([=]()
    {
        kfw_evidence_ctx_t ctx = {
            s_db,
            storeRootBytes.constData(),
            examinerBytes.constData()
        };
        kfw_db_evidence_t ev;
        memset(&ev, 0, sizeof(ev));
        int rc = kfw_evidence_import(&ctx,
                                      caseIdBytes.constData(),
                                      filePathBytes.constData(),
                                      "Original", &ev);

        const QString evId  = QString::fromUtf8(ev.evidence_id);
        const QString sha   = QString::fromUtf8(ev.sha256);
        const QString vstus = QString::fromUtf8(ev.verify_status);
        const QString dupOf = QString::fromUtf8(ev.duplicate_of);

        QMetaObject::invokeMethod(this, [=]()
        {
            if (rc == 0)
            {
                emit evidenceImported(evId, vstus, sha);
                emit notice(QStringLiteral("Imported %1 ??? SHA-256 %2")
                                .arg(evId, vstus));
                if (!dupOf.isEmpty())
                    emit errorOccurred(QStringLiteral(
                        "%1 is byte-identical to %2 (same SHA-256). "
                        "Both were kept; check you did not import the same "
                        "footage twice.").arg(evId, dupOf));
            }
            else if (rc == KFW_ERR_DIGEST_MISMATCH)
            {
                emit evidenceImported(evId, vstus, sha);
                emit errorOccurred(QStringLiteral(
                    "Imported %1, but the copy digest differs from the source."
                    " The copy was flagged; re-import from the original.")
                    .arg(evId));
            }
            else
            {
                emit errorOccurred(
                    QStringLiteral("Evidence import failed (code %1).").arg(rc));
            }

            reloadCases();
            if (caseId == m_activeCaseId)
            {
                reloadEvidence(caseId);
                reloadTimeline(caseId);
            }
            refreshAuditChain();
        }, Qt::QueuedConnection);
    });

    connect(worker, &QThread::finished, worker, &QThread::deleteLater);
    worker->start();
}

// ---- Integrity re-verification (worker thread) ------------------------------------------

void KFWShell::reverifyEvidence(const QString &evidenceId)
{
    if (!s_db)
    {
        emit errorOccurred(QStringLiteral("No case database open."));
        return;
    }

    const QByteArray evIdBytes     = evidenceId.toUtf8();
    const QByteArray examinerBytes = m_examinerName.toUtf8();
    const QString    storeRoot =
        kfwDataDir()
        + QStringLiteral("/evidence");
    const QByteArray storeRootBytes = storeRoot.toUtf8();

    QThread *worker = QThread::create([=]()
    {
        kfw_evidence_ctx_t ctx = {
            s_db,
            storeRootBytes.constData(),
            examinerBytes.constData()
        };
        kfw_verify_status_t status = KFW_VERIFY_NOT_CHECKED;
        int rc = kfw_evidence_reverify(&ctx, evIdBytes.constData(), &status);

        const QString statusStr =
            QString::fromUtf8(kfw_verify_status_str(status));

        QMetaObject::invokeMethod(this, [=]()
        {
            if (rc == 0)
            {
                emit evidenceVerified(evidenceId, statusStr);
                m_activeEvidenceStatus = statusStr;
                emit activeEvidenceChanged();
                m_evidenceModel->setVerifyStatus(evidenceId, statusStr);
                reloadCases();
                if (!m_activeCaseId.isEmpty())
                    reloadTimeline(m_activeCaseId);
            }
            else
            {
                emit errorOccurred(QStringLiteral(
                    "Re-verify failed for %1 ??? the stored evidence file was"
                    " not found.").arg(evidenceId));
            }
            refreshAuditChain();
        }, Qt::QueuedConnection);
    });

    connect(worker, &QThread::finished, worker, &QThread::deleteLater);
    worker->start();
}

// ---- Media probe (worker thread) ----------------------------------------------------------

void KFWShell::probeEvidenceMedia(const QString &evidenceId)
{
    if (!s_db)
        return;

    kfw_db_evidence_t ev;
    if (kfw_db_evidence_get(s_db, evidenceId.toUtf8().constData(), &ev) != 0)
        return;

    const QString stored   = QString::fromUtf8(ev.stored_path);
    const QString fileType = QString::fromUtf8(ev.file_type);
    if (!QFileInfo(stored).exists())
    {
        m_activeMediaInfo = QVariantMap{
            { QStringLiteral("available"), false },
            { QStringLiteral("error"),
              QStringLiteral("Stored file missing from the evidence store") } };
        emit mediaInfoChanged();
        return;
    }

    const QByteArray pathBytes = stored.toUtf8();
    const std::string evid = evidenceId.toStdString();
    const int gen = m_switchGen;

    ++m_probesRunning;
    QThread *worker = QThread::create([=]()
    {
        QMutexLocker probeGuard(&s_probeLock);   /* one probe at a time */
        QVariantMap info;
        info.insert(QStringLiteral("available"), false);

        qWarning("KFWShell: probe worker start for %s", pathBytes.constData());
        kfw_media_adapter_t *a = kfw_media_open(pathBytes.constData());
        if (a)
        {
            if (kfw_media_parse(a, 8000000) == 0)
            {
                const kfw_media_info_t *mi = kfw_media_get_info(a);
                if (mi)
                {
                    info.insert(QStringLiteral("available"), true);
                    info.insert(QStringLiteral("engine"),
                                QString::fromUtf8(kfw_media_engine_version()));
                    info.insert(QStringLiteral("container"),
                                fileType.toUpper());
                    info.insert(QStringLiteral("durationMs"),
                                (long long)(mi->duration_us > 0
                                    ? mi->duration_us / 1000 : -1));
                    info.insert(QStringLiteral("mrl"),
                                QString::fromUtf8(mi->mrl ? mi->mrl : ""));

                    long long w = 0, h = 0;
                    int64_t fpsNum = 0, fpsDen = 0;
                    QString vcodec, acodec;
                    int ch = 0, rate = 0;
                    unsigned bitrate = 0;
                    QVariantList tracks;

                    for (size_t i = 0; i < mi->n_tracks; ++i)
                    {
                        const kfw_track_t *t = &mi->tracks[i];
                        QVariantMap tm;
                        tm.insert(QStringLiteral("type"),
                                  t->track_type == 1 ? QStringLiteral("Video")
                                  : t->track_type == 0 ? QStringLiteral("Audio")
                                  : QStringLiteral("Text"));
                        tm.insert(QStringLiteral("codec"), codec_display(t));
                        tm.insert(QStringLiteral("language"),
                                  QString::fromUtf8(t->language ? t->language : ""));
                        tm.insert(QStringLiteral("bitrate"),
                                  (unsigned long long)t->bitrate);
                        if (t->track_type == 1)
                        {
                            w = t->width; h = t->height;
                            fpsNum = t->frame_rate_num;
                            fpsDen = t->frame_rate_den;
                            vcodec = codec_display(t);
                            bitrate = t->bitrate;
                            tm.insert(QStringLiteral("resolution"),
                                QStringLiteral("%1x%2").arg(t->width,
                                                            t->height));
                        }
                        else if (t->track_type == 0)
                        {
                            ch = (int)t->channels;
                            rate = (int)t->sample_rate;
                            if (acodec.isEmpty())
                                acodec = codec_display(t);
                            tm.insert(QStringLiteral("audio"),
                                QStringLiteral("%1 ch ?? %2 Hz")
                                    .arg(t->channels).arg(t->sample_rate));
                        }
                        tracks.append(tm);
                    }

                    info.insert(QStringLiteral("width"),  w);
                    info.insert(QStringLiteral("height"), h);
                    info.insert(QStringLiteral("videoCodec"), vcodec);
                    info.insert(QStringLiteral("audioCodec"), acodec);
                    info.insert(QStringLiteral("audioChannels"), ch);
                    info.insert(QStringLiteral("audioRate"), rate);
                    info.insert(QStringLiteral("videoBitrate"),
                                (unsigned long long)bitrate);
                    QString fps = QStringLiteral("???");
                    if (fpsDen > 0)
                        fps = QString::number((double)fpsNum / fpsDen, 'f', 3);
                    else if (fpsNum > 0)
                        fps = QString::number((double)fpsNum, 'f', 3);
                    info.insert(QStringLiteral("fps"), fps);
                    info.insert(QStringLiteral("tracks"), tracks);
                }
            }
            kfw_media_close(a);
        }
        else
        {
            info.insert(QStringLiteral("error"),
                        QStringLiteral("Media engine failed to open the file"));
        }
        qWarning("KFWShell: probe worker done ??? available=%s",
                 info.value(QStringLiteral("available")).toBool()
                     ? "true" : "false");

        QMetaObject::invokeMethod(this, [=, info = std::move(info)]() mutable
        {
            if (gen == m_switchGen
                && evid == m_activeEvidenceId.toStdString())
            {
                m_activeMediaInfo = info;
                emit mediaInfoChanged();
                emit mediaInfoReady();
                qWarning("KFWShell: probe finished ??? available=%s durationMs=%s",
                         info.value(QStringLiteral("available")).toBool()
                             ? "true" : "false",
                         info.value(QStringLiteral("durationMs")).toString()
                             .toUtf8().constData());
                setupLivePlayback();   /* engine ready ??? start the live player */
            }
        }, Qt::QueuedConnection);
        --m_probesRunning;
    });

    connect(worker, &QThread::finished, worker, &QThread::deleteLater);
    worker->start();
}

// ---- Timeline markers / notes ---------------------------------------------------------------

void KFWShell::addTimelineMarker(const QString &caseId,
                                  const QString &evidenceId,
                                  double          timestampMs,
                                  const QString &label)
{
    if (!s_db)
        return;
    kfw_timeline_add(s_db,
                      caseId.toUtf8().constData(),
                      evidenceId.toUtf8().constData(),
                      static_cast<int64_t>(timestampMs),
                      KFW_TIMELINE_MARKER,
                      label.isEmpty() ? "Marker" : label.toUtf8().constData(),
                      nullptr,
                      nullptr);
    reloadTimeline(caseId);
    refreshAuditChain();
    emit notice(QStringLiteral("Marker added at %1").arg(
                    fmt_ms(static_cast<int64_t>(timestampMs))));
}

void KFWShell::saveEvidenceNotes(const QString &evidenceId, const QString &notes)
{
    if (!s_db || notes.trimmed().isEmpty() || m_activeCaseId.isEmpty())
        return;

    kfw_timeline_add(s_db,
                      m_activeCaseId.toUtf8().constData(),
                      evidenceId.toUtf8().constData(),
                      0,
                      KFW_TIMELINE_NOTE,
                      "Examiner notes",
                      notes.toUtf8().constData(),
                      nullptr);
    reloadTimeline(m_activeCaseId);
    refreshAuditChain();
    emit notice(QStringLiteral("Notes recorded on the investigation timeline"));
}

// ---- Report generation (worker thread) ----------------------------------------------------

void KFWShell::generateReport(const QString &caseId,
                               const QString &reportId,
                               const QString &examinerSummary)
{
    if (!s_db)
    {
        emit errorOccurred(QStringLiteral("No case database open."));
        return;
    }

    const QString outDir =
        kfwDataDir()
        + QStringLiteral("/reports");
    QDir().mkpath(outDir);

    const QByteArray caseIdBytes    = caseId.toUtf8();
    const QByteArray reportIdBytes  = reportId.toUtf8();
    const QByteArray summaryBytes   = examinerSummary.toUtf8();
    const QByteArray examinerBytes  = m_examinerName.toUtf8();
    const QByteArray outDirBytes    = outDir.toUtf8();
    const char *vlcVer              = kfw_media_engine_version();

    QThread *worker = QThread::create([=]()
    {
        kfw_report_params_t params = {};
        params.case_id          = caseIdBytes.constData();
        params.out_dir          = outDirBytes.constData();
        params.report_id        = reportIdBytes.constData();
        params.examiner         = examinerBytes.constData();
        params.tool_name        = "Kishan Forensic Workbench";
        params.tool_version     = "0.1.0-alpha";
        params.vlc_version      = vlcVer;
        params.examiner_summary = summaryBytes.constData();

        kfw_report_output_t output = {};
        int rc = kfw_report_generate(s_db, &params, &output);

        const QString jsonPath  = QString::fromUtf8(output.json_path  ? output.json_path  : "");
        const QString htmlPath  = QString::fromUtf8(output.html_path  ? output.html_path  : "");
        const QString jsonSha   = QString::fromUtf8(output.json_sha256 ? output.json_sha256 : "");
        kfw_report_output_clear(&output);

        QMetaObject::invokeMethod(this, [=]()
        {
            if (rc == 0)
            {
                emit reportGenerated(jsonPath, htmlPath, jsonSha);
                emit notice(QStringLiteral("Report package written"));
            }
            else
            {
                emit errorOccurred(
                    QStringLiteral("Report generation failed (code %1).").arg(rc));
            }
        }, Qt::QueuedConnection);
    });

    connect(worker, &QThread::finished, worker, &QThread::deleteLater);
    worker->start();
}

// ---- Desktop integration ----------------------------------------------------------------------

void KFWShell::revealPath(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.isDir())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.path()));
}

/* =============================================================================
   LIVE VLC PLAYBACK + LIVE ANALYSIS

   The media adapter owns a libVLC media player wired to RGBA callback taps
   (kfw_player).  Every decoded frame lands here on the decoder thread:

     1. the detector set runs inline (cheap luminance-grid statistics) and
        fires indicator callbacks, and
     2. the frame is copied into the thread-safe latest-frame store, from
        which KFWVideoScreen paints and frame export snapshots.

   Indicators are queued to the GUI thread and auto-recorded to the
   investigation timeline ("indicators, not verdicts").
   ========================================================================== */

static void detect_trampoline(void *user, int64_t ts_ms,
                              const char *kind, const char *detail)
{
    static_cast<KFWShell *>(user)->onDetectEvent(ts_ms, kind, detail);
}

void KFWShell::onVlcFrame(int64_t tsMs, unsigned w, unsigned h,
                          const uint8_t *bgra, size_t pitch, size_t lines)
{
    const bool traceFrames = m_pipelineTrace;
    if (traceFrames)
    {
        static qint64 lastTrace = -400;
        if (tsMs - lastTrace >= 400)
        {
            lastTrace = tsMs;
            unsigned long acc = 0;
            const unsigned sampleN = std::min<unsigned>(16, w);
            for (unsigned i = 0; i < sampleN; ++i)
                acc += bgra[i * 4 + 1]; /* G channel of row 0 */
            qWarning("KFWShell: frame ts=%lld %ux%u meanG=%lu",
                     (long long)tsMs, w, h, acc / sampleN);
        }
    }

    if (m_detector != nullptr)
    {
        /* Reset requested by a seek on the GUI thread: apply it here, on the
         * thread that owns the detector state, instead of racing it. */
        if (m_detectorResetPending.exchange(false))
            kfw_detect_reset(m_detector);
        kfw_detect_frame(m_detector, tsMs, w, h, bgra, pitch, lines,
                         detect_trampoline, this);
    }

    {
        static bool dumped = false;
        const QString dumpPath =
            qEnvironmentVariable("KFW_FRAME_DUMP");
        if (!dumped && !dumpPath.isEmpty() && tsMs >= 7000)
        {
            dumped = true;
            QImage dbg(int(w), int(h), QImage::Format_RGB32);
            for (unsigned y = 0; y < h; ++y)
                memcpy(dbg.scanLine(int(y)), bgra + (size_t(y) * pitch),
                       size_t(w) * 4);
            dbg.save(dumpPath);
            qWarning("KFWShell: frame dump saved at ts=%lld", (long long)tsMs);
        }
    }

    if (w == 0 || h == 0 || bgra == nullptr || pitch < size_t(w) * 4
        || lines < h)
        return;

    {
        QMutexLocker lock(&m_frameLock);
        if (m_latestFrame.size() != QSize(int(w), int(h)))
            /* Format_RGB32: opaque. VLC 3's RV32 tap leaves the alpha byte
             * zero; ARGB32 would render the frame transparent (black). */
            m_latestFrame = QImage(int(w), int(h), QImage::Format_RGB32);
        for (unsigned y = 0; y < h; ++y)
            memcpy(m_latestFrame.scanLine(int(y)),
                   bgra + (size_t(y) * pitch), size_t(w) * 4);
        m_frameMs = tsMs;
    }

    if (!m_hasFrame)
    {
        m_hasFrame = true;
        qWarning("KFWShell: first VLC frame %ux%u at %lld ms",
              w, h, (long long)tsMs);
        /* Rare transition ??? safe to notify from the decoder thread (queued). */
        emit playbackChanged();
    }

    QMetaObject::invokeMethod(this, &KFWShell::pumpFrame,
                              Qt::QueuedConnection);
}

void KFWShell::pumpFrame()
{
    if (m_videoScreen != nullptr)
        m_videoScreen->update();
}

QImage KFWShell::frameSnapshot() const
{
    QMutexLocker lock(&m_frameLock);
    if (m_latestFrame.isNull())
        return QImage();
    return m_latestFrame.copy();
}

QSize KFWShell::liveVideoSize() const
{
    QMutexLocker lock(&m_frameLock);
    return m_latestFrame.isNull() ? QSize() : m_latestFrame.size();
}

void KFWShell::onDetectEvent(int64_t tsMs, const char *kind,
                             const char *detail)
{
    const QString k = QString::fromUtf8(kind);
    const QString d = QString::fromUtf8(detail != nullptr ? detail : "");
    QMetaObject::invokeMethod(this, [this, tsMs, k, d]()
    {
        recordDetection(tsMs, k, d);
    }, Qt::QueuedConnection);
}

void KFWShell::recordDetection(int64_t tsMs, const QString &kind,
                               const QString &detail)
{
    if (!s_db || m_activeCaseId.isEmpty() || m_activeEvidenceId.isEmpty())
        return;

    /* Rate guard: the detector is per-run throttled already; this catches a
     * storm after seeks/resets so the timeline stays readable. */
    const int64_t now = (int64_t)QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastIndicatorMs < 150 && m_lastIndicatorKind == kind)
        return;
    m_lastIndicatorMs = now;
    m_lastIndicatorKind = kind;

    static const QHash<QString, QString> labels = {
        { QStringLiteral("BlackFrame"),
          QStringLiteral("Black frame detected") },
        { QStringLiteral("Freeze"),
          QStringLiteral("Frozen video sequence") },
        { QStringLiteral("Flicker"),
          QStringLiteral("Luminance jump (splice / interference hint)") },
        { QStringLiteral("RapidCuts"),
          QStringLiteral("Rapid scene-change cluster") },
    };

    const QString label = labels.value(kind, kind);
    kfw_timeline_add(s_db,
                      m_activeCaseId.toUtf8().constData(),
                      m_activeEvidenceId.toUtf8().constData(),
                      tsMs,
                      kind.toUtf8().constData(),
                      label.toUtf8().constData(),
                      detail.isEmpty() ? nullptr
                                       : detail.toUtf8().constData(),
                      nullptr);

    m_indicatorCounts.insert(
        kind, m_indicatorCounts.value(kind, 0).toInt() + 1);
    emit indicatorsChanged();

    reloadTimeline(m_activeCaseId);
    refreshAuditChain();
}

void KFWShell::setupLivePlayback()
{
    if (m_player != nullptr)
        return;
    if (!m_activePlayerUrl.isValid() || m_activePlayerUrl.isEmpty())
        return;

    const QString path = m_activePlayerUrl.toLocalFile();
    if (path.isEmpty() || !QFileInfo(path).exists())
        return;

    /* fps hint for run-length gates, from the media probe. */
    float fps = 25.0f;
    const QString fpsStr = m_activeMediaInfo
        .value(QStringLiteral("fps")).toString();
    bool fpsOk = false;
    const double fpsVal = fpsStr.toDouble(&fpsOk);
    if (fpsOk && fpsVal > 1.0 && fpsVal <= 240.0)
        fps = float(fpsVal);

    m_detector = kfw_detect_create(fps);
    m_player = kfw_player_create(path.toUtf8().constData(),
                                 vlc_frame_trampoline, this);
    if (m_player == nullptr)
    {
        qWarning("KFWShell: kfw_player_create failed for %s",
                 path.toUtf8().constData());
        kfw_detect_destroy(m_detector);
        m_detector = nullptr;
        emit notice(QStringLiteral(
            "Live playback unavailable ??? evidence remains open for analysis"));
        return;
    }
    qWarning("KFWShell: live player created (fps hint %.3f), starting playback",
          double(fps));

    m_hasFrame   = false;
    m_playing    = false;
    m_positionMs = 0;
    m_durationMs = kfw_player_length_ms(m_player);
    m_rate       = 1.0;
    m_lastIndicatorMs = 0;
    m_lastIndicatorKind.clear();

    emit playbackChanged();
    emit positionChanged();

    m_detectorResetPending = false;
    m_trackRetries = 0;
    kfw_player_set_volume(m_player, m_volume);
    kfw_player_set_mute(m_player, m_muted ? 1 : 0);

    /* Live engine: start immediately so indicators run from t=0. */
    kfw_player_play(m_player);
    m_playPoll->start(40);
}

void KFWShell::teardownLivePlayback()
{
    const bool trace = m_pipelineTrace;
    const qint64 t0 = trace ? QDateTime::currentMSecsSinceEpoch() : 0;
    if (trace)
        qWarning("KFWShell: teardown enter (player=%p, poll=%d)",
                 (void *)m_player, m_playPoll ? 1 : 0);

    if (m_playPoll != nullptr)
        m_playPoll->stop();

    /* Destroy the player first: stop() joins the decoder thread, so no frame
     * callbacks can touch m_detector after it is destroyed. */
    if (m_player != nullptr)
    {
        kfw_player_destroy(m_player);
        m_player = nullptr;
        if (trace)
            qWarning("KFWShell: teardown player destroyed (+%lld ms)",
                     QDateTime::currentMSecsSinceEpoch() - t0);
    }
    if (m_detector != nullptr)
    {
        kfw_detect_destroy(m_detector);
        m_detector = nullptr;
    }

    {
        QMutexLocker lock(&m_frameLock);
        m_latestFrame = QImage();
        m_frameMs = -1;
    }

    m_hasFrame = false;
    m_playing  = false;
    m_positionMs = 0;
    m_durationMs = -1;
    m_rate       = 1.0;
    clearTracks();
    if (!m_indicatorCounts.isEmpty())
    {
        m_indicatorCounts.clear();
        emit indicatorsChanged();
    }
    emit playbackChanged();
    emit positionChanged();
}

void KFWShell::pollPlayback()
{
    if (m_player == nullptr)
        return;

    const kfw_player_state_t st = kfw_player_state(m_player);
    const bool playing  = (st == KFW_PLAYER_PLAYING);
    const int64_t pos   = kfw_player_time_ms(m_player);
    const int64_t len   = kfw_player_length_ms(m_player);

    const bool prevPlaying = m_playing;
    bool structural = false;

    if (pos >= 0 && static_cast<qlonglong>(pos) != m_positionMs)
    {
        m_positionMs = static_cast<qlonglong>(pos);
        emit positionChanged();
    }
    if (static_cast<qlonglong>(len) != m_durationMs)
    {
        m_durationMs = static_cast<qlonglong>(len);
        structural = true;
    }
    if (playing != m_playing)
    {
        m_playing = playing;
        structural = true;
        if (!playing && st == KFW_PLAYER_ENDED && prevPlaying)
            emit notice(QStringLiteral("Playback finished"));
    }
    if (structural)
        emit playbackChanged();

    /* The engine discovers audio/subtitle streams lazily after playback
     * starts; poll a few times (about every 400 ms, for ~10 s) until known. */
    if ((playing || st == KFW_PLAYER_PAUSED)
        && m_audioTracks.size() <= 1 && m_trackRetries < 250)
    {
        if ((m_trackRetries++ % 10) == 0)
            refreshTracks();
    }
}

void KFWShell::playToggle()
{
    if (m_player == nullptr)
        return;
    if (m_playing)
        kfw_player_pause(m_player);
    else
        kfw_player_play(m_player);
    m_playing = (kfw_player_state(m_player) == KFW_PLAYER_PLAYING);
    emit playbackChanged();
}

void KFWShell::playerPlay()
{
    if (m_player == nullptr)
        return;
    kfw_player_play(m_player);
    m_playing = (kfw_player_state(m_player) == KFW_PLAYER_PLAYING);
    emit playbackChanged();
}

void KFWShell::playerPause()
{
    if (m_player == nullptr)
        return;
    kfw_player_pause(m_player);
    m_playing = (kfw_player_state(m_player) == KFW_PLAYER_PLAYING);
    emit playbackChanged();
}

void KFWShell::playerSeekMs(double ms)
{
    if (m_player == nullptr)
        return;
    qlonglong t = static_cast<qlonglong>(ms);
    if (t < 0)
        t = 0;
    if (m_durationMs > 0 && t > m_durationMs)
        t = m_durationMs;

    kfw_player_seek_ms(m_player, static_cast<int64_t>(t));
    m_detectorResetPending = true;   /* discontinuity: drop run state */

    m_positionMs = t;
    emit positionChanged();
}

void KFWShell::playerStepMs(double delta)
{
    if (m_player == nullptr)
        return;
    qlonglong t = m_positionMs + static_cast<qlonglong>(delta);
    if (t < 0)
        t = 0;
    if (m_durationMs > 0 && t > m_durationMs)
        t = m_durationMs;
    playerSeekMs(static_cast<double>(t));
}

void KFWShell::playerSetRate(double rate)
{
    if (m_player == nullptr)
        return;
    if (rate < 0.1)
        rate = 0.1;
    if (rate > 10.0)
        rate = 10.0;
    kfw_player_set_rate(m_player, static_cast<float>(rate));
    m_rate = kfw_player_rate(m_player);
    emit playbackChanged();
}

void KFWShell::clearTracks()
{
    const bool changed = !m_audioTracks.isEmpty() || !m_subtitleTracks.isEmpty()
                         || m_audioTrackId != -1 || m_subtitleTrackId != -1;
    m_audioTracks.clear();
    m_subtitleTracks.clear();
    m_audioTrackId    = -1;
    m_subtitleTrackId = -1;
    m_trackRetries    = 0;
    if (changed)
        emit tracksChanged();
}

static QVariantList track_list(const kfw_track_desc_t *buf, int n)
{
    QVariantList list;
    for (int i = 0; i < n; ++i)
    {
        QVariantMap m;
        m.insert(QStringLiteral("id"),   buf[i].id);
        m.insert(QStringLiteral("name"), QString::fromUtf8(buf[i].name));
        list.append(m);
    }
    return list;
}

void KFWShell::refreshTracks()
{
    if (m_player == nullptr)
        return;

    kfw_track_desc_t buf[32];
    const QVariantList audio =
        track_list(buf, kfw_player_audio_tracks(m_player, buf, 32));
    const int audioId = kfw_player_audio_track(m_player);
    const QVariantList subs =
        track_list(buf, kfw_player_spu_tracks(m_player, buf, 32));
    const int subId = kfw_player_spu_track(m_player);

    if (audio != m_audioTracks || subs != m_subtitleTracks
        || audioId != m_audioTrackId || subId != m_subtitleTrackId)
    {
        /* First time audio streams appear: (re)apply the examiner's volume,
         * the engine ignores it until an audio output exists. */
        if (m_audioTracks.isEmpty() && !audio.isEmpty())
        {
            kfw_player_set_volume(m_player, m_volume);
            kfw_player_set_mute(m_player, m_muted ? 1 : 0);
        }
        m_audioTracks     = audio;
        m_subtitleTracks  = subs;
        m_audioTrackId    = audioId;
        m_subtitleTrackId = subId;
        emit tracksChanged();
    }
}

static QString track_name(const QVariantList &list, int id)
{
    for (const QVariant &v : list)
    {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toInt() == id)
            return m.value(QStringLiteral("name")).toString();
    }
    return QString::number(id);
}

void KFWShell::setAudioTrack(int trackId)
{
    if (m_player == nullptr)
        return;
    if (kfw_player_set_audio_track(m_player, trackId) != 0)
    {
        emit errorOccurred(QStringLiteral("Could not switch the audio track."));
        return;
    }
    refreshTracks();
    const QString name = track_name(m_audioTracks, trackId);
    emit notice(QStringLiteral("Audio: %1").arg(name));

    /* Examiner actions that change what is heard belong in the audit chain. */
    if (s_db && !m_activeEvidenceId.isEmpty())
    {
        const QByteArray detail = QJsonDocument(QJsonObject{
            { QStringLiteral("track_id"), trackId },
            { QStringLiteral("track_name"), name } })
            .toJson(QJsonDocument::Compact);
        kfw_db_audit_append(s_db, m_examinerName.toUtf8().constData(),
                            "playback.audio_track", "evidence",
                            m_activeEvidenceId.toUtf8().constData(),
                            detail.constData());
        refreshAuditChain();
    }
}

void KFWShell::setSubtitleTrack(int trackId)
{
    if (m_player == nullptr)
        return;
    if (kfw_player_set_spu_track(m_player, trackId) != 0)
    {
        emit errorOccurred(QStringLiteral("Could not switch the subtitle track."));
        return;
    }
    refreshTracks();
    emit notice(QStringLiteral("Subtitles: %1")
                    .arg(track_name(m_subtitleTracks, trackId)));
}

void KFWShell::setVolume(int percent)
{
    percent = std::max(0, std::min(200, percent));
    m_volume = percent;
    if (m_player != nullptr)
        kfw_player_set_volume(m_player, percent);
    emit volumeChanged();
}

void KFWShell::toggleMute()
{
    m_muted = !m_muted;
    if (m_player != nullptr)
        kfw_player_set_mute(m_player, m_muted ? 1 : 0);
    emit volumeChanged();
}

// ---- Verify all / search / tags / handover packages ------------------------------------

static QString local_path(const QString &p)
{
    return p.startsWith(QStringLiteral("file:")) ? QUrl(p).toLocalFile() : p;
}

QString KFWShell::packagesDir()
{
    const QString dir = kfwDataDir() + QStringLiteral("/packages");
    QDir().mkpath(dir);
    return dir;
}

void KFWShell::verifyAllEvidence(const QString &caseId)
{
    if (!s_db)
        return;
    const QByteArray caseIdBytes  = caseId.toUtf8();
    const QByteArray examinerBytes = m_examinerName.toUtf8();
    const QString storeRoot = kfwDataDir() + QStringLiteral("/evidence");
    const QByteArray storeBytes = storeRoot.toUtf8();

    ++m_probesRunning;
    QThread *worker = QThread::create([=]()
    {
        kfw_evidence_ctx_t ctx = { s_db, storeBytes.constData(),
                                   examinerBytes.constData() };
        kfw_db_evidence_t *arr = nullptr;
        int n = 0;
        int okCount = 0, bad = 0, other = 0;
        if (kfw_db_evidence_list(s_db, caseIdBytes.constData(), &arr, &n) == 0)
        {
            for (int i = 0; i < n; ++i)
            {
                kfw_verify_status_t st = KFW_VERIFY_NOT_CHECKED;
                if (kfw_evidence_reverify(&ctx, arr[i].evidence_id, &st) == 0)
                {
                    if (st == KFW_VERIFY_VERIFIED)      ++okCount;
                    else if (st == KFW_VERIFY_MISMATCH) ++bad;
                    else                                ++other;
                }
                else
                    ++other;
            }
        }
        free(arr);

        QMetaObject::invokeMethod(this, [=]()
        {
            const QString summary = QStringLiteral(
                "Verified %1 of %2 item(s)").arg(okCount).arg(n);
            if (bad > 0 || other > 0)
                emit errorOccurred(QStringLiteral(
                    "%1. %2 changed since import, %3 missing or unreadable.")
                    .arg(summary).arg(bad).arg(other));
            else
                emit notice(summary + QStringLiteral(" - all match their import hashes"));
            if (caseId == m_activeCaseId)
                reloadEvidence(caseId);
            refreshAuditChain();
        }, Qt::QueuedConnection);
        --m_probesRunning;
    });
    connect(worker, &QThread::finished, worker, &QThread::deleteLater);
    worker->start();
}

QVariantList KFWShell::search(const QString &caseId, const QString &query)
{
    QVariantList out;
    if (!s_db)
        return out;
    kfw_db_hit_t *hits = nullptr;
    int n = 0;
    if (kfw_db_search(s_db, caseId.toUtf8().constData(),
                      query.toUtf8().constData(), &hits, &n) == 0)
    {
        for (int i = 0; i < n; ++i)
        {
            QVariantMap m;
            m.insert(QStringLiteral("kind"),       QString::fromUtf8(hits[i].kind));
            m.insert(QStringLiteral("evidenceId"), QString::fromUtf8(hits[i].evidence_id));
            m.insert(QStringLiteral("eventId"),    QString::fromUtf8(hits[i].event_id));
            m.insert(QStringLiteral("tsMs"),       static_cast<double>(hits[i].ts_ms));
            m.insert(QStringLiteral("title"),      QString::fromUtf8(hits[i].title));
            m.insert(QStringLiteral("snippet"),    QString::fromUtf8(hits[i].snippet));
            out.append(m);
        }
    }
    free(hits);
    return out;
}

bool KFWShell::addTag(const QString &itemType, const QString &itemId, const QString &tag)
{
    if (!s_db)
        return false;
    const bool ok = kfw_db_tag_apply(s_db, m_examinerName.toUtf8().constData(),
                                     itemType.toUtf8().constData(),
                                     itemId.toUtf8().constData(),
                                     tag.toUtf8().constData()) == 0;
    if (ok)
    {
        emit tagsChanged();
        refreshAuditChain();
    }
    return ok;
}

bool KFWShell::removeTag(const QString &itemType, const QString &itemId, const QString &tag)
{
    if (!s_db)
        return false;
    const bool ok = kfw_db_tag_remove(s_db, m_examinerName.toUtf8().constData(),
                                      itemType.toUtf8().constData(),
                                      itemId.toUtf8().constData(),
                                      tag.toUtf8().constData()) >= 0;
    if (ok)
    {
        emit tagsChanged();
        refreshAuditChain();
    }
    return ok;
}

QVariantList KFWShell::tagsFor(const QString &itemType, const QString &itemId)
{
    QVariantList out;
    if (!s_db)
        return out;
    kfw_db_tag_t *t = nullptr;
    int n = 0;
    if (kfw_db_tag_list_for(s_db, itemType.toUtf8().constData(),
                            itemId.toUtf8().constData(), &t, &n) == 0)
        for (int i = 0; i < n; ++i)
            out.append(QString::fromUtf8(t[i].name));
    free(t);
    return out;
}

QVariantList KFWShell::allTags()
{
    QVariantList out;
    if (!s_db)
        return out;
    kfw_db_tag_t *t = nullptr;
    int n = 0;
    if (kfw_db_tag_list_all(s_db, &t, &n) == 0)
    {
        for (int i = 0; i < n; ++i)
        {
            QVariantMap m;
            m.insert(QStringLiteral("name"),  QString::fromUtf8(t[i].name));
            m.insert(QStringLiteral("count"), t[i].use_count);
            out.append(m);
        }
    }
    free(t);
    return out;
}

void KFWShell::exportHandoverPackage(const QString &caseId, bool includeEvidence)
{
    if (!s_db)
    {
        emit errorOccurred(QStringLiteral("No case database open."));
        return;
    }
    const QByteArray caseIdBytes   = caseId.toUtf8();
    const QByteArray examinerBytes = m_examinerName.toUtf8();
    const QByteArray outBytes      = packagesDir().toUtf8();

    emit notice(QStringLiteral("Building handover package..."));
    ++m_probesRunning;
    QThread *worker = QThread::create([=]()
    {
        kfw_package_params_t p;
        memset(&p, 0, sizeof(p));
        p.db               = s_db;
        p.case_id          = caseIdBytes.constData();
        p.examiner         = examinerBytes.constData();
        p.out_dir          = outBytes.constData();
        p.tool_name        = "Kishan Forensic Workbench";
        p.tool_version     = "0.1.0-alpha";
        p.include_evidence = includeEvidence ? 1 : 0;

        kfw_package_result_t r;
        memset(&r, 0, sizeof(r));
        const int rc = kfw_package_create(&p, &r);

        QStringList warn;
        if (r.n_evidence_missing > 0)
            warn << QStringLiteral("%1 evidence file(s) missing from the store")
                        .arg(r.n_evidence_missing);
        if (r.n_evidence_mismatch > 0)
            warn << QStringLiteral("%1 evidence file(s) no longer match their import hash")
                        .arg(r.n_evidence_mismatch);
        if (!r.report_ok)
            warn << QStringLiteral("the report could not be generated");
        const QString dir = QString::fromUtf8(r.package_dir);
        const QString sha = QString::fromUtf8(r.manifest_sha256);
        const QString summary = QStringLiteral("%1 files, %2 evidence item(s) copied")
                                    .arg(r.n_files).arg(r.n_evidence_copied)
                                + (warn.isEmpty() ? QString()
                                   : QStringLiteral(". Warning: ") + warn.join(QStringLiteral("; ")));

        QMetaObject::invokeMethod(this, [=]()
        {
            if (rc < 0)
                emit errorOccurred(QStringLiteral(
                    "Could not create the handover package (code %1).").arg(rc));
            else
            {
                emit packageCreated(dir, sha, summary, rc == 1);
                emit notice(QStringLiteral("Handover package created"));
            }
            refreshAuditChain();
        }, Qt::QueuedConnection);
        --m_probesRunning;
    });
    connect(worker, &QThread::finished, worker, &QThread::deleteLater);
    worker->start();
}

void KFWShell::verifyHandoverPackage(const QString &folder)
{
    const QByteArray pathBytes = local_path(folder).toUtf8();
    ++m_probesRunning;
    QThread *worker = QThread::create([=]()
    {
        kfw_package_verify_result_t r;
        memset(&r, 0, sizeof(r));
        const int rc = kfw_package_verify(pathBytes.constData(), &r);

        QVariantMap m;
        m.insert(QStringLiteral("readable"),       rc >= 0);
        m.insert(QStringLiteral("ok"),             rc == 0);
        m.insert(QStringLiteral("manifestDigest"), r.manifest_digest);
        m.insert(QStringLiteral("files"),          r.n_files);
        m.insert(QStringLiteral("okCount"),        r.n_ok);
        m.insert(QStringLiteral("mismatch"),       r.n_mismatch);
        m.insert(QStringLiteral("missing"),        r.n_missing);
        m.insert(QStringLiteral("extra"),          r.n_extra);
        m.insert(QStringLiteral("manifestSha256"), QString::fromUtf8(r.manifest_sha256));
        m.insert(QStringLiteral("firstProblem"),   QString::fromUtf8(r.first_problem));

        QMetaObject::invokeMethod(this, [=]() { emit packageVerified(m); },
                                  Qt::QueuedConnection);
        --m_probesRunning;
    });
    connect(worker, &QThread::finished, worker, &QThread::deleteLater);
    worker->start();
}

void KFWShell::attachVideoScreen(QObject *item)
{
    if (KFWVideoScreen *scr = qobject_cast<KFWVideoScreen *>(item))
    {
        m_videoScreen = scr;
        scr->m_shell = this;   /* the item snapshots frames from this shell */
        if (m_pipelineTrace)
            qWarning("KFWShell: video screen attached %p", (void *)scr);
    }
}

void KFWShell::detachVideoScreen(QObject *item)
{
    if (m_videoScreen != nullptr
        && static_cast<QObject *>(m_videoScreen) == item)
    {
        m_videoScreen->m_shell = nullptr;
        m_videoScreen = nullptr;
    }
}

/* =============================================================================
   FRAME EXPORT ??? snapshot of the live VLC tap with provenance
   ========================================================================== */

void KFWShell::exportFrame(const QString &evidenceId, double timestampMs)
{
    if (!s_db)
        return;

    QImage img;
    qlonglong gotMs = -1;
    {
        QMutexLocker lock(&m_frameLock);
        if (!m_latestFrame.isNull())
        {
            img = m_latestFrame.copy();
            gotMs = m_frameMs;
        }
    }
    if (img.isNull())
    {
        emit errorOccurred(QStringLiteral(
            "No frame to export ??? open the evidence and pause on the frame"
            " you want to keep."));
        return;
    }

    kfw_db_evidence_t ev;
    if (kfw_db_evidence_get(s_db, evidenceId.toUtf8().constData(), &ev) != 0)
    {
        emit errorOccurred(QStringLiteral("Evidence %1 not found.").arg(evidenceId));
        return;
    }

    const QString caseId = QString::fromUtf8(ev.case_id);
    const QString outDir =
        kfwDataDir()
        + QStringLiteral("/exports/") + caseId;
    QDir().mkpath(outDir);

    const qlonglong reqMs = static_cast<qlonglong>(timestampMs);
    QString base = QStringLiteral("%1_frame_%2ms")
        .arg(evidenceId).arg(gotMs);
    QString outPath = outDir + QStringLiteral("/") + base
        + QStringLiteral(".png");
    int uniq = 1;
    while (QFileInfo::exists(outPath))
        outPath = outDir + QStringLiteral("/") + base
            + QStringLiteral("_%1.png").arg(uniq++);

    img.save(outPath, "PNG");

    /* Provenance sidecar next to the PNG */
    const QString metalistPath = outPath + QStringLiteral(".json");
    QJsonObject meta;
    meta.insert(QStringLiteral("evidence_id"), evidenceId);
    meta.insert(QStringLiteral("case_id"),     caseId);
    meta.insert(QStringLiteral("source_sha256"), QString::fromUtf8(ev.sha256));
    meta.insert(QStringLiteral("requested_ms"), (double)reqMs);
    meta.insert(QStringLiteral("actual_ms"),    (double)gotMs);
    meta.insert(QStringLiteral("delta_ms"),     (double)(gotMs - reqMs));
    meta.insert(QStringLiteral("width"),  img.width());
    meta.insert(QStringLiteral("height"), img.height());
    meta.insert(QStringLiteral("format"), QStringLiteral("PNG"));
    meta.insert(QStringLiteral("extractor"),
                QStringLiteral("libVLC live frame tap"));
    meta.insert(QStringLiteral("parse_engine"),
                QString::fromUtf8(kfw_media_engine_version()));
    meta.insert(QStringLiteral("tool"),
                QStringLiteral("Kishan Forensic Workbench 0.1.0-alpha"));
    meta.insert(QStringLiteral("created_utc"),
                fmt_epoch((int64_t)time(nullptr)));
    QJsonDocument doc(meta);
    QFile metaf(metalistPath);
    if (metaf.open(QIODevice::WriteOnly | QIODevice::Text))
        metaf.write(doc.toJson(QJsonDocument::Indented));
    metaf.close();

    /* Timeline records where the frame really is (actual pts of the pixmap). */
    char tbuf[16];
    kfw_timeline_format_ts(gotMs, tbuf, sizeof(tbuf));
    const QString label = QStringLiteral("Frame export %1").arg(QString::fromUtf8(tbuf));
    kfw_timeline_add(s_db,
                      caseId.toUtf8().constData(),
                      evidenceId.toUtf8().constData(),
                      gotMs,
                      KFW_TIMELINE_EXPORT,
                      label.toUtf8().constData(),
                      outPath.toUtf8().constData(),
                      nullptr);

    {
        char detail[512];
        snprintf(detail, sizeof(detail),
                 "{\"evidence_id\":\"%s\",\"requested_ms\":%lld,"
                 "\"actual_ms\":%lld,\"png\":\"%s\",\"sidecar\":\"%s\"}",
                 evidenceId.toUtf8().constData(),
                 (long long)reqMs, (long long)gotMs,
                 outPath.toUtf8().constData(),
                 metalistPath.toUtf8().constData());
        kfw_db_audit_append(s_db, m_examinerName.toUtf8().constData(),
                             "frame.export", "artifact",
                             evidenceId.toUtf8().constData(), detail);
    }

    if (!caseId.isEmpty())
        reloadTimeline(caseId);
    refreshAuditChain();

    emit frameExported(evidenceId, outPath, QString::number(gotMs));
    emit notice(QStringLiteral("Frame exported at %1 ??? %2")
                    .arg(fmt_ms(gotMs), outPath));
}
