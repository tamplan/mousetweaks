/*
 * Copyright © 2007-2008 Gerd Kohlberger <lowfi@chello.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <panel-applet.h>
#include <gconf/gconf-client.h>
#include <dbus/dbus-glib.h>
#include <libgnomeui/gnome-help.h>

#include "mt-common.h"

typedef struct _DwellData DwellData;
struct _DwellData {
    GConfClient *client;
    DBusGProxy  *proxy;
    GladeXML    *xml;
    GtkWidget   *box;
    GtkWidget   *button;
    GdkPixbuf   *click[4];

    gint button_width;
    gint button_height;
    gint cct;
    gint active;
};

static const gchar *img_widgets[] = {
    "right_click_img",
    "drag_click_img",
    "double_click_img",
    "single_click_img"
};

static const gchar *img_widgets_v[] = {
    "right_click_img_v",
    "drag_click_img_v",
    "double_click_img_v",
    "single_click_img_v"
};

static void update_sensitivity (DwellData *dd);

static void preferences_dialog (BonoboUIComponent *component,
				gpointer data,
				const char *cname);
static void help_dialog        (BonoboUIComponent *component,
				gpointer data,
				const char *cname);
static void about_dialog       (BonoboUIComponent *component,
				gpointer data,
				const char *cname);

static const BonoboUIVerb menu_verb[] = {
    BONOBO_UI_UNSAFE_VERB ("PropertiesVerb", preferences_dialog),
    BONOBO_UI_UNSAFE_VERB ("HelpVerb", help_dialog),
    BONOBO_UI_UNSAFE_VERB ("AboutVerb", about_dialog),
    BONOBO_UI_VERB_END
};

static gboolean
do_not_eat (GtkWidget *widget, GdkEventButton *bev, gpointer user)
{
    if (bev->button != 1)
	g_signal_stop_emission_by_name (widget, "button_press_event");

    return FALSE;
}

static void
button_cb (GtkToggleButton *button, gpointer data)
{
    DwellData *dd = (DwellData *) data;

    if (gtk_toggle_button_get_active (button)) {
	GSList *group;

	group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button));
	dd->cct = g_slist_index (group, button);
	dbus_g_proxy_call_no_reply (dd->proxy, "SetClicktype",
				    G_TYPE_UINT, dd->cct,
				    G_TYPE_INVALID);
    }
}

static void
box_size_allocate (GtkWidget *widget, GtkAllocation *alloc, gpointer data)
{
    DwellData *dd = (DwellData *) data;
    GtkWidget *w;
    GdkPixbuf *tmp;
    const gchar *name;
    gint i;

    if (dd->button_width == alloc->width && dd->button_height == alloc->height)
	return;

    name = glade_get_widget_name (dd->box);

    if (g_str_equal (name, "box_vert"))
	/* vertical */
	for (i = 0; i < N_CLICK_TYPES; i++) {
	    w = glade_xml_get_widget (dd->xml, img_widgets_v[i]);

	    if (alloc->width < 32) {
		tmp = gdk_pixbuf_scale_simple (dd->click[i],
					       alloc->width - 7,
					       alloc->width - 7,
					       GDK_INTERP_HYPER);
		if (tmp) {
		    gtk_image_set_from_pixbuf (GTK_IMAGE(w), tmp);
		    g_object_unref (tmp);
		}
	    }
	    else
		gtk_image_set_from_pixbuf (GTK_IMAGE(w), dd->click[i]);
	}
    else
	/* horizontal */
	for (i = 0; i < N_CLICK_TYPES; i++) {
	    w = glade_xml_get_widget (dd->xml, img_widgets[i]);

	    if (alloc->height < 32) {
		tmp = gdk_pixbuf_scale_simple (dd->click[i],
					       alloc->height - 7,
					       alloc->height - 7,
					       GDK_INTERP_HYPER);
		if (tmp) {
		    gtk_image_set_from_pixbuf (GTK_IMAGE(w), tmp);
		    g_object_unref (tmp);
		}
	    }
	    else
		gtk_image_set_from_pixbuf (GTK_IMAGE(w), dd->click[i]);
	}

    dd->button_width = alloc->width;
    dd->button_height = alloc->height;
}

