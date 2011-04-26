 /***********************************************************************************
 *  status-area-applet-battery: Open source rewrite of the Maemo 5 battery applet
 *  Copyright (C) 2011 Mohammad Abu-Garbeyyeh
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
#include <gtk/gtk.h>
#include <hildon/hildon.h>
#include <libintl.h>
#include <dbus/dbus.h>
#include <hal/libhal.h>
#include <hildon/hildon-notification.h>
#include <profiled/libprofile.h>

#include "status-area-applet-battery.h"

#define BME_HAL_UDI "/org/freedesktop/Hal/devices/bme"
#define BME_HAL_PERCENTAGE_KEY "battery.charge_level.percentage"
#define BME_HAL_CURRENT_KEY "battery.reporting.current"
#define BME_HAL_BARS_KEY "battery.charge_level.current"
#define BME_HAL_IS_CHARGING_KEY "battery.rechargeable.is_charging"

#define BATTERY_STATUS_PLUGIN_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE (obj, \
                            TYPE_BATTERY_STATUS_PLUGIN, BatteryStatusAreaItemPrivate))

struct _BatteryStatusAreaItemPrivate
{
    GtkWidget *button;
    DBusConnection *sysbus_conn;
    LibHalContext *ctx;
    int percentage;
    int current;
    int number_of_bars;
    gboolean is_charging;
    gpointer data;
};

HD_DEFINE_PLUGIN_MODULE (BatteryStatusAreaItem, battery_status_plugin, HD_TYPE_STATUS_MENU_ITEM);

static void
battery_status_plugin_class_finalize (BatteryStatusAreaItemClass *klass) {}

static void
battery_status_plugin_finalize (GObject *object);

static void
battery_status_plugin_class_init (BatteryStatusAreaItemClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = (GObjectFinalizeFunc) battery_status_plugin_finalize;

    g_type_class_add_private (klass, sizeof (BatteryStatusAreaItemPrivate));
}

static void
battery_status_plugin_update_ui (BatteryStatusAreaItem *plugin)
{
    BatteryStatusAreaItemPrivate *priv = BATTERY_STATUS_PLUGIN_GET_PRIVATE (plugin);
    char *battery_charge_image_name;
    gchar percentage_text[64];

    if (priv->is_charging)
    {
        hildon_button_set_value (HILDON_BUTTON (plugin->priv->button), dgettext ("osso-dsm-ui", "incf_me_battery_charging"));
        return;
    }

    switch (priv->number_of_bars)
    {
        case 0:
            battery_charge_image_name = "statusarea_battery_low";
            break;
        case 1:
            battery_charge_image_name = "statusarea_battery_full13";
            break;
        case 2:
            battery_charge_image_name = "statusarea_battery_full25";
            break;
        case 3:
            battery_charge_image_name = "statusarea_battery_full38";
            break;
        case 4:
            battery_charge_image_name = "statusarea_battery_full50";
            break;
        case 5:
            battery_charge_image_name = "statusarea_battery_full63";
            break;
        case 6:
            battery_charge_image_name = "statusarea_battery_full75";
            break;
        case 7:
            battery_charge_image_name = "statusarea_battery_full88";
            break;
        case 8:
            battery_charge_image_name = "statusarea_battery_full100";
            break;
    }

    GtkIconTheme *icon_theme = gtk_icon_theme_get_default ();
    GdkPixbuf *pixbuf = gtk_icon_theme_load_icon (icon_theme, battery_charge_image_name,
                    18, GTK_ICON_LOOKUP_NO_SVG, NULL);
    hd_status_plugin_item_set_status_area_icon (HD_STATUS_PLUGIN_ITEM (plugin), pixbuf);
    g_object_unref (pixbuf);

    pixbuf = gtk_icon_theme_load_icon (icon_theme, battery_charge_image_name,
                                                  48, GTK_ICON_LOOKUP_NO_SVG, NULL);
    hildon_button_set_image (HILDON_BUTTON (plugin->priv->button),
                             gtk_image_new_from_pixbuf (pixbuf));
    g_object_unref (pixbuf);

    g_snprintf (percentage_text, sizeof (percentage_text), "%d%%, %d mAh", priv->percentage, priv->current);
    hildon_button_set_value (HILDON_BUTTON (plugin->priv->button), percentage_text);
}

static void
battery_status_plugin_get_values (BatteryStatusAreaItem *plugin)
{
    BatteryStatusAreaItemPrivate *priv = BATTERY_STATUS_PLUGIN_GET_PRIVATE (plugin);
    DBusError error;
    dbus_error_init (&error);
    gboolean is_charging;
    int number_of_bars;

    is_charging = libhal_device_get_property_bool (priv->ctx, BME_HAL_UDI, BME_HAL_IS_CHARGING_KEY, &error);
    if (priv->is_charging != is_charging)
    {
        priv->is_charging = is_charging;
        if (is_charging)
            hildon_banner_show_information (GTK_WIDGET (plugin), NULL, dgettext ("osso-dsm-ui", "incf_ib_battery_charging"));
        else
            hildon_banner_show_information (GTK_WIDGET (plugin), NULL, dgettext ("osso-dsm-ui", "incf_ib_disconnect_charger"));
    }

    number_of_bars = libhal_device_get_property_int (priv->ctx, BME_HAL_UDI, BME_HAL_BARS_KEY, &error);
    if (priv->number_of_bars != number_of_bars)
    {
        priv->number_of_bars = number_of_bars;
        if (priv->number_of_bars == 0)
            hildon_banner_show_information_override_dnd (GTK_WIDGET (plugin), dgettext ("osso-dsm-ui", "incf_ib_battery_low")); 
    }
        
    priv->percentage = libhal_device_get_property_int (priv->ctx, BME_HAL_UDI, BME_HAL_PERCENTAGE_KEY, &error);
    priv->current = libhal_device_get_property_int (priv->ctx, BME_HAL_UDI, BME_HAL_CURRENT_KEY, &error);

    dbus_error_free (&error);
    battery_status_plugin_update_ui (plugin);
}

static void
battery_status_plugin_hal_property_modified_cb (LibHalContext *ctx,
                                                const char *udi,
                                                const char *key,
                                                dbus_bool_t is_removed,
                                                dbus_bool_t is_added)
{
/*     if (strcmp (udi, BME_HAL_UDI) != 0)
         return;

     if (strcmp (key, BME_HAL_IS_CHARGING_KEY) != 0 || strcmp (key, BME_HAL_BARS_KEY) != 0 || strcmp (key,BME_HAL_PERCENTAGE_KEY) != 0 ||
         strcmp (key, BME_HAL_CURRENT_KEY) != 0)
         return;*/

     battery_status_plugin_get_values (libhal_ctx_get_user_data (ctx));
}

