/*****************************************************************************
 * kfw_video_screen.cpp: Kishan Forensic Workbench — VLC frame surface
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * paint() snapshots the shell's latest frame under the shell mutex, then
 * blits it letterboxed into the item rect. Playback-speed blits use the fast
 * transform; snapping during scrubbing keeps the mapping stable.
 *****************************************************************************/

#include "kfw_video_screen.hpp"

#include "kfw_shell.hpp"

#include <QPainter>

KFWVideoScreen::KFWVideoScreen(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setFillColor(Qt::black);
}

void KFWVideoScreen::paint(QPainter *painter)
{
    const QRectF area(0, 0, width(), height());
    if (area.isEmpty())
        return;

    static bool tracePaint = !qEnvironmentVariable("KFW_PIPELINE_DEBUG").isEmpty();
    if (tracePaint)
    {
        static int paints = 0;
        if ((paints++ % 100) == 0)
            qWarning("KFWVideoScreen: paint #%d (w=%d h=%d)",
                     paints, int(width()), int(height()));
    }

    painter->fillRect(area, Qt::black);
    if (m_shell == nullptr)
        return;

    const QImage frame = m_shell->frameSnapshot();
    if (frame.isNull())
        return;

    /* Aspect-fit into the viewport (letterbox/crop-free). */
    const qreal iw = frame.width();
    const qreal ih = frame.height();
    const qreal scale = qMin(area.width() / iw, area.height() / ih);
    const QRectF dst((area.width()  - iw * scale) / 2.0,
                     (area.height() - ih * scale) / 2.0,
                     iw * scale, ih * scale);

    painter->drawImage(dst, frame);
}
