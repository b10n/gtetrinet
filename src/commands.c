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

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "gtetrinet.h"
#include "client.h"
#include "tetrinet.h"
#include "partyline.h"
#include "misc.h"
#include "commands.h"
#include "dialogs.h"

/* GSimpleAction instances for each command */
static GSimpleAction *action_connect;
static GSimpleAction *action_disconnect;
static GSimpleAction *action_team;
static GSimpleAction *action_start;
static GSimpleAction *action_pause;
static GSimpleAction *action_end;
#ifdef ENABLE_DETACH
static GSimpleAction *action_detach;
#endif

/* Toolbar button widget references (for show/hide) */
static GtkWidget *tb_connect;
static GtkWidget *tb_disconnect;
static GtkWidget *tb_start;
static GtkWidget *tb_end;
static GtkWidget *tb_pause;
static GtkWidget *tb_team;

/* ---- helpers ------------------------------------------------------------ */

static GSimpleAction *make_action (const char *name,
                                   GCallback cb,
                                   gpointer  user_data)
{
    GSimpleAction *a = g_simple_action_new (name, NULL);
    if (cb)
        g_signal_connect (a, "activate", cb, user_data);
    return a;
}

static GtkWidget *make_toolbar_button (const char  *icon_name,
                                       const char  *label_text,
                                       GCallback    cb,
                                       gpointer     user_data)
{
    GtkWidget *btn = gtk_button_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *img = gtk_image_new_from_icon_name (icon_name);
    GtkWidget *lbl = gtk_label_new (label_text);

    gtk_box_append (GTK_BOX(box), img);
    gtk_box_append (GTK_BOX(box), lbl);
    gtk_button_set_child (GTK_BUTTON(btn), box);
    gtk_widget_add_css_class (btn, "flat");

    if (cb)
        g_signal_connect_swapped (btn, "clicked", cb, user_data);

    return btn;
}

/* ---- GSimpleAction activate callbacks ----------------------------------- */

static void on_connect    (GSimpleAction *a, GVariant *p, gpointer d) { connect_command (); }
static void on_disconnect (GSimpleAction *a, GVariant *p, gpointer d) { disconnect_command (); }
static void on_team       (GSimpleAction *a, GVariant *p, gpointer d) { team_command (); }
static void on_start      (GSimpleAction *a, GVariant *p, gpointer d) { start_command (); }
static void on_pause      (GSimpleAction *a, GVariant *p, gpointer d) { pause_command (); }
static void on_end        (GSimpleAction *a, GVariant *p, gpointer d) { end_command (); }
#ifdef ENABLE_DETACH
static void on_detach     (GSimpleAction *a, GVariant *p, gpointer d) { detach_command (); }
#endif
static void on_prefs      (GSimpleAction *a, GVariant *p, gpointer d) { preferences_command (); }
static void on_about      (GSimpleAction *a, GVariant *p, gpointer d) { about_command (); }
static void on_quit       (GSimpleAction *a, GVariant *p, gpointer d) { destroymain (); }

/* ---- make_menus --------------------------------------------------------- */

