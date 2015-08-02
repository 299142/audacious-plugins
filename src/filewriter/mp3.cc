/*  FileWriter-Plugin
 *  (C) copyright 2007 merging of Disk Writer and Out-Lame by Michael Färber
 *
 *  Original Out-Lame-Plugin:
 *  (C) copyright 2002 Lars Siebold <khandha5@gmx.net>
 *  (C) copyright 2006-2007 porting to audacious by Yoshiki Yazawa <yaz@cc.rim.or.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/* #define AUD_DEBUG 1 */

#include "filewriter.h"

#ifdef FILEWRITER_MP3

#include <string.h>
#include <lame/lame.h>
#include <gtk/gtk.h>

#include <libaudcore/audstrings.h>
#include <libaudcore/i18n.h>
#include <libaudcore/runtime.h>

#define MODES 4
static const int modes[MODES] = {NOT_SET, JOINT_STEREO, STEREO, MONO};
static const char * const mode_names[MODES] = {N_("Auto"), N_("Joint Stereo"),
 N_("Stereo"), N_("Mono")};

static GtkWidget *alg_quality_spin;
static GtkWidget *alg_quality_hbox;
static GtkAdjustment * alg_quality_adj;
static GtkWidget *notebook;
static GtkWidget *quality_vbox, *quality_hbox1, *alg_quality_frame;
static GtkWidget *enc_quality_frame, *enc_quality_label1, *enc_quality_label2;
static GtkWidget * enc_radio1, * enc_radio2;
static GtkWidget *compression_spin;
static GtkAdjustment * compression_adj;
static GtkWidget * mode_hbox, * mode_frame;
static GtkWidget * samplerate_hbox, * samplerate_label, * samplerate_frame;
static GtkWidget *misc_frame, *misc_vbox, *enforce_iso_toggle,
                 *error_protection_toggle;
static GtkWidget *vbr_vbox, *vbr_toggle, *vbr_options_vbox, *vbr_type_frame,
                 *vbr_type_hbox, *vbr_type_radio1, *vbr_type_radio2;
static GtkWidget * abr_frame, * abr_hbox, * abr_label;
static GtkWidget *vbr_frame, *vbr_options_vbox2;
static GtkWidget * vbr_options_hbox1, * vbr_min_label;
static GtkWidget * vbr_options_hbox2, * vbr_max_label, * enforce_min_toggle;
static GtkWidget *vbr_options_hbox3, *vbr_quality_spin, *vbr_quality_label;
static GtkAdjustment * vbr_quality_adj;
static GtkWidget *xing_header_toggle;
static GtkWidget *tags_vbox, *tags_frames_frame, *tags_frames_hbox,
                 *tags_copyright_toggle, *tags_original_toggle;
static GtkWidget *tags_id3_frame, *tags_id3_vbox, *tags_id3_hbox,
                 *tags_force_id3v2_toggle, *tags_only_v1_toggle, *tags_only_v2_toggle;

static GtkWidget *enc_quality_vbox, *hbox1, *hbox2;

static unsigned long numsamples = 0;
static int inside;

static int available_samplerates[] =
{ 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 } ;

static int available_bitrates[] =
{ 8, 16, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 } ;

struct lameid3_t {
    String track_name;
    String album_name;
    String performer;
    String genre;
    String year;
    String track_number;
};

static lameid3_t lameid3;

static lame_global_flags *gfp;
static unsigned char encbuffer[LAME_MAXMP3BUFFER];
static int id3v2_size;

static int channels;
static Index<unsigned char> write_buffer;

static void lame_debugf(const char *format, va_list ap)
{
    (void) vprintf(format, ap);
}

static const char * const mp3_defaults[] = {
 "vbr_on", "0",
 "vbr_type", "0",
 "vbr_min_val", "32",
 "vbr_max_val", "320",
 "enforce_min_val", "0",
 "vbr_quality_val", "4",
 "abr_val", "128",
 "toggle_xing_val", "1",
 "mark_original_val", "1",
 "mark_copyright_val", "0",
 "force_v2_val", "0",
 "only_v1_val", "0",
 "only_v2_val", "0",
 "algo_quality_val", "5",
 "out_samplerate_val", "0",
 "bitrate_val", "128",
 "compression_val", "11",
 "enc_toggle_val", "0",
 "audio_mode_val", aud::numeric_string<NOT_SET>::str,
 "enforce_iso_val", "0",
 "error_protect_val", "0",
 nullptr};

#define GET_INT(n)       aud_get_int("filewriter_mp3", n)
#define SET_INT(n, v)    aud_set_int("filewriter_mp3", n, v)
#define GET_DOUBLE(n)    aud_get_double("filewriter_mp3", n)
#define SET_DOUBLE(n, v) aud_set_double("filewriter_mp3", n, v)

