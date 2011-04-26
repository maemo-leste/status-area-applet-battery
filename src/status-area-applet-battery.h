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


#ifndef __BATTERY_STATUS_PLUGIN_H__
#define __BATTERY_STATUS_PLUGIN_H__

#include <libhildondesktop/libhildondesktop.h>

G_BEGIN_DECLS

#define TYPE_BATTERY_STATUS_PLUGIN            (battery_status_plugin_get_type ())

#define BATTERY_STATUS_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                                    TYPE_BATTERY_STATUS_PLUGIN, BatteryStatusAreaItem))

#define BATTERY_STATUS_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), \
                                TYPE_BATTERY_STATUS_PLUGIN, BatteryStatusAreaItemClass))

#define IS_BATTERY_STATUS_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                                                    TYPE_BATTERY_STATUS_PLUGIN))

#define IS_BATTERY_STATUS_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), \
                                                    TYPE_BATTERY_STATUS_PLUGIN))

#define BATTERY_STATUS_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                            TYPE_BATTERY_STATUS_PLUGIN, BatteryStatusAreaItemClass))

typedef struct _BatteryStatusAreaItem        BatteryStatusAreaItem;
typedef struct _BatteryStatusAreaItemClass   BatteryStatusAreaItemClass;
typedef struct _BatteryStatusAreaItemPrivate BatteryStatusAreaItemPrivate;

struct _BatteryStatusAreaItem
{
    HDStatusMenuItem parent;

    BatteryStatusAreaItemPrivate *priv;
};

struct _BatteryStatusAreaItemClass
{
    HDStatusMenuItemClass parent_class;
};

GType BATTERY_STATUS_plugin_get_type (void);

G_END_DECLS

#endif

