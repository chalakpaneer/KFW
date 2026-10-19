/*****************************************************************************
 * kfw_shell.hpp: Kishan Forensic Workbench — Qt application shell
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
 *
 * KFWShell is the top-level Qt object that:
 *   - Owns the QQmlApplicationEngine and exposes context properties:
 *       kfwShell, kfwCaseModel, kfwEvidenceModel, kfwTimelineModel
 *   - Bridges QML events to the C forensic services layer (cases, evidence,
 *     integrity, timeline, reporting, media adapter).
 *   - Reloads every list model straight from the SQLite case database so the
 *     UI never fabricates data.
 *   - Drives the live libVLC playback engine (media adapter) and pipes every
 *     decoded frame through the live-analysis detector set; indicators are
 *     auto-recorded to the investigation timeline.
 *   - Captures exported frames straight from the live VLC tap with provenance.
 *****************************************************************************/

#ifndef KFW_SHELL_HPP
#define KFW_SHELL_HPP

#include <cstdint>
#include <cstddef>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include "models/kfw_case_model.hpp"
#include "models/kfw_evidence_model.hpp"
#include "models/kfw_timeline_model.hpp"

class QTimer;
class KFWVideoScreen;
struct kfw_player;
struct kfw_detect;

class KFWShell : public QObject
{
    Q_OBJECT

    // ---- Shell state --------------------------------------------------------
    Q_PROPERTY(QString currentScreen   READ currentScreen NOTIFY currentScreenChanged)
    Q_PROPERTY(QString examinerName    READ examinerName  WRITE setExaminerName
               NOTIFY examinerNameChanged)
    Q_PROPERTY(QString databasePath    READ databasePath  NOTIFY databasePathChanged)
    Q_PROPERTY(QString appVersion      READ appVersion    CONSTANT)
    Q_PROPERTY(QString engineVersion   READ engineVersion CONSTANT)
    Q_PROPERTY(bool    auditChainOk    READ auditChainOk  NOTIFY auditChainChanged)

    // ---- Active case / evidence ---------------------------------------------
    Q_PROPERTY(QString activeCaseId      READ activeCaseId      NOTIFY activeCaseChanged)
    Q_PROPERTY(QString activeCaseTitle   READ activeCaseTitle   NOTIFY activeCaseChanged)
    Q_PROPERTY(QString activeEvidenceId  READ activeEvidenceId  NOTIFY activeEvidenceChanged)
    Q_PROPERTY(QString activeEvidenceName READ activeEvidenceName NOTIFY activeEvidenceChanged)
    Q_PROPERTY(QString activeEvidenceSha READ activeEvidenceSha NOTIFY activeEvidenceChanged)
    Q_PROPERTY(QString activeEvidenceStatus READ activeEvidenceStatus NOTIFY activeEvidenceChanged)
    Q_PROPERTY(QString activeEvidenceProvenance READ activeEvidenceProvenance NOTIFY activeEvidenceChanged)
    Q_PROPERTY(QUrl   activePlayerUrl    READ activePlayerUrl   NOTIFY activeEvidenceChanged)
    Q_PROPERTY(QVariantMap activeMediaInfo READ activeMediaInfo  NOTIFY mediaInfoChanged)
    Q_PROPERTY(bool   frameExportBusy    READ frameExportBusy   NOTIFY frameExportBusyChanged)

    // ---- Live playback engine (libVLC) ---------------------------------------
    Q_PROPERTY(bool       playerPlaying    READ playerPlaying    NOTIFY playbackChanged)
    Q_PROPERTY(bool       playerHasVideo   READ playerHasVideo   NOTIFY playbackChanged)
    Q_PROPERTY(qlonglong  playerPositionMs READ playerPositionMs NOTIFY positionChanged)
    Q_PROPERTY(qlonglong  playerDurationMs READ playerDurationMs NOTIFY playbackChanged)
    Q_PROPERTY(double     playerRate       READ playerRate       NOTIFY playbackChanged)
    Q_PROPERTY(QVariantMap liveIndicators  READ liveIndicators   NOTIFY indicatorsChanged)
    Q_PROPERTY(int        liveIndicatorTotal READ liveIndicatorTotal NOTIFY indicatorsChanged)

public:
    explicit KFWShell(QObject *parent = nullptr);
    ~KFWShell() override;

