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

#define UPOWER_BUS_NAME "org.freedesktop.UPower"
#define UPOWER_PATH_NAME "/org/freedesktop/UPower"
#define UPOWER_INTERFACE_NAME "org.freedesktop.UPower"
#define UPOWER_DEVICE_INTERFACE_NAME "org.freedesktop.UPower.Device"
#define DBUS_PROPERTIES_INTERFACE_NAME "org.freedesktop.DBus.Properties"

/*
 * TODO:
 * - Make init code async perhaps ?
 * - Do not hard fail if we cannot (currently) find a battery; so we can find
 *   one later (and also immediately support searching for batteries later on
 *   DeviceAdded and DeviceRemoved)
 * - Original applet also reads rx51_battery -- I think only for some design values.
 */

/* Only contains values we care about */
typedef struct {
  guint32  type;
  guint32  technology;
  gchar   *upower_path;
} UPowerDevice;

/* Private data */
static struct {
  UPowerDevice    *dev;
  GDBusProxy      *proxy;
  BatteryData      data;
  BatteryCallback *cb;
  void            *user_data;
} private = {0};


static void
free_upower_device(UPowerDevice *dev)
{
  g_free(dev->upower_path);
  g_free(dev);
}


static GVariant *
get_device_properties(GDBusConnection *bus, gchar *device)
{
  GVariant *res;
  GError *error = NULL;

  res = g_dbus_connection_call_sync(bus,
            UPOWER_BUS_NAME,
            device,
            DBUS_PROPERTIES_INTERFACE_NAME,
            "GetAll",
            g_variant_new("(s)", UPOWER_DEVICE_INTERFACE_NAME), /* params */
            NULL, /* reply type */
            G_DBUS_CALL_FLAGS_NONE, /* flags */
            -1, /* Timeout */
            NULL,
            &error);

  if (error)
  {
    fprintf(stderr, "Unable to get properties: %s\n", error->message);
    g_error_free(error);
    g_variant_unref(res);
    return NULL;
  }

  GVariant *tmp;
  g_variant_get(res, "(*)", &tmp);

  g_variant_unref(res);

  return tmp;
}

static UPowerDevice *
get_device(GVariant *device_properties)
{
  UPowerDevice *dev;
  GVariantDict *dict;

  dev = g_malloc0(sizeof(*dev));

  dict = g_variant_dict_new(device_properties);

  g_variant_dict_lookup(dict, "Type", "u", &dev->type);
  g_variant_dict_lookup(dict, "Technology", "u", &dev->technology);

  g_variant_dict_unref(dict);

  return dev;
}

static gboolean
want_device(UPowerDevice *dev)
{
  return (dev->type == UP_DEVICE_KIND_BATTERY) &&
    /* Check for sensible technology value to rule out non-batteries */
    (dev->technology != UP_DEVICE_TECHNOLOGY_UNKNOWN);
}

static UPowerDevice *
find_battery_device(GDBusConnection *bus)
{
  GVariantIter *iter;
  GVariant *res = NULL;
  GError *error = NULL;
  UPowerDevice *result = NULL;
  gsize i;

  res = g_dbus_connection_call_sync(bus,
                                    UPOWER_BUS_NAME,
                                    UPOWER_PATH_NAME,
                                    UPOWER_INTERFACE_NAME,
                                    "EnumerateDevices",
                                    NULL, /* params */
                                    NULL, /* reply type */
                                    G_DBUS_CALL_FLAGS_NONE, /* flags */
                                    -1, /* Timeout */
                                    NULL,
                                    &error);

  if (res == NULL)
  {
    fprintf(stderr, "Cannot enumerate devices: %s\n", error->message);
    g_error_free(error);
    return NULL;
  }

  /* Is tuple, extract first item (which is a list/tuple) */
  GVariant *tmp = g_variant_get_child_value(res, 0);
  GVariant *props;

  iter = g_variant_iter_new(tmp);
  for (i = 0;  i < g_variant_iter_n_children(iter);  i++)
  {
    GVariant *val;
    val = g_variant_iter_next_value(iter);

    gchar *device_path;
    g_variant_get(val, "o", &device_path);

    props = get_device_properties(bus, device_path);
    if (props)
    {
      UPowerDevice *dev = get_device(props);
      /*print_props(props);*/

      if (want_device(dev))
      {
        result = dev;
        result->upower_path = strdup(device_path);
      } else {
        free_upower_device(dev);
      }

      g_variant_unref(props);
    }

    g_free(device_path);
    g_variant_unref(val);

    if (result != NULL)
      break;
  }

  g_variant_unref(tmp);
  g_variant_unref(res);

  return result;
}

