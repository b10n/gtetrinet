
/*
 *  GTetrinet
 *  Copyright (C) 1999, 2000, 2001, 2002, 2003  Ka-shu Wong (kswong@zip.com.au)
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libintl.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <time.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <gobject/gtype.h>
#include <signal.h>

#include "gtetrinet.h"
#include "gtet_config.h"
#include "client.h"
#include "tetrinet.h"
#include "tetris.h"
#include "fields.h"
#include "partyline.h"
#include "winlist.h"
#include "misc.h"
#include "commands.h"
#include "sound.h"
#include "string.h"

#include "images/fields.xpm"
#include "images/partyline.xpm"
#include "images/winlist.xpm"

static GtkWidget *pixmapdata_label (char **d, char *str);
static int gtetrinet_key (int keyval, int mod);

static void setup_key_controller (GtkWidget *widget);

static GtkWidget *pfields, *pparty, *pwinlist;
static GtkWidget *winlistwidget, *partywidget, *fieldswidget;
static GtkWidget *notebook;

GtkWidget *app;

/* event controller for main window keyboard handling */
static GtkEventController *key_controller;

char *option_connect = 0, *option_nick = 0, *option_team = 0, *option_pass = 0;
int option_spec = 0;

int gamemode = ORIGINAL;

int fields_width, fields_height;

gulong keypress_signal;

GSettings* settings;
GSettings* settings_keys;
GSettings* settings_themes;

/* application main loop */
static GMainLoop *main_loop = NULL;

static int gtetrinet_poll_func(GPollFD *passed_fds,
                               guint nfds,
                               int timeout)
{ /* passing a timeout wastes time, even if data is ready... don't do that */
  int ret = 0;
  struct pollfd *fds = (struct pollfd *)passed_fds;

  ret = poll(fds, nfds, 0);
  if (!ret && timeout)
    ret = poll(fds, nfds, timeout);

  return (ret);
}

/*
 * based on https://developer.gnome.org/gio/stable/gio-GSettingsSchema-GSettingsSchemaSource.html
 * I have no idea why this is not the default behavior
 */
GSettings *get_schema_settings(const gchar *schema_id)
{
  GSettingsSchema *schema = NULL;
  GSettingsSchemaSource *schema_source;
  GError *error = NULL;

  /* Try the compile-time schema directory */
  schema_source = g_settings_schema_source_new_from_directory(GSETTINGSSCHEMADIR, g_settings_schema_source_get_default(), FALSE, &error);
  if (error != NULL)
    g_clear_error(&error);
  if (schema_source != NULL)
    {
      schema = g_settings_schema_source_lookup(schema_source, schema_id, FALSE);
      g_settings_schema_source_unref(schema_source);
    }

  /* Try GSETTINGS_SCHEMA_DIR environment variable (useful during development) */
  if (schema == NULL)
    {
      const gchar *schema_dir = g_getenv("GSETTINGS_SCHEMA_DIR");
      if (schema_dir != NULL)
        {
          schema_source = g_settings_schema_source_new_from_directory(schema_dir, g_settings_schema_source_get_default(), FALSE, &error);
          if (error != NULL)
            g_clear_error(&error);
          if (schema_source != NULL)
            {
              schema = g_settings_schema_source_lookup(schema_source, schema_id, FALSE);
              g_settings_schema_source_unref(schema_source);
            }
        }
    }

  if (schema == NULL)
    return g_settings_new(schema_id);
  return g_settings_new_full(schema, NULL, NULL);
}

