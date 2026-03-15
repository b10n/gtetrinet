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
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>

#include "gtetrinet.h"
#include "gtet_config.h"
#include "client.h"
#include "tetrinet.h"
#include "tetris.h"
#include "fields.h"
#include "misc.h"
#include "sound.h"
#include "partyline.h"

/* Adopted and renamed from libgnomeui */
#define GTET_PAD 8
#define GTET_PAD_SMALL 4

extern GtkWidget *app;

/* ---- Synchronous-style dialog run helper (gtk_dialog_run replacement) --- */

typedef struct {
    GMainLoop *loop;
    int        response;
} DialogRunData;

static void _dialog_run_response (GtkDialog *dialog,
                                   int        response,
                                   DialogRunData *d)
{
    d->response = response;
    if (g_main_loop_is_running (d->loop))
        g_main_loop_quit (d->loop);
}

static void _dialog_run_destroy (GtkWidget *widget, DialogRunData *d)
{
    if (g_main_loop_is_running (d->loop))
        g_main_loop_quit (d->loop);
}

int dialog_run (GtkDialog *dialog)
{
    DialogRunData d = { g_main_loop_new (NULL, FALSE), GTK_RESPONSE_NONE };
    gulong rid = g_signal_connect (dialog, "response",
                                   G_CALLBACK(_dialog_run_response), &d);
    gulong did = g_signal_connect (dialog, "destroy",
                                   G_CALLBACK(_dialog_run_destroy), &d);
    gtk_window_set_modal (GTK_WINDOW(dialog), TRUE);
    gtk_window_present (GTK_WINDOW(dialog));
    g_main_loop_run (d.loop);
    if (GTK_IS_DIALOG (dialog)) {
        g_signal_handler_disconnect (dialog, rid);
        g_signal_handler_disconnect (dialog, did);
    }
    g_main_loop_unref (d.loop);
    return d.response;
}

/* ---- margin helper ------------------------------------------------------- */
static void widget_set_margin_all (GtkWidget *w, int m)
{
    gtk_widget_set_margin_start  (w, m);
    gtk_widget_set_margin_end    (w, m);
    gtk_widget_set_margin_top    (w, m);
    gtk_widget_set_margin_bottom (w, m);
}

/*****************************************************/
/* connecting dialog - a dialog with a cancel button */
/*****************************************************/
static GtkWidget *connectingdialog = NULL, *connectdialog;
static GtkWidget *progressbar;
static gint timeouttag = 0;

GtkWidget *team_dialog;

void connectingdialog_button (GtkWidget *dialog, gint button)
{
    (void)dialog;
    switch (button) {
    case GTK_RESPONSE_CANCEL:
        g_source_remove (timeouttag);
        timeouttag = 0;
        if (connectingdialog == NULL) return;
        client_disconnect ();
        gtk_window_destroy (GTK_WINDOW(connectingdialog));
        connectingdialog = NULL;
        break;
    }
}

gint connectingdialog_delete (void)
{
    return TRUE; /* dont kill me */
}

gint connectingdialog_timeout (void)
{
    gtk_progress_bar_pulse (GTK_PROGRESS_BAR(progressbar));
    return TRUE;
}

void connectingdialog_new (void)
{
    if (connectingdialog != NULL)
    {
        gtk_window_present (GTK_WINDOW(connectingdialog));
        return;
    }
    connectingdialog = gtk_dialog_new_with_buttons ("Connect to server",
                                                    GTK_WINDOW(connectdialog),
                                                    GTK_DIALOG_MODAL,
                                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                                    NULL);
    progressbar = gtk_progress_bar_new ();
    gtk_widget_show (progressbar);
    gtk_box_append (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG(connectingdialog))),
                    progressbar);

    timeouttag = g_timeout_add (20, (GSourceFunc)connectingdialog_timeout, NULL);
    g_signal_connect (G_OBJECT(connectingdialog), "response",
                      G_CALLBACK(connectingdialog_button), NULL);
    g_signal_connect (G_OBJECT(connectingdialog), "close-request",
                      G_CALLBACK(connectingdialog_delete), NULL);
    gtk_window_present (GTK_WINDOW(connectingdialog));
}

void connectingdialog_destroy (void)
{
    if (timeouttag != 0) g_source_remove (timeouttag);
    timeouttag = 0;
    if (connectingdialog == NULL) return;
    gtk_window_destroy (GTK_WINDOW(connectingdialog));
    connectingdialog = NULL;
}

/*******************/
/* the team dialog */
/*******************/

static void teamdialog_nullify (GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    team_dialog = NULL;
}

void teamdialog_destroy (void)
{
    if (team_dialog == NULL) return;
    gtk_window_destroy (GTK_WINDOW(team_dialog));
    /* team_dialog NULLed by the "destroy" signal handler */
}

void teamdialog_button (GtkWidget *button, gint response, gpointer data)
{
    GtkEntry *entry = (GtkEntry *)data;
    (void)button;

    switch (response)
    {
    case GTK_RESPONSE_OK:
        g_settings_set_string (settings, "player-team",
                               gtk_editable_get_text (GTK_EDITABLE(entry)));
        tetrinet_changeteam (gtk_editable_get_text (GTK_EDITABLE(entry)));
        break;
    }

    teamdialog_destroy ();
}

