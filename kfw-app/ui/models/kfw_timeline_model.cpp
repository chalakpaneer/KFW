/*****************************************************************************
 * kfw_timeline_model.cpp: Kishan Forensic Workbench ??? timeline events model
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *****************************************************************************/

#include "kfw_timeline_model.hpp"

KFWTimelineModel::KFWTimelineModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int KFWTimelineModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_events.size());
}

QVariant KFWTimelineModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0
            || index.row() >= static_cast<int>(m_events.size()))
        return QVariant{};

    const KFWEvent &e = m_events[static_cast<size_t>(index.row())];

    switch (role) {
    case EventIdRole:         return e.eventId;
    case TsMsRole:            return e.tsMs;
    case TsTextRole:          return e.tsText;
    case KindRole:            return e.kind;
    case LabelRole:           return e.label;
    case BodyRole:            return e.body;
    case EventEvidenceIdRole: return e.evidenceId;
    default:                  return QVariant{};
    }
}

QHash<int, QByteArray> KFWTimelineModel::roleNames() const
{
    return {
        { EventIdRole,         "eventId"    },
        { TsMsRole,            "tsMs"       },
        { TsTextRole,          "tsText"     },
        { KindRole,            "kind"       },
        { LabelRole,           "label"      },
        { BodyRole,            "body"       },
        { EventEvidenceIdRole, "evidenceId" },
    };
}

void KFWTimelineModel::reload(const QVariantList &rows)
{
    beginResetModel();
    m_events.clear();

    for (const QVariant &rv : rows)
    {
        const QVariantMap m = rv.toMap();
        KFWEvent e;
        e.eventId    = m.value("eventId").toString();
        e.tsMs       = m.value("tsMs").toLongLong();
        e.tsText     = m.value("tsText").toString();
        e.kind       = m.value("kind").toString();
        e.label      = m.value("label").toString();
        e.body       = m.value("body").toString();
        e.evidenceId = m.value("evidenceId").toString();
        m_events.push_back(std::move(e));
    }

    endResetModel();
    emit countChanged();
}
