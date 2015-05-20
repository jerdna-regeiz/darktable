/*
 This file is part of darktable,
 copyright (c) 2009--2010 johannes hanika.
 copyright (c) 2010--2011 henrik andersson.

 darktable is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 darktable is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with darktable.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sstream>
#include <iostream>
#include <vector>
/**
 * Most of darktable is pure c
 * */
extern "C" {
#include "common/darktable.h"
#include "develop/imageop.h"
#include "common/collection.h"
#include "common/selection.h"
#include "common/image.h"
#include "common/history.h"
#include "common/debug.h"
#include "control/control.h"
#include "control/conf.h"
#include "libs/lib.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include <gdk/gdkkeysyms.h>
#include "dtgtk/button.h"
#include "common/file_location.h"
#include "gui/presets.h"
#include "develop/blend.h"
}

using namespace std;
/**
 * Make the module interface available as c functions
 * */
extern "C" {

DT_MODULE(1)
/** returns the readable name of the module shown in the interface */
const char *name();
/** returns which views shall contain this module */
uint32_t views();
/** returns which container this module is to be placed in the gui */
uint32_t container();
/** returns the positioning of the module */
int position();
/** is called to initialize the gui elements of the module
 * @param self pointer to the dt_lib_module_t struct belonging to this module
 * */
void gui_init(dt_lib_module_t *self);
/** cleans up any structes when gui is done
 * @param self pointer to the dt_lib_module_t struct belonging to this module
 * */
void gui_cleanup(dt_lib_module_t *self);

}

#define COL_WIDTH 4

const char *name() {
  return _("favorites");
}

uint32_t views() {
  return DT_VIEW_LIGHTTABLE;
}

uint32_t container() {
  return DT_UI_CONTAINER_PANEL_RIGHT_CENTER;
}

int position() {
  return 590;
}

typedef struct dt_lib_favoritestoggle_t{
} dt_lib_favoritestoggle_t;

/** open a namespace for this module */
namespace dt {
namespace favoritestoggle{
  
  static const uint8_t fallback_pixel[4] = { 0, 0, 0, 0 };
  /** loads the pixbuf of an image from the given path
   * @param path the path to the file
   * @param size the size^2 the pixbuf shall have
   * */
  static GdkPixbuf *load_image(const char *path, int size  )
  {
    GError *error = NULL;
    if(!g_file_test(path, G_FILE_TEST_IS_REGULAR)) return NULL;

    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_size(path, size, size, &error);
    if(!pixbuf)
    {
      fprintf(stderr, "error loading file `%s': %s\n", path, error->message);
      g_error_free(error);
    }
    return pixbuf;
  }
  
  /** returns the pixbuf of the icon belonging to a given module
   * it will try .svg as well as .png and falls back to template.svg/.png
   * @param module the module of which the icon shall be loaded
   * */
  GdkPixbuf* get_module_icon_pixbuf(dt_iop_module_so_t *module) {
    int bs = DT_PIXEL_APPLY_DPI(12);
    /* add module icon */
    GdkPixbuf *pixbuf;
    char filename[PATH_MAX] = { 0 };
    char datadir[PATH_MAX] = { 0 };
    //dt_loc_get_datadir(datadir, sizeof(datadir));
    snprintf(datadir, sizeof(datadir), "%s", darktable.datadir);

    // make the icons a little more visible
    #define ICON_SIZE (bs * 1.7)

    // Try to get an icon specific to the module (prefer svg) or else fall back to the template
    snprintf(filename, sizeof(filename), "%s/pixmaps/plugins/darkroom/%s.svg", datadir, module->op);
    pixbuf = load_image(filename, ICON_SIZE);
    if(pixbuf) return pixbuf;

    snprintf(filename, sizeof(filename), "%s/pixmaps/plugins/darkroom/%s.png", datadir, module->op);
    pixbuf = load_image(filename, ICON_SIZE);
    if(pixbuf) return pixbuf;

    snprintf(filename, sizeof(filename), "%s/pixmaps/plugins/darkroom/template.svg", datadir);
    pixbuf = load_image(filename, ICON_SIZE);
    if(pixbuf) return pixbuf;

    snprintf(filename, sizeof(filename), "%s/pixmaps/plugins/darkroom/template.png", datadir);
    pixbuf = load_image(filename, ICON_SIZE);
    if(pixbuf) return pixbuf;

    #undef ICON_SIZE

    // wow, we could neither load the SVG nor the PNG files. something is fucked up.
    pixbuf = gdk_pixbuf_new_from_data(fallback_pixel, GDK_COLORSPACE_RGB, TRUE, 8, 1, 1, 4, NULL, NULL);
    return pixbuf;
  }

