// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "scaleregistration.h"

#include <cmath>

bool ScaleRegistration::sample(const QImage &gray, const QRectF &box, Sample &values)
{
    if (!std::isfinite(box.x()) || !std::isfinite(box.y()) || !std::isfinite(box.width()) || !std::isfinite(box.height()) || box.width() < 4 ||
        box.height() < 4 || box.left() < 0 || box.top() < 0 || box.right() > gray.width() || box.bottom() > gray.height())
        return false;
    double mean = 0;
    for (int y = 0; y < Side; ++y) {
        const double py = qBound(0., box.y() + (y + 0.5) * box.height() / Side - 0.5, double(gray.height() - 1));
        const int iy = int(py), ny = qMin(iy + 1, gray.height() - 1);
        const double fy = py - iy;
        const auto row = gray.constScanLine(iy), next = gray.constScanLine(ny);
        for (int x = 0; x < Side; ++x) {
            const double px = qBound(0., box.x() + (x + 0.5) * box.width() / Side - 0.5, double(gray.width() - 1));
            const int ix = int(px), nx = qMin(ix + 1, gray.width() - 1);
            const double fx = px - ix;
            const double value = (1 - fy) * (row[ix] * (1 - fx) + row[nx] * fx) + fy * (next[ix] * (1 - fx) + next[nx] * fx);
            values[y * Side + x] = value;
            mean += value;
        }
    }
    mean /= values.size();
    for (auto &v : values)
        v -= mean;
    return true;
}

bool ScaleRegistration::initialize(const QImage &image, const QRectF &seed)
{
    m_seed = seed;
    m_scale = 1;
    m_energy = 0;
    // Include a narrow context rim: a box filled by the subject's texture alone
    // can favour the wrong scale while its actual silhouette moves outside it.
    const auto context = seed.adjusted(-seed.width() * 0.05, -seed.height() * 0.05, seed.width() * 0.05, seed.height() * 0.05);
    if (!sample(image.convertToFormat(QImage::Format_Grayscale8), context, m_reference)) return false;
    for (double v : m_reference)
        m_energy += v * v;
    return m_energy / m_reference.size() >= 16;
}

double ScaleRegistration::score(const QImage &gray, const QRectF &box) const
{
    Sample values;
    const auto context = box.adjusted(-box.width() * 0.05, -box.height() * 0.05, box.width() * 0.05, box.height() * 0.05);
    if (!sample(gray, context, values)) return -1;
    double energy = 0, dot = 0;
    for (unsigned i = 0; i < values.size(); ++i) {
        energy += values[i] * values[i];
        dot += values[i] * m_reference[i];
    }
    return energy / values.size() < 16 ? -1 : dot / std::sqrt(energy * m_energy);
}

bool ScaleRegistration::update(const QImage &image, const QRectF &measured, QRectF &result)
{
    if (!std::isfinite(measured.x()) || !std::isfinite(measured.y()) || !std::isfinite(measured.width()) || !std::isfinite(measured.height()) ||
        measured.width() < 4 || measured.height() < 4 || m_energy <= 0)
        return false;
    const auto gray = image.convertToFormat(QImage::Format_Grayscale8);
    const auto center = measured.center();
    double best = -1, bestScale = m_scale;
    QPointF bestCenter = center;
    auto candidate = [&](QPointF c, double scale) {
        if (scale < 0.5 || scale > 2) return;
        const QSizeF size = m_seed.size() * scale;
        const QRectF box(c - QPointF(size.width() / 2, size.height() / 2), size);
        const double value = score(gray, box);
        if (value > best) {
            best = value;
            bestScale = scale;
            bestCenter = c;
            result = box;
        }
    };
    // Fixed 9x17x17 search then two refinements. No retries or unbounded scan;
    // all frames remain inside the worker's existing child-process deadline.
    const double dx = qMax(1., m_seed.width() * m_scale * 0.025);
    const double dy = qMax(1., m_seed.height() * m_scale * 0.025);
    for (int s = -4; s <= 4; ++s)
        for (int y = -8; y <= 8; ++y)
            for (int x = -8; x <= 8; ++x)
                candidate(center + QPointF(x * dx, y * dy), m_scale * std::exp(s * 0.02));
    const auto coarseCenter = bestCenter;
    const double coarseScale = bestScale;
    for (int s = -3; s <= 3; ++s)
        for (int y = -3; y <= 3; ++y)
            for (int x = -3; x <= 3; ++x)
                candidate(coarseCenter + QPointF(x * dx / 3, y * dy / 3), coarseScale * std::exp(s * 0.005));
    const auto fineCenter = bestCenter;
    const double fineScale = bestScale;
    for (int s = -2; s <= 2; ++s)
        for (int y = -2; y <= 2; ++y)
            for (int x = -2; x <= 2; ++x)
                candidate(fineCenter + QPointF(x * dx / 9, y * dy / 9), fineScale * std::exp(s * 0.002));
    if (best < 0.8 || std::abs(std::log(bestScale / m_scale)) > 0.085) return false;
    // A ramp/flat patch can correlate at every scale. Require a distinct scale
    // optimum rather than turning an unobservable size into a verified zoom.
    for (double factor : {0.9, 1.1}) {
        const QSizeF size = m_seed.size() * (bestScale * factor);
        const QRectF alternative(bestCenter - QPointF(size.width() / 2, size.height() / 2), size);
        if (best - score(gray, alternative) < 0.01) return false;
    }
    m_scale = bestScale;
    return true;
}
