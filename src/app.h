/* FastNote C/RayGUI Edition — Application state */

#ifndef FASTNOTE_APP_H
#define FASTNOTE_APP_H

#include <stddef.h>

#define APP_VERSION "1.1.0"

typedef struct {
    char *notes_dir;
    char *current_path;
    char *document_content;
    size_t content_len;
    size_t content_cap;
    int dirty;
    int theme; /* 0 = light, 1 = dark */
    char *event_file; /* phase marker file (spec 5.1) */
} FastNoteApp;

FastNoteApp *fastnote_app_new(void);
void fastnote_app_free(FastNoteApp *app);
int fastnote_app_run(FastNoteApp *app, int argc, char **argv);

const char *fn_error(void);
void fn_set_error(const char *fmt, ...);
char *xstrdup(const char *s);

/* Append a phase marker to the event file (spec 5.1) */
void fn_event(FastNoteApp *app, const char *marker);

#endif /* FASTNOTE_APP_H */
