 /***********************************************************************************
 *  status-area-applet-battery: Open source rewrite of the Maemo 5 battery applet
 *  Copyright (C) 2011 Mohammad Abu-Garbeyyeh
 *  Copyright (C) 2011-2012 Pali Rohár
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libintl.h>

#include <dbus/dbus.h>
#include <upower.h>

#include <profiled/libprofile.h>
#include <canberra.h>

#include <gtk/gtk.h>
#include <hildon/hildon.h>
#include <libhildondesktop/libhildondesktop.h>

#include <gconf/gconf-client.h>

#include "batmon.h"

#define GCONF_PATH "/apps/osso/status-area-applet-battery"
#define GCONF_USE_DESIGN_KEY GCONF_PATH "/use_design_capacity"
#define GCONF_SHOW_CHARGE_CHARGING_KEY GCONF_PATH "/show_charge_charging"
#define GCONF_EXEC_APPLICATION GCONF_PATH "/exec_application"
#define DBUS_MATCH_RULE "type='signal',path='/com/nokia/mce/signal'," \
                        "interface='com.nokia.mce.signal'," \
                        "member='display_status_ind'"


/** Whether to support legacy pattery low led pattern; nonzero for yes */
#define SUPPORT_BATTERY_LOW_LED_PATTERN 0


typedef struct _BatteryStatusAreaItem        BatteryStatusAreaItem;
typedef struct _BatteryStatusAreaItemClass   BatteryStatusAreaItemClass;
typedef struct _BatteryStatusAreaItemPrivate BatteryStatusAreaItemPrivate;

struct _BatteryStatusAreaItem {
  HDStatusMenuItem parent;
  BatteryStatusAreaItemPrivate *priv;
};

struct _BatteryStatusAreaItemClass {
  HDStatusMenuItemClass parent_class;
};

struct _BatteryStatusAreaItemPrivate {
  GtkWidget *alignment;
  GtkWidget *title;
  GtkWidget *value;
  GtkWidget *image;
  GtkWidget *hbox;
  DBusConnection *dbus_conn;
  ca_context *context;
  ca_proplist *pl;
  GConfClient *gconf;
  guint gconf_notify;
  guint timer_id;
  int percentage;
  int charge_now;
  int charge_full;
  int active_time;
  int bars;
  int charging_idx;
  gboolean is_charging;
  gboolean is_discharging;
  gboolean charger_connected;
  gboolean verylow;
  gboolean display_is_off;
  gboolean use_design;
  gboolean show_charge_charging;
  time_t low_last_reported;
  const char *exec_application;
};

GType battery_status_plugin_get_type(void);

HD_DEFINE_PLUGIN_MODULE(BatteryStatusAreaItem,
                        battery_status_plugin,
                        HD_TYPE_STATUS_MENU_ITEM);

static gboolean
battery_status_plugin_replay_sound(gpointer data)
{
  BatteryStatusAreaItem *plugin = data;
  ca_context_play_full(plugin->priv->context, 1, plugin->priv->pl, NULL, NULL);
  return FALSE;
}

static void
battery_status_plugin_play_sound(BatteryStatusAreaItem *plugin,
                                 const char *file,
                                 gboolean repeat)
{
  int volume = profile_get_value_as_int(NULL, "system.sound.level");

  if (volume == 1)
    volume = -11;
  else if (volume == 2)
    volume = -1;
  else
    return;

  ca_proplist_sets(plugin->priv->pl, CA_PROP_MEDIA_NAME, "Battery Notification");
  ca_proplist_sets(plugin->priv->pl, CA_PROP_MEDIA_FILENAME, file);
  ca_proplist_setf(plugin->priv->pl, CA_PROP_CANBERRA_VOLUME, "%d", volume);

  ca_context_play_full(plugin->priv->context, 0, plugin->priv->pl, NULL, NULL);

  if (repeat)
    g_timeout_add_seconds(0, battery_status_plugin_replay_sound, plugin);
}