/* applet callbacks */
static void
applet_orient_changed (PanelApplet *applet, guint orient, gpointer data)
{
    DwellData *dd = (DwellData *) data;

    gtk_container_remove (GTK_CONTAINER (applet), dd->box);

    switch (orient) {
    case PANEL_APPLET_ORIENT_UP:
    case PANEL_APPLET_ORIENT_DOWN:
	dd->box = glade_xml_get_widget (dd->xml, "box_hori");
	dd->button = glade_xml_get_widget (dd->xml, "single_click");
	break;
    case PANEL_APPLET_ORIENT_LEFT:
    case PANEL_APPLET_ORIENT_RIGHT:
	dd->box = glade_xml_get_widget (dd->xml, "box_vert");
	dd->button = glade_xml_get_widget (dd->xml, "single_click_v");
    default:
	break;
    }

    if (dd->box->parent)
	gtk_widget_reparent (dd->box, GTK_WIDGET (applet));
    else
	gtk_container_add (GTK_CONTAINER (applet), dd->box);
}

static void
applet_unrealized (GtkWidget *widget, gpointer data)
{
    DwellData *dd = (DwellData *) data;
    gint i;

    for (i = 0; i < N_CLICK_TYPES; i++)
	g_object_unref (dd->click[i]);

    g_object_unref (glade_xml_get_widget (dd->xml, "box_vert"));
    g_object_unref (glade_xml_get_widget (dd->xml, "box_hori"));
    g_object_unref (dd->client);
    g_object_unref (dd->proxy);
    g_object_unref (dd->xml);

    g_slice_free (DwellData, dd);
}

static void
preferences_dialog (BonoboUIComponent *component,
		    gpointer           data,
		    const char        *cname)
{
    g_spawn_command_line_async ("gnome-mouse-properties -p accessibility",
				NULL);
}

static void
help_dialog (BonoboUIComponent *component, gpointer data, const char *cname)
{
    GError *error = NULL;

    if (!gnome_help_display_desktop (NULL,
				     "mousetweaks",
				     "mousetweaks",
				     NULL,
				     &error)) {
	mt_show_dialog (_("Couldn't display help"),
			error->message,
			GTK_MESSAGE_ERROR);
	g_error_free (error);
    }
}

static void
about_dialog (BonoboUIComponent *component, gpointer data, const char *cname)
{
    DwellData *dd = (DwellData *) data;
    GtkWidget *about;

    about = glade_xml_get_widget (dd->xml, "about");

    if (GTK_WIDGET_VISIBLE (about))
	gtk_window_present (GTK_WINDOW (about));
    else
	gtk_widget_show (about);
}

static void
about_response (GtkWidget *about, gint response, gpointer data)
{
    DwellData *dd = (DwellData *) data;

    gtk_widget_hide (glade_xml_get_widget (dd->xml, "about"));
}

static inline void
init_button (DwellData *dd, GtkWidget *button)
{
    gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (button), FALSE);
    g_signal_connect (button, "button-press-event",
		      G_CALLBACK (do_not_eat), NULL);
    g_signal_connect (button, "toggled",
		      G_CALLBACK (button_cb), dd);
}

