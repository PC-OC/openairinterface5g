/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief: primitives replacing xforms when compiling nr_phy_scope.c for the webserver shared lib
 */
#include <stdlib.h>
#include <jansson.h>
#include <ulfius.h>
#include "common/utils/LOG/log.h"
#include "common/utils/websrv/websrv.h"
#include "common/utils/websrv/websrv_noforms.h"
#include <common/utils/assertions.h>


#define MAX_LAYER 8

/* replacement for xforms function used in xforms softscope */
typedef struct {
  websrv_scopedata_msg_t *buf;
} websrv_scopedata_layermsgs_t;

typedef struct {
  char *title; /* overall title                */
  websrv_scopedata_layermsgs_t buff[MAX_LAYER]; /*  buff per layer */
  int n[MAX_LAYER]; /* total points */
} FLI_XYPLOT_SPEC;

FL_OBJECT *websrv_fl_add_xyplot(int t, FL_Coord x, FL_Coord y, FL_Coord w, FL_Coord h, const char *label)
{
  FL_OBJECT *obj;

  obj = (FL_OBJECT *)calloc(1, sizeof(FL_OBJECT));
  AssertFatal((obj != NULL), "Cannot allocate scope object for %s\n", (label!=NULL)?label:"??");
  if (obj) {
    obj->spec = (FLI_XYPLOT_SPEC *)calloc(1, sizeof(FLI_XYPLOT_SPEC));
    AssertFatal((obj->spec != NULL), "Cannot allocate scope object buffers for %s\n",(label!=NULL)?label:"??");
    FLI_XYPLOT_SPEC *spec = (FLI_XYPLOT_SPEC *)obj->spec;
    for (int j = 0; j < MAX_LAYER; j++) {
      spec->buff[j].buf = (websrv_scopedata_msg_t *)calloc(1, sizeof(websrv_scopedata_msg_t));
    }
    obj->objclass = FL_XYPLOT;
    obj->type = t;

    obj->x = x;
    obj->y = y;
    obj->w = w;
    obj->h = h;
    if (label)
      obj->label = strdup(label);
    LOG_I(UTIL, "[websrv], scope object for \"%s\" allocated obj at %p spec at %p\n", label, obj, obj->spec);
  }
  return obj;
};

void websrv_free_xyplot(FL_OBJECT *obj)
{
  FLI_XYPLOT_SPEC *spec = (FLI_XYPLOT_SPEC *)obj->spec;
  for (int j = 0; j < MAX_LAYER; j++) {
    free(spec->buff[j].buf);
  }
  free(obj->label);
  free(spec);
  free(obj);
};

FL_OBJECT *websrv_fl_add_canvas(int type, FL_Coord x, FL_Coord y, FL_Coord w, FL_Coord h, const char *label)
{
  return websrv_fl_add_xyplot(type, x, y, w, h, label);
}

void websrv_fl_get_xyplot_data(FL_OBJECT *ob, float *x, float *y, int *n)
{
  FLI_XYPLOT_SPEC *spec = (FLI_XYPLOT_SPEC *)(ob->spec);
  *n = spec->n[0];
};

void websrv_fl_get_xyplot_data_pointer(FL_OBJECT *ob, int id, float **x, float **y, int *n)
{
  FLI_XYPLOT_SPEC *spec = (FLI_XYPLOT_SPEC *)(ob->spec);
  *n = spec->n[id];
};

/*----------------------------------------------------------------------*/
/* new functions for interfacing with webserver                          */

int websrv_nf_getdata(FL_OBJECT *graph, int layer, websrv_scopedata_msg_t **msg)
{
  FLI_XYPLOT_SPEC *spec = (FLI_XYPLOT_SPEC *)(graph->spec);
  *msg = spec->buff[layer].buf;
  return spec->n[layer];
}

/* Additional XForms stub functions for nr_phy_scope.c */
/* When WEBSRVSCOPE is defined, nr_phy_scope.c uses these functions */

FL_FORM *fl_bgn_form(int type, FL_Coord w, FL_Coord h)
{
  FL_FORM *form = (FL_FORM *)calloc(1, sizeof(FL_FORM));
  if (form) {
    form->w = w;
    form->h = h;
  }
  return form;
}