static void
battery_status_plugin_update_icon(BatteryStatusAreaItem *plugin, int id)
{
  static const char *names[] = {
    "statusarea_battery_low",
    "statusarea_battery_full13",
    "statusarea_battery_full25",
    "statusarea_battery_full38",
    "statusarea_battery_full50",
    "statusarea_battery_full63",
    "statusarea_battery_full75",
    "statusarea_battery_full88",
    "statusarea_battery_full100",
    "statusarea_battery_verylow"
  };

  int last;
  GdkPixbuf *pixbuf;
  GtkIconTheme *icon_theme;

  if (plugin->priv->display_is_off)
    return;

  last = sizeof(names) / sizeof(*names) - 1;

  if (id > last)
    return;

  if (id < 0)
    id = last;

  icon_theme = gtk_icon_theme_get_default();

  pixbuf = gtk_icon_theme_load_icon(icon_theme, names[id], 18, GTK_ICON_LOOKUP_NO_SVG, NULL);
  hd_status_plugin_item_set_status_area_icon(HD_STATUS_PLUGIN_ITEM(plugin), pixbuf);
  g_object_unref(pixbuf);

  pixbuf = gtk_icon_theme_load_icon(icon_theme, names[id], 48, GTK_ICON_LOOKUP_NO_SVG, NULL);
  gtk_image_set_from_pixbuf(GTK_IMAGE(plugin->priv->image), pixbuf);
  g_object_unref(pixbuf);
}

static gint
battery_status_plugin_str_time(BatteryStatusAreaItem *plugin,
                               gchar *ptr,
                               gulong n,
                               int time)
{
  int num;
  const char *str;
  gchar text[64];
  gchar *ptr2;

  if (time/60/60/24 > 0)
  {
    num = time/60/60/24;
    str = dngettext("osso-clock", "cloc_va_amount_day", "cloc_va_amount_days", num);
  }
  else if (time/60/60 > 0)
  {
    num = time/60/60;
    if (plugin->priv->is_charging)
      ++num;
    str = dngettext("mediaplayer", "mp_bd_label_hour%s", "mp_bd_label_hours%s", num);

    strncpy(text, str, sizeof(text)-1);
    text[sizeof(text)-1] = 0;
    ptr2 = strstr(text, "%s");

    if (ptr2)
    {
      *(ptr2+1) = 'd';
      str = text;
    }
  }
  else if (time/60 > 0)
  {
    num = time/60;
    str = dngettext("osso-display", "disp_va_do_2", "disp_va_gene_2", num);
  }
  else
  {
    num = 0;
    str = NULL;
  }

  if (num && str)
    return g_snprintf(ptr, n, str, num);
  else
    return 0;
}

