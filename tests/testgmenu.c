/* testgmenu.c
 * Copyright (C) 2011  Red Hat, Inc.
 * Written by Matthias Clasen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

/* TODO
 *
 * - Debug initial click problem.
 *
 * - Dynamic changes. Add/Remove items, sections, submenus and
 *   reconstruct the widgetry.
 *
 * - Focus changes. Verify that stopping subscriptions works
 *   as intended.
 *
 * - Other attributes. What about icons ?
 */

/* GtkMenu construction {{{1 */

static void
enabled_changed (GActionGroup *group,
                 const gchar  *action_name,
                 gboolean      enabled,
                 GtkWidget    *widget)
{
  gtk_widget_set_sensitive (widget, enabled);
}

typedef struct {
  GActionGroup *group;
  const gchar  *name;
  const gchar  *target;
} Activation;

static void
activate_item (GtkWidget *w, gpointer data)
{
  Activation *a;

  a = g_object_get_data (G_OBJECT (w), "activation");

  g_action_group_activate_action (a->group, a->name, NULL);
}

static void
toggle_item_toggled (GtkCheckMenuItem *w, gpointer data)
{
  Activation *a;
  gboolean b;

  a = g_object_get_data (G_OBJECT (w), "activation");
  b = gtk_check_menu_item_get_active (w);
  g_action_group_change_action_state (a->group, a->name,
                                      g_variant_new_boolean (b));
}

static void
toggle_state_changed (GActionGroup     *group,
                      const gchar      *name,
                      GVariant         *state,
                      GtkCheckMenuItem *w)
{
  gtk_check_menu_item_set_active (w, g_variant_get_boolean (state));
}

static void
radio_item_toggled (GtkCheckMenuItem *w, gpointer data)
{
  Activation *a;
  GVariant *v;

  a = g_object_get_data (G_OBJECT (w), "activation");
  /*g_print ("Radio item %s toggled\n", a->name);*/
  if (gtk_check_menu_item_get_active (w))
    g_action_group_change_action_state (a->group, a->name,
                                        g_variant_new_string (a->target));
  else
    {
      v = g_action_group_get_action_state (a->group, a->name);
      if (g_strcmp0 (g_variant_get_string (v, NULL), a->target) == 0)
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (w), TRUE);
      g_variant_unref (v);
    }
}

static void
radio_state_changed (GActionGroup     *group,
                     const gchar      *name,
                     GVariant         *state,
                     GtkCheckMenuItem *w)
{
  Activation *a;
  gboolean b;

  /*g_print ("Radio state changed %s\n", name);*/
  a = g_object_get_data (G_OBJECT (w), "activation");
  b = g_strcmp0 (a->target, g_variant_get_string (state, NULL)) == 0;

  gtk_check_menu_item_set_active (w, b);
}

static GtkWidget *
create_menuitem_from_model (GMenuModelItem *item,
                            GActionGroup   *group)
{
  GtkWidget *w;
  gchar *label;
  gchar *action;
  gchar *target;
  gchar *s;
  Activation *a;
  const GVariantType *type;
  GVariant *v;

  g_menu_model_item_get_attribute (item, G_MENU_ATTRIBUTE_LABEL, "s", &label);

  action = NULL;
  g_menu_model_item_get_attribute (item, G_MENU_ATTRIBUTE_ACTION, "s", &action);

  if (action != NULL)
    type = g_action_group_get_action_state_type (group, action);
  else
    type = NULL;

  if (type == NULL)
    w = gtk_menu_item_new_with_mnemonic (label);
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_BOOLEAN))
    w = gtk_check_menu_item_new_with_label (label);
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_STRING))
    {
      w = gtk_check_menu_item_new_with_label (label);
      gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (w), TRUE);
    }
  else
    g_assert_not_reached ();

  if (action != NULL)
    {
      if (!g_action_group_get_action_enabled (group, action))
        gtk_widget_set_sensitive (w, FALSE);

      s = g_strconcat ("action-enabled-changed::", action, NULL);
      g_signal_connect (group, s, G_CALLBACK (enabled_changed), w);
      g_free (s);

      a = g_new0 (Activation, 1);
      a->group = group;
      a->name = action;
      g_object_set_data_full (G_OBJECT (w), "activation", a, g_free);

      if (type == NULL)
        g_signal_connect (w, "activate", G_CALLBACK (activate_item), NULL);
      else if (g_variant_type_equal (type, G_VARIANT_TYPE_BOOLEAN))
        {
          g_signal_connect (w, "toggled", G_CALLBACK (toggle_item_toggled), NULL);
          s = g_strconcat ("action-state-changed::", action, NULL);
          g_signal_connect (group, s, G_CALLBACK (toggle_state_changed), w);
          g_free (s);
          v = g_action_group_get_action_state (group, action);
          gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (w),
                                          g_variant_get_boolean (v));
          g_variant_unref (v);
        }
      else if (g_variant_type_equal (type, G_VARIANT_TYPE_STRING))
        {
          g_signal_connect (w, "toggled", G_CALLBACK (radio_item_toggled), NULL);
          s = g_strconcat ("action-state-changed::", action, NULL);
          g_signal_connect (group, s, G_CALLBACK (radio_state_changed), w);
          g_free (s);
          g_menu_model_item_get_attribute (item, G_MENU_ATTRIBUTE_TARGET, "s", &target);
          a->target = target;
          v = g_action_group_get_action_state (group, action);
          gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (w),
                                          g_strcmp0 (g_variant_get_string (v, NULL), target) == 0);
          g_variant_unref (v);
        }
    }

  g_free (label);

  return w;
}

