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

#include <gio/gio.h>
#include <upower.h>

#include "batmon.h"

static char* blacklist[] = {
  /* This driver should be removed from the kernel completely */
  "rx51-battery",
  /* Nokia N900 charger device is exposed as battery by UPower */
  "bq24150a-0",
  /* Droid4 line power device (driver doesn't send uevents) */
  "usb",
  /* End of list */
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
  guint  technology;
  gint   i;

  g_object_get(dev,
               "native-path", &native_path,
               "kind"       , &kind,
               "technology" , &technology,
               NULL);

  for (i = 0;  blacklist[i] != NULL;  i++)
  {
    if (!g_strcmp0(native_path, blacklist[i]))
      return;
  }

  if (kind == UP_DEVICE_KIND_BATTERY)
  {
    if (private.battery == NULL &&
        technology != UP_DEVICE_TECHNOLOGY_UNKNOWN)
    {
      private.battery = dev;
    }

    return;
  }

  if (kind == UP_DEVICE_KIND_LINE_POWER &&
      private.charger == NULL)
  {
    private.charger = dev;
  }
}

/* Find battery/charger devices and add them to private */
static void
find_upower_devices()
{
  GPtrArray *devices;
  guint      i;

  devices = up_client_get_devices(private.client);

  for (i = 0;  i < devices->len;  i++)
  {
    UpDevice *device = g_ptr_array_index(devices, i);

    check_device(device);

    if (private.battery && private.charger)
      break;
  }

  g_ptr_array_unref (devices);
}

static void
battery_prop_changed_cb(UpDevice *battery,
                        GParamSpec *pspec,
                        gpointer user_data)
{
  BatteryData *data = &private.data;
  const gchar *prop = pspec->name;

  if (!g_strcmp0(prop, "percentage"))
    g_object_get(battery, prop, &data->percentage,    NULL);
  else if (!g_strcmp0(prop, "time-to-empty"))
    g_object_get(battery, prop, &data->time_to_empty, NULL);
  else if (!g_strcmp0(prop, "time-to-full"))
    g_object_get(battery, prop, &data->time_to_full,  NULL);
  else if (!g_strcmp0(prop, "charge"))
    g_object_get(battery, prop, &data->charge_now,    NULL);
  else if (!g_strcmp0(prop, "charge-full"))
    g_object_get(battery, prop, &data->charge_full,   NULL);
  else
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

  g_object_get(private.battery,
               "percentage"   , &data->percentage,
               "state"        , &data->state,
               "time-to-empty", &data->time_to_empty,
               "time-to-full" , &data->time_to_full,
               "charge"       , &data->charge_now,
               "charge-full"  , &data->charge_full,
               NULL);

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
  GError *error = NULL;

  private.client = up_client_new_full(NULL, &error);
  if (private.client == NULL)
  {
    g_printerr("Cannot connect to upowerd: %s\n", error->message);
    g_error_free(error);
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
  return private.data.charge_full != 0;
}

void
free_batt(void)
{
  if (private.client)
    g_object_unref(private.client);

  memset(&private, 0, sizeof(private));
}

#if 0
static void
testf(BatteryData *d)
{
  g_warning("Test callback");
  return;
}

static int
main_loop(void)
{
  static GMainLoop *loop = NULL;

  if (init_batt()) {
    g_warning("Failed to find device");
    return 1;
  }

  //set_batt_cb(testf);

  loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(loop);

  free_batt();

  return 0;
}

int
main(int argc, char *argv[])
{
  main_loop();
}
#endif
