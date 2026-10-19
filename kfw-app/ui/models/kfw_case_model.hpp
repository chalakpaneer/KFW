/*****************************************************************************
 * kfw_case_model.hpp: Kishan Forensic Workbench — case list Qt model
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

#ifndef KFW_CASE_MODEL_HPP
#define KFW_CASE_MODEL_HPP

#include <QAbstractListModel>
#include <QDateTime>
#include <QQmlEngine>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <vector>

// ---------------------------------------------------------------------------
// KFWCase — in-memory representation of one investigation case
// ---------------------------------------------------------------------------
struct KFWCase
{
    QString  caseId;          // "KFW-2026-001"
    QString  title;
    QString  examiner;
    QString  status;          // "Open" | "Pending Review" | "Closed"
    QString  created;         // formatted for display
    QString  modified;        // formatted for display
    QString  notes;
    int      evidenceCount = 0;
    int      pendingVerify = 0;
};

// ---------------------------------------------------------------------------
// KFWCaseModel — QAbstractListModel for the case dashboard.
// Data comes from the SQLite case database via KFWShell::reloadCases(),
// which maps each kfw_db_case_t + evidence roll-up into a QVariantMap row.
// ---------------------------------------------------------------------------
class KFWCaseModel : public QAbstractListModel
{
    Q_OBJECT

    /** NOTIFIABLE count for QML bindings (rowCount() is not trackable). */
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        CaseIdRole = Qt::UserRole,
        TitleRole,
        ExaminerRole,
        StatusRole,
        CreatedRole,
        ModifiedRole,
        EvidenceCountRole,
        PendingVerifyRole,
        NotesRole,
    };
    Q_ENUM(Roles)

    explicit KFWCaseModel(QObject *parent = nullptr);

    // QAbstractListModel overrides
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /** Replace the whole content with rows supplied by the shell. */
    Q_INVOKABLE void loadList(const QVariantList &rows);

    /** Update the status of a single case (database is updated by the shell). */
    Q_INVOKABLE void setStatus(const QString &caseId, const QString &status);

    Q_INVOKABLE int  totalCases()     const;
    Q_INVOKABLE int  openCases()      const;
    Q_INVOKABLE int  pendingTotal()   const;
    Q_INVOKABLE int  evidenceTotal()  const;

    int indexOfCase(const QString &caseId) const;

    int count() const { return static_cast<int>(m_cases.size()); }

signals:
    void countChanged();

private:
    std::vector<KFWCase> m_cases;
};

#endif // KFW_CASE_MODEL_HPP
