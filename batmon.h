#ifndef _BATMON_H_
#define _BATMON_H_
#include <gio/gio.h>

/* upower data */
typedef struct {
    gboolean calibrated; /* TODO set this */

    gdouble percentage;
    gdouble voltage;
    gdouble temperature;

    guint32 technology;
    guint32 state;

    gint64 time_to_full;
    gint64 time_to_empty;
} BatteryData;

typedef void BatteryCallback (BatteryData*, void*);

int init_batt(void);
void set_batt_cb(BatteryCallback, void*);
BatteryData *get_batt_data(void);
void free_bat(void);

#endif /* _BATMON_H_ */