static void mp3_init ()
{
    aud_config_set_defaults ("filewriter_mp3", mp3_defaults);
}

static bool mp3_open (VFSFile & file, const format_info & info, const Tuple & tuple)
{
    int imp3;

    gfp = lame_init();
    if (gfp == nullptr)
        return false;

    /* setup id3 data */
    id3tag_init(gfp);

    /* XXX write UTF-8 even though libmp3lame does id3v2.3. --yaz */
    lameid3.track_name = tuple.get_str (Tuple::Title);
    id3tag_set_title(gfp, lameid3.track_name);

    lameid3.performer = tuple.get_str (Tuple::Artist);
    id3tag_set_artist(gfp, lameid3.performer);

    lameid3.album_name = tuple.get_str (Tuple::Album);
    id3tag_set_album(gfp, lameid3.album_name);

    lameid3.genre = tuple.get_str (Tuple::Genre);
    id3tag_set_genre(gfp, lameid3.genre);

    lameid3.year = String (int_to_str (tuple.get_int (Tuple::Year)));
    id3tag_set_year(gfp, lameid3.year);

    lameid3.track_number = String (int_to_str (tuple.get_int (Tuple::Track)));
    id3tag_set_track(gfp, lameid3.track_number);

    if (GET_INT("force_v2_val"))
        id3tag_add_v2(gfp);
    if (GET_INT("only_v1_val"))
        id3tag_v1_only(gfp);
    if (GET_INT("only_v2_val"))
        id3tag_v2_only(gfp);

    /* input stream description */

    lame_set_in_samplerate(gfp, info.frequency);
    lame_set_num_channels(gfp, info.channels);
    lame_set_out_samplerate(gfp, GET_INT("out_samplerate_val"));

    /* general control parameters */

    lame_set_bWriteVbrTag(gfp, GET_INT("toggle_xing_val"));
    lame_set_quality(gfp, GET_INT("algo_quality_val"));

    const int audio_mode_val = GET_INT("audio_mode_val");
    if (audio_mode_val != NOT_SET) {
        AUDDBG("set mode to %d\n", audio_mode_val);
        lame_set_mode(gfp, (MPEG_mode) audio_mode_val);
    }

    lame_set_errorf(gfp, lame_debugf);
    lame_set_debugf(gfp, lame_debugf);
    lame_set_msgf(gfp, lame_debugf);

    const int vbr_on = GET_INT("vbr_on");
    if (GET_INT("enc_toggle_val") == 0 && vbr_on == 0)
        lame_set_brate(gfp, GET_INT("bitrate_val"));
    else if (vbr_on == 0)
        lame_set_compression_ratio(gfp, GET_DOUBLE("compression_val"));

    /* frame params */

    lame_set_copyright(gfp, GET_INT("mark_copyright_val"));
    lame_set_original(gfp, GET_INT("mark_original_val"));
    lame_set_error_protection(gfp, GET_INT("error_protect_val"));
    lame_set_strict_ISO(gfp, GET_INT("enforce_iso_val"));

    if (vbr_on != 0) {
        const int vbr_min_val = GET_INT("vbr_min_val");
        const int vbr_max_val = GET_INT("vbr_max_val");

        if (GET_INT("vbr_type") == 0)
            lame_set_VBR(gfp, vbr_rh);
        else
            lame_set_VBR(gfp, vbr_abr);

        lame_set_VBR_q(gfp, GET_INT("vbr_quality_val"));
        lame_set_VBR_mean_bitrate_kbps(gfp, GET_INT("abr_val"));
        lame_set_VBR_min_bitrate_kbps(gfp, vbr_min_val);
        lame_set_VBR_max_bitrate_kbps(gfp, aud::max(vbr_min_val, vbr_max_val));
        lame_set_VBR_hard_min(gfp, GET_INT("enforce_min_val"));
    }

    /* not to write id3 tag automatically. */
    lame_set_write_id3tag_automatic(gfp, 0);

    if (lame_init_params(gfp) == -1)
        return false;

    /* write id3v2 header */
    imp3 = lame_get_id3v2_tag(gfp, encbuffer, sizeof(encbuffer));

    if (imp3 > 0) {
        if (file.fwrite (encbuffer, 1, imp3) != imp3)
            AUDERR ("write error\n");
        id3v2_size = imp3;
    }
    else {
        id3v2_size = 0;
    }

    channels = info.channels;
    return true;
}