    /** Bootstrap the QML engine, register types, and load the shell UI. */
    bool init(const QString &qmlRootDir = QString());

    // Property accessors -----------------------------------------------------
    QString currentScreen() const { return m_currentScreen; }
    QString examinerName()  const { return m_examinerName;  }
    QString databasePath()  const { return m_databasePath;  }
    QString appVersion()    const { return QStringLiteral("0.1.0-alpha"); }
    QString engineVersion() const;
    bool    auditChainOk()  const { return m_auditChainOk; }

    QString activeCaseId()  const { return m_activeCaseId; }
    QString activeCaseTitle() const { return m_activeCaseTitle; }
    QString activeEvidenceId() const { return m_activeEvidenceId; }
    QString activeEvidenceName() const { return m_activeEvidenceName; }
    QString activeEvidenceSha() const { return m_activeEvidenceSha; }
    QString activeEvidenceStatus() const { return m_activeEvidenceStatus; }
    QString activeEvidenceProvenance() const { return m_activeEvidenceProvenance; }
    QUrl    activePlayerUrl() const { return m_activePlayerUrl; }
    QVariantMap activeMediaInfo() const { return m_activeMediaInfo; }
    bool    frameExportBusy() const { return false; }  /* VLC export is instant */

    bool       playerPlaying()    const { return m_playing; }
    bool       playerHasVideo()   const { return m_hasFrame; }
    qlonglong  playerPositionMs() const { return m_positionMs; }
    qlonglong  playerDurationMs() const { return m_durationMs; }
    double     playerRate()       const { return m_rate; }
    QVariantMap liveIndicators()  const { return m_indicatorCounts; }
    int        liveIndicatorTotal() const
    {
        int total = 0;
        for (auto it = m_indicatorCounts.constBegin();
             it != m_indicatorCounts.constEnd(); ++it)
            total += it.value().toInt();
        return total;
    }

    /** Latest decoded frame (deep copy, thread-safe). Null if none. */
    QImage frameSnapshot() const;

    /** Live video dimensions as last delivered to the frame tap. */
    QSize liveVideoSize() const;

    void setExaminerName(const QString &name);

public slots:
    // ---- Navigation ---------------------------------------------------------
    Q_INVOKABLE void showDashboard();
    Q_INVOKABLE void openCase(const QString &caseId);
    Q_INVOKABLE void openEvidence(const QString &caseId, const QString &evidenceId);

    // ---- Case management -----------------------------------------------------
    Q_INVOKABLE QString createCase(const QString &title, const QString &examiner);
    Q_INVOKABLE void setCaseStatus(const QString &caseId, const QString &status);

    // ---- Evidence -------------------------------------------------------------
    Q_INVOKABLE void importEvidence(const QString &caseId, const QString &filePath);
    Q_INVOKABLE void reverifyEvidence(const QString &evidenceId);
    Q_INVOKABLE void probeEvidenceMedia(const QString &evidenceId);

    // ---- Live playback engine (libVLC driven) ---------------------------------
    Q_INVOKABLE void playToggle();
    Q_INVOKABLE void playerPlay();
    Q_INVOKABLE void playerPause();
    Q_INVOKABLE void playerSeekMs(double ms);
    Q_INVOKABLE void playerStepMs(double delta);
    Q_INVOKABLE void playerSetRate(double rate);
    Q_INVOKABLE void attachVideoScreen(QObject *item);
    Q_INVOKABLE void detachVideoScreen(QObject *item);

    // ---- Examination actions ---------------------------------------------------
    Q_INVOKABLE void exportFrame(const QString &evidenceId, double timestampMs);
    Q_INVOKABLE void addTimelineMarker(const QString &caseId, const QString &evidenceId,
                                        double timestampMs, const QString &label);
    Q_INVOKABLE void saveEvidenceNotes(const QString &evidenceId, const QString &notes);