void teamdialog_new (void)
{
    GtkWidget *hbox, *widget, *entry;
    gchar *team_utf8 = team;

    if (team_dialog != NULL)
    {
        gtk_window_present (GTK_WINDOW(team_dialog));
        return;
    }

    team_dialog = gtk_dialog_new_with_buttons (_("Change team"),
                                               GTK_WINDOW(app),
                                               0,
                                               _("_Cancel"), GTK_RESPONSE_CANCEL,
                                               _("_OK"),     GTK_RESPONSE_OK,
                                               NULL);
    gtk_dialog_set_default_response (GTK_DIALOG(team_dialog), GTK_RESPONSE_OK);
    gtk_window_set_resizable (GTK_WINDOW(team_dialog), FALSE);

    /* entry and label */
    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, GTET_PAD_SMALL);
    widget_set_margin_all (hbox, GTET_PAD_SMALL);
    widget = gtk_label_new ("Team name:");
    gtk_widget_show (widget);
    gtk_box_append (GTK_BOX(hbox), widget);
    entry = gtk_entry_new_with_buffer (gtk_entry_buffer_new ("Team", 4));
    gtk_editable_set_text (GTK_EDITABLE(entry), team_utf8);
    g_object_set (G_OBJECT(entry), "activates-default", TRUE, NULL);
    gtk_widget_show (entry);
    gtk_box_append (GTK_BOX(hbox), entry);
    gtk_widget_show (hbox);
    gtk_box_append (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG(team_dialog))), hbox);

    g_signal_connect (G_OBJECT(team_dialog), "response",
                      G_CALLBACK(teamdialog_button), (gpointer)entry);
    g_signal_connect (G_OBJECT(team_dialog), "destroy",
                      G_CALLBACK(teamdialog_nullify), NULL);
    gtk_window_present (GTK_WINDOW(team_dialog));
}

/**********************/
/* the connect dialog */
/**********************/
static int connecting;
static GtkWidget *serveraddressentry, *nicknameentry, *teamnameentry, *spectatorcheck, *passwordentry;
static GtkWidget *passwordlabel, *teamnamelabel;
static GtkWidget *originalradio, *tetrifastradio;

static void show_error_dialog (GtkWidget *parent, const char *msg)
{
    GtkWidget *d = gtk_message_dialog_new (GTK_WINDOW(parent),
                                           GTK_DIALOG_MODAL,
                                           GTK_MESSAGE_ERROR,
                                           GTK_BUTTONS_OK,
                                           "%s", msg);
    dialog_run (GTK_DIALOG(d));
    gtk_window_destroy (GTK_WINDOW(d));
}

void connectdialog_button (GtkDialog *dialog, gint button)
{
    gchar *nick_buf;
    const gchar *server1;

    switch (button) {
    case GTK_RESPONSE_OK:
        server1 = gtk_editable_get_text (GTK_EDITABLE(serveraddressentry));
        if (g_utf8_strlen (server1, -1) <= 0)
        {
            show_error_dialog (GTK_WIDGET(dialog), "You must specify a server name.");
            return;
        }

        spectating = gtk_check_button_get_active (GTK_CHECK_BUTTON(spectatorcheck));

        if (spectating)
        {
            g_utf8_strncpy (specpassword,
                            gtk_editable_get_text (GTK_EDITABLE(passwordentry)),
                            g_utf8_strlen (gtk_editable_get_text (GTK_EDITABLE(passwordentry)), -1));
            if (g_utf8_strlen (specpassword, -1) <= 0)
            {
                show_error_dialog (GTK_WIDGET(dialog), "Please specify a password to connect as spectator.");
                return;
            }
        }

        GTET_O_STRCPY (team, gtk_editable_get_text (GTK_EDITABLE(teamnameentry)));

        nick_buf = g_strdup (gtk_editable_get_text (GTK_EDITABLE(nicknameentry)));
        g_strstrip (nick_buf);
        if (g_utf8_strlen (nick_buf, -1) > 0)
        {
            client_init (server1, nick_buf);
        }
        else
        {
            show_error_dialog (GTK_WIDGET(dialog), "Please specify a valid nickname.");
            g_free (nick_buf);
            return;
        }

        g_settings_set_string  (settings, "server",          server1);
        g_settings_set_string  (settings, "player-nickname", nick_buf);
        g_settings_set_string  (settings, "player-team",     gtk_editable_get_text (GTK_EDITABLE(teamnameentry)));
        g_settings_set_boolean (settings, "gamemode",        gamemode);

        g_free (nick_buf);
        break;

    case GTK_RESPONSE_CANCEL:
        gtk_window_destroy (GTK_WINDOW(connectdialog));
        break;
    }
}

void connectdialog_spectoggle (GtkWidget *widget)
{
    if (gtk_check_button_get_active (GTK_CHECK_BUTTON(widget))) {
        gtk_widget_set_sensitive (passwordentry, TRUE);
        gtk_widget_set_sensitive (passwordlabel, TRUE);
        gtk_widget_set_sensitive (teamnameentry, FALSE);
        gtk_widget_set_sensitive (teamnamelabel, FALSE);
    }
    else {
        gtk_widget_set_sensitive (passwordentry, FALSE);
        gtk_widget_set_sensitive (passwordlabel, FALSE);
        gtk_widget_set_sensitive (teamnameentry, TRUE);
        gtk_widget_set_sensitive (teamnamelabel, TRUE);
    }
}