  /** Returns an icon as an GtkWidget for the given module
   * @param module the module to fetch the icon for
   **/
  GtkWidget *get_module_gtk_icon(dt_iop_module_so_t *module) {
    int bs = DT_PIXEL_APPLY_DPI(12);
    GdkPixbuf *pixbuf = get_module_icon_pixbuf(module);
    GtkWidget *wdg = gtk_image_new_from_pixbuf(pixbuf);
    gtk_widget_set_margin_start(GTK_WIDGET(wdg), DT_PIXEL_APPLY_DPI(5));
    gtk_widget_set_size_request(GTK_WIDGET(wdg), bs, bs);
    g_object_unref(pixbuf);
    return wdg;
  }

  /** Toggles the state of the module to the value of active
   * @param module the module to be toggled
   * @parem active if true enabled the module, else disabled it
   **/
  static void toggle_module(dt_iop_module_t* module, gboolean active)
  {
    if(!darktable.gui->reset)
    {
      if(active)
        module->enabled = 1;
      else
        module->enabled = 0;
      dt_dev_add_history_item(module->dev, module, active);
    }
  }

  static dt_develop_t* load_develop(int imgid) {
    dt_develop_t *dev = new dt_develop_t();
    dt_dev_init(dev, TRUE);
    dt_dev_load_image(dev, imgid);
    dt_iop_load_modules(dev);
    return dev;
  }

  static dt_iop_module_t* get_module_instance(dt_develop_t* dev, dt_iop_module_so_t *module) {
    GList *modules = dev->iop;
    if(modules) {
      do{
        dt_iop_module_t *iop = (dt_iop_module_t *) modules->data;
        if( iop->so == module ){
          return iop;
        }

      } while( modules = g_list_next(modules) );
    }
    return NULL;
  }

  static void dt_save_and_unload(dt_develop_t *dev, int imgid, gboolean duplicate) {
    dt_dev_write_history(dev);

    /* if current image in develop reload history */
    if(dt_dev_is_current_image(darktable.develop, imgid))
    {
      dt_dev_reload_history_items(darktable.develop);
      dt_dev_modulegroups_set(darktable.develop, dt_dev_modulegroups_get(darktable.develop));
    }

    /* update xmp file */
    dt_image_synch_xmp(imgid);
    /* remove old obsolete thumbnails */
    dt_mipmap_cache_remove(darktable.mipmap_cache, imgid);
    /* if we have created a duplicate, reset collected images */
    if(duplicate) dt_control_signal_raise(darktable.signals, DT_SIGNAL_COLLECTION_CHANGED);
    /* redraw center view to update visible mipmaps */
    dt_control_queue_redraw_center();

    // cleanup! free modules and develop
    dt_dev_cleanup(dev);
  }

  static void dt_togglefavorite_on_image(dt_iop_module_so_t *module, gboolean duplicate, int imgid, gboolean activate) {
    int newimgid = -1;
    if(duplicate)
    {
      newimgid = dt_image_duplicate(imgid);
      if(newimgid != -1) dt_history_copy_and_paste_on_image(imgid, newimgid, FALSE, NULL);
    } else {
      newimgid = imgid;
    }

    dt_develop_t *dev = new dt_develop_t();
    dt_dev_init(dev, TRUE);
    dt_dev_load_image(dev, newimgid);
    dt_iop_load_modules(dev);
    GList *modules = dev->iop;
    if(modules) {
      do{
        dt_iop_module_t *iop = (dt_iop_module_t *) modules->data;
        if( iop->so == module ){
          toggle_module(iop, activate);
        }

      } while( modules = g_list_next(modules) );
    }
    dt_dev_write_history(dev);

    /* if current image in develop reload history */
    if(dt_dev_is_current_image(darktable.develop, newimgid))
    {
      dt_dev_reload_history_items(darktable.develop);
      dt_dev_modulegroups_set(darktable.develop, dt_dev_modulegroups_get(darktable.develop));
    }

    /* update xmp file */
    dt_image_synch_xmp(newimgid);
    /* remove old obsolete thumbnails */
    dt_mipmap_cache_remove(darktable.mipmap_cache, newimgid);
    /* if we have created a duplicate, reset collected images */
    if(duplicate) dt_control_signal_raise(darktable.signals, DT_SIGNAL_COLLECTION_CHANGED);
    /* redraw center view to update visible mipmaps */
    dt_control_queue_redraw_center();

    // cleanup! free modules and develop
    dt_dev_cleanup(dev);
  }