static void
battery_status_plugin_update_text(BatteryStatusAreaItem *plugin)
{
  gchar text[64];
  gchar *ptr, *limit;
  gboolean calibrated;

  if (plugin->priv->display_is_off)
    return;

  ptr    = text;
  limit  = &text[sizeof(text)];
  ptr[0] = '\0';

  calibrated = batt_calibrated();
  if (!calibrated || !plugin->priv->use_design)
  {
    const char* batt_status = NULL;

    if (calibrated)
    {
      /* Show "Battery: xx%" if not fully charged */
      ptr += g_snprintf(text, sizeof(text), "%s: ", dgettext("osso-dsm-ui", "tncpa_li_plugin_sb_battery"));

      if (!plugin->priv->is_charging || !plugin->priv->is_discharging)
        ptr += g_snprintf(ptr, limit - ptr, "%d%%", plugin->priv->percentage);

      gtk_label_set_text(GTK_LABEL(plugin->priv->title), text);
      text[0] = '\0';
    }

    if (plugin->priv->is_charging && plugin->priv->is_discharging)
      batt_status = "incf_me_battery_charged";
    else if (plugin->priv->is_charging)
      batt_status = "incf_me_battery_charging";

    if (batt_status)
      g_snprintf(text, sizeof(text), "%s", dgettext("osso-dsm-ui", batt_status));
    else if (!calibrated)
      /* TODO: translate using gettext */
      g_snprintf(text, sizeof(text), "Calibration needed");

    gtk_label_set_text(GTK_LABEL(plugin->priv->value), text);

    return;
  }

  /* "Battery" */
  ptr += g_snprintf(text, sizeof(text), "%s: ", dgettext("osso-dsm-ui", "tncpa_li_plugin_sb_battery"));

  if (plugin->priv->is_charging && plugin->priv->is_discharging)
    /* "Fully charged" */
    ptr += g_snprintf(ptr, limit - ptr, "%s", dgettext("osso-dsm-ui", "incf_me_battery_charged"));
  else if (!plugin->priv->is_charging)
    ptr += g_snprintf(ptr, limit - ptr, "%d%%", plugin->priv->percentage);
  else
  {
    if (plugin->priv->percentage != 0)
      ptr += g_snprintf(ptr, limit - ptr, "%d%% ", plugin->priv->percentage);
    /* "Charging" */
    ptr += g_snprintf(ptr, limit - ptr, "%s", dgettext("osso-dsm-ui", "incf_me_battery_charging"));
  }

  gtk_label_set_text(GTK_LABEL(plugin->priv->title), text);

  ptr = text;
  ptr[0] = '\0';

  if (plugin->priv->charge_full > 0)
  {
    ptr += g_snprintf(ptr, limit - ptr, "%d/%d mAh", plugin->priv->charge_now, plugin->priv->charge_full);

    if ((!plugin->priv->is_charging || !plugin->priv->is_discharging) && plugin->priv->active_time)
    {
      ptr += g_snprintf(ptr, limit - ptr, "  ");
      ptr += battery_status_plugin_str_time(plugin, ptr, limit - ptr, plugin->priv->active_time);
    }
  }
  else
  {
    ptr += g_snprintf(ptr, limit - ptr, "No data");
  }

  gtk_label_set_text(GTK_LABEL(plugin->priv->value), text);
}

static gboolean
battery_status_plugin_animation(gpointer data)
{
  BatteryStatusAreaItem *plugin = data;
  int bars = plugin->priv->bars;

  if (!plugin->priv->timer_id)
    return FALSE;

  if (plugin->priv->is_charging && plugin->priv->is_discharging)
    return FALSE;

  if (bars == 0 || !plugin->priv->show_charge_charging)
    plugin->priv->charging_idx = 1 + plugin->priv->charging_idx % 8; /* id is 1..8 */
  else if (bars == 8)
    plugin->priv->charging_idx = 7 + plugin->priv->charging_idx % 2; /* id is 7..8 */
  else
    plugin->priv->charging_idx = bars +( plugin->priv->charging_idx - bars + 1 ) %( 9 - bars ); /* id is bars..8 */

  battery_status_plugin_update_icon(plugin, plugin->priv->charging_idx);
  return TRUE;
}

static void
battery_status_plugin_charger_connected(BatteryStatusAreaItem *plugin)
{
  hildon_banner_show_information(GTK_WIDGET(plugin), NULL, dgettext("osso-dsm-ui", "incf_ib_battery_charging"));
  battery_status_plugin_play_sound(plugin, "/usr/share/sounds/ui-charging_started.wav", FALSE);
}

static void
battery_status_plugin_charger_disconnected(BatteryStatusAreaItem *plugin)
{
  hildon_banner_show_information(GTK_WIDGET(plugin), NULL, dgettext("osso-dsm-ui", "incf_ib_disconnect_charger"));
}

static void
battery_status_plugin_animation_start(BatteryStatusAreaItem *plugin, gboolean resume)
{
  if (plugin->priv->display_is_off || !plugin->priv->is_charging || plugin->priv->is_discharging)
    return;

  if (plugin->priv->timer_id == 0)
  {
    plugin->priv->timer_id = g_timeout_add_seconds(1, battery_status_plugin_animation, plugin);

    if (resume)
    {
      if (plugin->priv->charging_idx != 8)
        battery_status_plugin_animation(plugin);
    }
    else
      plugin->priv->charging_idx = plugin->priv->bars;
  }
}

static void
battery_status_plugin_animation_stop(BatteryStatusAreaItem *plugin)
{
  if (plugin->priv->timer_id > 0)
  {
    g_source_remove(plugin->priv->timer_id);
    plugin->priv->timer_id = 0;
  }
}

