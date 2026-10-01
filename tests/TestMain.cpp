/*
    SPDX-FileCopyrightText: 2018-2022 Jean-Baptiste Mardelle <jb@kdenlive.org>
    SPDX-FileCopyrightText: 2017-2019 Nicolas Carion <french.ebook.lover@gmail.com>
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#define CATCH_CONFIG_RUNNER
#include "catch.hpp"
#include "tests_definitions.h"

#include <QApplication>
#include <QFile>
#include <QString>
#include <QtEnvironmentVariables>

#include "bin/projectitemmodel.h"
#include "core.h"
#include "mltconnection.h"
#include "src/effects/effectsrepository.hpp"
#include <mlt++/MltFactory.h>
#include <mlt++/MltRepository.h>

/* This file is intended to remain empty.
Write your tests in a file with a name corresponding to what you're testing */

QString getMltRepositoryPath()
{
    if (qEnvironmentVariableIsSet("MLT_REPOSITORY")) {
        return qEnvironmentVariable("MLT_REPOSITORY");
    }
    for (auto varname : {"CRAFT_ROOT", "KDEROOT"}) {
        if (!qEnvironmentVariableIsSet(varname)) {
            continue;
        }

        qDebug() << varname << "envvar is set, try to use it for MLT repository path";
        for (const auto &relativePath : {QStringLiteral("lib/mlt"), QStringLiteral("lib/mlt-7"), QStringLiteral("plugins/mlt")}) {
            const QString repositoryPath = QDir(qEnvironmentVariable(varname)).filePath(relativePath);
            if (QDir(repositoryPath).exists()) {
                qDebug() << "Using this path as MLT repository path:" << repositoryPath;
                return repositoryPath;
            }
        }
    }
    return {};
}

int main(int argc, char *argv[])
{
    QHashSeed::setDeterministicGlobalSeed();
    qputenv("MLT_REPOSITORY_DENY", "libmltqt:libmltglaxnimate");
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kdenlive"));
    qSetMessagePattern(QStringLiteral("%{time hh:mm:ss.zzz } %{file}:%{line} -- %{message}"));

    QString repoPath = getMltRepositoryPath();
    if (!repoPath.isEmpty() && !qEnvironmentVariableIsSet("MLT_DATA")) {
        QString dataPath = QDir::cleanPath(repoPath + QStringLiteral("/../../share/mlt"));
        if (QFile::exists(dataPath)) {
            qDebug() << "Setting MLT_DATA to" << dataPath;
            qputenv("MLT_DATA", dataPath.toUtf8());
        }
    }

    std::unique_ptr<Mlt::Repository> repo(Mlt::Factory::init(repoPath.isEmpty() ? NULL : repoPath.toUtf8().constData()));
    qputenv("MLT_TESTS", QByteArray("1"));
    Core::build(LinuxPackageType::Unknown, true);
    MltConnection::construct(QString());
    pCore->projectItemModel()->buildPlaylist(QUuid());
    // if Kdenlive is not installed, ensure we have one keyframable effect
    EffectsRepository::get()->reloadCustom(QDir(sourcesPath).filePath(QStringLiteral("../data/effects/audiobalance.xml")));

    int result = Catch::Session().run(argc, argv);
    pCore->cleanup();
    pCore->mediaUnavailable.reset();

    // global clean-up...
    // delete repo;
    pCore->projectItemModel()->clean();
    pCore->cleanup();
    return (result < 0xff ? result : 0xff);
}