void connectdialog_originaltoggle (GtkWidget *widget)
{
    if (gtk_check_button_get_active (GTK_CHECK_BUTTON(widget)))
        gamemode = ORIGINAL;
}

void connectdialog_tetrifasttoggle (GtkWidget *widget)
{
    if (gtk_check_button_get_active (GTK_CHECK_BUTTON(widget)))
        gamemode = TETRIFAST;
}

void connectdialog_connected (void)
{
    if (connectdialog != NULL) gtk_window_destroy (GTK_WINDOW(connectdialog));
}

void connectdialog_destroy (void)
{
    connecting = FALSE;
}

void connectdialog_new (void)
{
    GtkWidget *widget, *grid1, *grid2, *frame;

    if (connecting)
    {
        gtk_window_present (GTK_WINDOW(connectdialog));
        return;
    }
    connecting = TRUE;

    connectdialog = gtk_dialog_new_with_buttons ("Connect to server",
                                                 GTK_WINDOW(app),
                                                 0,
                                                 _("_Cancel"), GTK_RESPONSE_CANCEL,
                                                 _("_OK"),     GTK_RESPONSE_OK,
                                                 NULL);
    gtk_dialog_set_default_response (GTK_DIALOG(connectdialog), GTK_RESPONSE_OK);
    gtk_window_set_resizable (GTK_WINDOW(connectdialog), FALSE);
    g_signal_connect (G_OBJECT(connectdialog), "response",
                      G_CALLBACK(connectdialog_button), NULL);

    /* main grid */
    grid1 = gtk_grid_new ();
    gtk_grid_set_row_spacing    (GTK_GRID(grid1), GTET_PAD_SMALL);
    gtk_grid_set_column_spacing (GTK_GRID(grid1), GTET_PAD_SMALL);

    /* server address frame */
    grid2 = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID(grid2), GTET_PAD_SMALL);
    widget_set_margin_all (grid2, GTET_PAD);

    serveraddressentry = gtk_entry_new_with_buffer (gtk_entry_buffer_new ("Server", 6));
    g_object_set (G_OBJECT(serveraddressentry), "activates-default", TRUE, NULL);
    gtk_editable_set_text (GTK_EDITABLE(serveraddressentry), server);
    gtk_widget_set_hexpand (serveraddressentry, TRUE);
    gtk_widget_show (serveraddressentry);
    gtk_grid_attach (GTK_GRID(grid2), serveraddressentry, 0, 0, 1, 1);

    originalradio  = gtk_check_button_new_with_mnemonic ("O_riginal");
    tetrifastradio = gtk_check_button_new_with_mnemonic ("Tetri_Fast");
    gtk_check_button_set_group (GTK_CHECK_BUTTON(tetrifastradio), GTK_CHECK_BUTTON(originalradio));
    switch (gamemode) {
    case ORIGINAL:  gtk_check_button_set_active (GTK_CHECK_BUTTON(originalradio),  TRUE); break;
    case TETRIFAST: gtk_check_button_set_active (GTK_CHECK_BUTTON(tetrifastradio), TRUE); break;
    }
    gtk_widget_show (originalradio);
    gtk_widget_show (tetrifastradio);
    widget = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, GTET_PAD_SMALL);
    gtk_box_append (GTK_BOX(widget), originalradio);
    gtk_box_append (GTK_BOX(widget), tetrifastradio);
    gtk_widget_show (widget);
    gtk_grid_attach (GTK_GRID(grid2), widget, 0, 1, 1, 1);

    gtk_widget_show (grid2);
    frame = gtk_frame_new ("Server address");
    gtk_frame_set_child (GTK_FRAME(frame), grid2);
    gtk_widget_set_hexpand (frame, TRUE);
    gtk_widget_set_vexpand (frame, TRUE);
    gtk_widget_show (frame);
    gtk_grid_attach (GTK_GRID(grid1), frame, 0, 0, 2, 1);

    /* spectator frame */
    grid2 = gtk_grid_new ();
    gtk_grid_set_row_spacing    (GTK_GRID(grid2), GTET_PAD_SMALL);
    gtk_grid_set_column_spacing (GTK_GRID(grid2), GTET_PAD_SMALL);
    widget_set_margin_all (grid2, GTET_PAD);

    spectatorcheck = gtk_check_button_new_with_mnemonic ("Connect as a _spectator");
    gtk_widget_show (spectatorcheck);
    gtk_widget_set_hexpand (spectatorcheck, TRUE);
    gtk_grid_attach (GTK_GRID(grid2), spectatorcheck, 0, 0, 2, 1);

    passwordlabel = gtk_label_new_with_mnemonic ("_Password:");
    gtk_widget_show (passwordlabel);
    gtk_widget_set_hexpand (passwordlabel, TRUE);
    gtk_grid_attach (GTK_GRID(grid2), passwordlabel, 0, 1, 1, 1);

    passwordentry = gtk_entry_new ();
    gtk_label_set_mnemonic_widget (GTK_LABEL(passwordlabel), passwordentry);
    gtk_entry_set_visibility (GTK_ENTRY(passwordentry), FALSE);
    g_object_set (G_OBJECT(passwordentry), "activates-default", TRUE, NULL);
    gtk_widget_show (passwordentry);
    gtk_widget_set_hexpand (passwordentry, TRUE);
    gtk_grid_attach (GTK_GRID(grid2), passwordentry, 1, 1, 1, 1);

    gtk_widget_show (grid2);
    frame = gtk_frame_new ("Spectate game");
    gtk_frame_set_child (GTK_FRAME(frame), grid2);
    gtk_widget_set_hexpand (frame, TRUE);
    gtk_widget_set_vexpand (frame, TRUE);
    gtk_widget_show (frame);
    gtk_grid_attach (GTK_GRID(grid1), frame, 0, 1, 1, 1);

    /* player info frame */
    grid2 = gtk_grid_new ();
    gtk_grid_set_row_spacing    (GTK_GRID(grid2), GTET_PAD_SMALL);
    gtk_grid_set_column_spacing (GTK_GRID(grid2), GTET_PAD_SMALL);
    widget_set_margin_all (grid2, GTET_PAD);

    widget = gtk_label_new_with_mnemonic ("_Nick name:");
    gtk_widget_show (widget);
    gtk_widget_set_hexpand (widget, TRUE);
    gtk_grid_attach (GTK_GRID(grid2), widget, 0, 0, 1, 1);

    nicknameentry = gtk_entry_new_with_buffer (gtk_entry_buffer_new ("Nickname", 8));
    gtk_label_set_mnemonic_widget (GTK_LABEL(widget), nicknameentry);
    g_object_set (G_OBJECT(nicknameentry), "activates-default", TRUE, NULL);
    gtk_editable_set_text (GTK_EDITABLE(nicknameentry), nick);
    gtk_widget_show (nicknameentry);
    gtk_widget_set_hexpand (nicknameentry, TRUE);
    gtk_grid_attach (GTK_GRID(grid2), nicknameentry, 1, 0, 1, 1);

    teamnamelabel = gtk_label_new_with_mnemonic ("_Team name:");
    gtk_widget_show (teamnamelabel);
    gtk_widget_set_hexpand (teamnamelabel, TRUE);
    gtk_grid_attach (GTK_GRID(grid2), teamnamelabel, 0, 1, 1, 1);

    teamnameentry = gtk_entry_new_with_buffer (gtk_entry_buffer_new ("Teamname", 8));
    gtk_label_set_mnemonic_widget (GTK_LABEL(teamnamelabel), teamnameentry);
    g_object_set (G_OBJECT(teamnameentry), "activates-default", TRUE, NULL);
    gtk_editable_set_text (GTK_EDITABLE(teamnameentry), team);
    gtk_widget_show (teamnameentry);
    gtk_widget_set_hexpand (teamnameentry, TRUE);
    gtk_grid_attach (GTK_GRID(grid2), teamnameentry, 1, 1, 1, 1);

    gtk_widget_show (grid2);
    frame = gtk_frame_new ("Player information");
    gtk_frame_set_child (GTK_FRAME(frame), grid2);
    gtk_widget_set_hexpand (frame, TRUE);
    gtk_widget_set_vexpand (frame, TRUE);
    gtk_widget_show (frame);
    gtk_grid_attach (GTK_GRID(grid1), frame, 1, 1, 1, 1);

    gtk_widget_show (grid1);
    widget_set_margin_all (grid1, GTET_PAD_SMALL);
    gtk_box_append (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG(connectdialog))), grid1);

    gtk_check_button_set_active (GTK_CHECK_BUTTON(spectatorcheck), spectating);
    connectdialog_spectoggle (spectatorcheck);
    g_signal_connect (G_OBJECT(connectdialog), "destroy",
                      G_CALLBACK(connectdialog_destroy), NULL);
    g_signal_connect (G_OBJECT(spectatorcheck), "toggled",
                      G_CALLBACK(connectdialog_spectoggle), NULL);
    g_signal_connect (G_OBJECT(originalradio), "toggled",
                      G_CALLBACK(connectdialog_originaltoggle), NULL);
    g_signal_connect (G_OBJECT(tetrifastradio), "toggled",
                      G_CALLBACK(connectdialog_tetrifasttoggle), NULL);
    gtk_window_present (GTK_WINDOW(connectdialog));
}