static void
battery_status_plugin_charging_start(BatteryStatusAreaItem *plugin)
{
  battery_status_plugin_animation_start(plugin, FALSE);
}

static void
battery_status_plugin_charging_stop(BatteryStatusAreaItem *plugin)
{
  if (plugin->priv->timer_id > 0 && plugin->priv->is_charging && plugin->priv->is_discharging)
    hildon_banner_show_information(GTK_WIDGET(plugin), NULL, dgettext("osso-dsm-ui", "incf_ib_battery_full"));

  battery_status_plugin_animation_stop(plugin);

  if (plugin->priv->charger_connected && !plugin->priv->is_charging)
    hildon_banner_show_information(GTK_WIDGET(plugin), NULL, dgettext("osso-dsm-ui", "incf_ni_consumes_more_than_receives"));

  if (plugin->priv->is_charging && plugin->priv->is_discharging)
    battery_status_plugin_update_icon(plugin, 8);
  else
    battery_status_plugin_update_icon(plugin, plugin->priv->bars);
}

static DBusHandlerResult
battery_status_plugin_dbus_display(DBusConnection *connection,
                                   DBusMessage *message,
                                   void *data)
{
  BatteryStatusAreaItem *plugin = data;
  char *status = NULL;
  gboolean display_is_off = FALSE;

  if (!dbus_message_is_signal(message, "com.nokia.mce.signal", "display_status_ind") ||
      g_strcmp0(dbus_message_get_path(message), "/com/nokia/mce/signal") ||
      !dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &status, DBUS_TYPE_INVALID))
  {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (g_strcmp0(status, "off") == 0)
    display_is_off = TRUE;

  if (display_is_off == plugin->priv->display_is_off)
    return DBUS_HANDLER_RESULT_HANDLED;

  plugin->priv->display_is_off = display_is_off;

  if (display_is_off)
    battery_status_plugin_animation_stop(plugin);
  else
  {
    battery_status_plugin_animation_start(plugin, TRUE);
    battery_status_plugin_update_text(plugin);
    if (plugin->priv->is_charging && plugin->priv->is_discharging)
      battery_status_plugin_update_icon(plugin, 8);
    else if (plugin->priv->is_discharging)
      battery_status_plugin_update_icon(plugin, plugin->priv->bars);
  }

  return DBUS_HANDLER_RESULT_HANDLED;
}

static void
battery_status_plugin_battery_empty(BatteryStatusAreaItem *plugin)
{
  hildon_banner_show_information_override_dnd(GTK_WIDGET(plugin), dgettext("osso-dsm-ui", "incf_ib_battery_recharge"));
  battery_status_plugin_update_icon(plugin, -1);
  battery_status_plugin_play_sound(plugin, "/usr/share/sounds/ui-recharge_battery.wav", FALSE);
}

static void
battery_status_plugin_battery_low(BatteryStatusAreaItem *plugin)
{
  if (plugin->priv->low_last_reported < time(NULL) && plugin->priv->low_last_reported + 30 > time(NULL))
    return;

  plugin->priv->low_last_reported = time(NULL);

  hildon_banner_show_information_override_dnd(GTK_WIDGET(plugin), dgettext("osso-dsm-ui", "incf_ib_battery_low"));
  battery_status_plugin_update_icon(plugin, 0);
  battery_status_plugin_play_sound(plugin, "/usr/share/sounds/ui-battery_low.wav", plugin->priv->verylow);
}

static void
battery_status_plugin_update_value_visibility(BatteryStatusAreaItem *plugin)
{
  static gboolean visible = FALSE;

  gboolean show = plugin->priv->charger_connected ||
                  plugin->priv->use_design ||
                  !batt_calibrated();

  if (visible == show)
    return;

  if ((visible = show))
  {
    gtk_widget_show(plugin->priv->value);
    gtk_alignment_set_padding(GTK_ALIGNMENT(plugin->priv->alignment), 0, 0, 0, 0);
  }
  else
  {
    gtk_widget_hide(plugin->priv->value);
    gtk_alignment_set_padding(GTK_ALIGNMENT(plugin->priv->alignment), 12, 12, 0, 0);
  }
}