    // ---- Reporting ---------------------------------------------------------------
    Q_INVOKABLE void generateReport(const QString &caseId, const QString &reportId,
                                     const QString &examinerSummary);

    // ---- Desktop integration ------------------------------------------------------- 
    Q_INVOKABLE void revealPath(const QString &path);

signals:
    void currentScreenChanged();
    void examinerNameChanged();
    void databasePathChanged();
    void auditChainChanged(bool ok);
    void activeCaseChanged();
    void activeEvidenceChanged();
    void mediaInfoChanged();
    void playbackChanged();
    void positionChanged();
    void indicatorsChanged();

    void caseCreated(const QString &caseId);
    void evidenceImported(const QString &evidenceId, const QString &verifyStatus,
                           const QString &sha256);
    void evidenceVerified(const QString &evidenceId, const QString &status);
    void mediaInfoReady();
    void frameExported(const QString &evidenceId, const QString &outputPath,
                        const QString &actualTimestamp);
    void frameExportBusyChanged();
    void reportGenerated(const QString &jsonPath, const QString &htmlPath,
                          const QString &jsonSha256);
    void errorOccurred(const QString &message);
    void notice(const QString &message);

private:
    // ---- Database ------------------------------------------------------------
    bool                  openOrCreateDatabase(const QString &dbPath);
    void                  reloadCases();
    void                  reloadEvidence(const QString &caseId);
    void                  reloadTimeline(const QString &caseId);
    void                  refreshAuditChain();

    // ---- Live VLC playback + detection -----------------------------------------
    void                  setupLivePlayback();
    void                  teardownLivePlayback();
    void                  pollPlayback();
    void                  pumpFrame();
    void                  recordDetection(int64_t tsMs, const QString &kind,
                                          const QString &detail);

public:
    /* Called from the libVLC decoder thread — never touch QML state here. */
    void                  onVlcFrame(int64_t tsMs, unsigned w, unsigned h,
                                     const uint8_t *bgra, size_t pitch,
                                     size_t lines);
    void                  onDetectEvent(int64_t tsMs, const char *kind,
                                     const char *detail);

private:
    QQmlApplicationEngine *m_engine        = nullptr;
    KFWCaseModel          *m_caseModel     = nullptr;
    KFWEvidenceModel      *m_evidenceModel = nullptr;
    KFWTimelineModel      *m_timelineModel = nullptr;

    QString m_currentScreen = QStringLiteral("Dashboard");
    QString m_examinerName  = QStringLiteral("Kishan G A");
    QString m_databasePath;

    QString m_activeCaseId;
    QString m_activeCaseTitle;
    QString m_activeEvidenceId;
    QString m_activeEvidenceName;
    QString m_activeEvidenceSha;
    QString m_activeEvidenceStatus;
    QString m_activeEvidenceProvenance;
    QUrl    m_activePlayerUrl;
    QVariantMap m_activeMediaInfo;
    bool    m_auditChainOk = false;

    // ---- Live playback engine ---------------------------------------------------
    KFWVideoScreen      *m_videoScreen = nullptr;
    struct kfw_player   *m_player      = nullptr;
    struct kfw_detect   *m_detector    = nullptr;
    QTimer              *m_playPoll    = nullptr;

    bool                m_playing    = false;
    bool                m_hasFrame   = false;
    qlonglong           m_positionMs = 0;
    qlonglong           m_durationMs = -1;
    double              m_rate       = 1.0;

    /* Thread-safe latest-frame store: decoder thread writes (onVlcFrame),
     * GUI thread reads (paint, export). */
    mutable QMutex      m_frameLock;
    QImage              m_latestFrame;
    qlonglong           m_frameMs    = -1;

    /* Indicator storm guard (last event seen, wall-clock). */
    int64_t             m_lastIndicatorMs  = 0;
    QString             m_lastIndicatorKind;
    bool                m_pipelineTrace = false;

    QVariantMap         m_indicatorCounts;
};

#endif // KFW_SHELL_HPP
