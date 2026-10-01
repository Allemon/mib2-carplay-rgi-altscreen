/*
 * AltScreen cluster view area: rewrites the view area / safe area AltScreen reports for
 * the CarPlay instrument-cluster display, from ALT_VIEWAREA_CFG (GEM "Cluster map area").
 * Apps that ignore the safe area (Amap, Baidu) place the vehicle relative to the view
 * area, so a narrower view area moves it; the safe area inside it keeps Apple Maps centred.
 */

#ifndef ALT_VIEWAREA_H
#define ALT_VIEWAREA_H

#include <stdint.h>

#define ALT_VIEWAREA_CFG "/mnt/app/root/hooks/cluster_viewarea.cfg"

typedef struct { int x, y, w, h; } alt_rect_t;

/* "view x y w h" (required) and "safe x y w h" lines; returns 1 when a view line was found. */
int alt_viewarea_parse(const char* text, alt_rect_t* view, alt_rect_t* safe, int* have_safe);
/* view inside the display, safe inside the view (safe coordinates are view-relative). */
int alt_viewarea_valid(const alt_rect_t* view, const alt_rect_t* safe, int64_t disp_w, int64_t disp_h);
/* Rewrite the cluster display's first view area in a CopyDisplaysInfo result. */
void alt_viewarea_apply(const void* displays);
/* Host tests only. */
void alt_viewarea_set_config_path(const char* path);

#endif /* ALT_VIEWAREA_H */