static GtkWidget *create_menu_from_model (GMenuModel   *model,
                                          GActionGroup *group);

static void
append_items_from_model (GtkWidget    *menu,
                         GMenuModel   *model,
                         GActionGroup *group,
                         gboolean     *need_separator)
{
  gint n;
  gint i;
  GtkWidget *w;
  GtkWidget *menuitem;
  GMenuModelItem item;
  GMenuModel *m;

  n = g_menu_model_get_n_items (model);

  if (*need_separator && n > 0)
    {
      /* TODO section heading */
      w = gtk_separator_menu_item_new ();
      gtk_widget_show (w);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), w);

      *need_separator = FALSE;
    }

  for (i = 0; i < n; i++)
    {
      g_menu_model_get_item (model, i, &item);
      if ((m = g_menu_model_item_get_link (&item, G_MENU_LINK_SECTION)))
        {
          append_items_from_model (menu, m, group, need_separator);
          continue;
        }

      menuitem = create_menuitem_from_model (&item, group);

      if ((m = g_menu_model_item_get_link (&item, G_MENU_LINK_SUBMENU)))
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), create_menu_from_model (m, group));

      gtk_widget_show (menuitem);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);

      *need_separator = TRUE;
    }
}

static GtkWidget *
create_menu_from_model (GMenuModel   *model,
                        GActionGroup *group)
{
  GtkWidget *w;
  gboolean need_separator;

  w = gtk_menu_new ();
  need_separator = FALSE;
  append_items_from_model (w, model, group, &need_separator);

  return w;
}

/* The example menu {{{1 */

static const gchar menu_markup[] =
  "<menu id='edit-menu'>\n"
  "  <section>\n"
  "    <item label='Undo' action='undo'/>\n"
  "    <item label='Redo' action='redo'/>\n"
  "  </section>\n"
  "  <section></section>\n"
  "  <section label='Copy &amp; Paste'>\n"
  "    <item label='Cut' action='cut'/>\n"
  "    <item label='Copy' action='copy'/>\n"
  "    <item label='Paste' action='paste'/>\n"
  "  </section>\n"
  "  <section>\n"
  "    <item label='Bold' action='bold'/>\n"
  "    <submenu label='Language'>\n"
  "      <item label='Latin' action='lang' target='latin'/>\n"
  "      <item label='Greek' action='lang' target='greek'/>\n"
  "      <item label='Urdu'  action='lang' target='urdu'/>\n"
  "    </submenu>\n"
  "  </section>\n"
  "</menu>\n";

static void
start_element (GMarkupParseContext *context,
               const gchar         *element_name,
               const gchar        **attribute_names,
               const gchar        **attribute_values,
               gpointer             user_data,
               GError             **error)
{
  if (strcmp (element_name, "menu") == 0)
    g_menu_markup_parser_start_menu (context, NULL);
}

