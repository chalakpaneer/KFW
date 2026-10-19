/*****************************************************************************
 * kfw_video_screen.hpp: Kishan Forensic Workbench — VLC frame surface
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * QQuickPaintedItem that renders the live libVLC frame tap owned by KFWShell.
 * The shell keeps the thread-safe frame store (decoder thread writes, GUI
 * paints); this item only paints an aspect-fit snapshot per update cycle.
 *****************************************************************************/

#ifndef KFW_VIDEO_SCREEN_HPP
#define KFW_VIDEO_SCREEN_HPP

#include <QQuickPaintedItem>

class KFWShell;

class KFWVideoScreen : public QQuickPaintedItem
{
    Q_OBJECT

public:
    explicit KFWVideoScreen(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

private:
    KFWShell *m_shell = nullptr;   /* set via attachVideoScreen() */
    friend class KFWShell;
};

#endif /* KFW_VIDEO_SCREEN_HPP */