  static vector<int> get_selected_imgs() {
    vector<int> imgs;
    sqlite3_stmt *stmt;
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select * from selected_images", -1, &stmt, NULL);
    while(sqlite3_step(stmt) == SQLITE_ROW)
    {
      imgs.push_back( sqlite3_column_int(stmt, 0) );
    }
    sqlite3_finalize(stmt);
    return imgs;
  }

  static void dt_togglefavorite_on_selection(dt_iop_module_so_t* module, gboolean duplicate) {
    gboolean selected = FALSE;
    gboolean enabled = FALSE;

    sqlite3_stmt *stmt;
    // Select the count of images selected already having the module enabled
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
        "select count(*) from history "
        "WHERE imgid in (select * from selected_images) "
        "AND operation = ?1 AND enabled = 1", -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, module->op, -1, SQLITE_TRANSIENT);
    unsigned int enabled_count = 0;
    if(sqlite3_step(stmt) == SQLITE_ROW) {
      enabled_count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    // get the count of selected images in total
    uint32_t selected_count = dt_collection_get_selected_count(darktable.collection);

    // enable, if the count of enabled images is less than the count of selected images
    enabled = enabled_count < selected_count;
    /* for each selected image apply style */
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select * from selected_images", -1, &stmt, NULL);
    while(sqlite3_step(stmt) == SQLITE_ROW)
    {
      int imgid = sqlite3_column_int(stmt, 0);
      dt_togglefavorite_on_image(module, duplicate, imgid, enabled );
      selected = TRUE;
    }
    sqlite3_finalize(stmt);

    if(!selected) dt_control_log(_("no image selected!"));
  }

  
  static gchar *get_preset_name(GtkMenuItem *menuitem)
  {
    const gchar *name = gtk_label_get_label(GTK_LABEL(gtk_bin_get_child(GTK_BIN(menuitem))));
    const gchar *c = name;
  
    // move to marker < if it exists
    while(*c && *c != '<') c++;
    if(!*c) c = name;
  
    // remove <-> markup tag at beginning.
    if(*c == '<')
    {
      while(*c != '>') c++;
      c++;
    }
    gchar *pn = g_strdup(c);
    gchar *c2 = pn;
    // possibly remove trailing <-> markup tag
    while(*c2 != '<' && *c2 != '\0') c2++;
    if(*c2 == '<') *c2 = '\0';
    c2 = g_strrstr(pn, _("(default)"));
    if(c2 && c2 > pn) *(c2 - 1) = '\0';
    return pn;
  }

  static void apply_preset_on_image(gchar *preset, dt_iop_module_t *module) {
    sqlite3_stmt *stmt;
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                                "select op_params, enabled, blendop_params, blendop_version, writeprotect from "
                                "presets where operation = ?1 and op_version = ?2 and name = ?3",
                                -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, module->op, -1, SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, module->version());
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, preset, -1, SQLITE_TRANSIENT);
  
