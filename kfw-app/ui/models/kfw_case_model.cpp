/*****************************************************************************
 * kfw_case_model.cpp: Kishan Forensic Workbench — case list Qt model
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *****************************************************************************/

#include "kfw_case_model.hpp"

KFWCaseModel::KFWCaseModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int KFWCaseModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_cases.size());
}

QVariant KFWCaseModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0
            || index.row() >= static_cast<int>(m_cases.size()))
        return QVariant{};

    const KFWCase &c = m_cases[static_cast<size_t>(index.row())];

    switch (role) {
    case CaseIdRole:        return c.caseId;
    case TitleRole:         return c.title;
    case ExaminerRole:      return c.examiner;
    case StatusRole:        return c.status;
    case CreatedRole:       return c.created;
    case ModifiedRole:      return c.modified;
    case EvidenceCountRole: return c.evidenceCount;
    case PendingVerifyRole: return c.pendingVerify;
    case NotesRole:         return c.notes;
    default:                return QVariant{};
    }
}

QHash<int, QByteArray> KFWCaseModel::roleNames() const
{
    return {
        { CaseIdRole,        "caseId"        },
        { TitleRole,         "title"         },
        { ExaminerRole,      "examiner"      },
        { StatusRole,        "status"        },
        { CreatedRole,       "created"       },
        { ModifiedRole,      "modified"      },
        { EvidenceCountRole, "evidenceCount" },
        { PendingVerifyRole, "pendingVerify" },
        { NotesRole,         "notes"         },
    };
}

void KFWCaseModel::loadList(const QVariantList &rows)
{
    beginResetModel();
    m_cases.clear();

    for (const QVariant &rv : rows)
    {
        const QVariantMap m = rv.toMap();
        KFWCase c;
        c.caseId        = m.value("caseId").toString();
        c.title         = m.value("title").toString();
        c.examiner      = m.value("examiner").toString();
        c.status        = m.value("status").toString();
        c.created       = m.value("created").toString();
        c.modified      = m.value("modified").toString();
        c.notes         = m.value("notes").toString();
        c.evidenceCount = m.value("evidenceCount").toInt();
        c.pendingVerify = m.value("pendingVerify").toInt();
        m_cases.push_back(std::move(c));
    }

    endResetModel();
    emit countChanged();
}

void KFWCaseModel::setStatus(const QString &caseId, const QString &status)
{
    const int row = indexOfCase(caseId);
    if (row < 0)
        return;
    m_cases[static_cast<size_t>(row)].status = status;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { StatusRole });
}

int KFWCaseModel::totalCases() const
{
    return static_cast<int>(m_cases.size());
}

int KFWCaseModel::openCases() const
{
    int n = 0;
    for (const auto &c : m_cases)
        if (c.status.contains("Open"))
            ++n;
    return n;
}

int KFWCaseModel::pendingTotal() const
{
    int n = 0;
    for (const auto &c : m_cases)
        n += c.pendingVerify;
    return n;
}

int KFWCaseModel::evidenceTotal() const
{
    int n = 0;
    for (const auto &c : m_cases)
        n += c.evidenceCount;
    return n;
}

int KFWCaseModel::indexOfCase(const QString &caseId) const
{
    for (size_t i = 0; i < m_cases.size(); ++i)
        if (m_cases[i].caseId == caseId)
            return static_cast<int>(i);
    return -1;
}
