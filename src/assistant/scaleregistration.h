// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#pragma once

#include <QImage>
#include <QRectF>
#include <array>

// Short-window similarity registration against a fixed seed appearance. CSRT
// supplies the search position; scale is measured independently, without model drift.
class ScaleRegistration
{
public:
    bool initialize(const QImage &image, const QRectF &seed);
    bool update(const QImage &image, const QRectF &measured, QRectF &result);

private:
    static constexpr int Side = 32;
    using Sample = std::array<double, Side * Side>;
    static bool sample(const QImage &gray, const QRectF &box, Sample &values);
    double score(const QImage &gray, const QRectF &box) const;
    Sample m_reference{};
    QRectF m_seed;
    double m_energy = 0;
    double m_scale = 1;
};