GtkWidget *prefdialog;

/*************************/
/* the change key dialog */
/*************************/

static gboolean key_dialog_key_cb (GtkEventControllerKey *ctrl,
                                    guint                  keyval,
                                    guint                  keycode,
                                    GdkModifierType        state,
                                    gpointer               dialog)
{
    (void)ctrl; (void)keycode; (void)state;
    gtk_dialog_response (GTK_DIALOG(dialog), gdk_keyval_to_lower (keyval));
    return TRUE;
}

gint key_dialog (char *msg)
{
    GtkWidget *dialog, *label;
    GtkEventController *ctrl;
    gint result;

    dialog = gtk_dialog_new_with_buttons ("Change Key", GTK_WINDOW(prefdialog),
                                          GTK_DIALOG_MODAL,
                                          _("_Cancel"), GTK_RESPONSE_CLOSE,
                                          NULL);
    label = gtk_label_new (msg);
    gtk_widget_show (label);
    gtk_box_append (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG(dialog))), label);

    ctrl = gtk_event_controller_key_new ();
    g_signal_connect (ctrl, "key-pressed", G_CALLBACK(key_dialog_key_cb), dialog);
    gtk_widget_add_controller (dialog, ctrl);

    result = dialog_run (GTK_DIALOG(dialog));
    gtk_window_destroy (GTK_WINDOW(dialog));

    return (result != GTK_RESPONSE_CLOSE) ? result : 0;
}

