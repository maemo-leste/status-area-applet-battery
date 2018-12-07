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
  UpDevice        *charger;
  BatteryData      data;
  BatteryCallback *cb;
  gboolean         calibrated;
  gboolean         fallback;
  time_t           force_state;
  void            *user_data;
} private = {0};


/* If there're multiple batteries/chargers, we take the first suggested */
static void
check_device(UpDevice *dev)
{
  guint    kind;
  guint    technology;

  g_object_get(dev,
               "kind"       , &kind,
               "technology" , &technology,
               NULL);

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

  for (i = 0;  i < devices->len;  i++) {
    UpDevice *device = g_ptr_array_index(devices, i);

    check_device(device);

    if (private.battery && private.charger)
      break;
  }

  g_ptr_array_unref (devices);
}

/* Measure charge level using voltage if battery is not calibrated */
static void
update_percentage_fallback(void)
{
  BatteryData *data = &private.data;
  gdouble voltage = data->voltage;
  gdouble percentage;

  if (data->state == UP_DEVICE_STATE_EMPTY ||
      data->state == UP_DEVICE_STATE_FULLY_CHARGED)
  {
    data->percentage = data->state == UP_DEVICE_STATE_EMPTY ? 0 : 100;
    return;
  }

  if (data->charger_online)
    percentage = (voltage - 3.40) * 125;
  else
    percentage = (voltage - 3.25) * 105;

  /* Initial value */
  if (data->percentage == 0)
  {
    data->percentage = percentage;
    return;
  }

  /* Charging */
  if (data->charger_online)
  {
    if (percentage > data->percentage)
      data->percentage = percentage;
    return;
  }

  /* Discharging */
  if (percentage < data->percentage)
    data->percentage = percentage;
}

static void
battery_prop_changed_cb(UpDevice *battery,
                        GParamSpec *pspec,
                        gpointer user_data)
{
  BatteryData *data = &private.data;
  const gchar *prop = pspec->name;

  if (!g_strcmp0(prop, "update-time"))
    g_object_get(battery, prop, &data->update_time,   NULL);
  else if (!g_strcmp0(prop, "voltage"))
    g_object_get(battery, prop, &data->voltage,       NULL);
  else if (!g_strcmp0(prop, "percentage"))
    g_object_get(battery, prop, &data->percentage,    NULL);
  else if (!g_strcmp0(prop, "temperature"))
    g_object_get(battery, prop, &data->temperature,   NULL);
  else if (!g_strcmp0(prop, "time-to-empty"))
    g_object_get(battery, prop, &data->time_to_empty, NULL);
  else if (!g_strcmp0(prop, "time-to-full"))
    g_object_get(battery, prop, &data->time_to_full,  NULL);
  else if (!g_strcmp0(prop, "energy-rate"))
    g_object_get(battery, prop, &data->energy_rate,   NULL);
  else if (!g_strcmp0(prop, "energy"))
    g_object_get(battery, prop, &data->energy_now,    NULL);
  else if (!g_strcmp0(prop, "energy-empty"))
    g_object_get(battery, prop, &data->energy_empty,  NULL);
  else if (!g_strcmp0(prop, "energy-full"))
    g_object_get(battery, prop, &data->energy_full,   NULL);
  else
    return;

  if (private.cb)
    private.cb(data, private.user_data);
}

/* Additional handler for uncalibrated batteries */
static void
battery_voltage_changed_cb(UpDevice *battery,
                           GParamSpec *pspec,
                           gpointer user_data)
{
  g_object_get(battery, "voltage", &private.data.voltage, NULL);
  update_percentage_fallback();

  if (private.cb)
    private.cb(&private.data, private.user_data);
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
    /* Battery devices lag */
    if (data->charger_online)
    {
      if (data->state == UP_DEVICE_STATE_DISCHARGING)
        data->state = UP_DEVICE_STATE_CHARGING;
    }
    else if (data->state == UP_DEVICE_STATE_CHARGING)
      data->state = UP_DEVICE_STATE_DISCHARGING;
  }

  if (private.fallback &&
      (data->state == UP_DEVICE_STATE_FULLY_CHARGED ||
       data->state == UP_DEVICE_STATE_EMPTY))
  {
    update_percentage_fallback();
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
get_design_voltage(void)
{
  BatteryData *data = &private.data;
  int i;

  if (data->technology == UP_DEVICE_TECHNOLOGY_LITHIUM_ION)
  {
    for (i = 1;  i < 4;  i++)
    {
      if (2.9 * i < data->voltage && data->voltage < i * 4.3)
      {
        data->design_voltage = i * 4.2;
        return;
      }
    }
  }

  data->design_voltage = data->voltage;
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

  get_design_voltage();

  private.calibrated = data->percentage ? TRUE : FALSE;

  if (private.calibrated == FALSE &&
      data->voltage > 2.9 && data->voltage < 4.25)
  {
    private.fallback = TRUE;
    update_percentage_fallback();
  }


  if (private.charger)
  {
    g_object_get(private.charger, "online", &data->charger_online, NULL);

    if (data->charger_online)
      data->state = UP_DEVICE_STATE_CHARGING;
    else
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

  if (private.fallback)
  {
    g_signal_connect(private.battery, "notify::voltage",
                     G_CALLBACK(battery_voltage_changed_cb),
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
  return private.calibrated;
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