static void
battery_status_plugin_init (BatteryStatusAreaItem *plugin)
{
    plugin->priv = BATTERY_STATUS_PLUGIN_GET_PRIVATE (plugin);
    DBusError error;

    dbus_error_init (&error);

    plugin->priv->sysbus_conn = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
    if (dbus_error_is_set (&error))
    {
        g_warning ("Could not open D-Bus session bus connection.");
        dbus_error_free (&error);
    }

    plugin->priv->ctx = libhal_ctx_new ();
    libhal_ctx_set_dbus_connection (plugin->priv->ctx, plugin->priv->sysbus_conn);
    libhal_ctx_set_user_data (plugin->priv->ctx, plugin);
    libhal_ctx_set_device_property_modified (plugin->priv->ctx, battery_status_plugin_hal_property_modified_cb);
    libhal_device_add_property_watch (plugin->priv->ctx, BME_HAL_UDI, &error);
    hildon_banner_show_information (NULL, NULL, error.message);

    GtkIconTheme *icon_theme = gtk_icon_theme_get_default ();
    GList *list = gtk_icon_theme_list_icons (icon_theme, NULL);

    plugin->priv->button = hildon_button_new (HILDON_SIZE_FINGER_HEIGHT | HILDON_SIZE_AUTO_WIDTH, HILDON_BUTTON_ARRANGEMENT_VERTICAL);
    hildon_button_set_style (HILDON_BUTTON (plugin->priv->button), HILDON_BUTTON_STYLE_NORMAL);
    hildon_button_set_title (HILDON_BUTTON (plugin->priv->button), dgettext ("osso-dsm-ui", "tncpa_li_plugin_sb_battery"));
    hildon_button_set_value (HILDON_BUTTON (plugin->priv->button), "Percentage: 100%");
    gtk_button_set_alignment (GTK_BUTTON (plugin->priv->button), 0, 0);

    GdkPixbuf *pixbuf = gtk_icon_theme_load_icon (icon_theme, "statusarea_battery_full100",
                    18, GTK_ICON_LOOKUP_NO_SVG, NULL);
    hd_status_plugin_item_set_status_area_icon (HD_STATUS_PLUGIN_ITEM (plugin), pixbuf);
    g_object_unref (pixbuf);

    pixbuf = gtk_icon_theme_load_icon (icon_theme, "statusarea_battery_full100",
                                                  48, GTK_ICON_LOOKUP_NO_SVG, NULL);
    hildon_button_set_image (HILDON_BUTTON (plugin->priv->button),
                             gtk_image_new_from_pixbuf (pixbuf));
    g_object_unref (pixbuf);

    battery_status_plugin_get_values (plugin);

    gtk_container_add (GTK_CONTAINER (plugin), plugin->priv->button);

    gtk_widget_show_all (GTK_WIDGET (plugin));

}

static void
battery_status_plugin_finalize (GObject *object)
{
    BatteryStatusAreaItem *plugin = BATTERY_STATUS_PLUGIN (object);
 
    if (plugin->priv->ctx)
    {
        libhal_ctx_shutdown (plugin->priv->ctx, NULL);
        libhal_ctx_free (plugin->priv->ctx);
        plugin->priv->ctx = NULL;
    }

    if (plugin->priv->sysbus_conn)
    {
        dbus_connection_unref (plugin->priv->sysbus_conn);
        plugin->priv->sysbus_conn = NULL;
    }

    G_OBJECT_CLASS (battery_status_plugin_parent_class)->finalize (object);
}