static void
battery_status_plugin_update_charger(BatteryStatusAreaItem *plugin)
{
  if (plugin->priv->charger_connected)
  {
    battery_status_plugin_charger_connected(plugin);
    battery_status_plugin_update_value_visibility(plugin);
  }
  else
  {
    battery_status_plugin_charger_disconnected(plugin);
    battery_status_plugin_charging_stop(plugin);
    battery_status_plugin_update_value_visibility(plugin);
  }
}

static void
battery_status_plugin_update_charging(BatteryStatusAreaItem *plugin)
{
  if (plugin->priv->is_charging && !plugin->priv->is_discharging)
    battery_status_plugin_charging_start(plugin);
  else if (plugin->priv->is_discharging)
    battery_status_plugin_charging_stop(plugin);
}

static void
battery_status_plugin_update_image_padding(BatteryStatusAreaItem *plugin)
{
  guint padding = 0;

  if (!plugin->priv->use_design || !batt_calibrated())
    padding = 10;

  gtk_box_set_child_packing(GTK_BOX(plugin->priv->hbox),
                            plugin->priv->image,
                            FALSE, FALSE, padding,
                            GTK_PACK_START);
}

static void
battery_status_plugin_gconf_notify(GConfClient *client,
                                   guint cnxn_id,
                                   GConfEntry *entry,
                                   gpointer data)
{
  BatteryStatusAreaItem *plugin = data;
  const char *key = gconf_entry_get_key(entry);
  GConfValue *value = gconf_entry_get_value(entry);

  if (strcmp(key, GCONF_USE_DESIGN_KEY) == 0)
  {
    plugin->priv->use_design = value ? gconf_value_get_int(value) : 1;
    battery_status_plugin_update_value_visibility(plugin);
    battery_status_plugin_update_image_padding(plugin);
    battery_status_plugin_update_text(plugin);
  }
  else if (strcmp(key, GCONF_SHOW_CHARGE_CHARGING_KEY) == 0)
  {
    plugin->priv->show_charge_charging = value ? gconf_value_get_bool(value) : FALSE;
  }
  else if (strcmp(key, GCONF_EXEC_APPLICATION) == 0)
  {
    plugin->priv->exec_application = value ? gconf_value_get_string(value) : NULL;
  }
}

static void
on_property_changed(BatteryData *data, void *user_data)
{
  BatteryStatusAreaItem *plugin = (BatteryStatusAreaItem *)user_data;

  int bars;

  /* Used to store previous values, e.g. the values *before* we update them */
  gboolean is_charging = plugin->priv->is_charging;
  gboolean is_discharging = plugin->priv->is_discharging;
  gboolean charger_connected = plugin->priv->charger_connected;

  plugin->priv->charger_connected = data->charger_online;

  plugin->priv->charge_now  = (int)(1000 * data->charge_now);
  plugin->priv->charge_full = (int)(1000 * data->charge_full);

  plugin->priv->percentage = (int)data->percentage;

  bars = (int)(8 *(6.25 + data->percentage) / 100);

  switch(data->state)
  {
    case UP_DEVICE_STATE_UNKNOWN:
    case UP_DEVICE_STATE_DISCHARGING:
    case UP_DEVICE_STATE_EMPTY:
    case UP_DEVICE_STATE_PENDING_CHARGE: /* Unsure about this one */
      plugin->priv->is_discharging = TRUE;
      plugin->priv->is_charging = FALSE;
      plugin->priv->active_time = data->time_to_empty;
      break;

    case UP_DEVICE_STATE_PENDING_DISCHARGE:
      plugin->priv->is_discharging = FALSE;
      plugin->priv->is_charging = FALSE;
      plugin->priv->active_time = 0;
      break;

    case UP_DEVICE_STATE_FULLY_CHARGED:
      plugin->priv->is_discharging = TRUE;
      plugin->priv->is_charging = TRUE;
      plugin->priv->active_time = 0;
      break;

    case UP_DEVICE_STATE_CHARGING:
      if (plugin->priv->is_discharging == TRUE &&
          plugin->priv->is_charging == TRUE)
      {
        /* Prevent undesired messages when fully charged */
        return;
      }
      plugin->priv->is_discharging = FALSE;
      plugin->priv->is_charging = TRUE;
      plugin->priv->active_time = data->time_to_full;
      break;
  }

  if (plugin->priv->bars != bars)
  {
    plugin->priv->bars = bars;
    if (plugin->priv->is_charging && plugin->priv->is_discharging)
      battery_status_plugin_update_icon(plugin, 8);
    else if (plugin->priv->is_discharging)
      battery_status_plugin_update_icon(plugin, bars);
  }

  if (plugin->priv->charger_connected != charger_connected &&
      data->state != UP_DEVICE_STATE_FULLY_CHARGED)
  {
    battery_status_plugin_update_charger(plugin);
  }

  if (plugin->priv->is_charging != is_charging || plugin->priv->is_discharging != is_discharging)
    battery_status_plugin_update_charging(plugin);

  battery_status_plugin_update_text(plugin);
  battery_status_plugin_update_icon(plugin, bars);


  /*
   * Fire a warning message if battery is near empty
   * This check is for voltage estimated percentage: plugin->priv->percentage != 0
   */
  if ((batt_calibrated() || plugin->priv->percentage != 0) &&
      plugin->priv->is_discharging)
  {
    if (plugin->priv->percentage < 5)
      battery_status_plugin_battery_empty(plugin);
    else if (plugin->priv->percentage < 10)
      battery_status_plugin_battery_low(plugin);
  }
}

