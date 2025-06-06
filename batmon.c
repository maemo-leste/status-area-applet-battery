/***********************************************************************************
*  status-area-applet-battery: Open source rewrite of the Maemo 5 battery applet
*  for Maemo Leste
*  Copyright (C) 2017 Merlijn B. W. Wajer
*  Copyright (C) 2018 Arthur Demchenkov
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include <gio/gio.h>
#include <upower.h>

#include "batmon.h"

/** Skip these devices */
static const char* blacklist[] = {
  /* List drivers to be blacklisted. The name is obtained from
   * `upower -d`, and is the value of the `native-path` key for that
   * upower device. Example:
   * "rx51-battery",
   */
  NULL
};

/*
 * TODO:
 * - Make init code async perhaps ?
 * - Do not hard fail if we cannot (currently) find a battery; so we can find
 *   one later (and also immediately support searching for batteries later on
 *   DeviceAdded and DeviceRemoved)
 * - Original applet also reads rx51_battery -- I think only for some design values.
 */

/* Private data */
static struct {
  UpClient        *client;
  UpDevice        *battery;
  UpDevice        *charger;
  BatteryData      data;
  BatteryCallback *cb;
  time_t           force_state;
  void            *user_data;
} private = {0};


/* If there're multiple batteries/chargers, we take the first suggested */
static void
check_device(UpDevice *dev)
{
  gchar *native_path;
  guint  kind;
  gint   i;

  g_object_get(dev,
               "native-path", &native_path,
               "kind"       , &kind,
               NULL);

  for (i = 0;  blacklist[i] != NULL;  i++)
  {
    if (!g_strcmp0(native_path, blacklist[i]))
      return;
  }

  if (kind == UP_DEVICE_KIND_BATTERY)
  {
    if (private.battery == NULL)
    {
      private.battery = dev;
      g_object_ref(private.battery);
    }

    return;
  }

  if (kind == UP_DEVICE_KIND_LINE_POWER &&
      private.charger == NULL)
  {
    private.charger = dev;
    g_object_ref(private.charger);
  }
}

/* Find battery/charger devices and add them to private */
static void
find_upower_devices()
{
  GPtrArray *devices;
  guint      i;

  devices = up_client_get_devices2(private.client);

  for (i = 0;  i < devices->len;  i++)
  {
    UpDevice *device = g_ptr_array_index(devices, i);

    check_device(device);

    if (private.battery && private.charger)
      break;
  }

  g_ptr_array_unref (devices);
}

/* Estimate percentage based on battery voltage if driver does not report it */
static void
refresh_capacity_estimation(void)
{
  BatteryData *data = &private.data;
  gdouble upower_percentage;
  gdouble delta;

  /* data->percentage might contain an estimation, so fetch real value from upower */
  g_object_get(private.battery, "percentage", &upower_percentage, NULL);

  if (upower_percentage || data->charger_online)
    return;

  if (!data->voltage_min)
    data->voltage_min = 3.1;
  if (!data->voltage_max)
    data->voltage_max = 4.2;
  delta = data->voltage_max - data->voltage_min;

  if (data->voltage_now > (data->voltage_min + 0.2))
  {
    if (data->voltage_now <= (data->voltage_min + delta * 1.5 / 6))
      data->percentage = 4;
    else if (data->voltage_now <= (data->voltage_min + delta * 2.3 / 6))
      data->percentage = 9;
    else if (data->voltage_now <= (data->voltage_min + delta * 3 / 6))
      data->percentage = 12;
    else if (data->voltage_now <= (data->voltage_min + delta * 4 / 6))
      data->percentage = 32;
    else if (data->voltage_now <= (data->voltage_min + delta * 5 / 6))
      data->percentage = 57;
    else if (data->voltage_now <= (data->voltage_min + delta * 5.7 / 6))
      data->percentage = 82;
    else
      data->percentage = 99;

    /* Indicate that the capacity is an estimation */
    data->estimating = true;
  }
}

static void
battery_prop_changed_cb(UpDevice *battery,
                        GParamSpec *pspec,
                        gpointer user_data)
{
  BatteryData *data = &private.data;
  const gchar *prop = pspec->name;

  if (!g_strcmp0(prop, "percentage")) {
    g_object_get(battery, prop, &data->percentage,    NULL);
    /* The fuel gauge can start reporting percentage during runtime */
    if (data->estimating)
      data->estimating = false;
  } else if (!g_strcmp0(prop, "time-to-empty"))
    g_object_get(battery, prop, &data->time_to_empty, NULL);
  else if (!g_strcmp0(prop, "time-to-full"))
    g_object_get(battery, prop, &data->time_to_full,  NULL);
  else if (!g_strcmp0(prop, "energy"))
    g_object_get(battery, prop, &data->energy_now,    NULL);
  else if (!g_strcmp0(prop, "energy-full"))
    g_object_get(battery, prop, &data->energy_full,   NULL);
  else if (!g_strcmp0(prop, "voltage")) {
    g_object_get(battery, prop, &data->voltage_now,  NULL);
    refresh_capacity_estimation();
  } else
    return;

  if (private.cb)
    private.cb(data, private.user_data);
}

