#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include <neaacdec.h>

#include "mp4ff.h"

#include <libaudcore/input.h>
#include <libaudcore/plugin.h>
#include <libaudcore/i18n.h>

/*
 * BUFFER_SIZE is the highest amount of memory that can be pulled.
 * We use this for sanity checks, among other things, as mp4ff needs
 * a labotomy sometimes.
 */
#define BUFFER_SIZE (FAAD_MIN_STREAMSIZE * 16)

static const char *fmts[] = { "m4a", "mp4", nullptr };

int getAACTrack (mp4ff_t *);

static uint32_t mp4_read_callback (void *data, void *buffer, uint32_t len)
{
    if (data == nullptr || buffer == nullptr)
        return -1;

    return vfs_fread (buffer, 1, len, (VFSFile *) data);
}

static uint32_t mp4_seek_callback (void *data, uint64_t pos)
{
    if (! data || pos > INT64_MAX)
        return -1;

    return vfs_fseek ((VFSFile *) data, pos, SEEK_SET);
}

static bool is_mp4_aac_file (const char * filename, VFSFile * handle)
{
    mp4ff_callback_t mp4_data = {
        mp4_read_callback,
        nullptr,  // write
        mp4_seek_callback,
        nullptr,  // truncate
        handle
    };

    mp4ff_t *mp4_handle = mp4ff_open_read (&mp4_data);
    bool success;

    if (mp4_handle == nullptr)
        return false;

    success = (getAACTrack (mp4_handle) != -1);

    mp4ff_close (mp4_handle);
    return success;
}

static void read_and_set_string (mp4ff_t * mp4, int (*func) (const mp4ff_t *
 mp4, char * *string), Tuple & tuple, int field)
{
    char *string = nullptr;

    func (mp4, &string);

    if (string != nullptr)
        tuple.set_str (field, string);

    g_free (string);
}

static Tuple generate_tuple (const char * filename, mp4ff_t * mp4, int track)
{
    Tuple tuple;
    int64_t length;
    int scale, rate, channels, bitrate;
    char *year = nullptr, *cd_track = nullptr;
    char scratch[32];

    tuple.set_filename (filename);
    tuple.set_str (FIELD_CODEC, "MPEG-2/4 AAC");

    length = mp4ff_get_track_duration (mp4, track);
    scale = mp4ff_time_scale (mp4, track);

    if (length > 0 && scale > 0)
        tuple.set_int (FIELD_LENGTH, length * 1000 / scale);

    rate = mp4ff_get_sample_rate (mp4, track);
    channels = mp4ff_get_channel_count (mp4, track);

    if (rate > 0 && channels > 0)
    {
        snprintf (scratch, sizeof scratch, "%d kHz, %s", rate / 1000, channels
         == 1 ? _("mono") : channels == 2 ? _("stereo") : _("surround"));
        tuple.set_str (FIELD_QUALITY, scratch);
    }

    bitrate = mp4ff_get_avg_bitrate (mp4, track);

    if (bitrate > 0)
        tuple.set_int (FIELD_BITRATE, bitrate / 1000);

    read_and_set_string (mp4, mp4ff_meta_get_title, tuple, FIELD_TITLE);
    read_and_set_string (mp4, mp4ff_meta_get_album, tuple, FIELD_ALBUM);
    read_and_set_string (mp4, mp4ff_meta_get_artist, tuple, FIELD_ARTIST);
    read_and_set_string (mp4, mp4ff_meta_get_comment, tuple, FIELD_COMMENT);
    read_and_set_string (mp4, mp4ff_meta_get_genre, tuple, FIELD_GENRE);

    mp4ff_meta_get_date (mp4, &year);

    if (year != nullptr)
        tuple.set_int (FIELD_YEAR, atoi (year));

    g_free (year);

    mp4ff_meta_get_track (mp4, &cd_track);

    if (cd_track != nullptr)
        tuple.set_int (FIELD_TRACK_NUMBER, atoi (cd_track));

    g_free (cd_track);

    return tuple;
}

static Tuple mp4_get_tuple (const char * filename, VFSFile * handle)
{
    mp4ff_callback_t mp4cb = {
        mp4_read_callback,
        nullptr,  // write
        mp4_seek_callback,
        nullptr,  // truncate
        handle
    };

    mp4ff_t *mp4;
    int track;

    mp4 = mp4ff_open_read (&mp4cb);

    if (mp4 == nullptr)
        return Tuple ();

    track = getAACTrack (mp4);

    if (track < 0)
    {
        mp4ff_close (mp4);
        return Tuple ();
    }

    Tuple tuple = generate_tuple (filename, mp4, track);
    mp4ff_close (mp4);
    return tuple;
}