static void
end_element (GMarkupParseContext *context,
             const gchar         *element_name,
             gpointer             user_data,
             GError             **error)
{
  GMenu **menu = user_data;

  if (strcmp (element_name, "menu") == 0)
    *menu = g_menu_markup_parser_end_menu (context);
}

static const GMarkupParser parser = {
   start_element, end_element, NULL, NULL, NULL
};

static GMenuModel *
get_model (void)
{
  GMarkupParseContext *context;
  GMenu *menu = NULL;
  GError *error = NULL;

  context = g_markup_parse_context_new (&parser, 0, &menu, NULL);
  if (!g_markup_parse_context_parse (context, menu_markup, -1, &error))
    {
       g_warning ("menu parsing failed: %s\n", error->message);
       exit (1);
    }
  g_markup_parse_context_free (context);
  g_assert (menu);

   return G_MENU_MODEL (menu);
}

/* The example actions {{{1 */

static void
activate_action (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  g_print ("Action %s activated\n", g_action_get_name (G_ACTION (action)));
}

static void
toggle_changed (GSimpleAction *action, GVariant *value, gpointer user_data)
{
  g_print ("Toggle action %s state changed to %d\n",
           g_action_get_name (G_ACTION (action)),
           g_variant_get_boolean (value));

  g_simple_action_set_state (action, value);
}

static void
radio_changed (GSimpleAction *action, GVariant *value, gpointer user_data)
{
  g_print ("Radio action %s state changed to %s\n",
           g_action_get_name (G_ACTION (action)),
           g_variant_get_string (value, NULL));

  g_simple_action_set_state (action, value);
}

static GActionEntry actions[] = {
  { "undo",  activate_action, NULL, NULL,      NULL },
  { "redo",  activate_action, NULL, NULL,      NULL },
  { "cut",   activate_action, NULL, NULL,      NULL },
  { "copy",  activate_action, NULL, NULL,      NULL },
  { "paste", activate_action, NULL, NULL,      NULL },
  { "bold",  NULL,            NULL, "true",    toggle_changed },
  { "lang",  NULL,            NULL, "'latin'", radio_changed },
};

static GActionGroup *
get_group (void)
{
  GSimpleActionGroup *group;

  group = g_simple_action_group_new ();

  g_simple_action_group_add_entries (group, actions, G_N_ELEMENTS (actions), NULL);

  return G_ACTION_GROUP (group);
}
 
/* The action treeview {{{1 */

static void
enabled_cell_func (GtkTreeViewColumn *column,
                   GtkCellRenderer   *cell,
                   GtkTreeModel      *model,
                   GtkTreeIter       *iter,
                   gpointer           data)
{
  GActionGroup *group = data;
  gchar *name;
  gboolean enabled;

  gtk_tree_model_get (model, iter, 0, &name, -1);
  enabled = g_action_group_get_action_enabled (group, name);
  g_free (name);

  gtk_cell_renderer_toggle_set_active (GTK_CELL_RENDERER_TOGGLE (cell), enabled);
}

static void
state_cell_func (GtkTreeViewColumn *column,
                 GtkCellRenderer   *cell,
                 GtkTreeModel      *model,
                 GtkTreeIter       *iter,
                 gpointer           data)
{
  GActionGroup *group = data;
  gchar *name;
  GVariant *state;

  gtk_tree_model_get (model, iter, 0, &name, -1);
  state = g_action_group_get_action_state (group, name);
  g_free (name);

  gtk_cell_renderer_set_visible (cell, FALSE);
  g_object_set (cell, "mode", GTK_CELL_RENDERER_MODE_INERT, NULL);

  if (state &&
      g_variant_is_of_type (state, G_VARIANT_TYPE_BOOLEAN) &&
      GTK_IS_CELL_RENDERER_TOGGLE (cell))
    {
      gtk_cell_renderer_set_visible (cell, TRUE);
      g_object_set (cell, "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE, NULL);
      gtk_cell_renderer_toggle_set_active (GTK_CELL_RENDERER_TOGGLE (cell),
                                           g_variant_get_boolean (state));
    }
  else if (state &&
           g_variant_is_of_type (state, G_VARIANT_TYPE_STRING) &&
           GTK_IS_CELL_RENDERER_COMBO (cell))
    {
      gtk_cell_renderer_set_visible (cell, TRUE);
      g_object_set (cell, "mode", GTK_CELL_RENDERER_MODE_EDITABLE, NULL);
      g_object_set (cell, "text", g_variant_get_string (state, NULL), NULL);
    }

  if (state)
    g_variant_unref (state);
}

static void
enabled_cell_toggled (GtkCellRendererToggle *cell,
                      const gchar           *path_str,
                      GtkTreeModel          *model)
{
  GActionGroup *group;
  GAction *action;
  gchar *name;
  GtkTreePath *path;
  GtkTreeIter iter;
  gboolean enabled;

  group = g_object_get_data (G_OBJECT (model), "group");
  path = gtk_tree_path_new_from_string (path_str);
  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter, 0, &name, -1);

  enabled = g_action_group_get_action_enabled (group, name);
  action = g_simple_action_group_lookup (G_SIMPLE_ACTION_GROUP (group), name);
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), !enabled);

  gtk_tree_model_row_changed (model, path, &iter);

  g_free (name);
  gtk_tree_path_free (path);
}