static void mp3_write (VFSFile & file, const void * data, int length)
{
    int encoded;

    if (! write_buffer.len ())
        write_buffer.resize (8192);

    while (1)
    {
        if (channels == 1)
            encoded = lame_encode_buffer_ieee_float (gfp, (const float *) data,
             (const float *) data, length / sizeof (float),
             write_buffer.begin (), write_buffer.len ());
        else
            encoded = lame_encode_buffer_interleaved_ieee_float (gfp,
             (const float *) data, length / (2 * sizeof (float)),
             write_buffer.begin (), write_buffer.len ());

        if (encoded != -1)
            break;

        write_buffer.resize (write_buffer.len () * 2);
    }

    if (encoded > 0 && file.fwrite (write_buffer.begin (), 1, encoded) != encoded)
        AUDERR ("write error\n");

    numsamples += length / (2 * channels);
}

static void mp3_close (VFSFile & file)
{
    int imp3, encout;

    /* write remaining mp3 data */
    encout = lame_encode_flush_nogap(gfp, encbuffer, LAME_MAXMP3BUFFER);
    if (file.fwrite (encbuffer, 1, encout) != encout)
        AUDERR ("write error\n");

    /* set gfp->num_samples for valid TLEN tag */
    lame_set_num_samples(gfp, numsamples);

    /* append v1 tag */
    imp3 = lame_get_id3v1_tag(gfp, encbuffer, sizeof(encbuffer));
    if (imp3 > 0 && file.fwrite (encbuffer, 1, imp3) != imp3)
        AUDERR ("write error\n");

    /* update v2 tag */
    imp3 = lame_get_id3v2_tag(gfp, encbuffer, sizeof(encbuffer));
    if (imp3 > 0) {
        if (file.fseek (0, VFS_SEEK_SET) != 0)
            AUDERR ("seek error\n");
        else if (file.fwrite (encbuffer, 1, imp3) != imp3)
            AUDERR ("write error\n");
    }

    /* update lame tag */
    if (id3v2_size) {
        if (file.fseek (id3v2_size, VFS_SEEK_SET) != 0)
            AUDERR ("seek error\n");
        else {
            imp3 = lame_get_lametag_frame(gfp, encbuffer, sizeof(encbuffer));
            if (file.fwrite (encbuffer, 1, imp3) != imp3)
                AUDERR ("write error\n");
        }
    }

    write_buffer.clear ();

    lame_close(gfp);
    AUDDBG("lame_close() done\n");

    lameid3 = lameid3_t ();
    numsamples = 0;
}

/*****************/
/* Configuration */
/*****************/

/* Various Signal-Fuctions */

static void algo_qual(GtkAdjustment * adjustment, void * user_data)
{
    SET_INT("algo_quality_val",
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(alg_quality_spin)));

}

static void samplerate_changed (GtkComboBox * combo)
{
    int i = gtk_combo_box_get_active (combo) - 1;

    if (i >= 0 && i < aud::n_elems (available_samplerates))
        SET_INT("out_samplerate_val", available_samplerates[i]);
    else
        SET_INT("out_samplerate_val", 0);
}

static void bitrate_changed (GtkComboBox * combo)
{
    int i = gtk_combo_box_get_active (combo);

    if (i >= 0 && i < aud::n_elems (available_bitrates))
        SET_INT("bitrate_val", available_bitrates[i]);
    else
        SET_INT("bitrate_val", 128);
}

static void compression_change(GtkAdjustment * adjustment,
                               void * user_data)
{
    SET_DOUBLE("compression_val",
        gtk_spin_button_get_value(GTK_SPIN_BUTTON(compression_spin)));
}

static void encoding_toggle(GtkToggleButton * togglebutton,
                            void * user_data)
{
    SET_INT("enc_toggle_val", GPOINTER_TO_INT(user_data));
}

static void mode_changed (GtkComboBox * combo)
{
    int i = gtk_combo_box_get_active (combo);

    if (i >= 0 && i < MODES)
        SET_INT("audio_mode_val", modes[i]);
    else
        SET_INT("audio_mode_val", NOT_SET);
}

static void toggle_enforce_iso(GtkToggleButton * togglebutton,
                               void * user_data)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enforce_iso_toggle)))
        SET_INT("enforce_iso_val", 1);
    else
        SET_INT("enforce_iso_val", 0);
}

static void toggle_error_protect(GtkToggleButton * togglebutton,
                                 void * user_data)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(error_protection_toggle)))
        SET_INT("error_protect_val", 1);
    else
        SET_INT("error_protect_val", 0);
}