static void
init_horizontal_box (DwellData *dd)
{
    GtkWidget *w;
    gint i;

    w = glade_xml_get_widget (dd->xml, "box_hori");
    g_object_ref (w);

    w = glade_xml_get_widget (dd->xml, "single_click");
    g_signal_connect (w, "size-allocate",
		      G_CALLBACK (box_size_allocate), dd);
    init_button (dd, w);

    w = glade_xml_get_widget (dd->xml, "double_click");
    init_button (dd, w);

    w = glade_xml_get_widget (dd->xml, "drag_click");
    init_button (dd, w);

    w = glade_xml_get_widget (dd->xml, "right_click");
    init_button (dd, w);

    for (i = 0; i < N_CLICK_TYPES; i++) {
	w = glade_xml_get_widget (dd->xml, img_widgets[i]);
	gtk_image_set_from_pixbuf (GTK_IMAGE(w), dd->click[i]);
    }
}

static void
init_vertical_box (DwellData *dd)
{
    GtkWidget *w;
    gint i;

    w = glade_xml_get_widget (dd->xml, "box_vert");
    g_object_ref (w);

    w = glade_xml_get_widget (dd->xml, "single_click_v");
    g_signal_connect (G_OBJECT(w), "size-allocate",
		      G_CALLBACK(box_size_allocate), dd);
    init_button (dd, w);

    w = glade_xml_get_widget (dd->xml, "double_click_v");
    init_button (dd, w);

    w = glade_xml_get_widget (dd->xml, "drag_click_v");
    init_button (dd, w);

    w = glade_xml_get_widget (dd->xml, "right_click_v");
    init_button (dd, w);

    for (i = 0; i < N_CLICK_TYPES; i++) {
	w = glade_xml_get_widget (dd->xml, img_widgets_v[i]);
	gtk_image_set_from_pixbuf (GTK_IMAGE(w), dd->click[i]);
    }
}

static void
update_sensitivity (DwellData *dd)
{
    gint mode;
    gboolean enabled, sensitive;

    enabled = gconf_client_get_bool (dd->client, OPT_DWELL, NULL);
    mode = gconf_client_get_int (dd->client, OPT_MODE, NULL);

    sensitive = dd->active && enabled && mode == DWELL_MODE_CTW;
    gtk_widget_set_sensitive (dd->box, sensitive);
}

static void
clicktype_changed (DBusGProxy *proxy,
		   guint       clicktype,
		   gpointer    data)
{
    DwellData *dd = (DwellData *) data;
    GtkToggleButton *button;
    GSList *group;

    group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (dd->button));
    button = GTK_TOGGLE_BUTTON (g_slist_nth_data (group, clicktype));

    g_signal_handlers_block_by_func (button, button_cb, dd);
    gtk_toggle_button_set_active (button, TRUE);
    g_signal_handlers_unblock_by_func (button, button_cb, dd);
}

static void
status_changed (DBusGProxy *proxy,
		gboolean    status,
		gpointer    data)
{
    DwellData *dd = (DwellData *) data;

    dd->active = status;
    update_sensitivity (dd);
}

