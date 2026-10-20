/*****************************************************************************
 * kfw_evidence_model.hpp: Kishan Forensic Workbench — evidence list Qt model
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

#ifndef KFW_EVIDENCE_MODEL_HPP
#define KFW_EVIDENCE_MODEL_HPP

#include <QAbstractListModel>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <vector>

struct KFWEvidence
{
    QString evidenceId;      // "EV-0007"
    QString filename;        // original filename
    QString fileType;        // "MP4" | "AVI" | …
    QString sizeText;        // human-readable ("1.2 GB")
    qint64  sizeBytes = 0;
    QString sha256;
    QString storedPath;
    QString origPath;
    QString provenance;      // "Original" | "Derived"
    QString verifyStatus;    // "Verified" | "Mismatch" | "Not checked"
    qint64  importedUtc = 0;
    QString importedText;    // formatted display date
    bool    hasMedia    = false; // stored file exists
    QString duplicateOf;     // evidence id with identical SHA-256, or ""
};

// ---------------------------------------------------------------------------
// KFWEvidenceModel — drives the evidence browser panel of the workspace.
// Rows are produced by KFWShell from kfw_db_evidence_list().
// ---------------------------------------------------------------------------
class KFWEvidenceModel : public QAbstractListModel
{
    Q_OBJECT

    /** NOTIFIABLE count for QML bindings (rowCount() is not trackable). */
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        EvidenceIdRole = Qt::UserRole,
        FileNameRole,
        FileTypeRole,
        SizeTextRole,
        SizeBytesRole,
        ShaRole,
        StoredPathRole,
        OrigPathRole,
        ProvenanceRole,
        VerifyStatusRole,
        ImportedRole,
        HasMediaRole,
        DuplicateOfRole,
    };
    Q_ENUM(Roles)

    explicit KFWEvidenceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload(const QVariantList &rows);

    /** Evidence lookup by ID; returns an empty map if not found. */
    Q_INVOKABLE QVariantMap get(const QString &evidenceId) const;

    /** Patch the verify-status of one row (issued after a re-verify run). */
    void setVerifyStatus(const QString &evidenceId, const QString &status);

    int count() const { return static_cast<int>(m_rows.size()); }

signals:
    void countChanged();

private:
    std::vector<KFWEvidence> m_rows;
};

#endif // KFW_EVIDENCE_MODEL_HPP
