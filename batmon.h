#ifndef _BATMON_H_
#define _BATMON_H_
#include <gio/gio.h>

#include "upower-defs.h"

/* upower data */
typedef struct {
    gdouble percentage;
    gdouble voltage;
    gdouble temperature;

    guint32 technology;
    guint32 state;

    gint64 time_to_full;
    gint64 time_to_empty;

    gdouble energy_now;
    gdouble energy_empty;
    gdouble energy_full;

    gdouble voltage;

    guint64 update_time;

    gboolean calibrated; /* TODO set this */
} BatteryData;

typedef void BatteryCallback (BatteryData*, void*);

int init_batt(void);
void set_batt_cb(BatteryCallback, void*);
BatteryData *get_batt_data(void);
void free_bat(void);

#endif /* _BATMON_H_ */