static bool my_decode_mp4 (const char * filename, mp4ff_t * mp4file)
{
    // We are reading an MP4 file
    int mp4track = getAACTrack (mp4file);
    NeAACDecHandle decoder;
    NeAACDecConfigurationPtr decoder_config;
    unsigned char *buffer = nullptr;
    unsigned bufferSize = 0;
    unsigned long samplerate = 0;
    unsigned char channels = 0;
    unsigned numSamples;
    unsigned long sampleID = 1;
    unsigned framesize = 0;

    if (mp4track < 0)
    {
        fprintf (stderr, "Unsupported Audio track type\n");
        return true;
    }

    // Open decoder
    decoder = NeAACDecOpen ();

    // Configure for floating point output
    decoder_config = NeAACDecGetCurrentConfiguration (decoder);
    decoder_config->outputFormat = FAAD_FMT_FLOAT;
    NeAACDecSetConfiguration (decoder, decoder_config);

    mp4ff_get_decoder_config (mp4file, mp4track, &buffer, &bufferSize);
    if (!buffer)
    {
        NeAACDecClose (decoder);
        return false;
    }
    if (NeAACDecInit2 (decoder, buffer, bufferSize, &samplerate, &channels) < 0)
    {
        NeAACDecClose (decoder);

        return false;
    }

    g_free (buffer);
    if (!channels)
    {
        NeAACDecClose (decoder);

        return false;
    }
    numSamples = mp4ff_num_samples (mp4file, mp4track);

    if (!aud_input_open_audio (FMT_FLOAT, samplerate, channels))
    {
        NeAACDecClose (decoder);
        return false;
    }

    aud_input_set_tuple (generate_tuple (filename, mp4file, mp4track));
    aud_input_set_bitrate (mp4ff_get_avg_bitrate (mp4file, mp4track));

    while (! aud_input_check_stop ())
    {
        void *sampleBuffer;
        NeAACDecFrameInfo frameInfo;
        int rc;

        buffer = nullptr;
        bufferSize = 0;

        /* If we've run to the end of the file, we're done. */
        if (sampleID >= numSamples)
            break;

        rc = mp4ff_read_sample (mp4file, mp4track,
         sampleID++, &buffer, &bufferSize);

        /* If we can't read the file, we're done. */
        if ((rc == 0) || (buffer == nullptr) || (bufferSize == 0)
         || (bufferSize > BUFFER_SIZE))
        {
            fprintf (stderr, "MP4: read error\n");
            sampleBuffer = nullptr;

            NeAACDecClose (decoder);

            return false;
        }

        sampleBuffer = NeAACDecDecode (decoder, &frameInfo, buffer, bufferSize);

        /* If there was an error decoding, we're done. */
        if (frameInfo.error > 0)
        {
            fprintf (stderr, "MP4: %s\n", NeAACDecGetErrorMessage (frameInfo.error));
            NeAACDecClose (decoder);

            return false;
        }
        if (buffer)
        {
            g_free (buffer);
            buffer = nullptr;
            bufferSize = 0;
        }

        /* Calculate frame size from the first (non-blank) frame.  This needs to
         * be done before we try to seek. */
        if (!framesize)
        {
            framesize = frameInfo.samples / frameInfo.channels;

            if (!framesize)
                continue;
        }

        /* Respond to seek/stop requests.  This needs to be done after we
         * calculate frame size but of course before we write any audio. */
        int seek_value = aud_input_check_seek ();

        if (seek_value >= 0)
        {
            sampleID = (int64_t) seek_value * samplerate / 1000 / framesize;
            continue;
        }

        aud_input_write_audio (sampleBuffer, sizeof (float) * frameInfo.samples);
    }

    NeAACDecClose (decoder);

    return true;
}

static bool mp4_play (const char * filename, VFSFile * file)
{
    mp4ff_callback_t mp4cb = {
        mp4_read_callback,
        nullptr,  // write
        mp4_seek_callback,
        nullptr,  // truncate
        file
    };

    bool result;

    mp4ff_t * mp4file = mp4ff_open_read (& mp4cb);
    result = my_decode_mp4 (filename, mp4file);
    mp4ff_close (mp4file);

    return result;
}

bool read_itunes_cover (const char * filename, VFSFile * file, void * *
 data, int64_t * size);

#define AUD_PLUGIN_NAME        N_("AAC (MP4) Decoder")
#define AUD_INPUT_PLAY         mp4_play
#define AUD_INPUT_IS_OUR_FILE  is_mp4_aac_file
#define AUD_INPUT_READ_TUPLE   mp4_get_tuple
#define AUD_INPUT_READ_IMAGE   read_itunes_cover
#define AUD_INPUT_EXTS         fmts

#define AUD_DECLARE_INPUT
#include <libaudcore/plugin-declare.h>