void make_menus (GtkWindow *win)
{
    GSimpleActionGroup *ag;
    GMenu *menubar_model, *game_menu, *settings_menu, *help_menu;
    GtkWidget *menubar;
    GtkWidget *toolbar;
    GtkWidget *vbox;
    GtkWidget *main_widget;
    GtkWidget *sep;

    /* Create action group and populate actions */
    ag = g_simple_action_group_new ();

    action_connect    = make_action ("connect",    G_CALLBACK(on_connect),    NULL);
    action_disconnect = make_action ("disconnect", G_CALLBACK(on_disconnect), NULL);
    action_team       = make_action ("team",       G_CALLBACK(on_team),       NULL);
    action_start      = make_action ("start",      G_CALLBACK(on_start),      NULL);
    action_pause      = make_action ("pause",      G_CALLBACK(on_pause),      NULL);
    action_end        = make_action ("end",        G_CALLBACK(on_end),        NULL);
#ifdef ENABLE_DETACH
    action_detach     = make_action ("detach",     G_CALLBACK(on_detach),     NULL);
#endif
    GSimpleAction *action_prefs = make_action ("prefs",  G_CALLBACK(on_prefs), NULL);
    GSimpleAction *action_about = make_action ("about",  G_CALLBACK(on_about), NULL);
    GSimpleAction *action_quit  = make_action ("quit",   G_CALLBACK(on_quit),  NULL);

    g_action_map_add_action (G_ACTION_MAP(ag), G_ACTION(action_connect));
    g_action_map_add_action (G_ACTION_MAP(ag), G_ACTION(action_disconnect));
    g_action_map_add_action (G_ACTION_MAP(ag), G_ACTION(action_team));
    g_action_map_add_action (G_ACTION_MAP(ag), G_ACTION(action_start));
    g_action_map_add_action (G_ACTION_MAP(ag), G_ACTION(action_pause));
    g_action_map_add_action (G_ACTION_MAP(ag), G_ACTION(action_end));
#ifdef ENABLE_DETACH
    g_action_map_add_action (G_ACTION_MAP(ag), G_ACTION(action_detach));
#endif
    g_action_map_add_action (G_ACTION_MAP(ag), G_ACTION(action_prefs));
    g_action_map_add_action (G_ACTION_MAP(ag), G_ACTION(action_about));
    g_action_map_add_action (G_ACTION_MAP(ag), G_ACTION(action_quit));

    /* Insert as "win" prefix so menu items can use "win.connect" etc. */
    gtk_widget_insert_action_group (GTK_WIDGET(win), "win", G_ACTION_GROUP(ag));
    g_object_unref (ag);

    /* Build GMenuModel for the menu bar */
    {
        GMenu *s1, *s2, *s3, *s4;

        game_menu = g_menu_new ();

        s1 = g_menu_new ();
        g_menu_append (s1, N_("_Connect to server..."), "win.connect");
        g_menu_append (s1, N_("_Disconnect from server"), "win.disconnect");
        g_menu_append_section (game_menu, NULL, G_MENU_MODEL(s1));
        g_object_unref (s1);

        s2 = g_menu_new ();
        g_menu_append (s2, N_("Change _team..."), "win.team");
        g_menu_append_section (game_menu, NULL, G_MENU_MODEL(s2));
        g_object_unref (s2);

        s3 = g_menu_new ();
        g_menu_append (s3, N_("_Start game"), "win.start");
        g_menu_append (s3, N_("_Pause game"), "win.pause");
        g_menu_append (s3, N_("_End game"), "win.end");
#ifdef ENABLE_DETACH
        g_menu_append (s3, N_("Detac_h page..."), "win.detach");
#endif
        g_menu_append_section (game_menu, NULL, G_MENU_MODEL(s3));
        g_object_unref (s3);

        s4 = g_menu_new ();
        g_menu_append (s4, N_("_Quit"), "win.quit");
        g_menu_append_section (game_menu, NULL, G_MENU_MODEL(s4));
        g_object_unref (s4);
    }

    settings_menu = g_menu_new ();
    g_menu_append (settings_menu, N_("_Preferences"), "win.prefs");

    help_menu = g_menu_new ();
    g_menu_append (help_menu, N_("_About"), "win.about");

    menubar_model = g_menu_new ();
    g_menu_append_submenu (menubar_model, N_("_Game"),     G_MENU_MODEL(game_menu));
    g_menu_append_submenu (menubar_model, N_("_Settings"), G_MENU_MODEL(settings_menu));
    g_menu_append_submenu (menubar_model, N_("_Help"),     G_MENU_MODEL(help_menu));

    g_object_unref (game_menu);
    g_object_unref (settings_menu);
    g_object_unref (help_menu);

    /* Create the menu bar widget */
    menubar = gtk_popover_menu_bar_new_from_model (G_MENU_MODEL(menubar_model));
    g_object_unref (menubar_model);

    /* Build toolbar */
    toolbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class (toolbar, "toolbar");

    tb_connect = make_toolbar_button ("network-connect",     N_("Connect"),    G_CALLBACK(connect_command),    NULL);
    tb_disconnect = make_toolbar_button ("network-disconnect", N_("Disconnect"), G_CALLBACK(disconnect_command), NULL);
    sep = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
    tb_start  = make_toolbar_button ("media-playback-start", N_("Start game"), G_CALLBACK(start_command),      NULL);
    tb_end    = make_toolbar_button ("media-playback-stop",  N_("End game"),   G_CALLBACK(end_command),        NULL);
    tb_pause  = make_toolbar_button ("media-playback-pause", N_("Pause game"), G_CALLBACK(pause_command),      NULL);
    GtkWidget *sep2 = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
    tb_team   = make_toolbar_button ("system-users",         N_("Change team"),G_CALLBACK(team_command),       NULL);

    gtk_box_append (GTK_BOX(toolbar), tb_connect);
    gtk_box_append (GTK_BOX(toolbar), tb_disconnect);
    gtk_box_append (GTK_BOX(toolbar), sep);
    gtk_box_append (GTK_BOX(toolbar), tb_start);
    gtk_box_append (GTK_BOX(toolbar), tb_end);
    gtk_box_append (GTK_BOX(toolbar), tb_pause);
    gtk_box_append (GTK_BOX(toolbar), sep2);
    gtk_box_append (GTK_BOX(toolbar), tb_team);
#ifdef ENABLE_DETACH
    GtkWidget *sep3 = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
    GtkWidget *tb_detach = make_toolbar_button ("edit-cut", N_("Detach page"), G_CALLBACK(detach_command), NULL);
    gtk_box_append (GTK_BOX(toolbar), sep3);
    gtk_box_append (GTK_BOX(toolbar), tb_detach);
#endif

    gtk_widget_show (toolbar);

    /* Build the outer vbox: menubar + toolbar + existing main widget */
    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

    gtk_box_append (GTK_BOX(vbox), menubar);
    gtk_box_append (GTK_BOX(vbox), toolbar);

    main_widget = gtk_window_get_child (GTK_WINDOW(win));
    if (main_widget != NULL)
    {
        g_object_ref (main_widget);
        gtk_window_set_child (GTK_WINDOW(win), NULL);
        gtk_box_append (GTK_BOX(vbox), main_widget);
        g_object_unref (main_widget);
        gtk_window_set_child (GTK_WINDOW(win), vbox);
    }

    /* Initial visibility: hide end/disconnect, show connect/start */
    gtk_widget_hide (tb_end);
    gtk_widget_hide (tb_disconnect);
}

