// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
// Bounded, separate-process native decoding/tracking. Never reads an editor project.
#include "scaleregistration.h"
#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mlt++/Mlt.h>

namespace {
QImage patch(const QImage &image, const mlt_rect &r)
{
    auto result = image.copy(QRect(qRound(r.x), qRound(r.y), qRound(r.w), qRound(r.h))).scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // Compare low-frequency appearance, tolerating subpixel tracker/codec variation.
    // A flat or disappearing patch still fails the variance/correlation gate.
    for (int pass = 0; pass < 2; ++pass) {
        QImage blurred(result.size(), QImage::Format_RGB32);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                int value = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        value += qGray(result.pixel(qBound(0, x + dx, 15), qBound(0, y + dy, 15))) * (dx == 0 ? 2 : 1) * (dy == 0 ? 2 : 1);
                value /= 16;
                blurred.setPixel(x, y, qRgb(value, value, value));
            }
        result = blurred;
    }
    return result;
}
double similarity(const QImage &a, const QImage &b)
{
    double ma = 0, mb = 0, aa = 0, bb = 0, ab = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            ma += qGray(a.pixel(x, y));
            mb += qGray(b.pixel(x, y));
        }
    ma /= 256;
    mb /= 256;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            double av = qGray(a.pixel(x, y)) - ma, bv = qGray(b.pixel(x, y)) - mb;
            aa += av * av;
            bb += bv * bv;
            ab += av * bv;
        }
    return aa < 256 || bb < 256 ? -1 : ab / std::sqrt(aa * bb);
}
QString jpeg(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPG", 80);
    return QString::fromLatin1(bytes.toBase64());
}
} // namespace
int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    if (argc != 2) return 2;
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) return 2;
    const auto input = QJsonDocument::fromJson(file.readAll()).object();
    int width = input.value("width").toInt(), height = input.value("height").toInt();
    int start = input.value("start").toInt(-1), length = input.value("length").toInt();
    int num = input.value("numerator").toInt(), den = input.value("denominator").toInt();
    if (width < 32 || width > 640 || height < 32 || height > 640 || start < 0 || length < 2 || length > 121 || num < 1 || den < 1) return 2;
    Mlt::Factory::init(qgetenv("MLT_REPOSITORY").constData());
    Mlt::Profile profile;
    profile.set_explicit(1);
    profile.set_width(width);
    profile.set_height(height);
    profile.set_frame_rate(num, den);
    profile.set_progressive(1);
    profile.set_sample_aspect(1, 1);
    profile.set_display_aspect(width, height);
    Mlt::Producer source(profile, input.value("path").toString().toUtf8().constData());
    if (!source.is_valid() || start + qint64(length) > source.get_length()) return 3;
    std::unique_ptr<Mlt::Producer> cut(source.cut(start, start + length - 1));
    Mlt::Filter filter(profile, "opencv.tracker");
    const bool tracking = input.contains("box");
    if (tracking) {
        if (!filter.is_valid()) return 4;
        const auto box = input.value("box").toArray();
        if (box.size() != 4) return 2;
        const double x = box[0].toDouble(), y = box[1].toDouble(), w = box[2].toDouble(), h = box[3].toDouble();
        if (!std::isfinite(x + y + w + h) || x < 0 || y < 0 || w < 0.04 || h < 0.04 || x + w > 1 || y + h > 1) return 2;
        filter.set("rect", QString("%1 %2 %3 %4").arg(x * width).arg(y * height).arg(w * width).arg(h * height).toUtf8().constData());
        filter.set("algo", "CSRT");
        filter.set("steps", 1);
        filter.set("shape_width", 1);
        filter.set("shape_color", "0x00000000");
        filter.set("blur", 0);
        filter.set_in_and_out(start, start + length - 1);
        cut->attach(filter);
    }
    ScaleRegistration registration;
    QJsonArray boxes;
    QImage reference, first, last;
    for (int i = 0; i < length; ++i) {
        cut->seek(i);
        std::unique_ptr<Mlt::Frame> frame(cut->get_frame());
        mlt_image_format format = mlt_image_rgb;
        int w = width, h = height;
        auto pixels = frame->get_image(format, w, h);
        if (!pixels || format != mlt_image_rgb || w != width || h != height) return 3;
        QImage image(pixels, w, h, w * 3, QImage::Format_RGB888);
        image = image.copy();
        if (i == 0) first = image;
        if (i == length - 1) last = image;
        if (tracking) {
            const char *key = filter.get("results") && *filter.get("results") ? "results" : "_results";
            auto r = filter.anim_get_rect(key, start + i, start + length);
            QRectF registered;
            if (i == 0) {
                const auto seed = input.value("box").toArray();
                r.x = seed[0].toDouble() * w;
                r.y = seed[1].toDouble() * h;
                r.w = seed[2].toDouble() * w;
                r.h = seed[3].toDouble() * h;
                if (!registration.initialize(image, QRectF(r.x, r.y, r.w, r.h))) return 5;
            } else {
                if (!registration.update(image, QRectF(r.x, r.y, r.w, r.h), registered)) {
                    std::fprintf(stderr, "Track rejected at sample %d (scale registration)\n", i);
                    return 5;
                }
                r.x = registered.x();
                r.y = registered.y();
                r.w = registered.width();
                r.h = registered.height();
            }
            if (r.w < 4 || r.h < 4 || r.x < 0 || r.y < 0 || r.x + r.w > w || r.y + r.h > h) return 5;
            const auto current = patch(image, r);
            if (i == 0) reference = current;
            const double score = similarity(reference, current);
            if (score < 0.65) {
                std::fprintf(stderr, "Track rejected at sample %d (appearance %.3f)\n", i, score);
                return 5;
            }
            boxes.append(QJsonArray{r.x / w, r.y / h, r.w / w, r.h / h});
        }
    }
    const QJsonObject output{{"boxes", boxes}, {"first", jpeg(first)}, {"last", jpeg(last)}};
    const auto bytes = QJsonDocument(output).toJson(QJsonDocument::Compact);
    std::fwrite(bytes.constData(), 1, bytes.size(), stdout);
    return 0;
}