    if(sqlite3_step(stmt) == SQLITE_ROW)
    {
      const void *op_params = sqlite3_column_blob(stmt, 0);
      int op_length = sqlite3_column_bytes(stmt, 0);
      int enabled = sqlite3_column_int(stmt, 1);
      const void *blendop_params = sqlite3_column_blob(stmt, 2);
      int bl_length = sqlite3_column_bytes(stmt, 2);
      int blendop_version = sqlite3_column_int(stmt, 3);
      int writeprotect = sqlite3_column_int(stmt, 4);
      if(op_params && (op_length == module->params_size))
      {
        memcpy(module->params, op_params, op_length);
        module->enabled = enabled;
      }
      if(blendop_params && (blendop_version == dt_develop_blend_version())
         && (bl_length == sizeof(dt_develop_blend_params_t)))
      {
        memcpy(module->blend_params, blendop_params, sizeof(dt_develop_blend_params_t));
      }
      else if(blendop_params
              && dt_develop_blend_legacy_params(module, blendop_params, blendop_version, module->blend_params,
                                                dt_develop_blend_version(), bl_length) == 0)
      {
        // do nothing
      }
      else
      {
        memcpy(module->blend_params, module->default_blendop_params, sizeof(dt_develop_blend_params_t));
      }
  
      if(!writeprotect) dt_gui_store_last_preset(preset);
    }
    sqlite3_finalize(stmt);
    // Always enable the module, after a preset has been choosen
    toggle_module(module, TRUE);
  }

  static void apply_preset_on_image(gchar *preset, dt_iop_module_so_t *module, int imgid, gboolean duplicate) {
    // Create a duplicate, if requested
    int newimgid = -1;
    if(duplicate)
    {
      newimgid = dt_image_duplicate(imgid);
      if(newimgid != -1) dt_history_copy_and_paste_on_image(imgid, newimgid, FALSE, NULL);
    }else{
      newimgid = imgid;
    }
    if( newimgid == -1 ){
      dt_control_log(_("illegal imgid (duplicate failed?)!"));
      return;
    }

    // get dt_iop_module_t instance
    dt_develop_t* dev = load_develop(newimgid);
    dt_iop_module_t* module_inst = get_module_instance(dev, module);
    // apply preset
    apply_preset_on_image(preset, module_inst);
    // unload develop
    dt_save_and_unload(dev, newimgid, duplicate);
  }

  static void apply_preset_on_selection(gchar *preset, dt_iop_module_so_t *module, gboolean duplicate) {
    vector<int> imgs = get_selected_imgs();
    for( vector<int>::iterator img = imgs.begin(); img != imgs.end(); ++img) {
      apply_preset_on_image(preset, module, *img, duplicate);
    }
  }

  //TODO: have two different callbacks for ctrl/duplicate or not
  static void menuitem_pick_preset(GtkMenuItem *menuitem, dt_iop_module_so_t *module) {
    gchar *preset = get_preset_name(menuitem);
    cout << "Preset: " <<  preset << endl;
    apply_preset_on_selection(preset, module, false);
    g_free(preset);
  }

  GtkMenu* get_preset_popup_menu() {
    // Create a new menu, destroying the old one if present
    GtkMenu *menu = darktable.gui->presets_popup_menu;
    if(menu) gtk_widget_destroy(GTK_WIDGET(menu));
    darktable.gui->presets_popup_menu = GTK_MENU(gtk_menu_new());
    menu = darktable.gui->presets_popup_menu;
    return menu;
  }

  /** Called to show a popup menu of the available presets */
  void prepare_presets_popup_menu(dt_iop_module_so_t* module) {
    GtkMenu *menu = get_preset_popup_menu();
  
    GtkWidget *mi;
    sqlite3_stmt *stmt;
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select name, op_params, writeprotect, "
                                                               "description, blendop_params, op_version, "
                                                               "enabled from presets where operation=?1 "
                                                               "order by writeprotect desc, name, rowid",
                                -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, module->op, -1, SQLITE_TRANSIENT);

    //int found = 0;
    int cnt = 0;
    while(sqlite3_step(stmt) == SQLITE_ROW)
    {
      // Read the row/preset
      //void *op_params = (void *)sqlite3_column_blob(stmt, 1);
      //int32_t op_params_size = sqlite3_column_bytes(stmt, 1);
      //void *blendop_params = (void *)sqlite3_column_blob(stmt, 4);
      //int32_t bl_params_size = sqlite3_column_bytes(stmt, 4);
      int32_t preset_version = sqlite3_column_int(stmt, 5);
      //int32_t enabled = sqlite3_column_int(stmt, 6);
      //int32_t isdefault = 0;
      int32_t isdisabled = (preset_version == module->version() ? 0 : 1);
      const char *name = (char *)sqlite3_column_text(stmt, 0);
      // See if the current preset was the last used
      //if(darktable.gui->last_preset && strcmp(darktable.gui->last_preset, name) == 0) found = 1;
      
      //TODO: I abadoned showing default and current preset. default because I don't have an instance yet and preset
      // becuse we might have multiple images selected. Could manage to compare all if everything is equal ...
      // Both could be done thought, but I leave it for later
      
      // Use the preset name for a lable
      mi = gtk_menu_item_new_with_label((const char *)name);
  
      if(isdisabled)
      {
        gtk_widget_set_sensitive(mi, 0);
        g_object_set(G_OBJECT(mi), "tooltip-text", _("disabled: wrong module version"), (char *)NULL);
      }
      else
      {
        g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_pick_preset), module);
        g_object_set(G_OBJECT(mi), "tooltip-text", sqlite3_column_text(stmt, 3), (char *)NULL);
      }
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
      cnt++;
    }
    sqlite3_finalize(stmt);

    if( cnt <= 0 ) {
      //If there is no entry, show "no presets"
      mi = gtk_menu_item_new_with_label(_("no presets"));
      gtk_widget_set_sensitive(mi, 0);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }
  }

  static void _preset_popup_position(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer data)
  {
    GtkRequisition requisition = { 0 };
    gdk_window_get_origin(gtk_widget_get_window(GTK_WIDGET(data)), x, y);
    gtk_widget_get_preferred_size(GTK_WIDGET(menu), &requisition, NULL);
  
    GtkAllocation allocation;
    gtk_widget_get_allocation(GTK_WIDGET(data), &allocation);

    (*y) += allocation.y + allocation.height;
    (*x) += allocation.x - ( MAX(requisition.width - allocation.width, 0) );

  }

  static void popup_callback(GtkButton *button, dt_iop_module_so_t *module)
  {
    prepare_presets_popup_menu(module);
    gtk_menu_popup(darktable.gui->presets_popup_menu, NULL, NULL, _preset_popup_position, button, 0,
                   gtk_get_current_event_time());
    gtk_widget_show_all(GTK_WIDGET(darktable.gui->presets_popup_menu));
    gtk_menu_reposition(GTK_MENU(darktable.gui->presets_popup_menu));
  }

  /** Gets called when a favorites button gets pressed */
  static int toggle_module_button_pressed(GtkWidget *widget, GdkEventButton *event, dt_iop_module_so_t *module)
  {
    if(darktable.gui->reset) return FALSE;
    //GdkModifierType modifiers = gtk_accelerator_get_default_mod_mask();
    switch(event->button) {
      case 1:
      default:
        dt_togglefavorite_on_selection(module, event->state & GDK_CONTROL_MASK);
      break;
      case 3:
        popup_callback(GTK_BUTTON(widget), module);
      break;
    }
    return TRUE;
  }
}
}