/* ---- command callbacks -------------------------------------------------- */

void connect_command (void)
{
    connectdialog_new ();
}

void disconnect_command (void)
{
    client_disconnect ();
}

void team_command (void)
{
    teamdialog_new ();
}

#ifdef ENABLE_DETACH
void detach_command (void)
{
    move_current_page_to_window ();
}
#endif

void start_command (void)
{
  char buf[22];

  g_snprintf (buf, sizeof(buf), "%i %i", 1, playernum);
  client_outmessage (OUT_STARTGAME, buf);
}

void show_connect_button (void)
{
    if (tb_disconnect) gtk_widget_hide (tb_disconnect);
    if (tb_connect)    gtk_widget_show (tb_connect);
}

void show_disconnect_button (void)
{
    if (tb_connect)    gtk_widget_hide (tb_connect);
    if (tb_disconnect) gtk_widget_show (tb_disconnect);
}

void show_stop_button (void)
{
    if (tb_start) gtk_widget_hide (tb_start);
    if (tb_end)   gtk_widget_show (tb_end);
}

void show_start_button (void)
{
    if (tb_end)   gtk_widget_hide (tb_end);
    if (tb_start) gtk_widget_show (tb_start);
}

void end_command (void)
{
  char buf[22];

  g_snprintf (buf, sizeof(buf), "%i %i", 0, playernum);
  client_outmessage (OUT_STARTGAME, buf);
}

void pause_command (void)
{
  char buf[22];

  g_snprintf (buf, sizeof(buf), "%i %i", paused?0:1, playernum);
  client_outmessage (OUT_PAUSE, buf);
}

void preferences_command (void)
{
    prefdialog_new ();
}


/* the following function enable/disable things */

