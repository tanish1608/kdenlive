// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "doc/kdenlivedoc.h"
#include "jobs/transcodetask.h"
#include "kdenlivesettings.h"
#include "test_utils.hpp"

#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

TEST_CASE("Transcode preserves usable audio and rejects failed output", "[Transcode]")
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        WARN("FFmpeg is required for the native conversion integration check");
        return;
    }
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.mov"));
    QProcess generator;
    generator.start(ffmpeg, {"-v",   "error", "-f",   "lavfi",    "-i",   "color=s=64x64:r=25:d=1",
                             "-f",   "lavfi", "-i",   "sine=d=1", "-map", "0:v",
                             "-map", "1:a",   "-map", "1:a",      "-c:v", "mpeg4",
                             "-c:a", "aac",   "-f",   "mp4",      source});
    REQUIRE(generator.waitForFinished(15000));
    REQUIRE(generator.exitCode() == 0);

    int expectedAudio = 2;
    bool failedEncoder = false;
    SECTION("Unknown auxiliary audio does not prevent conversion")
    {
        // Model a phone MOV containing one valid AAC track and an unknown
        // auxiliary audio codec, without storing private footage in the tests.
        expectedAudio = 1;
        QFile file(source);
        REQUIRE(file.open(QIODevice::ReadWrite));
        auto bytes = file.readAll();
        const int first = bytes.indexOf("mp4a");
        REQUIRE(first >= 0);
        const int second = bytes.indexOf("mp4a", first + 4);
        REQUIRE(second > first);
        bytes.replace(second, 4, "apac");
        const int extra = bytes.indexOf("esds", second);
        REQUIRE(extra > second);
        bytes.replace(extra, 4, "free");
        REQUIRE(file.seek(0));
        REQUIRE(file.write(bytes) == bytes.size());
    }
    SECTION("All valid audio streams remain present") {}
    SECTION("Silent footage does not require an audio stream")
    {
        expectedAudio = 0;
        const QString silent = directory.filePath(QStringLiteral("silent.mov"));
        QProcess stripAudio;
        stripAudio.start(ffmpeg, {"-v", "error", "-i", source, "-an", "-c:v", "copy", silent});
        REQUIRE(stripAudio.waitForFinished(15000));
        REQUIRE(stripAudio.exitCode() == 0);
        REQUIRE(QFile::remove(source));
        REQUIRE(QFile::rename(silent, source));
    }
    SECTION("Failed encoder cannot publish a nonempty partial file")
    {
        failedEncoder = true;
    }

    auto bin = pCore->projectItemModel();
    bin->clean();
    auto stack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(stack);
    pCore->projectManager()->testSetDocument(&document);
    auto producer = std::make_shared<Mlt::Producer>(pCore->getProjectProfile(), "avformat", source.toUtf8().constData());
    REQUIRE(producer->is_valid());
    QString id;
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    REQUIRE(bin->requestAddBinClip(id, producer, QStringLiteral("-1"), undo, redo));
    auto clip = bin->getClipByBinID(id);
    const QString oldFfmpeg = KdenliveSettings::ffmpegpath();
    KdenliveSettings::setFfmpegpath(ffmpeg);
    if (failedEncoder) {
        const QString fakeEncoder = directory.filePath(QStringLiteral("failed-encoder"));
        QFile script(fakeEncoder);
        REQUIRE(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nfor output; do :; done\nprintf partial > \"$output\"\nexit 3\n");
        script.close();
        REQUIRE(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        KdenliveSettings::setFfmpegpath(fakeEncoder);
    }
    TranscodeSeek::TranscodeInfo info;
    info.url = source;
    info.type = ClipType::AV;
    info.fps_info = {25, 1};
    const ObjectId owner(KdenliveObjectType::BinClip, id.toInt(), QUuid());
    TranscodeTask::start(owner, "-converted", {}, "-c:v mpeg4 -c:a aac %1.mp4", info, -1, -1, false, clip.get());
    QElapsedTimer deadline;
    deadline.start();
    while (pCore->taskManager.hasPendingJob(owner, AbstractTask::TRANSCODEJOB) && deadline.elapsed() < 15000) {
        QThread::msleep(10);
    }
    REQUIRE_FALSE(pCore->taskManager.hasPendingJob(owner, AbstractTask::TRANSCODEJOB));
    const QString output = directory.filePath(QStringLiteral("source-converted.mp4"));
    if (failedEncoder) {
        REQUIRE_FALSE(QFile::exists(output));
    } else {
        REQUIRE(QFileInfo(output).size() > 0);
        Mlt::Producer converted(pCore->getProjectProfile(), "avformat", output.toUtf8().constData());
        REQUIRE(converted.is_valid());
        REQUIRE(converted.get_int("meta.media.nb_streams") == 1 + expectedAudio);
    }
    KdenliveSettings::setFfmpegpath(oldFfmpeg);
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    undo = []() { return true; };
    redo = []() { return true; };
    clip.reset();
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
}