void fl_end_form(void) {}
FL_OBJECT *fl_check_forms(void) { return NULL; }
FL_OBJECT *fl_do_forms(void) { return NULL; }
void fl_freeze_form(FL_FORM *form) {}
void fl_unfreeze_form(FL_FORM *form) {}
void fl_redraw_form(FL_FORM *form) {}
Window fl_show_form(FL_FORM *form, int place, int border, const char *name) { return NULL; }
void fl_winset(Window win) {}
void fl_set_form_dblbuffer(FL_FORM *form, int y) {}
FL_OBJECT *fl_add_box(int type, FL_Coord x, FL_Coord y, FL_Coord w, FL_Coord h, const char *label) { return NULL; }
FL_OBJECT *fl_add_browser(int type, FL_Coord x, FL_Coord y, FL_Coord w, FL_Coord h, const char *label) { return NULL; }
FL_OBJECT *fl_add_button(int type, FL_Coord x, FL_Coord y, FL_Coord w, FL_Coord h, const char *label) { return NULL; }
FL_OBJECT *fl_add_canvas(int type, FL_Coord x, FL_Coord y, FL_Coord w, FL_Coord h, const char *label) { return NULL; }
FL_OBJECT *fl_add_text(int type, FL_Coord x, FL_Coord y, FL_Coord w, FL_Coord h, const char *label) { return NULL; }
FL_OBJECT *fl_add_xyplot(int t, FL_Coord x, FL_Coord y, FL_Coord w, FL_Coord h, const char *label) { return NULL; }
void fl_add_xyplot_overlay(FL_OBJECT *ob, int id, float *x, float *y, int n, FL_COLOR col) {}
void fl_get_object_bbox(FL_OBJECT *obj, FL_Coord *x, FL_Coord *y, FL_Coord *w, FL_Coord *h) { if (x) *x = 0; if (y) *y = 0; if (w) *w = 0; if (h) *h = 0; }
FL_OBJECT *fl_get_button(FL_OBJECT *ob) { return NULL; }
void fl_set_button(FL_OBJECT *ob, int val) {}
void fl_hide_object(FL_OBJECT *ob) {}
void fl_redraw_object(FL_OBJECT *obj) {}
void fl_set_object_color(FL_OBJECT *ob, FL_COLOR col1, FL_COLOR col2) {}
FL_CALLBACKPTR fl_set_object_callback(FL_OBJECT *obj, FL_CALLBACKPTR callback, long argument) { return NULL; }
void fl_set_object_label(FL_OBJECT *ob, const char *label) {}
void fl_set_object_label_f(FL_OBJECT *obj, const char *fmt, ...) {}
void fl_set_object_lalign(FL_OBJECT *obj, int align) {}
void fl_set_object_lcolor(FL_OBJECT *ob, FL_COLOR lcol) {}
void fl_set_object_lsize(FL_OBJECT *obj, int lsize) {}
void fl_get_xyplot_data(FL_OBJECT *ob, float *x, float *y, int *n) { if (n) *n = 0; }
void fl_get_xyplot_data_pointer(FL_OBJECT *ob, int id, float **x, float **y, int *n) { if (n) *n = 0; if (x) *x = NULL; if (y) *y = NULL; }
void fl_get_xyplot_ybounds(FL_OBJECT *ob, float *ymin, float *ymax) { if (ymin) *ymin = 0; if (ymax) *ymax = 0; }
void fl_set_xyplot_data(FL_OBJECT *ob, float *x, float *y, int n, const char *title, const char *xlabel, const char *ylabel) {}
FL_XYPLOT_SYMBOL fl_set_xyplot_symbol(FL_OBJECT *ob, int id, FL_XYPLOT_SYMBOL symbol) { return NULL; }
void fl_set_xyplot_xbounds(FL_OBJECT *ob, double xmin, double xmax) {}
void fl_set_xyplot_xgrid(FL_OBJECT *ob, int xgrid) {}
void fl_set_xyplot_xtics(FL_OBJECT *ob, int xtics) {}
void fl_set_xyplot_ybounds(FL_OBJECT *ob, double ymin, double ymax) {}
void fl_point(FL_Coord x, FL_Coord y, FL_COLOR c) {}
void fl_points(FL_POINT *p, int np, FL_COLOR c) {}
void fl_set_browser_fontsize(FL_OBJECT *ob, int size) {}

/* Additional stubs for functions used in nr_phy_scope.c */
Display *fl_initialize(int *na, char *arg[], const char *appclass, FL_CMD_OPT *appopt, int nappopt) { return NULL; }
void fl_set_form_callback(FL_FORM *form, FL_FORMCALLBACKPTR callback, void *d) {}

/* Webserver-specific versions (renamed via macros in nr_phy_scope.c) */
FL_FORM *websrv_fl_bgn_form(int type, FL_Coord w, FL_Coord h)
{
  return fl_bgn_form(type, w, h);
}

FL_OBJECT *websrv_fl_do_forms(void)
{
  return fl_do_forms();
}

void websrv_fl_end_form(void)
{
  fl_end_form();
}
