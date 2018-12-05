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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <gio/gio.h>
#include <upower.h>

#include "batmon.h"

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
  BatteryData      data;
  BatteryCallback *cb;
  void            *user_data;
} private = {0};


static gboolean
want_device(UpDevice *dev)
{
  gchar   *native_path;
  guint    kind;
  guint    technology;
  gboolean result = FALSE;

  g_object_get(dev,
               "native-path", &native_path,
               "kind"       , &kind,
               "technology" , &technology,
               NULL);

  if (kind == UP_DEVICE_KIND_BATTERY &&
      technology != UP_DEVICE_TECHNOLOGY_UNKNOWN)
  {
    /* We blacklist bq27200-0 for now as it gives some weird stuff */
    result = g_strcmp0(native_path, "bq27200-0") != 0;

    if (result)
      private.data.fallback = g_strcmp0(native_path, "rx51-battery") == 0;
  }

  g_free(native_path);

  return result;
}

/* Return appropriate battery device or NULL */
static UpDevice *
find_battery_device()
{
  UpDevice  *result = NULL;
  GPtrArray *devices;
  guint      i;

  devices = up_client_get_devices(private.client);

  for (i = 0;  i < devices->len;  i++) {
    UpDevice *device = g_ptr_array_index(devices, i);

    if (want_device(device))
    {
      result = device;
      break;
    }
  }

  g_ptr_array_unref (devices);

  return result;
}

/* Measure charge level using voltage if battery is not calibrated */
static void
update_percentage_fallback(void)
{
  BatteryData *data = &private.data;
  gdouble voltage = data->voltage;

  if (data->state == UP_DEVICE_STATE_CHARGING)
  {
    data->percentage = (voltage - 3.40) * 125;
  }
  else
    data->percentage = (voltage - 3.25) * 105;
}

static void
prop_changed_cb(UpDevice *device,
                GParamSpec *pspec,
                gpointer user_data)
{
  BatteryData *data = &private.data;
  const gchar *name    = pspec->name;

  if (!g_strcmp0(name, "update-time"))
    g_object_get(private.battery, name, &data->update_time,   NULL);
  else if (!g_strcmp0(name, "voltage"))
  {
    g_object_get(private.battery, name, &data->voltage,       NULL);

    if (!data->calibrated && data->fallback)
      update_percentage_fallback();
  }
  else if (!g_strcmp0(name, "percentage"))
    g_object_get(private.battery, name, &data->percentage,    NULL);
  else if (!g_strcmp0(name, "temperature"))
    g_object_get(private.battery, name, &data->temperature,   NULL);
  else if (!g_strcmp0(name, "time-to-empty"))
    g_object_get(private.battery, name, &data->time_to_empty, NULL);
  else if (!g_strcmp0(name, "time-to-full"))
    g_object_get(private.battery, name, &data->time_to_full,  NULL);
  else if (!g_strcmp0(name, "energy-rate"))
    g_object_get(private.battery, name, &data->energy_rate,   NULL);
  else if (!g_strcmp0(name, "energy"))
    g_object_get(private.battery, name, &data->energy_now,    NULL);
  else if (!g_strcmp0(name, "energy-empty"))
    g_object_get(private.battery, name, &data->energy_empty,  NULL);
  else if (!g_strcmp0(name, "energy-full"))
    g_object_get(private.battery, name, &data->energy_full,   NULL);
  else if (!g_strcmp0(name, "state"))
    g_object_get(private.battery, name, &data->state,         NULL);
  else
    return;

  if (private.cb)
    private.cb(&private.data, private.user_data);
}

static void
get_battery_properties(void)
{
  BatteryData *data = &private.data;

  g_object_get(private.battery,
               "percentage"   , &data->percentage,
               "voltage"      , &data->voltage,
               "temperature"  , &data->temperature,
               "technology"   , &data->technology,
               "state"        , &data->state,
               "time-to-empty", &data->time_to_empty,
               "time-to-full" , &data->time_to_full,
               "energy"       , &data->energy_now,
               "energy-empty" , &data->energy_empty,
               "energy-full"  , &data->energy_full,
               "energy-rate"  , &data->energy_rate,
               "update-time"  , &data->update_time,
               NULL);

  data->calibrated = data->percentage ? TRUE : FALSE;
}

static int
monitor_battery(void)
{
  gulong rv = g_signal_connect(private.battery, "notify",
                               G_CALLBACK (prop_changed_cb),
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

  private.battery = find_battery_device();

  if (private.battery == NULL)
  {
    fprintf(stderr, "Failed to find battery\n");
    goto fail;
  }

  if (monitor_battery())
  {
    fprintf(stderr, "Failed to monitor events\n");
    goto fail;
  }

  get_battery_properties();

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
  fprintf(stderr, "Test callback\n");
  return;
}

static int
main_loop(void)
{
  static GMainLoop *loop = NULL;

  if (init_batt()) {
    fprintf(stderr, "Failed to find device\n");
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
