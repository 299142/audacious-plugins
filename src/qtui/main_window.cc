/*
 * main_window.cc
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

#include <QtGui>

#include <libaudcore/audstrings.h>
#include <libaudcore/drct.h>
#include <libaudcore/hook.h>
#include <libaudcore/runtime.h>
#include <libaudcore/interface.h>

#include <libaudqt/libaudqt.h>

#include "filter_input.h"
#include "main_window.h"
#include "main_window.moc"
#include "main_window_actions.h"
#include "playlist.h"

MainWindow::MainWindow (QMainWindow * parent) : QMainWindow (parent)
{
    int lvol, rvol;

    QIcon::setThemeName ("QtUi");

    QString appDir = qApp->applicationDirPath ();
    QStringList paths = QIcon::themeSearchPaths ();
    paths.prepend (appDir + "/../share/audacious");
    QIcon::setThemeSearchPaths (paths);

    setupUi (this);

    setUnifiedTitleAndToolBarOnMac (true);

    volumeButton = new audqt::VolumeButton (this, 0, 100);

    aud_drct_get_volume (&lvol, &rvol);
    volumeButton->setValue ((lvol + rvol) / 2);

    connect (volumeButton, &audqt::VolumeButton::valueChanged, [=] (int value) {
        aud_drct_set_volume (value, value);
    });

    toolBar->addWidget (volumeButton);

    filterInput = new FilterInput ();

    toolBar->addWidget (filterInput);

    slider = new QSlider (Qt::Horizontal);
    slider->setDisabled (true);
    slider->setFocusPolicy (Qt::NoFocus);

    timeCounterLabel = new QLabel ("0:00 / 0:00");
    timeCounterLabel->setContentsMargins (5, 0, 0, 2);
    timeCounterLabel->setDisabled (true);

    timeCounter = new QTimer;
    timeCounter->setInterval (250);

    toolBar->insertWidget (actionRepeat, slider);
    toolBar->insertWidget (actionRepeat, timeCounterLabel);

    playlistTabs = new PlaylistTabs;
    playlistTabs->setFocusPolicy (Qt::NoFocus);
    mainLayout->addWidget (playlistTabs);

    createStatusBar ();

    updateToggles ();

    connect (actionAbout, &QAction::triggered, aud_ui_show_about_window);
    connect (actionPreferences, &QAction::triggered, aud_ui_show_prefs_window);

    connect (actionQuit, &QAction::triggered, aud_quit);

    connect (actionRepeat, &QAction::toggled, [=] (bool checked)
    {
        aud_set_bool (nullptr, "repeat", checked);
    });

    connect (actionShuffle, &QAction::triggered, [=] (bool checked)
    {
        aud_set_bool (nullptr, "shuffle", checked);
    });

    connect (actionNoPlaylistAdvance, &QAction::triggered, [=] (bool checked)
    {
        aud_set_bool (nullptr, "no_playlist_advance", checked);
    });

    connect (actionStopAfterThisSong, &QAction::triggered, [=] (bool checked)
    {
        aud_set_bool (nullptr, "stop_after_current_song", checked);
    });

    connect (actionOpenFiles, &QAction::triggered, audqt::fileopener_show);

    connect (actionAddFiles,  &QAction::triggered, [=] ()
    {
        audqt::fileopener_show (true);
    });

    connect (actionPlayPause, &QAction::triggered, aud_drct_play_pause);
    connect (actionStop,      &QAction::triggered, aud_drct_stop);
    connect (actionPrevious,  &QAction::triggered, aud_drct_pl_prev);
    connect (actionNext,      &QAction::triggered, aud_drct_pl_next);

    connect (actionEqualizer, &QAction::triggered, audqt::equalizer_show);

    connect (timeCounter, &QTimer::timeout,         this, &MainWindow::timeCounterSlot);
    connect (slider,      &QSlider::valueChanged,   this, &MainWindow::sliderValueChanged);
    connect (slider,      &QSlider::sliderPressed,  this, &MainWindow::sliderPressed);
    connect (slider,      &QSlider::sliderReleased, this, &MainWindow::sliderReleased);

    connect (filterInput, &QLineEdit::textChanged, playlistTabs, &PlaylistTabs::filterTrigger);

    setupActions ();

    hook_associate ("title change",     (HookFunction) title_change_cb, this);
    hook_associate ("playback begin",   (HookFunction) playback_begin_cb, this);
    hook_associate ("playback ready",   (HookFunction) playback_ready_cb, this);
    hook_associate ("playback pause",   (HookFunction) pause_cb, this);
    hook_associate ("playback unpause", (HookFunction) pause_cb, this);
    hook_associate ("playback stop",    (HookFunction) playback_stop_cb, this);

    hook_associate ("set repeat",                  (HookFunction) update_toggles_cb, this);
    hook_associate ("set shuffle",                 (HookFunction) update_toggles_cb, this);
    hook_associate ("set no_playlist_advance",     (HookFunction) update_toggles_cb, this);
    hook_associate ("set stop_after_current_song", (HookFunction) update_toggles_cb, this);

    hook_associate ("ui show progress",   (HookFunction) show_progress_cb, this);
    hook_associate ("ui show progress 2", (HookFunction) show_progress_2_cb, this);
    hook_associate ("ui hide progress",   (HookFunction) hide_progress_cb, this);
    hook_associate ("ui show error",      (HookFunction) show_error_cb, this);

    if (aud_drct_get_playing ())
    {
        playback_begin_cb (nullptr, this);
        if (aud_drct_get_ready ())
            playback_ready_cb (nullptr, this);
    }
    else
        playback_stop_cb (nullptr, this);

    title_change_cb (nullptr, this);
}

MainWindow::~MainWindow ()
{
    hook_dissociate ("title change",     (HookFunction) title_change_cb);
    hook_dissociate ("playback begin",   (HookFunction) playback_begin_cb);
    hook_dissociate ("playback ready",   (HookFunction) playback_ready_cb);
    hook_dissociate ("playback pause",   (HookFunction) pause_cb);
    hook_dissociate ("playback unpause", (HookFunction) pause_cb);
    hook_dissociate ("playback stop",    (HookFunction) playback_stop_cb);

    hook_dissociate ("ui show progress",   (HookFunction) show_progress_cb);
    hook_dissociate ("ui show progress 2", (HookFunction) show_progress_2_cb);
    hook_dissociate ("ui hide progress",   (HookFunction) hide_progress_cb);
    hook_dissociate ("ui show error",      (HookFunction) show_error_cb);

    hook_dissociate ("set repeat",                  (HookFunction) update_toggles_cb);
    hook_dissociate ("set shuffle",                 (HookFunction) update_toggles_cb);
    hook_dissociate ("set no_playlist_advance",     (HookFunction) update_toggles_cb);
    hook_dissociate ("set stop_after_current_song", (HookFunction) update_toggles_cb);

    hook_dissociate ("playlist activate", (HookFunction) update_playlist_length_cb);
    hook_dissociate ("playlist update",   (HookFunction) update_playlist_length_cb);

    hook_dissociate ("playback ready", (HookFunction) update_codec_info_cb);
    hook_dissociate ("info change",    (HookFunction) update_codec_info_cb);

    delete slider;
    delete timeCounterLabel;
    delete timeCounter;
    delete progressDialog;
    delete errorDialog;
    delete playlistTabs;
    delete filterInput;
    delete playlistLengthLabel;
    delete codecInfoLabel;
}

void MainWindow::timeCounterSlot ()
{
    if (slider->isSliderDown ())
        return;

    int time   = aud_drct_get_time ();
    int length = aud_drct_get_length ();
    setTimeCounterLabel (time, length);

    slider->setValue (time);
}

void MainWindow::setTimeCounterLabel (int time, int length)
{
    QString text = QString (str_format_time (time)) + QString (" / ") + QString (str_format_time (length));
    timeCounterLabel->setText (text);
}

void MainWindow::enableSlider ()
{
    int time = aud_drct_get_time ();
    int length = aud_drct_get_length ();

    slider->setRange (0, length);
    slider->setValue (time);
    slider->setDisabled (false);
}

void MainWindow::disableSlider ()
{
    slider->setRange (0, 0);
    slider->setDisabled (true);
}

void MainWindow::enableTimeCounter ()
{
    int time   = aud_drct_get_time ();
    int length = aud_drct_get_length ();

    setTimeCounterLabel (time, length);
    timeCounter->start ();
    timeCounterLabel->setDisabled (false);
}

void MainWindow::disableTimeCounter ()
{
    timeCounter->stop ();
    timeCounterLabel->setText ("0:00 / 0:00");
    timeCounterLabel->setDisabled (true);
}

void MainWindow::sliderValueChanged (int value)
{
    if (! slider->isSliderDown ())
        return;

    int length = aud_drct_get_length ();
    setTimeCounterLabel (value, length);
}

void MainWindow::sliderPressed ()
{
    timeCounter->stop ();
}

void MainWindow::sliderReleased ()
{
    aud_drct_seek (slider->value ());
    timeCounter->start ();
}

void MainWindow::createProgressDialog ()
{
    if (! progressDialog)
    {
        progressDialog = new QMessageBox (this);
        progressDialog->setIcon (QMessageBox::Information);
        progressDialog->setText ("Working ...");
        progressDialog->setStandardButtons (QMessageBox::NoButton);
        progressDialog->setWindowModality (Qt::WindowModal);
    }
}

void MainWindow::createErrorDialog (const QString &message)
{
    if (! errorDialog)
    {
        errorDialog = new QMessageBox (this);
        errorDialog->setIcon (QMessageBox::Warning);
        errorDialog->setWindowModality (Qt::WindowModal);
    }
    errorDialog->setText (message);
    errorDialog->show ();
}

void MainWindow::closeEvent (QCloseEvent * e)
{
    aud_quit ();
    e->ignore ();
}

void MainWindow::keyPressEvent (QKeyEvent * e)
{
    switch (e->modifiers ())
    {
    case Qt::ControlModifier:
        switch (e->key ())
        {
        case Qt::Key_F:
            filterInput->setFocus ();
            break;
        }
        break;
    }

    QMainWindow::keyPressEvent (e);
}

void MainWindow::updateToggles ()
{
    actionRepeat->setChecked (aud_get_bool (nullptr, "repeat"));
    actionShuffle->setChecked (aud_get_bool (nullptr, "shuffle"));
    actionNoPlaylistAdvance->setChecked (aud_get_bool (nullptr, "no_playlist_advance"));
    actionStopAfterThisSong->setChecked (aud_get_bool (nullptr, "stop_after_current_song"));
}

void MainWindow::createStatusBar ()
{
    QStatusBar * bar = QMainWindow::statusBar();

    playlistLengthLabel = new QLabel ("0:00 / 0:00");
    playlistLengthLabel->setAlignment(Qt::AlignRight);

    codecInfoLabel = new QLabel ("");

    bar->addPermanentWidget(playlistLengthLabel);
    bar->addWidget(codecInfoLabel);

    hook_associate ("playlist activate", (HookFunction) update_playlist_length_cb, playlistLengthLabel);
    hook_associate ("playlist update", (HookFunction) update_playlist_length_cb, playlistLengthLabel);

    hook_associate ("playback ready", (HookFunction) update_codec_info_cb, codecInfoLabel);
    hook_associate ("info change", (HookFunction) update_codec_info_cb, codecInfoLabel);
}