/**************************/
/* the preferences dialog */
/**************************/
GtkWidget *themelist, *keyclist;
GtkWidget *timestampcheck;
GtkWidget *soundcheck;
GtkWidget *namelabel, *authlabel, *desclabel;

gchar *actions[K_NUM];

struct themelistentry {
    char dir[1024];
    char name[1024];
} themes[64];

int themecount;
int theme_select;

void prefdialog_destroy (void)
{
    prefdialog = NULL;
}

void prefdialog_drawkeys (void)
{
    gchar *gconf_keys[K_NUM];
    int i;
    GtkTreeIter iter;
    GtkListStore *keys_store = GTK_LIST_STORE(gtk_tree_view_get_model (GTK_TREE_VIEW(keyclist)));

    actions[K_RIGHT]    = ("Move right");
    actions[K_LEFT]     = ("Move left");
    actions[K_DOWN]     = ("Move down");
    actions[K_ROTRIGHT] = ("Rotate right");
    actions[K_ROTLEFT]  = ("Rotate left");
    actions[K_DROP]     = ("Drop piece");
    actions[K_DISCARD]  = ("Discard special");
    actions[K_GAMEMSG]  = ("Send message");
    actions[K_SPECIAL1] = ("Special to field 1");
    actions[K_SPECIAL2] = ("Special to field 2");
    actions[K_SPECIAL3] = ("Special to field 3");
    actions[K_SPECIAL4] = ("Special to field 4");
    actions[K_SPECIAL5] = ("Special to field 5");
    actions[K_SPECIAL6] = ("Special to field 6");

    gconf_keys[K_RIGHT]    = g_strdup ("right");
    gconf_keys[K_LEFT]     = g_strdup ("left");
    gconf_keys[K_DOWN]     = g_strdup ("down");
    gconf_keys[K_ROTRIGHT] = g_strdup ("rotate-right");
    gconf_keys[K_ROTLEFT]  = g_strdup ("rotate-left");
    gconf_keys[K_DROP]     = g_strdup ("drop");
    gconf_keys[K_DISCARD]  = g_strdup ("discard");
    gconf_keys[K_GAMEMSG]  = g_strdup ("message");
    gconf_keys[K_SPECIAL1] = g_strdup ("special1");
    gconf_keys[K_SPECIAL2] = g_strdup ("special2");
    gconf_keys[K_SPECIAL3] = g_strdup ("special3");
    gconf_keys[K_SPECIAL4] = g_strdup ("special4");
    gconf_keys[K_SPECIAL5] = g_strdup ("special5");
    gconf_keys[K_SPECIAL6] = g_strdup ("special6");

    for (i = 0; i < K_NUM; i++) {
        gtk_list_store_append (keys_store, &iter);
        gtk_list_store_set (keys_store, &iter,
                            0, actions[i],
                            1, gdk_keyval_name (keys[i]),
                            2, i,
                            3, gconf_keys[i], -1);
    }

    for (i = 0; i < K_NUM; i++) g_free (gconf_keys[i]);
}

void prefdialog_restorekeys (void)
{
    GtkTreeIter iter;
    GtkListStore *keys_store = GTK_LIST_STORE(gtk_tree_view_get_model (GTK_TREE_VIEW(keyclist)));
    gboolean valid;
    gchar *gconf_key;
    gint i;

    valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(keys_store), &iter);
    while (valid)
    {
        gtk_tree_model_get (GTK_TREE_MODEL(keys_store), &iter, 2, &i, 3, &gconf_key, -1);
        gtk_list_store_set (keys_store, &iter, 1, gdk_keyval_name (defaultkeys[i]), -1);
        g_settings_set_string (settings_keys, gconf_key, gdk_keyval_name (defaultkeys[i]));
        valid = gtk_tree_model_iter_next (GTK_TREE_MODEL(keys_store), &iter);
        g_free (gconf_key);
    }
}

void prefdialog_changekey (void)
{
    gchar buf[256], *key, *gconf_key;
    gint k;
    GtkListStore *keys_store = GTK_LIST_STORE(gtk_tree_view_get_model (GTK_TREE_VIEW(keyclist)));
    GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(keyclist));
    GtkTreeIter selected_row;

    if (!gtk_tree_selection_get_selected (selection, NULL, &selected_row)) return;

    gtk_tree_model_get (GTK_TREE_MODEL(keys_store), &selected_row, 0, &key, 3, &gconf_key, -1);
    g_snprintf (buf, sizeof(buf), ("Press new key for \"%s\""), key);
    k = key_dialog (buf);
    if (k) {
        gtk_list_store_set (keys_store, &selected_row, 1, gdk_keyval_name (k), -1);
        g_settings_set_string (settings_keys, gconf_key, gdk_keyval_name (k));
    }

    g_free (gconf_key);
}