int main (int argc, char *argv[])
{
    GtkWidget *label;

    bindtextdomain(PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(PACKAGE, "UTF-8");
    textdomain(PACKAGE);

    srand (time(NULL));

    gtk_init ();

    textbox_setup (); /* needs to be done before text boxes are created */

    // First, try to get settings from compiled schema directory (as we can properly check these), then try generic system directories chosen by gsettings library
    settings = get_schema_settings (GSETTINGS_DOMAIN);
    settings_keys = get_schema_settings (GSETTINGS_DOMAIN_KEYS);
    settings_themes = get_schema_settings (GSETTINGS_DOMAIN_THEMES);

    g_signal_connect_swapped (settings, "changed", G_CALLBACK(config_loadconfig), NULL);
    g_signal_connect_swapped (settings_keys, "changed", G_CALLBACK(config_loadconfig_keys), NULL);
    g_signal_connect_swapped (settings_themes, "changed", G_CALLBACK(config_loadconfig_themes), NULL);

    /* load settings */
    config_loadconfig ();
    config_loadconfig_keys ();

    /* first set up the display */

    /* create the main window */
    app = gtk_window_new ();
    gtk_window_set_title (GTK_WINDOW (app), APPNAME);

    g_signal_connect (G_OBJECT(app), "destroy",
                        G_CALLBACK(destroymain), NULL);

    /* key events via event controller */
    setup_key_controller (app);

    gtk_window_set_resizable (GTK_WINDOW (app), TRUE);

    /* create the notebook */
    notebook = gtk_notebook_new ();
    gtk_notebook_set_tab_pos (GTK_NOTEBOOK(notebook), GTK_POS_TOP);

    /* put it in the main window */
    gtk_window_set_child (GTK_WINDOW(app), notebook);

    /* make menus + toolbar */
    make_menus (GTK_WINDOW(app));

    /* create the pages in the notebook */
    fieldswidget = fields_page_new ();
    gtk_widget_set_sensitive (fieldswidget, TRUE);
    gtk_widget_show (fieldswidget);
    pfields = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append (GTK_BOX(pfields), fieldswidget);
    gtk_widget_show (pfields);
    g_object_set_data (G_OBJECT(fieldswidget), "title", "Playing Fields"); // FIXME
    label = pixmapdata_label (fields_xpm, "Playing Fields");
    gtk_widget_show (label);
    gtk_notebook_append_page (GTK_NOTEBOOK(notebook), pfields, label);

    partywidget = partyline_page_new ();
    gtk_widget_show (partywidget);
    pparty = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append (GTK_BOX(pparty), partywidget);
    gtk_widget_show (pparty);
    g_object_set_data (G_OBJECT(partywidget), "title", "Partyline"); // FIXME
    label = pixmapdata_label (partyline_xpm, "Partyline");
    gtk_widget_show (label);
    gtk_notebook_append_page (GTK_NOTEBOOK(notebook), pparty, label);

    winlistwidget = winlist_page_new ();
    gtk_widget_show (winlistwidget);
    pwinlist = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append (GTK_BOX(pwinlist), winlistwidget);
    gtk_widget_show (pwinlist);
    g_object_set_data (G_OBJECT(winlistwidget), "title", "Winlist"); // FIXME
    label = pixmapdata_label (winlist_xpm, "Winlist");
    gtk_widget_show (label);
    gtk_notebook_append_page (GTK_NOTEBOOK(notebook), pwinlist, label);

    /* add signal to focus the text entry when switching to the partyline page*/
    g_signal_connect_after(G_OBJECT (notebook), "switch-page",
		           G_CALLBACK (switch_focus),
		           NULL);

    gtk_widget_show (notebook);
    g_object_set (G_OBJECT (notebook), "can-focus", FALSE, NULL);

    partyline_show_channel_list (list_enabled);
    gtk_window_present (GTK_WINDOW(app));

    /* initialise some stuff */
    config_loadconfig_themes ();
    commands_checkstate ();

    /* check command line params */
    if (option_nick) GTET_O_STRCPY(nick, option_nick);
    if (option_team) GTET_O_STRCPY(team, option_team);
    if (option_pass) GTET_O_STRCPY(specpassword, option_pass);
    if (option_spec) spectating = TRUE;
    if (option_connect) {
        client_init (option_connect, nick);
    }

    /* Don't schedule if data is ready, glib should do this itself,
     * but welcome to anything that works... */
    g_main_context_set_poll_func(NULL, gtetrinet_poll_func);

    main_loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (main_loop);
    g_main_loop_unref (main_loop);
    main_loop = NULL;

    g_object_unref (settings);
    g_object_unref (settings_keys);
    g_object_unref (settings_themes);

    client_disconnect ();
    /* cleanup */
    fields_cleanup ();

    return 0;
}

GtkWidget *pixmapdata_label (char **d, char *str)
{
    GdkPixbuf *pb;
    GdkTexture *texture;
    GtkWidget *box, *widget;

    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

    pb = gdk_pixbuf_new_from_xpm_data ((const char **)d);
    texture = gdk_texture_new_for_pixbuf (pb);
    g_object_unref (pb);
    widget = gtk_image_new_from_paintable (GDK_PAINTABLE(texture));
    g_object_unref (texture);
    gtk_widget_show (widget);
    gtk_box_append (GTK_BOX(box), widget);

    widget = gtk_label_new (str);
    gtk_widget_show (widget);
    gtk_box_append (GTK_BOX(box), widget);

    return box;
}

/* called when the main window is destroyed */
void destroymain (void)
{
    if (main_loop && g_main_loop_is_running (main_loop))
        g_main_loop_quit (main_loop);
}

/*
 The key press/release handlers requires a little hack:
 There is no indication whether each keypress/release is a real press
 or a real release, or whether it is just typematic action.
 However, if it is a result of typematic action, the keyrelease and the
 following keypress event have the same value in the time field.
 The solution is: when a keyrelease event is received, the event is stored
 and a timeout handler is installed.  if a subsequent keypress event is
 received with the same value in the time field, the keyrelease event is
 discarded.  The keyrelease event is sent if the timeout is reached without
 being cancelled.
 */

static struct {
    guint keyval;
    guint32 time;
} k;
gint keytimeoutid = 0;

gint keytimeout (gpointer data)
{
    tetrinet_upkey (k.keyval);
    keytimeoutid = 0;
    return FALSE;
}

gboolean keypress (GtkEventControllerKey *controller,
                   guint keyval, guint keycode, GdkModifierType state,
                   gpointer user_data)
{
    GtkWidget *widget = GTK_WIDGET(user_data);
    int game_area;
    GdkEvent *event;
    guint32 event_time;

    if (widget == app)
    {
      int cur_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
      int pfields_page = gtk_notebook_page_num(GTK_NOTEBOOK(notebook),
                                               pfields);
      /* Main window - check the notebook */
      game_area = (cur_page == pfields_page);
    }
    else
    {
        /* Sub-window - find out which */
        char *title = NULL;

        title = g_object_get_data(G_OBJECT(widget), "title");
        game_area =  title && !strcmp( title, "Playing Fields");
    }

    if (game_area)
    { /* keys for the playing field - key releases needed - install timeout */
      event = gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER(controller));
      event_time = event ? gdk_event_get_time (event) : 0;
      if (keytimeoutid && event_time && event_time == k.time)
        g_source_remove (keytimeoutid);
    }

    /* Check if it's a GTetrinet key */
    if (gtetrinet_key (keyval, state & GDK_ALT_MASK))
    {
      return TRUE;
    }

    if (game_area && ingame && (gdk_keyval_to_lower (keyval) == keys[K_GAMEMSG]))
    {
      g_signal_handler_block (key_controller, keypress_signal);
      fields_gmsginputactivate (TRUE);
      return TRUE;
    }

    if (game_area && tetrinet_key (keyval))
    {
      return TRUE;
    }

    return FALSE;
}