static gboolean
battery_status_plugin_on_button_clicked_cb(GtkWidget *widget,
                                           GdkEvent *event,
                                           gpointer user_data)
{
  BatteryStatusAreaItem *plugin = user_data;

  if (plugin->priv->exec_application && plugin->priv->exec_application[0])
    g_spawn_command_line_async(plugin->priv->exec_application, NULL);

  return TRUE;
}

static void
battery_status_plugin_init(BatteryStatusAreaItem *plugin)
{
  DBusError error;
  GtkWidget *alignment;
  GtkWidget *label_box;
  GtkWidget *event_box;
  GtkStyle *style;

  plugin->priv = G_TYPE_INSTANCE_GET_PRIVATE(plugin, battery_status_plugin_get_type(), BatteryStatusAreaItemPrivate);

  dbus_error_init(&error);

  plugin->priv->dbus_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
  if (dbus_error_is_set(&error))
  {
    g_warning("Could not open D-Bus session bus connection");
    dbus_error_free(&error);
    return;
  }

  dbus_error_free(&error);

  /* FIXME: Change init_batt so that it will not fail when it fails to find a
   * battery, and instead attempt to find one later, when
   * DeviceAdded/DeviceRemoved occurs */
  int bat_err = init_batt();
  if (bat_err)
  {
    g_warning("Could not initialise batmon");
    return;
  }
  set_batt_cb(on_property_changed, plugin);

  plugin->priv->context = NULL;
  if (ca_context_create(&plugin->priv->context) < 0)
  {
    g_warning("Could not create Canberra context");
    return;
  }

  ca_context_open(plugin->priv->context);

  plugin->priv->pl = NULL;
  if (ca_proplist_create(&plugin->priv->pl) < 0)
  {
    g_warning("Could not create Canberra proplist");
    return;
  }

  plugin->priv->gconf = gconf_client_get_default();
  if (!plugin->priv->gconf)
  {
    g_warning("Could not get gconf client");
    return;
  }

  gconf_client_add_dir(plugin->priv->gconf, GCONF_PATH, GCONF_CLIENT_PRELOAD_NONE, NULL);
  plugin->priv->gconf_notify = gconf_client_notify_add(plugin->priv->gconf, GCONF_USE_DESIGN_KEY, battery_status_plugin_gconf_notify, plugin, NULL, NULL);
  plugin->priv->gconf_notify = gconf_client_notify_add(plugin->priv->gconf, GCONF_SHOW_CHARGE_CHARGING_KEY, battery_status_plugin_gconf_notify, plugin, NULL, NULL);
  plugin->priv->gconf_notify = gconf_client_notify_add(plugin->priv->gconf, GCONF_EXEC_APPLICATION, battery_status_plugin_gconf_notify, plugin, NULL, NULL);

  plugin->priv->title = gtk_label_new(dgettext("osso-dsm-ui", "tncpa_li_plugin_sb_battery"));
  if (!plugin->priv->title)
  {
    g_warning("Could not create GtkLabel");
    return;
  }

  plugin->priv->value = gtk_label_new(NULL);
  if (!plugin->priv->value)
  {
    g_warning("Could not create GtkLabel");
    gtk_widget_destroy(plugin->priv->title);
    return;
  }

  plugin->priv->image = gtk_image_new();
  if (!plugin->priv->image)
  {
    g_warning("Could not create GtkImage");
    gtk_widget_destroy(plugin->priv->title);
    gtk_widget_destroy(plugin->priv->value);
    return;
  }

  alignment = gtk_alignment_new(0, 0.5, 0, 0);
  if (!alignment)
  {
    g_warning("Could not create GtkAlignment");
    gtk_widget_destroy(plugin->priv->title);
    gtk_widget_destroy(plugin->priv->value);
    gtk_widget_destroy(plugin->priv->image);
    return;
  }

  event_box = gtk_event_box_new();
  if (!event_box)
  {
    g_warning("Could not create GtkEventBox");
    gtk_widget_destroy(plugin->priv->title);
    gtk_widget_destroy(plugin->priv->value);
    gtk_widget_destroy(plugin->priv->image);
    gtk_widget_destroy(alignment);
    return;
  }

  plugin->priv->hbox = gtk_hbox_new(FALSE, 0);
  if (!plugin->priv->hbox)
  {
    g_warning("Could not create GtkHBox");
    gtk_widget_destroy(plugin->priv->title);
    gtk_widget_destroy(plugin->priv->value);
    gtk_widget_destroy(plugin->priv->image);
    gtk_widget_destroy(alignment);
    gtk_widget_destroy(event_box);
    return;
  }

  label_box = gtk_vbox_new(FALSE, 0);
  if (!label_box)
  {
    g_warning("Could not create GtkVBox");
    gtk_widget_destroy(plugin->priv->title);
    gtk_widget_destroy(plugin->priv->value);
    gtk_widget_destroy(plugin->priv->image);
    gtk_widget_destroy(plugin->priv->hbox);
    gtk_widget_destroy(alignment);
    gtk_widget_destroy(event_box);
    return;
  }

  plugin->priv->alignment = gtk_alignment_new(0, 0.5, 0, 0);
  if (!plugin->priv->alignment)
  {
    g_warning("Could not create GtkAlignment");
    gtk_widget_destroy(plugin->priv->title);
    gtk_widget_destroy(plugin->priv->value);
    gtk_widget_destroy(plugin->priv->image);
    gtk_widget_destroy(plugin->priv->hbox);
    gtk_widget_destroy(alignment);
    gtk_widget_destroy(event_box);
    gtk_widget_destroy(label_box);
    return;
  }

  gtk_widget_set_name(plugin->priv->title, "hildon-button-title");
  gtk_widget_set_name(plugin->priv->value, "hildon-button-value");

  gtk_misc_set_alignment(GTK_MISC(plugin->priv->title), 0, 0.5);
  gtk_misc_set_alignment(GTK_MISC(plugin->priv->value), 0, 0.5);

  style = gtk_rc_get_style_by_paths(gtk_settings_get_default(), "SmallSystemFont", NULL, G_TYPE_NONE);
  if (style && style->font_desc)
    gtk_widget_modify_font(GTK_WIDGET(plugin->priv->value), style->font_desc);

  gtk_container_add(GTK_CONTAINER(plugin->priv->alignment), plugin->priv->title);
  gtk_box_pack_start(GTK_BOX(label_box), plugin->priv->alignment, FALSE, FALSE, 0);
  gtk_alignment_set_padding(GTK_ALIGNMENT(plugin->priv->alignment), 12, 12, 0, 0);

  gtk_box_pack_start(GTK_BOX(label_box), plugin->priv->value, TRUE, TRUE, 0);
  gtk_widget_set_no_show_all(plugin->priv->value, TRUE);

  gtk_box_pack_start(GTK_BOX(plugin->priv->hbox), plugin->priv->image, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(plugin->priv->hbox), label_box, TRUE, TRUE, 0);

  gtk_container_add(GTK_CONTAINER(alignment), plugin->priv->hbox);
  gtk_container_add(GTK_CONTAINER(event_box), alignment);
  gtk_widget_set_events(event_box, GDK_BUTTON_PRESS_MASK);
  g_signal_connect_after(G_OBJECT(event_box), "button-press-event", G_CALLBACK(battery_status_plugin_on_button_clicked_cb), plugin);

  plugin->priv->is_discharging = TRUE;
  plugin->priv->display_is_off = FALSE;

  plugin->priv->use_design = gconf_client_get_int(plugin->priv->gconf, GCONF_USE_DESIGN_KEY, NULL);
  plugin->priv->show_charge_charging = gconf_client_get_bool(plugin->priv->gconf, GCONF_SHOW_CHARGE_CHARGING_KEY, NULL);
  plugin->priv->exec_application = gconf_client_get_string(plugin->priv->gconf, GCONF_EXEC_APPLICATION, NULL);

  plugin->priv->bars = -2;
  on_property_changed(get_batt_data(), plugin);
  battery_status_plugin_update_image_padding(plugin);
  battery_status_plugin_update_value_visibility(plugin);

  dbus_bus_add_match(plugin->priv->dbus_conn, DBUS_MATCH_RULE, NULL);
  dbus_connection_add_filter(plugin->priv->dbus_conn, battery_status_plugin_dbus_display, plugin, NULL);

  gtk_container_add(GTK_CONTAINER(plugin), event_box);
  gtk_widget_show_all(GTK_WIDGET(plugin));
}