static void
battery_state_changed_cb(UpDevice *battery,
                         GParamSpec *pspec,
                         gpointer user_data)
{
  BatteryData *data = &private.data;
  g_object_get(battery, "state", &data->state, NULL);

  if (private.charger == NULL)
  {
    data->charger_online = data->state == UP_DEVICE_STATE_CHARGING ||
                           data->state == UP_DEVICE_STATE_FULLY_CHARGED ||
                           data->state == UP_DEVICE_STATE_PENDING_DISCHARGE;
  }
  else if (time(NULL) < private.force_state)
  {
    if (data->charger_online)
    {
      if (data->state == UP_DEVICE_STATE_DISCHARGING)
        data->state = UP_DEVICE_STATE_CHARGING;
    }
    else if (data->state == UP_DEVICE_STATE_CHARGING)
      data->state = UP_DEVICE_STATE_DISCHARGING;
  }

  if (private.cb)
    private.cb(data, private.user_data);
}

static void
charger_state_changed_cb(UpDevice *charger,
                         GParamSpec *pspec,
                         gpointer user_data)
{
  BatteryData *data = &private.data;

  g_object_get(charger, "online", &data->charger_online, NULL);

  private.force_state = time(NULL) + 10;
  if (data->charger_online)
    data->state = UP_DEVICE_STATE_CHARGING;
  else
    data->state = UP_DEVICE_STATE_DISCHARGING;

  if (private.cb)
    private.cb(data, private.user_data);
}

static void
get_battery_properties(void)
{
  BatteryData *data = &private.data;

  data->estimating = false;

  g_object_get(private.battery,
               "percentage"          , &data->percentage,
               "state"               , &data->state,
               "time-to-empty"       , &data->time_to_empty,
               "time-to-full"        , &data->time_to_full,
               "energy"              , &data->energy_now,
               "energy-full"         , &data->energy_full,
               "voltage"             , &data->voltage_now,
               "voltage-min-design"  , &data->voltage_min,
               "voltage-max-design"  , &data->voltage_max,
               NULL);

  if (!data->percentage)
    refresh_capacity_estimation();

  if (private.charger)
  {
    g_object_get(private.charger, "online", &data->charger_online, NULL);
    private.force_state = time(NULL) + 10;

    if (data->charger_online)
    {
      if (data->state == UP_DEVICE_STATE_DISCHARGING)
        data->state = UP_DEVICE_STATE_CHARGING;
    }
    else if (data->state == UP_DEVICE_STATE_CHARGING)
      data->state = UP_DEVICE_STATE_DISCHARGING;
  }
  else
  {
    /* Guess charging state using battery device state */
    data->charger_online = data->state == UP_DEVICE_STATE_CHARGING ||
                           data->state == UP_DEVICE_STATE_FULLY_CHARGED ||
                           data->state == UP_DEVICE_STATE_PENDING_DISCHARGE;
  }
}

static int
monitor_battery(void)
{
  gulong rv = g_signal_connect(private.battery, "notify",
                               G_CALLBACK(battery_prop_changed_cb),
                               NULL);

  if (private.charger)
  {
    g_signal_connect(private.charger, "notify::online",
                     G_CALLBACK(charger_state_changed_cb),
                     NULL);
  }

  g_signal_connect(private.battery, "notify::state",
                   G_CALLBACK(battery_state_changed_cb),
                   NULL);

  return rv ? 0 : 1;
}

int
init_batt(void)
{
  private.client = up_client_new();
  if (private.client == NULL)
  {
    g_printerr("Cannot connect to upowerd\n");
    goto fail;
  }

  find_upower_devices();

  if (private.battery == NULL)
  {
    g_warning("Failed to find battery");
    goto fail;
  }

  get_battery_properties();

  if (monitor_battery())
  {
    g_warning("Failed to monitor events");
    goto fail;
  }

  return 0;

fail:
  free_batt();
  return 1;
}

void
set_batt_cb(BatteryCallback *cb, void *user_data)
{
  private.cb = cb;
  private.user_data = user_data;
}

BatteryData *
get_batt_data(void)
{
  return &private.data;
}

gboolean
batt_calibrated(void)
{
  return private.data.energy_full && private.data.energy_now;
}

void
free_batt(void)
{
  if (private.client)
    g_object_unref(private.client);
  if (private.battery)
    g_object_unref(private.battery);
  if (private.charger)
    g_object_unref(private.charger);

  memset(&private, 0, sizeof(private));
}