void prefdialog_soundtoggle (GtkWidget *check)
{
    gboolean enabled = gtk_check_button_get_active (GTK_CHECK_BUTTON(check));
    g_settings_set_boolean (settings, "sound-enable", enabled);
}

void prefdialog_channeltoggle (GtkWidget *check)
{
    gboolean enabled = gtk_check_button_get_active (GTK_CHECK_BUTTON(check));
    g_settings_set_boolean (settings, "partyline-enable-channel-list", enabled);
}

void prefdialog_timestampstoggle (GtkWidget *check)
{
    gboolean enabled = gtk_check_button_get_active (GTK_CHECK_BUTTON(check));
    g_settings_set_boolean (settings, "partyline-enable-timestamps", enabled);
}

void prefdialog_themelistselect (int n)
{
    char author[1024], desc[1024];

    config_getthemeinfo (themes[n].dir, NULL, author, desc);
    leftlabel_set (namelabel, themes[n].name);
    leftlabel_set (authlabel, author);
    leftlabel_set (desclabel, desc);

    g_settings_set_string (settings_themes, "directory", themes[n].dir);
}

void prefdialog_themeselect (GtkTreeSelection *treeselection)
{
    GtkListStore *model;
    GtkTreeIter iter;
    gint row;

    if (gtk_tree_selection_get_selected (treeselection, NULL, &iter))
    {
        model = GTK_LIST_STORE(gtk_tree_view_get_model (gtk_tree_selection_get_tree_view (treeselection)));
        gtk_tree_model_get (GTK_TREE_MODEL(model), &iter, 1, &row, -1);
        prefdialog_themelistselect (row);
    }
}

static int themelistcomp (const void *a1, const void *b1)
{
    const struct themelistentry *a = a1, *b = b1;
    return strcmp (a->name, b->name);
}

void prefdialog_themelist (void)
{
    DIR *d;
    struct dirent *de;
    char str[1024], buf[1024];
    gchar *dir;
    int i;
    char *basedir[2];
    GtkListStore *theme_store = GTK_LIST_STORE(gtk_tree_view_get_model (GTK_TREE_VIEW(themelist)));
    GtkTreeSelection *theme_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(themelist));
    GtkTreeIter iter, iter_selected;

    dir = g_build_filename (getenv ("HOME"), ".gtetrinet", "themes", NULL);
    basedir[0] = dir;
    basedir[1] = GTETRINET_THEMES;

    themecount = 0;

    for (i = 0; i < 2; i++) {
        d = opendir (basedir[i]);
        if (d) {
            while ((de = readdir (d))) {
                GTET_O_STRCPY (buf, basedir[i]);
                GTET_O_STRCAT (buf, "/");
                GTET_O_STRCAT (buf, de->d_name);
                GTET_O_STRCAT (buf, "/");

                if (config_getthemeinfo (buf, str, NULL, NULL) == 0) {
                    GTET_O_STRCPY (themes[themecount].dir, buf);
                    GTET_O_STRCPY (themes[themecount].name, str);
                    themecount++;
                    if (themecount == (int)(sizeof(themes) / sizeof(themes[0])))
                    {
                        g_warning ("Too many theme files.\n");
                        closedir (d);
                        goto too_many_themes;
                    }
                }
            }
            closedir (d);
        }
    }
    g_free (dir);
too_many_themes:
    qsort (themes, themecount, sizeof(struct themelistentry), themelistcomp);

    theme_select = 0;
    gtk_list_store_clear (theme_store);
    for (i = 0; i < themecount; i++) {
        gtk_list_store_append (theme_store, &iter);
        gtk_list_store_set (theme_store, &iter, 0, themes[i].name, 1, i, -1);
        if (strcmp (themes[i].dir, currenttheme->str) == 0)
        {
            iter_selected = iter;
            theme_select = i;
        }
    }
    if (theme_select != 0)
    {
        gtk_tree_selection_select_iter (theme_selection, &iter_selected);
        prefdialog_themelistselect (theme_select);
    }
}

void prefdialog_response (GtkDialog *dialog, gint arg1)
{
    (void)dialog;
    switch (arg1)
    {
    case GTK_RESPONSE_CLOSE:
        gtk_window_destroy (GTK_WINDOW(prefdialog));
        break;
    case GTK_RESPONSE_HELP:
        /* here we should open yelp */
        break;
    }
}