static gboolean
setup_dbus_proxy (DwellData *dd)
{
    DBusGConnection *bus;
    GError *error = NULL;

    bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
    if (error != NULL) {
	g_print ("Unable to connect to session bus: %s\n", error->message);
	g_error_free (error);
	return FALSE;
    }

    dd->proxy = dbus_g_proxy_new_for_name (bus,
					   "org.gnome.Mousetweaks",
					   "/org/gnome/Mousetweaks",
					   "org.gnome.Mousetweaks");

    dbus_g_proxy_add_signal (dd->proxy, "ClicktypeChanged",
			     G_TYPE_UINT, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (dd->proxy, "ClicktypeChanged",
				 G_CALLBACK (clicktype_changed), dd, NULL);

    dbus_g_proxy_add_signal (dd->proxy, "StatusChanged",
			     G_TYPE_BOOLEAN, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (dd->proxy, "StatusChanged",
				 G_CALLBACK (status_changed), dd, NULL);

    return TRUE;
}

static void
gconf_value_changed (GConfClient *client,
		     const gchar *key,
		     GConfValue *value,
		     gpointer data)
{
    if (g_str_equal (key, OPT_MODE) || g_str_equal (key, OPT_DWELL))
	update_sensitivity ((DwellData *) data);
}

static gboolean
fill_applet (PanelApplet *applet)
{
    DwellData *dd;
    GtkWidget *about;
    PanelAppletOrient orient;

    dd = g_slice_new0 (DwellData);
    if (!dd)
	return FALSE;

    dd->cct = DWELL_CLICK_TYPE_SINGLE;

    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    g_set_application_name (_("Dwell Click Applet"));
    gtk_window_set_default_icon_name (MT_ICON_NAME);

    dd->xml = glade_xml_new (DATADIR "/dwell-click-applet.glade", NULL, NULL);
    if (!dd->xml) {
	g_slice_free (DwellData, dd);
	return FALSE;
    }

    if (!setup_dbus_proxy (dd)) {
	g_object_unref (dd->xml);
	g_slice_free (DwellData, dd);
	return FALSE;
    }

    about = glade_xml_get_widget (dd->xml, "about");
    g_object_set (about, "version", VERSION, NULL);
    g_signal_connect (about, "delete-event",
		      G_CALLBACK (gtk_widget_hide_on_delete), NULL);
    g_signal_connect (about, "response",
		      G_CALLBACK (about_response), dd);

    dd->client = gconf_client_get_default ();
    gconf_client_add_dir (dd->client, MT_GCONF_HOME,
			  GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
    g_signal_connect (dd->client, "value_changed",
		      G_CALLBACK (gconf_value_changed), dd);

    dd->click[DWELL_CLICK_TYPE_SINGLE] =
	gdk_pixbuf_new_from_file (DATADIR "/single-click.png", NULL);
    dd->click[DWELL_CLICK_TYPE_DOUBLE] =
	gdk_pixbuf_new_from_file (DATADIR "/double-click.png", NULL);
    dd->click[DWELL_CLICK_TYPE_DRAG] =
	gdk_pixbuf_new_from_file (DATADIR "/drag-click.png", NULL);
    dd->click[DWELL_CLICK_TYPE_RIGHT] =
	gdk_pixbuf_new_from_file (DATADIR "/right-click.png", NULL);

    panel_applet_set_flags (applet,
			    PANEL_APPLET_EXPAND_MINOR |
			    PANEL_APPLET_HAS_HANDLE);
    panel_applet_set_background_widget (applet, GTK_WIDGET(applet));
    panel_applet_setup_menu_from_file (applet,
				       DATADIR, "DwellClick.xml",
				       NULL, menu_verb, dd);

    g_signal_connect (applet, "change-orient",
		      G_CALLBACK (applet_orient_changed), dd);
    g_signal_connect (applet, "unrealize",
		      G_CALLBACK (applet_unrealized), dd);

    orient = panel_applet_get_orient (applet);
    if (orient == PANEL_APPLET_ORIENT_UP ||
	orient == PANEL_APPLET_ORIENT_DOWN) {
	dd->box = glade_xml_get_widget (dd->xml, "box_hori");
	dd->button = glade_xml_get_widget (dd->xml, "single_click");
    }
    else {
	dd->box = glade_xml_get_widget (dd->xml, "box_vert");
	dd->button = glade_xml_get_widget (dd->xml, "single_click_v");
    }

    init_horizontal_box (dd);
    init_vertical_box (dd);

    gtk_widget_reparent (dd->box, GTK_WIDGET (applet));
    gtk_widget_show (GTK_WIDGET (applet));

    update_sensitivity (dd);

    return TRUE;
}

static gboolean
applet_factory (PanelApplet *applet, const gchar *iid, gpointer data)
{
    if (!g_str_equal (iid, "OAFIID:DwellClickApplet"))
	return FALSE;

    return fill_applet (applet);
}

PANEL_APPLET_BONOBO_FACTORY ("OAFIID:DwellClickApplet_Factory",
			     PANEL_TYPE_APPLET,
			     "Dwell Click Factory",
			     VERSION,
			     applet_factory,
			     NULL);
