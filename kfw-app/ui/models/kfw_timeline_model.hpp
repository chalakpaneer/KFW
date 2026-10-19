/*****************************************************************************
 * kfw_timeline_model.hpp: Kishan Forensic Workbench — timeline events model
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

#ifndef KFW_TIMELINE_MODEL_HPP
#define KFW_TIMELINE_MODEL_HPP

#include <QAbstractListModel>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <vector>

struct KFWEvent
{
    QString eventId;
    qint64  tsMs = 0;
    QString tsText;      // "HH:MM:SS.mmm"
    QString kind;        // "Marker" | "Bookmark" | "Note" | "FrameExport" | "IntegrityCheck"
    QString label;
    QString body;
    QString evidenceId;
};

// ---------------------------------------------------------------------------
// KFWTimelineModel — investigation timeline strip (events loaded from the
// case database via KFWShell).
// ---------------------------------------------------------------------------
class KFWTimelineModel : public QAbstractListModel
{
    Q_OBJECT

    /** NOTIFIABLE count for QML bindings (rowCount() is not trackable). */
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        EventIdRole = Qt::UserRole,
        TsMsRole,
        TsTextRole,
        KindRole,
        LabelRole,
        BodyRole,
        EventEvidenceIdRole,
    };
    Q_ENUM(Roles)

    explicit KFWTimelineModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload(const QVariantList &rows);

    int count() const { return static_cast<int>(m_events.size()); }

signals:
    void countChanged();

private:
    std::vector<KFWEvent> m_events;
};

#endif // KFW_TIMELINE_MODEL_HPP
