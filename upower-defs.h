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

#ifndef _UPOWER_DEFS_H_
#define _UPOWER_DEFS_H_

#define UPOWER_BUS_NAME "org.freedesktop.UPower"
#define UPOWER_PATH_NAME "/org/freedesktop/UPower"
#define UPOWER_INTERFACE_NAME "org.freedesktop.UPower"
#define UPOWER_DEVICE_INTERFACE_NAME "org.freedesktop.UPower.Device"
#define DBUS_PROPERTIES_INTERFACE_NAME "org.freedesktop.DBus.Properties"

/* Enums taken from mce modules/battery-upower.c ; but also in 
 * /usr/share/dbus-1/interfaces/org.freedesktop.UPower.Device.xml */

/** Values for upower device State property */
enum
{
    UPOWER_STATE_UNKNOWN           = 0,
    UPOWER_STATE_CHARGING          = 1,
    UPOWER_STATE_DISCHARGING       = 2,
    UPOWER_STATE_EMPTY             = 3,
    UPOWER_STATE_FULLY_CHARGED     = 4,
    UPOWER_STATE_PENDING_CHARGE    = 5,
    UPOWER_STATE_PENDING_DISCHARGE = 6,
};

/** Values for upower device Type property */
enum
{
    UPOWER_TYPE_UNKNOWN    = 0,
    UPOWER_TYPE_LINE_POWER = 1,
    UPOWER_TYPE_BATTERY    = 2,
    UPOWER_TYPE_UPS        = 3,
    UPOWER_TYPE_MONITOR    = 4,
    UPOWER_TYPE_MOUSE      = 5,
    UPOWER_TYPE_KEYBOARD   = 6,
    UPOWER_TYPE_PDA        = 7,
    UPOWER_TYPE_PHONE      = 8,
};

/** Values for upower device Technology property */
enum
{
    UPOWER_TECHNOLOGY_UNKNOWN                = 0,
    UPOWER_TECHNOLOGY_LITHIUM_ION            = 1,
    UPOWER_TECHNOLOGY_LITHIUM_POLYMER        = 2,
    UPOWER_TECHNOLOGY_LITHIUM_IRON_PHOSPHATE = 3,
    UPOWER_TECHNOLOGY_LEAD_ACID              = 4,
    UPOWER_TECHNOLOGY_NICKEL_CADMIUM         = 5,
    UPOWER_TECHNOLOGY_NICKEL_METAL_HYDRIDE   = 6,
};

#endif /* UPOWER_DEFS_H_ */