static void
state_cell_toggled (GtkCellRendererToggle *cell,
                    const gchar           *path_str,
                    GtkTreeModel          *model)
{
  GActionGroup *group;
  GAction *action;
  gchar *name;
  GtkTreePath *path;
  GtkTreeIter iter;
  GVariant *state;

  group = g_object_get_data (G_OBJECT (model), "group");
  path = gtk_tree_path_new_from_string (path_str);
  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter, 0, &name, -1);

  state = g_action_group_get_action_state (group, name);
  action = g_simple_action_group_lookup (G_SIMPLE_ACTION_GROUP (group), name);
  if (state && g_variant_is_of_type (state, G_VARIANT_TYPE_BOOLEAN))
    {
      gboolean b;

      b = g_variant_get_boolean (state);
      g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (!b));
    }
  else
    {
      /* nothing to do */
    }

  gtk_tree_model_row_changed (model, path, &iter);

  g_free (name);
  gtk_tree_path_free (path);
  if (state)
    g_variant_unref (state);
}

static void
state_cell_edited (GtkCellRendererCombo  *cell,
                   const gchar           *path_str,
                   const gchar           *new_text,
                   GtkTreeModel          *model)
{
  GActionGroup *group;
  GAction *action;
  gchar *name;
  GtkTreePath *path;
  GtkTreeIter iter;

  group = g_object_get_data (G_OBJECT (model), "group");
  path = gtk_tree_path_new_from_string (path_str);
  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter, 0, &name, -1);
  action = g_simple_action_group_lookup (G_SIMPLE_ACTION_GROUP (group), name);
  g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_string (new_text));

  gtk_tree_model_row_changed (model, path, &iter);

  g_free (name);
  gtk_tree_path_free (path);
}

static GtkWidget *
create_action_treeview (GActionGroup *group)
{
  GtkWidget *tv;
  GtkListStore *store;
  GtkListStore *values;
  GtkTreeIter iter;
  GtkTreeViewColumn *column;
  GtkCellRenderer *cell;
  gchar **actions;
  gint i;

  store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
  actions = g_action_group_list_actions (group);
  for (i = 0; actions[i]; i++)
    {
      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, actions[i], -1);
    }
  g_strfreev (actions);
  g_object_set_data (G_OBJECT (store), "group", group);

  tv = gtk_tree_view_new ();

  g_signal_connect_swapped (group, "action-enabled-changed",
                            G_CALLBACK (gtk_widget_queue_draw), tv);
  g_signal_connect_swapped (group, "action-state-changed",
                            G_CALLBACK (gtk_widget_queue_draw), tv);

  gtk_tree_view_set_model (GTK_TREE_VIEW (tv), GTK_TREE_MODEL (store));

  cell = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes ("Action", cell,
                                                     "text", 0,
                                                     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tv), column);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column, "Enabled");
  cell = gtk_cell_renderer_toggle_new ();
  gtk_tree_view_column_pack_start (column, cell, FALSE);
  gtk_tree_view_column_set_cell_data_func (column, cell, enabled_cell_func, group, NULL);
  g_signal_connect (cell, "toggled", G_CALLBACK (enabled_cell_toggled), store);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tv), column);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column, "State");
  cell = gtk_cell_renderer_toggle_new ();
  gtk_tree_view_column_pack_start (column, cell, FALSE);
  gtk_tree_view_column_set_cell_data_func (column, cell, state_cell_func, group, NULL);
  g_signal_connect (cell, "toggled", G_CALLBACK (state_cell_toggled), store);
  cell = gtk_cell_renderer_combo_new ();
  values = gtk_list_store_new (1, G_TYPE_STRING);
  gtk_list_store_append (values, &iter);
  gtk_list_store_set (values, &iter, 0, "latin", -1);
  gtk_list_store_append (values, &iter);
  gtk_list_store_set (values, &iter, 0, "greek", -1);
  gtk_list_store_append (values, &iter);
  gtk_list_store_set (values, &iter, 0, "urdu", -1);
  gtk_list_store_append (values, &iter);
  gtk_list_store_set (values, &iter, 0, "sumerian", -1);
  g_object_set (cell,
                "has-entry", FALSE,
                "model", values,
                "text-column", 0,
                "editable", TRUE,
                NULL);
  gtk_tree_view_column_pack_start (column, cell, FALSE);
  gtk_tree_view_column_set_cell_data_func (column, cell, state_cell_func, group, NULL);
  g_signal_connect (cell, "edited", G_CALLBACK (state_cell_edited), store);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tv), column);

  return tv;
}

