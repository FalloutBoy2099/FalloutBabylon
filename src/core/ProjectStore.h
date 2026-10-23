#pragma once

#include "Project.h"

// Persistence of project definitions (one JSON file each in the application data folder),
// the list of open projects and the recently used folders.
namespace ProjectStore {

QString baseDirectory();
QString newId();

QVector<ProjectConfig> loadAll();
bool save(const ProjectConfig &config);
void remove(const QString &id);
QString stateFile(const QString &id);
QString statsCacheFile(const QString &id);
QString qaCacheFile(const QString &id);
// Working folder of a project whose output is a .dat archive (see OutputArchive).
QString stagingDir(const QString &id);

QStringList openProjectIds();
void setOpenProjectIds(const QStringList &ids);

QStringList recentFolders();
void addRecentFolder(const QString &path);

}