static void toggle_vbr(GtkToggleButton * togglebutton, void * user_data)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vbr_toggle)) ==
        true) {
        gtk_widget_set_sensitive(vbr_options_vbox, true);
        gtk_widget_set_sensitive(enc_quality_frame, false);
        SET_INT("vbr_on", 1);

        if (GET_INT("vbr_type") == 0) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (vbr_type_radio1), true);
            gtk_widget_set_sensitive(abr_frame, false);
            gtk_widget_set_sensitive(vbr_frame, true);
        }
        else {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (vbr_type_radio2), true);
            gtk_widget_set_sensitive(abr_frame, true);
            gtk_widget_set_sensitive(vbr_frame, false);
        }

    }
    else {
        gtk_widget_set_sensitive(vbr_options_vbox, false);
        gtk_widget_set_sensitive(enc_quality_frame, true);
        SET_INT("vbr_on", 0);
    }
}

static void vbr_abr_toggle(GtkToggleButton * togglebutton,
                           void * user_data)
{
    if (!strcmp((char *) user_data, "VBR")) {
        gtk_widget_set_sensitive(abr_frame, false);
        gtk_widget_set_sensitive(vbr_frame, true);
        SET_INT("vbr_type", 0);
    }
    else if (!strcmp((char *) user_data, "ABR")) {
        gtk_widget_set_sensitive(abr_frame, true);
        gtk_widget_set_sensitive(vbr_frame, false);
        SET_INT("vbr_type", 1);
    }
}

static void vbr_min_changed (GtkComboBox * combo)
{
    int i = gtk_combo_box_get_active (combo);

    if (i >= 0 && i < aud::n_elems (available_bitrates))
        SET_INT("vbr_min_val", available_bitrates[i]);
    else
        SET_INT("vbr_min_val", 32);
}

static void vbr_max_changed (GtkComboBox * combo)
{
    int i = gtk_combo_box_get_active (combo);

    if (i >= 0 && i < aud::n_elems (available_bitrates))
        SET_INT("vbr_max_val", available_bitrates[i]);
    else
        SET_INT("vbr_max_val", 320);
}

static void toggle_enforce_min(GtkToggleButton * togglebutton,
                               void * user_data)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enforce_min_toggle)))
        SET_INT("enforce_min_val", 1);
    else
        SET_INT("enforce_min_val", 0);
}

static void vbr_qual(GtkAdjustment * adjustment, void * user_data)
{
    SET_INT("vbr_quality_val",
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(vbr_quality_spin)));
}

static void abr_changed (GtkComboBox * combo)
{
    int i = gtk_combo_box_get_active (combo);

    if (i >= 0 && i < aud::n_elems (available_bitrates))
        SET_INT("abr_val", available_bitrates[i]);
    else
        SET_INT("abr_val", 128);
}

static void toggle_xing(GtkToggleButton * togglebutton, void * user_data)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(xing_header_toggle)))
        SET_INT("toggle_xing_val", 0);
    else
        SET_INT("toggle_xing_val", 1);
}

static void toggle_original(GtkToggleButton * togglebutton,
                            void * user_data)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tags_original_toggle)))
        SET_INT("mark_original_val", 1);
    else
        SET_INT("mark_original_val", 0);
}

static void toggle_copyright(GtkToggleButton * togglebutton,
                             void * user_data)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tags_copyright_toggle)))
        SET_INT("mark_copyright_val", 1);
    else
        SET_INT("mark_copyright_val", 0);
}

static void force_v2_toggle(GtkToggleButton * togglebutton,
                            void * user_data)
{

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tags_force_id3v2_toggle))) {
        SET_INT("force_v2_val", 1);
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tags_only_v1_toggle))) {
            inside = 1;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tags_only_v1_toggle), false);
            SET_INT("only_v1_val", 0);
            inside = 0;
        }
    }
    else
        SET_INT("force_v2_val", 0);

}

static void id3_only_version(GtkToggleButton * togglebutton,
                             void * user_data)
{
    if (!strcmp((char *) user_data, "v1") && inside != 1) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tags_only_v1_toggle)))
        {
            inside = 1;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tags_only_v2_toggle), false);
            SET_INT("only_v1_val", 1);
            SET_INT("only_v2_val", 0);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tags_force_id3v2_toggle), false);
            inside = 0;
        }
    }
    else if (!strcmp((char *) user_data, "v2") && inside != 1) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tags_only_v2_toggle)))
        {
            inside = 1;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tags_only_v1_toggle), false);
            SET_INT("only_v1_val", 0);
            SET_INT("only_v2_val", 1);
            inside = 0;
        }
    }

}

/************************/
/* Configuration Widget */
/************************/