/* Dynamic menu changes {{{1 */

static void
toggle_sumerian (GtkToggleButton *button, gpointer data)
{
  GMenuModel *model;
  gboolean adding;
  GMenuModel *m;

  model = g_object_get_data (G_OBJECT (button), "model");

  adding = gtk_toggle_button_get_active (button);

  m = g_menu_model_get_item_link (model, g_menu_model_get_n_items (model) - 1, G_MENU_LINK_SECTION);
  m = g_menu_model_get_item_link (m, g_menu_model_get_n_items (m) - 1, G_MENU_LINK_SUBMENU);
  if (adding)
    g_menu_append (G_MENU (m), "Sumerian", "lang::sumerian");
  else
    g_menu_remove (G_MENU (m), g_menu_model_get_n_items (m) - 1);
}

static void
toggle_italic (GtkToggleButton *button, gpointer data)
{
  GMenuModel *model;
  GActionGroup *group;
  GSimpleAction *action;
  gboolean adding;
  GMenuModel *m;
  GtkTreeView *tv = data;
  GtkTreeModel *store;
  GtkTreeIter iter;

  model = g_object_get_data (G_OBJECT (button), "model");
  group = g_object_get_data (G_OBJECT (button), "group");

  store = gtk_tree_view_get_model (tv);

  adding = gtk_toggle_button_get_active (button);

  m = g_menu_model_get_item_link (model, g_menu_model_get_n_items (model) - 1, G_MENU_LINK_SECTION);
  if (adding)
    {
      action = g_simple_action_new_stateful ("italic", NULL, g_variant_new_boolean (FALSE));
      g_simple_action_group_insert (G_SIMPLE_ACTION_GROUP (group),
                                    G_ACTION (action));
      g_object_unref (action);
      g_menu_insert (G_MENU (m), 1, "Italic", "italic");
      gtk_list_store_prepend (GTK_LIST_STORE (store), &iter);
      gtk_list_store_set (GTK_LIST_STORE (store), &iter,
                          0, "italic",
                          -1);
    }
  else
    {
      g_simple_action_group_remove (G_SIMPLE_ACTION_GROUP (group), "italic");
      g_menu_remove (G_MENU (m), 1);
      gtk_tree_model_get_iter_first (store, &iter);
      gtk_list_store_remove (GTK_LIST_STORE (store), &iter);
    }
}

static GtkWidget *
create_add_remove_buttons (GActionGroup *group,
                           GMenuModel   *model,
                           GtkWidget    *treeview)
{
  GtkWidget *box;
  GtkWidget *button;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

  button = gtk_check_button_new_with_label ("Add Italic");
  gtk_container_add (GTK_CONTAINER (box), button);

  g_object_set_data  (G_OBJECT (button), "group", group);
  g_object_set_data  (G_OBJECT (button), "model", model);

  g_signal_connect (button, "toggled",
                    G_CALLBACK (toggle_italic), treeview);

  button = gtk_check_button_new_with_label ("Add Sumerian");
  gtk_container_add (GTK_CONTAINER (box), button);

  g_object_set_data  (G_OBJECT (button), "group", group);
  g_object_set_data  (G_OBJECT (button), "model", model);

  g_signal_connect (button, "toggled",
                    G_CALLBACK (toggle_sumerian), NULL);

  return box;
}

