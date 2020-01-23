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

HD_DEFINE_PLUGIN_MODULE_EXTENDED(BatteryStatusAreaItem,
                        battery_status_plugin,
                        HD_TYPE_STATUS_MENU_ITEM,
			G_ADD_PRIVATE(BatteryStatusAreaItem), , );

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
  BatteryStatusAreaItemPrivate *priv = plugin->priv;

  if (priv->display_is_off)
    return;

  ptr    = text;
  limit  = &text[sizeof(text)];
  ptr[0] = '\0';

  calibrated = batt_calibrated();
  if (!calibrated || !priv->use_design)
  {
    const char* batt_status = NULL;

    if (calibrated)
    {
      /* Show "Battery: xx%" if not fully charged */
      ptr += g_snprintf(text, sizeof(text), "%s: ", dgettext("osso-dsm-ui", "tncpa_li_plugin_sb_battery"));

      if (!priv->is_charging || !priv->is_discharging)
        ptr += g_snprintf(ptr, limit - ptr, "%d%%", priv->percentage);

      gtk_label_set_text(GTK_LABEL(priv->title), text);
      text[0] = '\0';
    }

    if (priv->is_charging && priv->is_discharging)
      batt_status = "incf_me_battery_charged";
    else if (priv->is_charging)
      batt_status = "incf_me_battery_charging";

    if (batt_status)
      g_snprintf(text, sizeof(text), "%s", dgettext("osso-dsm-ui", batt_status));
    else if (!calibrated)
      /* TODO: translate using gettext */
      g_snprintf(text, sizeof(text), "Calibration needed");

    gtk_label_set_text(GTK_LABEL(priv->value), text);

    return;
  }

  /* "Battery" */
  ptr += g_snprintf(text, sizeof(text), "%s: ", dgettext("osso-dsm-ui", "tncpa_li_plugin_sb_battery"));

  if (priv->is_charging && priv->is_discharging)
    /* "Fully charged" */
    ptr += g_snprintf(ptr, limit - ptr, "%s", dgettext("osso-dsm-ui", "incf_me_battery_charged"));
  else if (!priv->is_charging)
    ptr += g_snprintf(ptr, limit - ptr, "%d%%", priv->percentage);
  else
  {
    if (priv->percentage != 0)
      ptr += g_snprintf(ptr, limit - ptr, "%d%% ", priv->percentage);
    /* "Charging" */
    ptr += g_snprintf(ptr, limit - ptr, "%s", dgettext("osso-dsm-ui", "incf_me_battery_charging"));
  }

  gtk_label_set_text(GTK_LABEL(priv->title), text);

  ptr = text;
  ptr[0] = '\0';

  if (priv->charge_full > 0)
  {
    ptr += g_snprintf(ptr, limit - ptr, "%d/%d mAh", priv->charge_now, priv->charge_full);

    if ((!priv->is_charging || !priv->is_discharging) && priv->active_time)
    {
      ptr += g_snprintf(ptr, limit - ptr, "  ");
      ptr += battery_status_plugin_str_time(plugin, ptr, limit - ptr, priv->active_time);
    }
  }
  else
  {
    ptr += g_snprintf(ptr, limit - ptr, "No data");
  }

  gtk_label_set_text(GTK_LABEL(priv->value), text);
}

