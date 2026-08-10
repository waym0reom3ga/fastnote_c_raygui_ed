/* FastNote C/RayGUI Edition — Application state */

#ifndef FASTNOTE_APP_H
#define FASTNOTE_APP_H

#include <stddef.h>

typedef struct {
    char *notes_dir;
    char *current_path;
    char *document_content;
    size_t content_len;
    int dirty;
    int theme; /* 0 = light, 1 = dark */
} FastNoteApp;

FastNoteApp *fastnote_app_new(void);
void fastnote_app_free(FastNoteApp *app);
int fastnote_app_run(FastNoteApp *app, int argc, char **argv);

const char *fn_error(void);
void fn_set_error(const char *fmt, ...);
char *xstrdup(const char *s);

#endif /* FASTNOTE_APP_H */
