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


  static int toggle_module_button_pressed(GtkWidget *widget, GdkEventButton *event, dt_iop_module_so_t *module)
  {
    if(darktable.gui->reset) return FALSE;
    //GdkModifierType modifiers = gtk_accelerator_get_default_mod_mask();

    dt_togglefavorite_on_selection(module, event->state & GDK_CONTROL_MASK);
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
  GList *modules = darktable.iop
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
