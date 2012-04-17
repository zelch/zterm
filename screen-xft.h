#ifndef TEMU_SCREEN_XFT_h
#define TEMU_SCREEN_XFT_h 1

void temu_screen_render_moves_xft(TemuScreen *screen, GdkRegion *inv_region);
void temu_screen_render_text_xft(TemuScreen *screen, GdkRegion *region);

#endif
