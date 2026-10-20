/*****************************************************************************
 * kfw_evidence_model.cpp: Kishan Forensic Workbench — evidence list Qt model
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *****************************************************************************/

#include "kfw_evidence_model.hpp"

#include <QFileInfo>

KFWEvidenceModel::KFWEvidenceModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

static QString humanSize(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("-");
    const double kb = 1024.0, mb = kb * 1024.0, gb = mb * 1024.0;
    if (bytes >= gb)  return QString::number(bytes / gb, 'f', 2) + QStringLiteral(" GB");
    if (bytes >= mb)  return QString::number(bytes / mb, 'f', 1) + QStringLiteral(" MB");
    if (bytes >= kb)  return QString::number(bytes / kb, 'f', 1) + QStringLiteral(" KB");
    return QString::number(bytes) + QStringLiteral(" B");
}

int KFWEvidenceModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant KFWEvidenceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0
            || index.row() >= static_cast<int>(m_rows.size()))
        return QVariant{};

    const KFWEvidence &r = m_rows[static_cast<size_t>(index.row())];

    switch (role) {
    case EvidenceIdRole:   return r.evidenceId;
    case FileNameRole:     return r.filename;
    case FileTypeRole:     return r.fileType;
    case SizeTextRole:     return r.sizeText;
    case SizeBytesRole:    return r.sizeBytes;
    case ShaRole:          return r.sha256;
    case StoredPathRole:   return r.storedPath;
    case OrigPathRole:     return r.origPath;
    case ProvenanceRole:   return r.provenance;
    case VerifyStatusRole: return r.verifyStatus;
    case ImportedRole:     return r.importedText;
    case HasMediaRole:     return r.hasMedia;
    case DuplicateOfRole:  return r.duplicateOf;
    default:               return QVariant{};
    }
}

QHash<int, QByteArray> KFWEvidenceModel::roleNames() const
{
    return {
        { EvidenceIdRole,   "evId"          },
        { FileNameRole,     "filename"      },
        { FileTypeRole,     "fileType"      },
        { SizeTextRole,     "sizeText"      },
        { SizeBytesRole,    "sizeBytes"     },
        { ShaRole,          "sha256"        },
        { StoredPathRole,   "storedPath"    },
        { OrigPathRole,     "origPath"      },
        { ProvenanceRole,   "provenance"    },
        { VerifyStatusRole, "verifyStatus"  },
        { ImportedRole,     "importedText"  },
        { HasMediaRole,     "hasMedia"      },
        { DuplicateOfRole,  "duplicateOf"   },
    };
}

void KFWEvidenceModel::reload(const QVariantList &rows)
{
    beginResetModel();
    m_rows.clear();

    for (const QVariant &rv : rows)
    {
        const QVariantMap m = rv.toMap();
        KFWEvidence r;
        r.evidenceId    = m.value("evidenceId").toString();
        r.filename      = m.value("filename").toString();
        r.fileType      = m.value("fileType").toString();
        r.sizeBytes     = m.value("sizeBytes").toLongLong();
        r.sizeText      = humanSize(r.sizeBytes);
        r.sha256        = m.value("sha256").toString();
        r.storedPath    = m.value("storedPath").toString();
        r.origPath      = m.value("origPath").toString();
        r.provenance    = m.value("provenance").toString();
        r.verifyStatus  = m.value("verifyStatus").toString();
        r.importedText  = m.value("importedText").toString();
        r.hasMedia      = QFileInfo::exists(r.storedPath);
        r.duplicateOf   = m.value("duplicateOf").toString();
        m_rows.push_back(std::move(r));
    }

    endResetModel();
    emit countChanged();
}

QVariantMap KFWEvidenceModel::get(const QString &evidenceId) const
{
    for (const auto &r : m_rows)
    {
        if (r.evidenceId == evidenceId)
        {
            return {
                { "evId",         r.evidenceId   },
                { "filename",     r.filename     },
                { "fileType",     r.fileType     },
                { "sizeText",     r.sizeText     },
                { "sizeBytes",    r.sizeBytes    },
                { "sha256",       r.sha256       },
                { "storedPath",   r.storedPath   },
                { "origPath",     r.origPath     },
                { "provenance",   r.provenance   },
                { "verifyStatus", r.verifyStatus },
                { "importedText", r.importedText },
                { "hasMedia",     r.hasMedia     },
                { "duplicateOf",  r.duplicateOf  },
            };
        }
    }
    return {};
}

void KFWEvidenceModel::setVerifyStatus(const QString &evidenceId,
                                       const QString &status)
{
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (m_rows[i].evidenceId == evidenceId)
        {
            m_rows[i].verifyStatus = status;
            const QModelIndex idx = index(static_cast<int>(i));
            emit dataChanged(idx, idx, { VerifyStatusRole });
            return;
        }
    }
}
