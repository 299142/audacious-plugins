/*
 * main_window.h
 * Copyright 2014 Michał Lipski
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QLabel>
#include <QSlider>
#include <QTimer>
#include <QMessageBox>
#include <QtCore>

#include <libaudcore/drct.h>

#include "playlist_tabs.h"
#include "ui_main_window.h"
#include "filter_input.h"

class MainWindow : public QMainWindow, private Ui::MainWindow
{
    Q_OBJECT

public:
    MainWindow (QMainWindow * parent = 0);
    ~MainWindow ();

protected:
    void keyPressEvent (QKeyEvent * e);

public slots:
    void timeCounterSlot ();
    void sliderValueChanged (int value);
    void sliderPressed ();
    void sliderReleased ();

private:
    QLabel * timeCounterLabel = nullptr;
    QTimer * timeCounter = nullptr;
    QSlider * slider = nullptr;
    FilterInput * filterInput = nullptr;
    QMessageBox * progressDialog = nullptr;
    QMessageBox * errorDialog = nullptr;
    PlaylistTabs * playlistTabs = nullptr;
    void setTimeCounterLabel (int time, int length);
    void enableSlider ();
    void disableSlider ();
    void enableTimeCounter ();
    void disableTimeCounter ();
    void createProgressDialog ();
    void createErrorDialog (const QString &message);

    static void title_change_cb (void * unused, MainWindow * window)
    {
        auto title = aud_drct_get_title ();
        if (title)
            window->setWindowTitle (QString ("Audacious - ") + QString (title));
    }

    static void playback_begin_cb (void * unused, MainWindow * window)
    {
        window->setWindowTitle ("Audacious - Buffering...");

        pause_cb (nullptr, window);
    }

    static void playback_ready_cb (void * unused, MainWindow * window)
    {
        title_change_cb (nullptr, window);
        pause_cb (nullptr, window);

        window->enableSlider ();
        window->enableTimeCounter ();
    }

    static void pause_cb (void * unused, MainWindow * window)
    {
        if (aud_drct_get_paused ())
            window->actionPlayPause->setIcon (QIcon::fromTheme ("media-playback-start"));
        else
            window->actionPlayPause->setIcon (QIcon::fromTheme ("media-playback-pause"));
        window->playlistTabs->activePlaylistWidget ()->positionUpdate (); /* updates indicator icon */
    }

    static void playback_stop_cb (void * unused, MainWindow * window)
    {
        window->setWindowTitle ("Audacious");
        window->disableTimeCounter ();
        window->disableSlider ();

        window->actionPlayPause->setIcon (QIcon::fromTheme ("media-playback-start"));
        window->playlistTabs->activePlaylistWidget ()->positionUpdate (); /* updates indicator icon */
    }

    static void show_progress_cb (void * message, MainWindow * window)
    {
        window->createProgressDialog ();
        window->progressDialog->setInformativeText ((const char *) message);
        window->progressDialog->show ();
    }

    static void show_progress_2_cb (void * message, MainWindow * window)
    {
        window->createProgressDialog ();
        window->progressDialog->setText ((const char *) message);
        window->progressDialog->show ();
    }

    static void hide_progress_cb (void * unused, MainWindow * window)
    {
        if (window->progressDialog)
            window->progressDialog->hide ();
    }

    static void show_error_cb (void * message, MainWindow * window)
    {
        window->createErrorDialog (QString ((const char *) message));
    }
};

#endif
