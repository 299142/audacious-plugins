#ifndef XS_CONFIG_H
#define XS_CONFIG_H

#include <pthread.h>
#include <libaudcore/core.h>

/* Configuration structure
 */
enum XS_CHANNELS {
    XS_CHN_MONO = 1,
    XS_CHN_STEREO = 2
};


enum XS_CLOCK {
    XS_CLOCK_PAL = 1,
    XS_CLOCK_NTSC,
    XS_CLOCK_VBI,
    XS_CLOCK_CIA,
    XS_CLOCK_ANY
};


enum XS_SIDMODEL {
    XS_SIDMODEL_UNKNOWN = 0,
    XS_SIDMODEL_6581,
    XS_SIDMODEL_8580,
    XS_SIDMODEL_ANY
};


extern struct xs_cfg_t {
    /* General audio settings */
    int     audioChannels;
    int     audioFrequency;

    /* Emulation settings */
    bool_t  mos8580;            /* TRUE = 8580, FALSE = 6581 */
    bool_t  forceModel;
    int     clockSpeed;         /* PAL (50Hz) or NTSC (60Hz) */
    bool_t  forceSpeed;         /* TRUE = force to given clockspeed */

    bool_t  emulateFilters;

    /* Playing settings */
    bool_t  playMaxTimeEnable,
            playMaxTimeUnknown; /* Use max-time only when song-length is unknown */
    int     playMaxTime;        /* MAX playtime in seconds */

    bool_t  playMinTimeEnable;
    int     playMinTime;        /* MIN playtime in seconds */

    bool_t  songlenDBEnable;
    char    *songlenDBPath;     /* Path to Songlengths.txt */

    /* Miscellaneous settings */
    bool_t  stilDBEnable;
    char    *stilDBPath;        /* Path to STIL.txt */
    char    *hvscPath;          /* Path-prefix for HVSC */

    bool_t  subAutoEnable,
            subAutoMinOnly;
    int     subAutoMinTime;
} xs_cfg;

extern pthread_mutex_t xs_cfg_mutex;


/* Functions
 */
void xs_init_configuration(void);

#endif    /* XS_CONFIG_H */