#define _UPDATE_BATT_DATA(keyname, structname, keytype) \
  if (!strcmp(name, keyname)) { \
    g_variant_get(value, keytype, structname); \
    fprintf(stderr, "*** UPDATING %s\n", keyname); \
    return 0; \
  }

static int
update_property(const gchar *name, GVariant *value)
{
  _UPDATE_BATT_DATA("Percentage", &private.data.percentage, "d");
  _UPDATE_BATT_DATA("Voltage", &private.data.voltage, "d");
  _UPDATE_BATT_DATA("Temperature", &private.data.temperature, "d");

  _UPDATE_BATT_DATA("Technology", &private.data.technology, "u");
  _UPDATE_BATT_DATA("State", &private.data.state, "u");

  _UPDATE_BATT_DATA("TimeToEmpty", &private.data.time_to_empty, "x");
  _UPDATE_BATT_DATA("TimeToFull", &private.data.time_to_full, "x");

  _UPDATE_BATT_DATA("Energy", &private.data.energy_now, "d");
  _UPDATE_BATT_DATA("EnergyEmpty", &private.data.energy_empty, "d");
  _UPDATE_BATT_DATA("EnergyFull", &private.data.energy_full, "d");
  _UPDATE_BATT_DATA("EnergyRate", &private.data.energy_rate, "d");

  _UPDATE_BATT_DATA("Voltage", &private.data.voltage, "d");

  _UPDATE_BATT_DATA("UpdateTime", &private.data.update_time, "t");

  /* No match */
  return 1;
}


/* Inspiration taken from gio/tests/gdbus-example-watch-proxy.c */
static void
on_properties_changed(GDBusProxy *proxy,
                      GVariant *changed_properties,
                      const gchar *const *invalidated_properties,
                      gpointer user_data)
{
  if (g_variant_n_children(changed_properties) > 0)
  {
    GVariantIter *iter;
    const gchar *key;
    GVariant *value;

    g_variant_get(changed_properties, "a{sv}", &iter);
    while (g_variant_iter_loop(iter, "{&sv}", &key, &value))
      update_property(key, value);
    g_variant_iter_free(iter);
  }

  if (private.cb)
    private.cb(&private.data, private.user_data);
}

/* Inspiration taken from gio/tests/gdbus-example-watch-proxy.c */
static void
get_initial_properties_from_proxy(GDBusProxy *proxy)
{
  gchar **property_names;
  guint n;

  property_names = g_dbus_proxy_get_cached_property_names(proxy);
  for (n = 0;  property_names != NULL && property_names[n] != NULL;  n++)
  {
    const gchar *key = property_names[n];
    GVariant *value;
    value = g_dbus_proxy_get_cached_property(proxy, key);
    update_property(key, value);
  }
  g_strfreev(property_names);
}

static int
monitor_battery(void)
{
  GDBusProxyFlags flags = G_DBUS_PROXY_FLAGS_NONE;
  GError *error = NULL;

  private.proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                                                flags,
                                                NULL, /* GDBusInterfaceInfo */
                                                UPOWER_BUS_NAME,
                                                private.dev->upower_path,
                                                UPOWER_DEVICE_INTERFACE_NAME,
                                                NULL, /* GCancellable */
                                                &error);
  if (private.proxy == NULL)
  {
    fprintf(stderr, "Could not create dbus proxy\n");
    g_error_free(error);
    return 1;
  }

  g_signal_connect(private.proxy, "g-properties-changed",
                   G_CALLBACK(on_properties_changed), NULL);

  return 0;
}

int
init_batt(void)
{
  GDBusConnection *bus = NULL;
  GError *error = NULL;

  bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
  if (bus == NULL)
  {
    fprintf(stderr, "Could not get dbus system session bus: %s\n", error->message);
    g_error_free(error);
    return 1;
  }

  private.dev = find_battery_device(bus);

  if (private.dev == NULL)
  {
    fprintf(stderr, "Failed to find device\n");
    return 1;
  }

  if (monitor_battery())
  {
    fprintf(stderr, "Failed to monitor events\n");
    return 1;
  }

  get_initial_properties_from_proxy(private.proxy);

  return 0;
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
  free_upower_device(private.dev);
  g_object_unref(private.proxy);
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