static void
battery_status_plugin_finalize(GObject *object)
{
  BatteryStatusAreaItem *plugin = G_TYPE_CHECK_INSTANCE_CAST(object, battery_status_plugin_get_type(), BatteryStatusAreaItem);

  if (plugin->priv->pl)
  {
    ca_proplist_destroy(plugin->priv->pl);
    plugin->priv->pl = NULL;
  }

  if (plugin->priv->context)
  {
    ca_context_destroy(plugin->priv->context);
    plugin->priv->context = NULL;
  }

  if (plugin->priv->gconf)
  {
    if (plugin->priv->gconf_notify)
    {
      gconf_client_notify_remove(plugin->priv->gconf, plugin->priv->gconf_notify);
      plugin->priv->gconf_notify = 0;
    }
    gconf_client_remove_dir(plugin->priv->gconf, GCONF_PATH, NULL);
    gconf_client_clear_cache(plugin->priv->gconf);
    g_object_unref(plugin->priv->gconf);
    plugin->priv->gconf = NULL;
  }

  free_batt();

  if (plugin->priv->dbus_conn)
  {
    dbus_connection_unref(plugin->priv->dbus_conn);
    plugin->priv->dbus_conn = NULL;
  }

  if (plugin->priv->timer_id > 0)
  {
    g_source_remove(plugin->priv->timer_id);
    plugin->priv->timer_id = 0;
  }

  dbus_bus_remove_match(plugin->priv->dbus_conn, DBUS_MATCH_RULE, NULL);
  dbus_connection_remove_filter(plugin->priv->dbus_conn, battery_status_plugin_dbus_display, plugin);

  hd_status_plugin_item_set_status_area_icon(HD_STATUS_PLUGIN_ITEM(plugin), NULL);

  G_OBJECT_CLASS(battery_status_plugin_parent_class)->finalize(object);
}

static void
battery_status_plugin_class_init(BatteryStatusAreaItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->finalize = (GObjectFinalizeFunc)battery_status_plugin_finalize;

  g_type_class_add_private(klass, sizeof(BatteryStatusAreaItemPrivate));
}

static void
battery_status_plugin_class_finalize(BatteryStatusAreaItemClass *klass)
{
}