void keyrelease (GtkEventControllerKey *controller,
                 guint keyval, guint keycode, GdkModifierType state,
                 gpointer user_data)
{
    GtkWidget *widget = GTK_WIDGET(user_data);
    int game_area;
    GdkEvent *event;

    if (widget == app)
    {
      int cur_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
      int pfields_page = gtk_notebook_page_num(GTK_NOTEBOOK(notebook),
                                               pfields);
      /* Main window - check the notebook */
      game_area = (cur_page == pfields_page);
    }
    else
    {
        /* Sub-window - find out which */
        char *title = NULL;

        title = g_object_get_data(G_OBJECT(widget), "title");
        game_area =  title && !strcmp( title, "Playing Fields");
    }

    if (game_area)
    {
        event = gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER(controller));
        k.keyval = keyval;
        k.time = event ? gdk_event_get_time (event) : 0;
        keytimeoutid = g_timeout_add (10, keytimeout, 0);
    }
}

/*
 TODO: make this switch between detached pages too
 */
static int gtetrinet_key (int keyval, int mod)
{
  if (mod != GDK_ALT_MASK)
    return FALSE;

  switch (keyval)
  {
  case GDK_KEY_1: gtk_notebook_set_current_page (GTK_NOTEBOOK(notebook), 0); break;
  case GDK_KEY_2: gtk_notebook_set_current_page (GTK_NOTEBOOK(notebook), 1); break;
  case GDK_KEY_3: gtk_notebook_set_current_page (GTK_NOTEBOOK(notebook), 2); break;
  default:
    return FALSE;
  }
  return TRUE;
}

