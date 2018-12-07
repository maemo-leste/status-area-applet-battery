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

typedef struct {
  /* The percentage charge of the device */
  gdouble percentage;
  /* The current voltage of the device */
  gdouble voltage;
  /* Voltage used to calculate full Ah capacity */
  gdouble design_voltage;
  /* The temperature of the device in degrees Celsius */
  gdouble temperature;

  /* The battery technology e.g. UP_DEVICE_TECHNOLOGY_LITHIUM_ION */
  guint   technology;
  /* Is charging device connected to AC? */
  gboolean charger_online;
  /* The state the device is in at this time, e.g. UP_DEVICE_STATE_EMPTY */
  guint   state;

  /* The amount of time until the device is empty */
  gint64  time_to_empty;
  /* The amount of time until the device is fully charged */
  gint64  time_to_full;

  /* Energy left, mWh */
  gdouble energy_now;
  /* The energy the device will have when it is empty. This is usually zero. */
  gdouble energy_empty;
  /* The amount of energy when the device is fully charged */
  gdouble energy_full;
  /* The rate of discharge or charge */
  gdouble energy_rate;

  /* The last time the device was updated */
  guint64 update_time;
} BatteryData;

typedef void BatteryCallback (BatteryData *, void *);

int            init_batt       (void);
void           set_batt_cb     (BatteryCallback, void *);
BatteryData   *get_batt_data   (void);
gboolean       batt_calibrated (void);
void           free_batt       (void);

#endif /* _BATMON_H_ */
