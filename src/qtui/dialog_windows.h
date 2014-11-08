/*
 * dialog_windows.h
 * Copyright 2014 John Lindgren and Michał Lipski
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

#ifndef DIALOG_WINDOWS_H
#define DIALOG_WINDOWS_H

#include <libaudcore/hook.h>

class QMessageBox;
class QWidget;

class DialogWindows
{
public:
    DialogWindows (QWidget * parent);
    ~DialogWindows ();

private:
    QWidget * m_parent;
    QMessageBox * m_progress = nullptr;
    QMessageBox * m_error = nullptr;

    void create_progress ();
    void show_error (const char * message);
    void show_progress (const char * message);
    void show_progress_2 (const char * message);
    void hide_progress ();

    // unfortunately GCC cannot handle these as an array
    HookReceiver<DialogWindows, const char *> show_hook1, show_hook2, show_hook3;
    HookReceiver<DialogWindows> hide_hook;
};

#endif // DIALOG_WINDOWS_H
