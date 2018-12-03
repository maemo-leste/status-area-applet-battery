/***********************************************************************************
*  status-area-applet-battery: Open source rewrite of the Maemo 5 battery applet
*  for Maemo Leste
*  Copyright (C) 2017 Merlijn B. W. Wajer
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
*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
***********************************************************************************/

#ifndef _BATMON_H_
#define _BATMON_H_

#include <glib.h>

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
  gdouble energy_rate;

  guint64 update_time;

  /* TODO: set this */
  gboolean calibrated;
} BatteryData;

typedef void BatteryCallback (BatteryData *, void *);

int            init_batt     (void);
void           set_batt_cb   (BatteryCallback, void *);
BatteryData   *get_batt_data (void);
void           free_bat      (void);

#endif /* _BATMON_H_ */