void prefdialog_new (void)
{
    GtkWidget *label, *grid, *frame, *button, *button1, *widget, *grid1, *divider, *notebook;
    GtkWidget *themelist_scroll, *key_scroll, *url;
    GtkWidget *channel_list_check;
    GtkListStore *theme_store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
    GtkListStore *keys_store  = gtk_list_store_new (4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING);
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
    GtkTreeSelection *theme_selection;

    if (prefdialog != NULL)
    {
        gtk_window_present (GTK_WINDOW(prefdialog));
        return;
    }

    prefdialog = gtk_dialog_new_with_buttons ("GTetrinet Preferences",
                                              GTK_WINDOW(app),
                                              GTK_DIALOG_DESTROY_WITH_PARENT,
                                              _("_Help"),  GTK_RESPONSE_HELP,
                                              _("_Close"), GTK_RESPONSE_CLOSE,
                                              NULL);
    notebook = gtk_notebook_new ();
    gtk_window_set_resizable (GTK_WINDOW(prefdialog), FALSE);

    /* Remove the default 12px content-area margin added by GTK4/Adwaita;
       each notebook tab already carries its own padding via widget_set_margin_all. */
    {
        GtkWidget *ca = gtk_dialog_get_content_area (GTK_DIALOG(prefdialog));
        gtk_widget_set_margin_start  (ca, 0);
        gtk_widget_set_margin_end    (ca, 0);
        gtk_widget_set_margin_top    (ca, 0);
        gtk_widget_set_margin_bottom (ca, 0);
    }

    /* ---- themes tab ---- */
    themelist        = gtk_tree_view_new_with_model (GTK_TREE_MODEL(theme_store));
    themelist_scroll = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(themelist_scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW(themelist_scroll), themelist);
    theme_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(themelist));
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW(themelist), FALSE);
    gtk_widget_set_size_request (themelist, 160, 200);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW(themelist), -1, "theme", renderer,
                                                 "text", 0, NULL);

    label = leftlabel_new ("Select a theme from the list.\n"
                           "Install new themes in ~/.gtetrinet/themes/");

    /* theme info grid */
    grid1 = gtk_grid_new ();
    gtk_grid_set_row_spacing    (GTK_GRID(grid1), 0);
    gtk_grid_set_column_spacing (GTK_GRID(grid1), GTET_PAD_SMALL);
    widget_set_margin_all (grid1, GTET_PAD_SMALL);

    widget = leftlabel_new ("Name:");
    gtk_widget_set_hexpand (widget, TRUE);
    gtk_grid_attach (GTK_GRID(grid1), widget, 0, 0, 1, 1);

    widget = leftlabel_new ("Author:");
    gtk_widget_set_hexpand (widget, TRUE);
    gtk_grid_attach (GTK_GRID(grid1), widget, 0, 1, 1, 1);

    widget = leftlabel_new ("Description:");
    gtk_widget_set_hexpand (widget, TRUE);
    gtk_grid_attach (GTK_GRID(grid1), widget, 0, 2, 1, 1);

    namelabel = leftlabel_new ("");
    gtk_widget_set_hexpand (namelabel, TRUE);
    gtk_grid_attach (GTK_GRID(grid1), namelabel, 1, 0, 1, 1);

    authlabel = leftlabel_new ("");
    gtk_widget_set_hexpand (authlabel, TRUE);
    gtk_grid_attach (GTK_GRID(grid1), authlabel, 1, 1, 1, 1);

    desclabel = leftlabel_new ("");
    gtk_widget_set_hexpand (desclabel, TRUE);
    gtk_grid_attach (GTK_GRID(grid1), desclabel, 1, 2, 1, 1);

    frame = gtk_frame_new ("Selected Theme");
    widget_set_margin_all (frame, GTET_PAD_SMALL);
    gtk_widget_set_size_request (frame, 240, 100);
    gtk_frame_set_child (GTK_FRAME(frame), grid1);

    /* outer themes grid */
    grid = gtk_grid_new ();
    widget_set_margin_all (grid, GTET_PAD);
    gtk_grid_set_row_spacing    (GTK_GRID(grid), GTET_PAD_SMALL);
    gtk_grid_set_column_spacing (GTK_GRID(grid), GTET_PAD_SMALL);

    gtk_widget_set_hexpand  (themelist_scroll, TRUE);
    gtk_widget_set_vexpand  (themelist_scroll, TRUE);
    gtk_grid_attach (GTK_GRID(grid), themelist_scroll, 0, 0, 1, 3);

    gtk_widget_set_hexpand (label, TRUE);
    gtk_grid_attach (GTK_GRID(grid), label, 1, 0, 1, 1);

    gtk_widget_set_hexpand (frame, TRUE);
    gtk_widget_set_vexpand (frame, TRUE);
    gtk_grid_attach (GTK_GRID(grid), frame, 1, 1, 1, 1);

    url = gtk_link_button_new_with_label ("http://gtetrinet.sourceforge.net/themes.html",
                                          "Download new themes");
    gtk_widget_set_hexpand (url, TRUE);
    gtk_grid_attach (GTK_GRID(grid), url, 1, 2, 1, 1);
    gtk_widget_show (grid);

    label = gtk_label_new ("Themes");
    gtk_widget_show (label);
    gtk_notebook_append_page (GTK_NOTEBOOK(notebook), grid, label);

    /* ---- partyline tab ---- */
    timestampcheck   = gtk_check_button_new_with_mnemonic (_("Enable _Timestamps"));
    channel_list_check = gtk_check_button_new_with_mnemonic ("Enable Channel _List");

    frame = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append (GTK_BOX(frame), timestampcheck);
    gtk_box_append (GTK_BOX(frame), channel_list_check);
    gtk_widget_show (frame);

    gtk_check_button_set_active (GTK_CHECK_BUTTON(timestampcheck),    timestampsenable);
    gtk_check_button_set_active (GTK_CHECK_BUTTON(channel_list_check), list_enabled);

    g_signal_connect (G_OBJECT(timestampcheck),   "toggled",
                      G_CALLBACK(prefdialog_timestampstoggle), NULL);
    g_signal_connect (G_OBJECT(channel_list_check), "toggled",
                      G_CALLBACK(prefdialog_channeltoggle), NULL);

    grid = gtk_grid_new ();
    widget_set_margin_all (grid, GTET_PAD);
    gtk_grid_set_row_spacing (GTK_GRID(grid), GTET_PAD_SMALL);
    gtk_widget_set_hexpand (frame, TRUE);
    gtk_grid_attach (GTK_GRID(grid), frame, 0, 0, 1, 1);
    gtk_widget_show (grid);

    label = gtk_label_new ("Partyline");
    gtk_widget_show (label);
    gtk_notebook_append_page (GTK_NOTEBOOK(notebook), grid, label);

    /* ---- keyboard tab ---- */
    keyclist   = GTK_WIDGET(gtk_tree_view_new_with_model (GTK_TREE_MODEL(keys_store)));
    key_scroll = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(key_scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW(key_scroll), keyclist);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW(keyclist), -1, "Action", renderer,
                                                 "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW(keyclist), -1, "Key", renderer,
                                                 "text", 1, NULL);
    gtk_widget_set_size_request (key_scroll, 240, 200);
    gtk_widget_show (key_scroll);

    label = gtk_label_new ("Select an action from the list and press Change "
                           "Key to change the key associated with the action.");
    gtk_label_set_justify   (GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_label_set_wrap      (GTK_LABEL(label), TRUE);
    gtk_widget_show (label);
    gtk_widget_set_size_request (label, 180, 100);

    button  = gtk_button_new_with_mnemonic ("Change _key...");
    g_signal_connect (G_OBJECT(button), "clicked",
                      G_CALLBACK(prefdialog_changekey), NULL);
    gtk_widget_show (button);

    button1 = gtk_button_new_with_mnemonic ("_Restore defaults");
    g_signal_connect (G_OBJECT(button1), "clicked",
                      G_CALLBACK(prefdialog_restorekeys), NULL);
    gtk_widget_show (button1);

    /* button column: pack end (bottom) */
    frame = gtk_box_new (GTK_ORIENTATION_VERTICAL, GTET_PAD_SMALL);
    /* prepend so button1 is below button (pack_end reversed order) */
    gtk_box_prepend (GTK_BOX(frame), button1);
    gtk_box_prepend (GTK_BOX(frame), button);
    gtk_widget_show (frame);

    grid = gtk_grid_new ();
    widget_set_margin_all (grid, GTET_PAD);
    gtk_grid_set_row_spacing    (GTK_GRID(grid), GTET_PAD_SMALL);
    gtk_grid_set_column_spacing (GTK_GRID(grid), GTET_PAD_SMALL);

    gtk_grid_attach (GTK_GRID(grid), key_scroll, 0, 0, 1, 2);

    gtk_widget_set_hexpand (label, TRUE);
    gtk_grid_attach (GTK_GRID(grid), label, 1, 0, 1, 1);

    gtk_widget_set_hexpand (frame, TRUE);
    gtk_widget_set_vexpand (frame, TRUE);
    gtk_grid_attach (GTK_GRID(grid), frame, 1, 1, 1, 1);
    gtk_widget_show (grid);

    label = gtk_label_new ("Keyboard");
    gtk_widget_show (label);
    gtk_notebook_append_page (GTK_NOTEBOOK(notebook), grid, label);

    /* ---- sound tab ---- */
    soundcheck = gtk_check_button_new_with_mnemonic ("Enable _Sound");
    gtk_widget_show (soundcheck);

    frame = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append (GTK_BOX(frame), soundcheck);
    gtk_widget_show (frame);

    divider = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_show (divider);

    grid = gtk_grid_new ();
    widget_set_margin_all (grid, GTET_PAD);
    gtk_grid_set_row_spacing (GTK_GRID(grid), GTET_PAD_SMALL);

    gtk_widget_set_hexpand (frame, TRUE);
    gtk_grid_attach (GTK_GRID(grid), frame, 0, 0, 1, 1);

    gtk_widget_set_hexpand (divider, TRUE);
    gtk_widget_set_margin_bottom (divider, GTET_PAD_SMALL);
    gtk_grid_attach (GTK_GRID(grid), divider, 0, 1, 1, 1);
    gtk_widget_show (grid);

    label = gtk_label_new ("Sound");
    gtk_widget_show (label);
    gtk_notebook_append_page (GTK_NOTEBOOK(notebook), grid, label);

    /* ---- finish up ---- */
    prefdialog_themelist ();
    prefdialog_drawkeys ();

    gtk_check_button_set_active (GTK_CHECK_BUTTON(soundcheck), soundenable);

#ifndef HAVE_CANBERRAGTK
    gtk_widget_set_sensitive (soundcheck, FALSE);
#endif

    gtk_widget_show (notebook);
    gtk_box_append (GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG(prefdialog))), notebook);

    g_signal_connect (G_OBJECT(soundcheck),       "toggled",  G_CALLBACK(prefdialog_soundtoggle), NULL);
    g_signal_connect (G_OBJECT(theme_selection),  "changed",  G_CALLBACK(prefdialog_themeselect), NULL);
    g_signal_connect (G_OBJECT(prefdialog),        "destroy",  G_CALLBACK(prefdialog_destroy), NULL);
    g_signal_connect (G_OBJECT(prefdialog),        "response", G_CALLBACK(prefdialog_response), NULL);
    gtk_window_present (GTK_WINDOW(prefdialog));
}