using namespace dt::favoritestoggle;

void gui_init(dt_lib_module_t *self) {
  dt_lib_favoritestoggle_t *d = (dt_lib_favoritestoggle_t *) malloc(sizeof(dt_lib_favoritestoggle_t) );
  self->data = d;
  self->widget = gtk_grid_new();
  GtkGrid *grid = GTK_GRID(self->widget);
  gtk_grid_set_row_spacing(grid, DT_PIXEL_APPLY_DPI(5));
  gtk_grid_set_column_spacing(grid, DT_PIXEL_APPLY_DPI(5));
  gtk_grid_set_column_homogeneous(grid, TRUE);
  int line = 0;
  GtkWidget *button;

  char option[1024];
  GList *modules = darktable.iop;
  if (modules) {
    /*
     * iterate over ip modules and detect if the modules should be shown
     */
    do {
      dt_iop_module_so_t *module = (dt_iop_module_so_t *) modules->data;

      snprintf(option, sizeof(option), "plugins/darkroom/%s/favorite",
          module->op);
      int fav = dt_conf_get_bool(option);
      if (fav) {
        stringstream tooltip;
        tooltip << "Toggle " << module->name() << " (ctrl+click for duplicate)";
        button = gtk_button_new();
        gtk_container_add((GtkContainer*)button, get_module_gtk_icon(module));
        g_object_set(G_OBJECT(button), "tooltip-text", tooltip.str().c_str(), (char *) NULL);
        gtk_grid_attach(grid, button, line % COL_WIDTH, line / COL_WIDTH, 1, 1);
        ++line;


        g_signal_connect(G_OBJECT(button),  "button-press-event",
            G_CALLBACK(toggle_module_button_pressed), module);
      }
    } while ((modules = g_list_next(modules)) != NULL);
  }
}

void gui_cleanup(dt_lib_module_t *self) {
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