/* The menu button {{{1 */

static void
button_clicked (GtkButton *button, gpointer data)
{
  GMenuModel *model;
  GActionGroup *group;
  GtkWidget *menu;

  menu = g_object_get_data (G_OBJECT (button), "menu");
  if (!menu)
    {
      model = g_object_get_data (G_OBJECT (button), "model");
      group = g_object_get_data (G_OBJECT (button), "group");
      menu = create_menu_from_model (model, group);
      g_object_set_data_full (G_OBJECT (button), "menu", menu, (GDestroyNotify)gtk_widget_destroy);
    }

  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 0, 0);
}

static void
items_changed (GMenuModel *model,
               gint        position,
               gint        removed,
               gint        added,
               GtkButton  *button)
{
  g_object_set_data (G_OBJECT (button), "menu", NULL);
}

static GtkWidget *
create_menu_button (GMenuModel *model, GActionGroup *group)
{
  GtkWidget *button;

  button = gtk_button_new_with_label ("Click here");
  g_object_set_data (G_OBJECT (button), "model", model);
  g_object_set_data (G_OBJECT (button), "group", group);

  g_signal_connect (button, "clicked", G_CALLBACK (button_clicked), NULL);
  g_signal_connect (model, "items-changed", G_CALLBACK (items_changed), button);

  return button;
}

/* main {{{1 */

int
main (int argc, char *argv[])
{
  GtkWidget *window;
  GtkWidget *box;
  GtkWidget *button;
  GtkWidget *tv;
  GtkWidget *buttons;
  GMenuModel *model;
  GActionGroup *group;
  GDBusConnection *bus;
  GError *error = NULL;
  gboolean do_export = FALSE;
  gboolean do_import = FALSE;
  GOptionEntry entries[] = {
    { "export", 0, 0, G_OPTION_ARG_NONE, &do_export, "Export actions and menus over D-Bus", NULL },
    { "import", 0, 0, G_OPTION_ARG_NONE, &do_import, "Use exported actions and menus", NULL },
    { NULL, }
  };

  gtk_init_with_args (&argc, &argv, NULL, entries, NULL, NULL);

  if (do_export && do_import)
    {
       g_error ("can't have it both ways\n");
       exit (1);
    }

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_add (GTK_CONTAINER (window), box);

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);

  if (do_import)
    {
      g_print ("Getting menus from the bus...\n");
      model = (GMenuModel*)g_menu_proxy_get (bus, "org.gtk.TestMenus", "/path");
      g_print ("Getting actions from the bus...\n");
      group = (GActionGroup*)g_dbus_action_group_new_sync (bus, "org.gtk.TestMenus", "/path", 0, NULL, NULL);
    }
  else
    {
      group = get_group ();
      model = get_model ();

      tv = create_action_treeview (group);
      gtk_container_add (GTK_CONTAINER (box), tv);
      buttons = create_add_remove_buttons (group, model, tv);
      gtk_container_add (GTK_CONTAINER (box), buttons);
    }

  if (do_export)
    {
      g_print ("Exporting menus on the bus...\n");
      if (!g_menu_exporter_export (bus, "/path", model, &error))
        {
          g_warning ("Menu export failed: %s", error->message);
          exit (1);
        }
      g_print ("Exporting actions on the bus...\n");
      if (!g_action_group_exporter_export (bus, "/path", group, &error))
        {
          g_warning ("Action export failed: %s", error->message);
          exit (1);
        }
      g_bus_own_name_on_connection (bus, "org.gtk.TestMenus",
                                    0, NULL, NULL, NULL, NULL);
    }
  else
    {
      button = create_menu_button (model, group);
      gtk_container_add (GTK_CONTAINER (box), button);
    }

  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}

/* Epilogue {{{1 */
/* vim:set foldmethod=marker: */