/****************************************************************************
 * This file is part of Hawaii.
 *
 * Copyright (C) 2014-2015 Pier Luigi Fiorini
 *               2015 Michael Spencer <sonrisesoftware@gmail.com>
 *
 * Author(s):
 *    Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>
 *    Michael Spencer <sonrisesoftware@gmail.com>
 *
 * $BEGIN_LICENSE:LGPL2.1+$
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * $END_LICENSE$
 ***************************************************************************/

#include <QtGui/QIcon>
#include <QDebug>

#include <GreenIsland/ApplicationManager>

#include "launchermodel.h"
#include "application.h"

using namespace GreenIsland;

LauncherModel::LauncherModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // Settings
    // m_settings = new QGSettings(QStringLiteral("org.hawaii.desktop.panel"),
    //                             QStringLiteral("/org/hawaii/desktop/panel/"),
    //                             this);

    // Application manager instance
    ApplicationManager *appMan = ApplicationManager::instance();

    // Connect to application events
    connect(appMan, &ApplicationManager::applicationAdded, this, [this](const QString &appId, pid_t pid) {
        // Do we have already an icon?
        for (int i = 0; i < m_list.size(); i++) {
            Application *app = m_list.at(i);
            if (app->appId() == appId) {
                app->m_pids.insert(pid);
                app->setState(Application::Running);
                QModelIndex modelIndex = index(i);
                emit dataChanged(modelIndex, modelIndex);
                return;
            }
        }

        // Otherwise create one
        beginInsertRows(QModelIndex(), m_list.size(), m_list.size());
        Application *item = new Application(appId, this);
        item->m_pids.insert(pid);
        m_list.append(item);
        endInsertRows();
    });
    connect(appMan, &ApplicationManager::applicationRemoved, this, [this](const QString &appId, pid_t pid) {
        for (int i = 0; i < m_list.size(); i++) {
            Application *app = m_list.at(i);
            if (app->appId() == appId) {
                // Remove this pid and determine if there are any processes left
                app->m_pids.remove(pid);
                if (app->m_pids.count() > 0)
                    return;

                if (app->isPinned()) {
                    // If it's pinned we just unset the flags if all pids are gone
                    app->setState(Application::NotRunning);
                    app->setFocused(false);
                    QModelIndex modelIndex = index(i);
                    emit dataChanged(modelIndex, modelIndex);
                } else {
                    // Otherwise the icon goes away because it wasn't meant
                    // to stay
                    beginRemoveRows(QModelIndex(), i, i);
                    m_list.takeAt(i)->deleteLater();
                    endRemoveRows();
                }
                break;
            }
        }
    });
    connect(appMan, &ApplicationManager::applicationFocused, this, [this](const QString &appId) {
        for (int i = 0; i < m_list.size(); i++) {
            Application *app = m_list.at(i);
            if (app->appId() == appId) {
                app->setFocused(true);
                QModelIndex modelIndex = index(i);
                emit dataChanged(modelIndex, modelIndex);
                break;
            }
        }
    });
    connect(appMan, &ApplicationManager::applicationUnfocused, this, [this](const QString &appId) {
        for (int i = 0; i < m_list.size(); i++) {
            Application *app = m_list.at(i);
            if (app->appId() == appId) {
                app->setFocused(false);
                QModelIndex modelIndex = index(i);
                Q_EMIT dataChanged(modelIndex, modelIndex);
                break;
            }
        }
    });

    // Add pinned launchers
    //const QStringList pinnedLaunchers = m_settings->value(QStringLiteral("pinnedLaunchers")).toStringList();
    beginInsertRows(QModelIndex(), m_list.size(), m_list.size() + 2);
    m_list.append(new Application("papyros-files", true, this));
    m_list.append(new Application("gnome-dictionary", true, this));
    endInsertRows();
}

LauncherModel::~LauncherModel()
{
    // Delete the items
    while (!m_list.isEmpty())
        m_list.takeFirst()->deleteLater();
}

QHash<int, QByteArray> LauncherModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles.insert(AppIdRole, "appId");
    roles.insert(DesktopFileRole, "desktopFile");
    roles.insert(ActionsRole, "actions");
    roles.insert(StateRole, "state");
    roles.insert(RunningRole, "running");
    roles.insert(FocusedRole, "focused");
    roles.insert(PinnedRole, "pinned");
    return roles;
}

int LauncherModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_list.size();
}

QVariant LauncherModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    Application *item = m_list.at(index.row());

    switch (role) {
    case Qt::DecorationRole:
        return QIcon::fromTheme(item->desktopFile()->m_iconName);
    case Qt::DisplayRole:
        return item->desktopFile()->m_name;
    case AppIdRole:
        return item->appId();
    case DesktopFileRole:
        qDebug() << "Desktop file in C++" << item->desktopFile();
        return QVariant::fromValue(item->desktopFile());
    case PinnedRole:
        return item->isPinned();
    case RunningRole:
        return item->isRunning();
    case FocusedRole:
        return item->isFocused();
    default:
        break;
    }

    return QVariant();
}

Application *LauncherModel::get(int index) const
{
    if (index < 0 || index >= m_list.size())
        return Q_NULLPTR;
    return m_list.at(index);
}

int LauncherModel::indexFromAppId(const QString &appId) const
{
    for (int i = 0; i < m_list.size(); i++) {
        if (m_list.at(i)->appId() == appId)
            return 0;
    }

    return -1;
}

void LauncherModel::pin(const QString &appId)
{
    Application *found = Q_NULLPTR;

    Q_FOREACH (Application *item, m_list) {
        if (item->appId() != appId)
            continue;

        found = item;
        break;
    }

    qDebug() << found;
    if (!found)
        return;

    found->setPinned(true);
    QModelIndex modelIndex = index(m_list.indexOf(found));
    Q_EMIT dataChanged(modelIndex, modelIndex);

    pinLauncher(appId, true);
}

void LauncherModel::unpin(const QString &appId)
{
    Application *found = Q_NULLPTR;

    Q_FOREACH (Application *item, m_list) {
        if (item->appId() != appId)
            continue;

        found = item;
        break;
    }

    if (!found)
        return;

    int i = m_list.indexOf(found);

    // Remove the item when unpinned and not running
    if (found->isRunning()) {
        found->setPinned(false);
        QModelIndex modelIndex = index(i);
        Q_EMIT dataChanged(modelIndex, modelIndex);
    } else {
        beginRemoveRows(QModelIndex(), i, i);
        m_list.takeAt(i)->deleteLater();
        endRemoveRows();
    }

    pinLauncher(appId, false);
}

void LauncherModel::pinLauncher(const QString &appId, bool pinned)
{
    // Currently pinned launchers
    //QStringList pinnedLaunchers = m_settings->value(QStringLiteral("pinnedLaunchers")).toStringList();

    // Add or remove from the pinned launchers
    // if (pinned)
    //     pinnedLaunchers.append(appId);
    // else
    //     pinnedLaunchers.removeOne(appId);
    // m_settings->setValue(QStringLiteral("pinnedLaunchers"), pinnedLaunchers);
}