void * mp3_configure ()
{
        notebook = gtk_notebook_new();

        /* Quality */

        quality_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(quality_vbox), 5);

        quality_hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_box_pack_start(GTK_BOX(quality_vbox), quality_hbox1, false, false, 0);

        /* Algorithm Quality */

        alg_quality_frame = gtk_frame_new(_("Algorithm Quality:"));
        gtk_box_pack_start(GTK_BOX(quality_hbox1), alg_quality_frame, true, true, 0);

        alg_quality_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(alg_quality_hbox), 5);
        gtk_container_add(GTK_CONTAINER(alg_quality_frame),
                          alg_quality_hbox);

        alg_quality_adj = (GtkAdjustment *) gtk_adjustment_new (5, 0, 9, 1, 1, 0);
        alg_quality_spin =
            gtk_spin_button_new(GTK_ADJUSTMENT(alg_quality_adj), 8, 0);
        gtk_box_pack_start(GTK_BOX(alg_quality_hbox), alg_quality_spin, false, false, 0);
        g_signal_connect (alg_quality_adj, "value-changed", (GCallback)
         algo_qual, nullptr);

        gtk_spin_button_set_value(GTK_SPIN_BUTTON(alg_quality_spin),
                                  GET_INT("algo_quality_val"));

        /* Output Samplerate */

        samplerate_frame = gtk_frame_new(_("Output Sample Rate:"));
        gtk_box_pack_start(GTK_BOX(quality_hbox1), samplerate_frame, true, true, 0);

        samplerate_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(samplerate_hbox), 5);
        gtk_container_add(GTK_CONTAINER(samplerate_frame),
                          samplerate_hbox);

        GtkWidget * combo = gtk_combo_box_text_new ();
        gtk_combo_box_text_append_text ((GtkComboBoxText *) combo, _("Auto"));

        const int out_samplerate_val = GET_INT("out_samplerate_val");
        if (! out_samplerate_val)
            gtk_combo_box_set_active ((GtkComboBox *) combo, 0);

        for (int i = 0; i < aud::n_elems (available_samplerates); i ++)
        {
            gtk_combo_box_text_append_text ((GtkComboBoxText *) combo,
             int_to_str (available_samplerates[i]));

            if (out_samplerate_val == available_samplerates[i])
                gtk_combo_box_set_active ((GtkComboBox *) combo, 1 + i);
        }

        gtk_box_pack_start ((GtkBox *) samplerate_hbox, combo, false, false, 0);
        g_signal_connect (combo, "changed", (GCallback) samplerate_changed, nullptr);

        samplerate_label = gtk_label_new(_("(Hz)"));
        gtk_widget_set_halign(samplerate_label, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(samplerate_hbox), samplerate_label, false, false, 0);

        /* Encoder Quality */

        enc_quality_frame = gtk_frame_new(_("Bitrate / Compression Ratio:"));
        gtk_box_pack_start(GTK_BOX(quality_vbox), enc_quality_frame, false, false, 0);

        // vbox sorrounding hbox1 and hbox2
        enc_quality_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(enc_quality_vbox), 5);

        // pack vbox to frame
        gtk_container_add(GTK_CONTAINER(enc_quality_frame), enc_quality_vbox);

        // hbox1 for bitrate
        hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_add(GTK_CONTAINER(enc_quality_vbox), hbox1);

        // radio 1
        enc_radio1 = gtk_radio_button_new(nullptr);
        gtk_box_pack_start(GTK_BOX(hbox1), enc_radio1, false, false, 0);

        // label 1
        enc_quality_label1 = gtk_label_new(_("Bitrate (kbps):"));
        gtk_box_pack_start(GTK_BOX(hbox1), enc_quality_label1, false, false, 0);

        // bitrate menu

        combo = gtk_combo_box_text_new ();

        const int bitrate_val = GET_INT("bitrate_val");
        for (int i = 0; i < aud::n_elems (available_bitrates); i ++)
        {
            gtk_combo_box_text_append_text ((GtkComboBoxText *) combo,
             int_to_str (available_bitrates[i]));

            if (bitrate_val == available_bitrates[i])
                gtk_combo_box_set_active ((GtkComboBox *) combo, i);
        }

        gtk_box_pack_start ((GtkBox *) hbox1, combo, false, false, 0);
        g_signal_connect (combo, "changed", (GCallback) bitrate_changed, nullptr);

        // hbox2 for compression ratio
        hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_add(GTK_CONTAINER(enc_quality_vbox), hbox2);

        // radio 2
        enc_radio2 = gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(enc_radio1));
        gtk_box_pack_start(GTK_BOX(hbox2), enc_radio2, false, false, 0);

        // label
        enc_quality_label2 = gtk_label_new(_("Compression ratio:"));
        gtk_box_pack_start(GTK_BOX(hbox2), enc_quality_label2, false, false, 0);

        // comp-ratio spin
        compression_adj = (GtkAdjustment *) gtk_adjustment_new (11, 0, 100, 1, 1, 0);
        compression_spin =
            gtk_spin_button_new(GTK_ADJUSTMENT(compression_adj), 8, 0);
        gtk_box_pack_start(GTK_BOX(hbox2), compression_spin, false, false, 0);

        g_signal_connect (compression_adj, "value-changed", (GCallback)
         compression_change, nullptr);

        gtk_spin_button_set_value(GTK_SPIN_BUTTON(compression_spin),
                                  GET_DOUBLE("compression_val"));

        if (GET_INT("enc_toggle_val") == 0)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enc_radio1), true);
        else
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enc_radio2), true);

        // radio button signale connect
        g_signal_connect (enc_radio1, "toggled", (GCallback) encoding_toggle,
         GINT_TO_POINTER (0));
        g_signal_connect (enc_radio2, "toggled", (GCallback) encoding_toggle,
         GINT_TO_POINTER (1));

        /* Audio Mode */

        mode_frame = gtk_frame_new(_("Audio Mode:"));
        gtk_box_pack_start(GTK_BOX(quality_vbox), mode_frame, false, false, 0);

        mode_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(mode_hbox), 5);
        gtk_container_add(GTK_CONTAINER(mode_frame), mode_hbox);

        combo = gtk_combo_box_text_new ();

        const int audio_mode_val = GET_INT("audio_mode_val");
        for (int i = 0; i < MODES; i ++)
        {
            gtk_combo_box_text_append_text ((GtkComboBoxText *) combo,
             _(mode_names[i]));

            if (audio_mode_val == modes[i])
                gtk_combo_box_set_active ((GtkComboBox *) combo, i);
        }

        gtk_box_pack_start ((GtkBox *) mode_hbox, combo, false, false, 0);
        g_signal_connect (combo, "changed", (GCallback) mode_changed, nullptr);

        /* Misc */

        misc_frame = gtk_frame_new(_("Miscellaneous:"));
        gtk_box_pack_start(GTK_BOX(quality_vbox), misc_frame, false, false, 0);

        misc_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(misc_vbox), 5);
        gtk_container_add(GTK_CONTAINER(misc_frame), misc_vbox);

        enforce_iso_toggle =
            gtk_check_button_new_with_label
            (_("Enforce strict ISO compliance"));
        gtk_box_pack_start(GTK_BOX(misc_vbox), enforce_iso_toggle, true, true, 0);
        g_signal_connect (enforce_iso_toggle, "toggled", (GCallback)
         toggle_enforce_iso, nullptr);

        if (GET_INT("enforce_iso_val") == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (enforce_iso_toggle), true);

        error_protection_toggle =
            gtk_check_button_new_with_label(_("Error protection"));
        gtk_box_pack_start(GTK_BOX(misc_vbox), error_protection_toggle, true, true, 0);
        g_signal_connect (error_protection_toggle, "toggled", (GCallback)
         toggle_error_protect, nullptr);

        if (GET_INT("error_protect_val") == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (error_protection_toggle), true);

        /* Add the Notebook */
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), quality_vbox,
                                 gtk_label_new(_("Quality")));

        /* VBR/ABR */

        vbr_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(vbr_vbox), 5);

        /* Toggle VBR */

        vbr_toggle = gtk_check_button_new_with_label(_("Enable VBR/ABR"));
        gtk_box_pack_start(GTK_BOX(vbr_vbox), vbr_toggle, false, false, 0);
        g_signal_connect (vbr_toggle, "toggled", (GCallback) toggle_vbr, nullptr);

        vbr_options_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_add(GTK_CONTAINER(vbr_vbox), vbr_options_vbox);
        gtk_widget_set_sensitive(vbr_options_vbox, false);

        /* Choose VBR/ABR */

        vbr_type_frame = gtk_frame_new(_("Type:"));
        gtk_box_pack_start(GTK_BOX(vbr_options_vbox), vbr_type_frame, false, false, 0);

        vbr_type_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(vbr_type_hbox), 5);
        gtk_container_add(GTK_CONTAINER(vbr_type_frame), vbr_type_hbox);

        vbr_type_radio1 = gtk_radio_button_new_with_label(nullptr, "VBR");
        gtk_box_pack_start(GTK_BOX(vbr_type_hbox), vbr_type_radio1, false, false, 0);

        vbr_type_radio2 =
            gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON
                                                        (vbr_type_radio1),
                                                        "ABR");
        gtk_box_pack_start(GTK_BOX(vbr_type_hbox), vbr_type_radio2, false, false, 0);

        if (GET_INT("vbr_type") == 0)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vbr_type_radio1), true);
        else
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vbr_type_radio2), true);

        g_signal_connect (vbr_type_radio1, "toggled", (GCallback)
         vbr_abr_toggle, (void *) "VBR");
        g_signal_connect (vbr_type_radio2, "toggled", (GCallback)
         vbr_abr_toggle, (void *) "ABR");

        /* VBR Options */

        vbr_frame = gtk_frame_new(_("VBR Options:"));
        gtk_box_pack_start(GTK_BOX(vbr_options_vbox), vbr_frame, false, false, 0);

        vbr_options_vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(vbr_options_vbox2), 5);
        gtk_container_add(GTK_CONTAINER(vbr_frame), vbr_options_vbox2);

        vbr_options_hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_add(GTK_CONTAINER(vbr_options_vbox2),
                          vbr_options_hbox1);

        vbr_min_label = gtk_label_new(_("Minimum bitrate (kbps):"));
        gtk_widget_set_halign(vbr_min_label, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(vbr_options_hbox1), vbr_min_label, false, false, 0);

        combo = gtk_combo_box_text_new ();

        const int vbr_min_val = GET_INT("vbr_min_val");
        for (int i = 0; i < aud::n_elems (available_bitrates); i ++)
        {
            gtk_combo_box_text_append_text ((GtkComboBoxText *) combo,
             int_to_str (available_bitrates[i]));

            if (vbr_min_val == available_bitrates[i])
                gtk_combo_box_set_active ((GtkComboBox *) combo, i);
        }

        gtk_box_pack_start ((GtkBox *) vbr_options_hbox1, combo, false, false, 0);
        g_signal_connect (combo, "changed", (GCallback) vbr_min_changed, nullptr);

        vbr_options_hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_add(GTK_CONTAINER(vbr_options_vbox2),
                          vbr_options_hbox2);

        vbr_max_label = gtk_label_new(_("Maximum bitrate (kbps):"));
        gtk_widget_set_halign(vbr_max_label, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(vbr_options_hbox2), vbr_max_label, false, false, 0);

        combo = gtk_combo_box_text_new ();

        const int vbr_max_val = GET_INT("vbr_max_val");
        for (int i = 0; i < aud::n_elems (available_bitrates); i ++)
        {
            gtk_combo_box_text_append_text ((GtkComboBoxText *) combo,
             int_to_str (available_bitrates[i]));

            if (vbr_max_val == available_bitrates[i])
                gtk_combo_box_set_active ((GtkComboBox *) combo, i);
        }

        gtk_box_pack_start ((GtkBox *) vbr_options_hbox2, combo, false, false, 0);
        g_signal_connect (combo, "changed", (GCallback) vbr_max_changed, nullptr);

        enforce_min_toggle =
            gtk_check_button_new_with_label
            (_("Strictly enforce minimum bitrate"));
        gtk_box_pack_start(GTK_BOX(vbr_options_vbox2), enforce_min_toggle, false, false, 0);
        g_signal_connect (enforce_min_toggle, "toggled", (GCallback)
         toggle_enforce_min, nullptr);

        if (GET_INT("enforce_min_val") == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (enforce_min_toggle), true);

        /* ABR Options */

        abr_frame = gtk_frame_new(_("ABR Options:"));
        gtk_box_pack_start(GTK_BOX(vbr_options_vbox), abr_frame, false, false, 0);
        gtk_widget_set_sensitive(abr_frame, false);

        abr_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(abr_hbox), 5);
        gtk_container_add(GTK_CONTAINER(abr_frame), abr_hbox);

        abr_label = gtk_label_new(_("Average bitrate (kbps):"));
        gtk_widget_set_halign(abr_label, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(abr_hbox), abr_label, false, false, 0);

        combo = gtk_combo_box_text_new ();

        const int abr_val = GET_INT("abr_val");
        for (int i = 0; i < aud::n_elems (available_bitrates); i ++)
        {
            gtk_combo_box_text_append_text ((GtkComboBoxText *) combo,
             int_to_str (available_bitrates[i]));

            if (abr_val == available_bitrates[i])
                gtk_combo_box_set_active ((GtkComboBox *) combo, i);
        }

        gtk_box_pack_start ((GtkBox *) abr_hbox, combo, false, false, 0);
        g_signal_connect (combo, "changed", (GCallback) abr_changed, nullptr);

        /* Quality Level */

        vbr_options_hbox3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_add(GTK_CONTAINER(vbr_options_vbox),
                          vbr_options_hbox3);

        vbr_quality_label = gtk_label_new(_("VBR quality level:"));
        gtk_widget_set_halign(vbr_quality_label, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(vbr_options_hbox3), vbr_quality_label, false, false, 0);

        vbr_quality_adj = (GtkAdjustment *) gtk_adjustment_new (4, 0, 9, 1, 1, 0);
        vbr_quality_spin =
            gtk_spin_button_new(GTK_ADJUSTMENT(vbr_quality_adj), 8, 0);
        gtk_box_pack_start(GTK_BOX(vbr_options_hbox3), vbr_quality_spin, false, false, 0);
        g_signal_connect (vbr_quality_adj, "value-changed", (GCallback)
         vbr_qual, nullptr);

        gtk_spin_button_set_value(GTK_SPIN_BUTTON(vbr_quality_spin),
                                  GET_INT("vbr_quality_val"));

        /* Xing Header */

        xing_header_toggle =
            gtk_check_button_new_with_label(_("Omit Xing VBR header"));
        gtk_box_pack_start(GTK_BOX(vbr_options_vbox), xing_header_toggle, false, false, 0);
        g_signal_connect (xing_header_toggle, "toggled", (GCallback)
         toggle_xing, nullptr);

        if (GET_INT("toggle_xing_val") == 0)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (xing_header_toggle), true);

        /* Add the Notebook */

        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbr_vbox,
                                 gtk_label_new(_("VBR/ABR")));

        /* Tags */

        tags_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(tags_vbox), 5);

        /* Frame Params */

        tags_frames_frame = gtk_frame_new(_("Frame Parameters:"));
        gtk_box_pack_start(GTK_BOX(tags_vbox), tags_frames_frame, false, false, 0);

        tags_frames_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(tags_frames_hbox), 5);
        gtk_container_add(GTK_CONTAINER(tags_frames_frame),
                          tags_frames_hbox);

        tags_copyright_toggle =
            gtk_check_button_new_with_label(_("Mark as copyright"));
        gtk_box_pack_start(GTK_BOX(tags_frames_hbox),
                           tags_copyright_toggle, false, false, 0);
        g_signal_connect (tags_copyright_toggle, "toggled", (GCallback)
         toggle_copyright, nullptr);

        if (GET_INT("mark_copyright_val") == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_copyright_toggle), true);

        tags_original_toggle =
            gtk_check_button_new_with_label(_("Mark as original"));
        gtk_box_pack_start(GTK_BOX(tags_frames_hbox), tags_original_toggle, false, false, 0);
        g_signal_connect (tags_original_toggle, "toggled", (GCallback)
         toggle_original, nullptr);

        if (GET_INT("mark_original_val") == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_original_toggle), true);

        /* ID3 Params */

        tags_id3_frame = gtk_frame_new(_("ID3 Parameters:"));
        gtk_box_pack_start(GTK_BOX(tags_vbox), tags_id3_frame, false, false, 0);

        tags_id3_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(tags_id3_vbox), 5);
        gtk_container_add(GTK_CONTAINER(tags_id3_frame), tags_id3_vbox);

        tags_force_id3v2_toggle =
            gtk_check_button_new_with_label
            (_("Force addition of version 2 tag"));
        gtk_box_pack_start(GTK_BOX(tags_id3_vbox), tags_force_id3v2_toggle, false, false, 0);
        g_signal_connect (tags_force_id3v2_toggle, "toggled", (GCallback)
         force_v2_toggle, nullptr);

        tags_id3_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_add(GTK_CONTAINER(tags_id3_vbox), tags_id3_hbox);

        tags_only_v1_toggle =
            gtk_check_button_new_with_label(_("Only add v1 tag"));
        gtk_box_pack_start(GTK_BOX(tags_id3_hbox), tags_only_v1_toggle, false, false, 0);
        g_signal_connect (tags_only_v1_toggle, "toggled", (GCallback)
         id3_only_version, (void *) "v1");

        tags_only_v2_toggle =
            gtk_check_button_new_with_label(_("Only add v2 tag"));
        gtk_box_pack_start(GTK_BOX(tags_id3_hbox), tags_only_v2_toggle, false, false, 0);
        g_signal_connect (tags_only_v2_toggle, "toggled", (GCallback)
         id3_only_version, (void *) "v2");

        if (GET_INT("force_v2_val") == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_force_id3v2_toggle), true);

        if (GET_INT("only_v1_val") == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_only_v1_toggle), true);

        if (GET_INT("only_v2_val") == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_only_v2_toggle), true);

        /* Add the Notebook */

        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tags_vbox,
                                 gtk_label_new(_("Tags")));

        /* Set States */

        if (GET_INT("vbr_on") == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vbr_toggle),
                                         true);
        else
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vbr_toggle),
                                         false);

        return notebook;
}

static int mp3_format_required (int fmt)
{
    return FMT_FLOAT;
}

FileWriterImpl mp3_plugin = {
    mp3_init,
    mp3_open,
    mp3_write,
    mp3_close,
    mp3_format_required,
};

#endif