static gboolean
battery_status_plugin_animation(gpointer data)
{
  BatteryStatusAreaItem *plugin = data;
  BatteryStatusAreaItemPrivate *priv = plugin->priv;
  int bars = priv->bars;

  if (!priv->timer_id)
    return FALSE;

  if (priv->is_charging && priv->is_discharging)
    return FALSE;

  if (bars == 0 || !priv->show_charge_charging)
    priv->charging_idx = 1 + priv->charging_idx % 8; /* id is 1..8 */
  else if (bars == 8)
    priv->charging_idx = 7 + priv->charging_idx % 2; /* id is 7..8 */
  else
    priv->charging_idx = bars + (priv->charging_idx - bars + 1) % (9 - bars); /* id is bars..8 */

  battery_status_plugin_update_icon(plugin, priv->charging_idx);
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
  BatteryStatusAreaItemPrivate *priv = plugin->priv;

  if (priv->display_is_off || !priv->is_charging || priv->is_discharging)
    return;

  if (priv->timer_id == 0)
  {
    priv->timer_id = g_timeout_add_seconds(1, battery_status_plugin_animation, plugin);

    if (resume)
    {
      if (priv->charging_idx != 8)
        battery_status_plugin_animation(plugin);
    }
    else
      priv->charging_idx = priv->bars;
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
  BatteryStatusAreaItemPrivate *priv = plugin->priv;

  if (priv->timer_id > 0 && priv->is_charging && priv->is_discharging)
    hildon_banner_show_information(GTK_WIDGET(plugin), NULL, dgettext("osso-dsm-ui", "incf_ib_battery_full"));

  battery_status_plugin_animation_stop(plugin);

  if (priv->charger_connected && !priv->is_charging)
    hildon_banner_show_information(GTK_WIDGET(plugin), NULL, dgettext("osso-dsm-ui", "incf_ni_consumes_more_than_receives"));

  if (priv->is_charging && priv->is_discharging)
    battery_status_plugin_update_icon(plugin, 8);
  else
    battery_status_plugin_update_icon(plugin, priv->bars);
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
on_property_changed(BatteryData *batt_data, void *user_data)
{
  BatteryStatusAreaItem *plugin = (BatteryStatusAreaItem *)user_data;
  BatteryStatusAreaItemPrivate *priv = plugin->priv;

  int bars;

  /* Used to store previous values, e.g. the values *before* we update them */
  gboolean is_charging = priv->is_charging;
  gboolean is_discharging = priv->is_discharging;
  gboolean charger_connected = priv->charger_connected;

  priv->charger_connected = batt_data->charger_online;

  priv->charge_now  = (int)(1000 * batt_data->charge_now);
  priv->charge_full = (int)(1000 * batt_data->charge_full);

  priv->percentage = (int)batt_data->percentage;

  bars = (int)(8 *(6.25 + batt_data->percentage) / 100);

  switch(batt_data->state)
  {
    case UP_DEVICE_STATE_UNKNOWN:
    case UP_DEVICE_STATE_DISCHARGING:
    case UP_DEVICE_STATE_EMPTY:
    case UP_DEVICE_STATE_PENDING_CHARGE: /* Unsure about this one */
      priv->is_discharging = TRUE;
      priv->is_charging = FALSE;
      priv->active_time = batt_data->time_to_empty;
      break;

    case UP_DEVICE_STATE_PENDING_DISCHARGE:
      priv->is_discharging = FALSE;
      priv->is_charging = FALSE;
      priv->active_time = 0;
      break;

    case UP_DEVICE_STATE_FULLY_CHARGED:
      priv->is_discharging = TRUE;
      priv->is_charging = TRUE;
      priv->active_time = 0;
      break;

    case UP_DEVICE_STATE_CHARGING:
      if (priv->is_discharging == TRUE &&
          priv->is_charging == TRUE)
      {
        /* Prevent undesired messages when fully charged */
        return;
      }
      priv->is_discharging = FALSE;
      priv->is_charging = TRUE;
      priv->active_time = batt_data->time_to_full;
      break;
  }

  if (priv->bars != bars)
  {
    priv->bars = bars;
    if (priv->is_charging && priv->is_discharging)
      battery_status_plugin_update_icon(plugin, 8);
    else if (priv->is_discharging)
      battery_status_plugin_update_icon(plugin, bars);
  }

  if (priv->charger_connected != charger_connected &&
      batt_data->state != UP_DEVICE_STATE_FULLY_CHARGED)
  {
    battery_status_plugin_update_charger(plugin);
  }

  if (priv->is_charging != is_charging ||
      priv->is_discharging != is_discharging)
  {
    battery_status_plugin_update_charging(plugin);
  }

  battery_status_plugin_update_text(plugin);
  battery_status_plugin_update_icon(plugin, bars);

  /*
   * Fire a warning message if battery is almost empty.
   *
   * This check is for voltage estimated percentage:
   * priv->percentage != 0
   */
  if ((batt_calibrated() || priv->percentage != 0) &&
      priv->is_discharging)
  {
    if (priv->percentage < 5)
      battery_status_plugin_battery_empty(plugin);
    else if (priv->percentage < 10)
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
  BatteryStatusAreaItemPrivate *priv;
  DBusError error;
  GtkWidget *alignment;
  GtkWidget *label_box;
  GtkWidget *event_box;
  GtkStyle *style;

  plugin->priv = (BatteryStatusAreaItemPrivate*)battery_status_plugin_get_instance_private(plugin);
  priv = plugin->priv;

  dbus_error_init(&error);

  priv->dbus_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
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

  priv->context = NULL;
  if (ca_context_create(&priv->context) < 0)
  {
    g_warning("Could not create Canberra context");
    return;
  }

  ca_context_open(priv->context);

  priv->pl = NULL;
  if (ca_proplist_create(&priv->pl) < 0)
  {
    g_warning("Could not create Canberra proplist");
    return;
  }

  priv->gconf = gconf_client_get_default();
  if (!priv->gconf)
  {
    g_warning("Could not get gconf client");
    return;
  }

  gconf_client_add_dir(priv->gconf, GCONF_PATH, GCONF_CLIENT_PRELOAD_NONE, NULL);
  priv->gconf_notify = gconf_client_notify_add(priv->gconf, GCONF_USE_DESIGN_KEY, battery_status_plugin_gconf_notify, plugin, NULL, NULL);
  priv->gconf_notify = gconf_client_notify_add(priv->gconf, GCONF_SHOW_CHARGE_CHARGING_KEY, battery_status_plugin_gconf_notify, plugin, NULL, NULL);
  priv->gconf_notify = gconf_client_notify_add(priv->gconf, GCONF_EXEC_APPLICATION, battery_status_plugin_gconf_notify, plugin, NULL, NULL);

  priv->title = gtk_label_new(dgettext("osso-dsm-ui", "tncpa_li_plugin_sb_battery"));
  if (!priv->title)
  {
    g_warning("Could not create GtkLabel");
    return;
  }

  priv->value = gtk_label_new(NULL);
  if (!priv->value)
  {
    g_warning("Could not create GtkLabel");
    goto no_label;
  }

  priv->image = gtk_image_new();
  if (!priv->image)
  {
    g_warning("Could not create GtkImage");
    goto no_image;
  }

  alignment = gtk_alignment_new(0, 0.5, 0, 0);
  if (!alignment)
  {
    g_warning("Could not create GtkAlignment");
    goto no_halignment;
  }

  event_box = gtk_event_box_new();
  if (!event_box)
  {
    g_warning("Could not create GtkEventBox");
    goto no_event_box;
  }

  priv->hbox = gtk_hbox_new(FALSE, 0);
  if (!priv->hbox)
  {
    g_warning("Could not create GtkHBox");
    goto no_hbox;
  }

  label_box = gtk_vbox_new(FALSE, 0);
  if (!label_box)
  {
    g_warning("Could not create GtkVBox");
    goto no_vbox;
  }

  priv->alignment = gtk_alignment_new(0, 0.5, 0, 0);
  if (!priv->alignment)
  {
    g_warning("Could not create GtkAlignment");
    goto no_valignment;
  }

  gtk_widget_set_name(priv->title, "hildon-button-title");
  gtk_widget_set_name(priv->value, "hildon-button-value");

  gtk_misc_set_alignment(GTK_MISC(priv->title), 0, 0.5);
  gtk_misc_set_alignment(GTK_MISC(priv->value), 0, 0.5);

  style = gtk_rc_get_style_by_paths(gtk_settings_get_default(), "SmallSystemFont", NULL, G_TYPE_NONE);
  if (style && style->font_desc)
    gtk_widget_modify_font(GTK_WIDGET(priv->value), style->font_desc);

  gtk_container_add(GTK_CONTAINER(priv->alignment), priv->title);
  gtk_box_pack_start(GTK_BOX(label_box), priv->alignment, FALSE, FALSE, 0);
  gtk_alignment_set_padding(GTK_ALIGNMENT(priv->alignment), 12, 12, 0, 0);

  gtk_box_pack_start(GTK_BOX(label_box), priv->value, TRUE, TRUE, 0);
  gtk_widget_set_no_show_all(priv->value, TRUE);

  gtk_box_pack_start(GTK_BOX(priv->hbox), priv->image, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(priv->hbox), label_box, TRUE, TRUE, 0);

  gtk_container_add(GTK_CONTAINER(alignment), priv->hbox);
  gtk_container_add(GTK_CONTAINER(event_box), alignment);
  gtk_widget_set_events(event_box, GDK_BUTTON_PRESS_MASK);
  g_signal_connect_after(G_OBJECT(event_box), "button-press-event", G_CALLBACK(battery_status_plugin_on_button_clicked_cb), plugin);

  priv->is_discharging = TRUE;
  priv->display_is_off = FALSE;

  priv->use_design = gconf_client_get_int(priv->gconf, GCONF_USE_DESIGN_KEY, NULL);
  priv->show_charge_charging = gconf_client_get_bool(priv->gconf, GCONF_SHOW_CHARGE_CHARGING_KEY, NULL);
  priv->exec_application = gconf_client_get_string(priv->gconf, GCONF_EXEC_APPLICATION, NULL);

  priv->bars = -2;
  on_property_changed(get_batt_data(), plugin);
  battery_status_plugin_update_image_padding(plugin);
  battery_status_plugin_update_value_visibility(plugin);

  dbus_bus_add_match(priv->dbus_conn, DBUS_MATCH_RULE, NULL);
  dbus_connection_add_filter(priv->dbus_conn, battery_status_plugin_dbus_display, plugin, NULL);

  gtk_container_add(GTK_CONTAINER(plugin), event_box);
  gtk_widget_show_all(GTK_WIDGET(plugin));

  return;

no_valignment:
    gtk_widget_destroy(label_box);
no_vbox:
    gtk_widget_destroy(priv->hbox);
no_hbox:
    gtk_widget_destroy(event_box);
no_event_box:
    gtk_widget_destroy(alignment);
no_halignment:
    gtk_widget_destroy(priv->image);
no_image:
    gtk_widget_destroy(priv->value);
no_label:
    gtk_widget_destroy(priv->title);
}

static void
battery_status_plugin_finalize(GObject *object)
{
  BatteryStatusAreaItem *plugin = G_TYPE_CHECK_INSTANCE_CAST(object, battery_status_plugin_get_type(), BatteryStatusAreaItem);
  BatteryStatusAreaItemPrivate *priv = plugin->priv;

  if (priv->pl)
  {
    ca_proplist_destroy(priv->pl);
    priv->pl = NULL;
  }

  if (priv->context)
  {
    ca_context_destroy(priv->context);
    priv->context = NULL;
  }

  if (priv->gconf)
  {
    if (priv->gconf_notify)
    {
      gconf_client_notify_remove(priv->gconf, priv->gconf_notify);
      priv->gconf_notify = 0;
    }
    gconf_client_remove_dir(priv->gconf, GCONF_PATH, NULL);
    gconf_client_clear_cache(priv->gconf);
    g_object_unref(priv->gconf);
    priv->gconf = NULL;
  }

  free_batt();

  if (priv->dbus_conn)
  {
    dbus_connection_unref(priv->dbus_conn);
    priv->dbus_conn = NULL;
  }

  if (priv->timer_id > 0)
  {
    g_source_remove(priv->timer_id);
    priv->timer_id = 0;
  }

  dbus_bus_remove_match(priv->dbus_conn, DBUS_MATCH_RULE, NULL);
  dbus_connection_remove_filter(priv->dbus_conn, battery_status_plugin_dbus_display, plugin);

  hd_status_plugin_item_set_status_area_icon(HD_STATUS_PLUGIN_ITEM(plugin), NULL);

  G_OBJECT_CLASS(battery_status_plugin_parent_class)->finalize(object);
}

static void
battery_status_plugin_class_init(BatteryStatusAreaItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->finalize = (GObjectFinalizeFunc)battery_status_plugin_finalize;
}

static void
battery_status_plugin_class_finalize(BatteryStatusAreaItemClass *klass)
{
}