/* attach a GtkEventControllerKey to a window for game key handling */
static void setup_key_controller (GtkWidget *widget)
{
    GtkEventController *ctrl = gtk_event_controller_key_new ();
    /* Use capture phase so game keys are consumed before reaching any
       focused button widget (e.g. space would otherwise activate Disconnect). */
    gtk_event_controller_set_propagation_phase (ctrl, GTK_PHASE_CAPTURE);
    gulong press_id = g_signal_connect (ctrl, "key-pressed",
                                        G_CALLBACK (keypress), widget);
    g_signal_connect (ctrl, "key-released",
                      G_CALLBACK (keyrelease), widget);
    gtk_widget_add_controller (widget, ctrl);
    if (widget == app) {
        key_controller = ctrl;
        keypress_signal = press_id;
    }
}

/* funky page detach stuff */

/* Type to hold primary widget and its label in the notebook page */
typedef struct {
    GtkWidget *parent;
    GtkWidget *widget;
    int pageNo;
} WidgetPageData;

void destroy_page_window (GtkWidget *window, gpointer data)
{
    WidgetPageData *pageData = (WidgetPageData *)data;
    GtkWidget *win_parent;

    /* Put widget back into a page */
    win_parent = gtk_widget_get_parent (pageData->widget);
    g_object_ref (pageData->widget);
    gtk_window_set_child (GTK_WINDOW(win_parent), NULL);
    gtk_box_append (GTK_BOX(pageData->parent), pageData->widget);
    g_object_unref (pageData->widget);

    /* Select it */
    gtk_notebook_set_current_page (GTK_NOTEBOOK(notebook), pageData->pageNo);

    /* Free return data */
    g_free (data);
}

void move_current_page_to_window (void)
{
    WidgetPageData *pageData;
    GtkWidget *page, *child, *newWindow;
    gint pageNo;
    char *title;

    /* Extract current page's widget & it's parent from the notebook */
    pageNo = gtk_notebook_get_current_page (GTK_NOTEBOOK(notebook));
    page   = gtk_notebook_get_nth_page (GTK_NOTEBOOK(notebook), pageNo);
    child  = gtk_widget_get_first_child (page);
    if (!child)
    {
        /* Must already be a window */
        return;
    }

    /* Create new window for widget, plus container, etc. */
    newWindow = gtk_window_new ();
    title = g_object_get_data (G_OBJECT(child), "title");
    if (!title)
        title = "GTetrinet";
    gtk_window_set_title (GTK_WINDOW (newWindow), title);
    gtk_window_set_resizable (GTK_WINDOW(newWindow), TRUE);

    /* Attach key events to window */
    setup_key_controller (newWindow);

    /* Store context to restore widget to notebook page on close */
    pageData = g_new( WidgetPageData, 1 );
    pageData->parent = page;
    pageData->widget = child;
    pageData->pageNo = pageNo;

    /* Move main widget to window */
    g_object_ref (child);
    gtk_box_remove (GTK_BOX (gtk_widget_get_parent (child)), child);
    gtk_window_set_child (GTK_WINDOW(newWindow), child);
    g_object_unref (child);

    /* Pass ID of parent (to put widget back) to window's destroy */
    g_signal_connect (G_OBJECT(newWindow), "destroy",
                        G_CALLBACK(destroy_page_window),
                        (gpointer)(pageData));

    gtk_window_present (GTK_WINDOW(newWindow));

    /* cure annoying side effect */
    if (gmsgstate)
        fields_gmsginput(TRUE);
    else
        fields_gmsginput(FALSE);

}

/* show the fields notebook tab */
void show_fields_page (void)
{
    gtk_notebook_set_current_page (GTK_NOTEBOOK(notebook), 0);
}

/* show the partyline notebook tab */
void show_partyline_page (void)
{
    gtk_notebook_set_current_page (GTK_NOTEBOOK(notebook), 1);
}

void unblock_keyboard_signal (void)
{
    g_signal_handler_unblock (key_controller, keypress_signal);
}

void switch_focus (GtkNotebook *notebook,
                   void *page,
                   guint page_num)
{
    if (connected)
      switch (page_num)
      {
        case 0:
          if (gmsgstate) fields_gmsginputactivate (1);
          else partyline_entryfocus ();
          break;
        case 1: partyline_entryfocus (); break;
        case 2: winlist_focus (); break;
      }
}