void commands_checkstate ()
{
    if (connected) {
        g_simple_action_set_enabled (action_connect,    FALSE);
        g_simple_action_set_enabled (action_disconnect, TRUE);
        if (tb_connect)    gtk_widget_set_sensitive (tb_connect,    FALSE);
        if (tb_disconnect) gtk_widget_set_sensitive (tb_disconnect, TRUE);
    }
    else {
        g_simple_action_set_enabled (action_connect,    TRUE);
        g_simple_action_set_enabled (action_disconnect, FALSE);
        if (tb_connect)    gtk_widget_set_sensitive (tb_connect,    TRUE);
        if (tb_disconnect) gtk_widget_set_sensitive (tb_disconnect, FALSE);
    }
    if (moderator) {
        if (ingame) {
            g_simple_action_set_enabled (action_start, FALSE);
            g_simple_action_set_enabled (action_pause, TRUE);
            g_simple_action_set_enabled (action_end,   TRUE);
            if (tb_start) gtk_widget_set_sensitive (tb_start, FALSE);
            if (tb_pause) gtk_widget_set_sensitive (tb_pause, TRUE);
            if (tb_end)   gtk_widget_set_sensitive (tb_end,   TRUE);
        }
        else {
            g_simple_action_set_enabled (action_start, TRUE);
            g_simple_action_set_enabled (action_pause, FALSE);
            g_simple_action_set_enabled (action_end,   FALSE);
            if (tb_start) gtk_widget_set_sensitive (tb_start, TRUE);
            if (tb_pause) gtk_widget_set_sensitive (tb_pause, FALSE);
            if (tb_end)   gtk_widget_set_sensitive (tb_end,   FALSE);
        }
    }
    else {
        g_simple_action_set_enabled (action_start, FALSE);
        g_simple_action_set_enabled (action_pause, FALSE);
        g_simple_action_set_enabled (action_end,   FALSE);
        if (tb_start) gtk_widget_set_sensitive (tb_start, FALSE);
        if (tb_pause) gtk_widget_set_sensitive (tb_pause, FALSE);
        if (tb_end)   gtk_widget_set_sensitive (tb_end,   FALSE);
    }
    if (ingame || spectating) {
        g_simple_action_set_enabled (action_team, FALSE);
        if (tb_team) gtk_widget_set_sensitive (tb_team, FALSE);
    }
    else {
        g_simple_action_set_enabled (action_team, TRUE);
        if (tb_team) gtk_widget_set_sensitive (tb_team, TRUE);
    }

    partyline_connectstatus (connected);

    if (ingame) partyline_status (_("Game in progress"));
    else if (connected) {
        char buf[256];
        GTET_O_STRCPY(buf, _("Connected to\n"));
        GTET_O_STRCAT(buf, server);
        partyline_status (buf);
    }
    else partyline_status (_("Not connected"));
}

void about_command (void)
{
    GdkTexture *logo_texture = gdk_texture_new_from_resource ("/apps/gtetrinet/gtetrinet.png");

    const char *authors[] = {"Ka-shu Wong <kswong@zip.com.au>",
			     "James Antill <james@and.org>",
			     "Jordi Mallach <jordi@sindominio.net>",
			     "Dani Carbonell <bocata@panete.net>",
			     NULL};
    const char *documenters[] = {"Jordi Mallach <jordi@sindominio.net>",
				 NULL};
    /* Translators: translate as your names & emails */
    const char *translators = _("translator-credits");

    gtk_show_about_dialog (NULL,
			   "program-name", APPNAME,
			   "version", APPVERSION,
			   "copyright", "Copyright \xc2\xa9 2004, 2005 Jordi Mallach, Dani Carbonell\nCopyright \xc2\xa9 1999, 2000, 2001, 2002, 2003 Ka-shu Wong",
			   "comments", _("A Tetrinet client for GNOME.\n"),
			   "authors", authors,
			   "documenters", documenters,
			   "translator-credits", strcmp (translators, "translator-credits") != 0 ? translators : NULL,
			   "logo", logo_texture,
			   "website", "http://gtetrinet.sf.net",
			   "website-label", "GTetrinet Home Page",
			   NULL);

    if (logo_texture != NULL)
	    g_object_unref (logo_texture);
}

void handle_links (GtkAboutDialog *about G_GNUC_UNUSED, const gchar *link, gpointer data G_GNUC_UNUSED)
{
    (void)link; /* unused */
}
