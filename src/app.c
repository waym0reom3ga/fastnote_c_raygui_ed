/* FastNote C/RayGUI Edition — Application state */

#define RAYGUI_IMPLEMENTATION
#include "app.h"
#include "raygui.h"
#include <raylib.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char errbuf[512];

const char *fn_error(void) { return errbuf[0] ? errbuf : NULL; }

void fn_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(errbuf, sizeof(errbuf), fmt, ap);
    va_end(ap);
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* Append a phase marker to the event file (spec 5.1) */
void fn_event(FastNoteApp *app, const char *marker) {
    if (!app || !app->event_file || !marker) return;
    FILE *f = fopen(app->event_file, "a");
    if (!f) return;
    fprintf(f, "%s\n", marker);
    fclose(f);
}

char *read_file_all(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len) *out_len = got;
    return buf;
}

int write_file_all(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return (written == len) ? 0 : -1;
}

FastNoteApp *fastnote_app_new(void) {
    FastNoteApp *app = calloc(1, sizeof(*app));
    if (!app) return NULL;
    const char *home = getenv("HOME");
    app->notes_dir = home ? xstrdup(home) : xstrdup("/tmp");
    app->content_cap = 65536;
    app->document_content = calloc(1, app->content_cap);
    return app;
}

void fastnote_app_free(FastNoteApp *app) {
    if (!app) return;
    free(app->notes_dir);
    free(app->current_path);
    free(app->document_content);
    free(app->event_file);
    free(app);
}

/* ---- Markdown renderer (minimal) ---- */

static char *html_escape(const char *in, size_t len) {
    /* Worst case: every char becomes 6 chars */
    char *out = malloc(len * 6 + 1);
    if (!out) return NULL;
    char *p = out;
    for (size_t i = 0; i < len; i++) {
        switch (in[i]) {
            case '&':  p += sprintf(p, "&amp;"); break;
            case '<':  p += sprintf(p, "&lt;"); break;
            case '>':  p += sprintf(p, "&gt;"); break;
            case '"':  p += sprintf(p, "&quot;"); break;
            default:   *p++ = in[i]; break;
        }
    }
    *p = '\0';
    return out;
}

static char *render_markdown(const char *md, size_t len) {
    /* Simple line-by-line renderer */
    char *escaped = html_escape(md, len);
    if (!escaped) return NULL;

    /* Build HTML with basic formatting */
    size_t cap = len * 3 + 200;
    char *html = malloc(cap);
    if (!html) { free(escaped); return NULL; }
    char *hp = html;
    size_t left = cap;
    sprintf(html, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>FastNote</title>"
                   "<style>body{font-family:sans-serif;max-width:800px;margin:auto;padding:1em}"
                   "code{background:#f4f4f4;padding:2px 4px;border-radius:3px}"
                   "pre{background:#f4f4f4;padding:1em;overflow-x:auto;border-radius:4px}"
                   "table{border-collapse:collapse}th,td{border:1px solid #ccc;padding:4px 8px}"
                   "</style></head><body>\n");
    hp += strlen(html);
    left -= strlen(html);

    /* Process lines */
    const char *line = escaped;
    const char *end = escaped + strlen(escaped);
    while (line < end && left > 100) {
        const char *nl = strchr(line, '\n');
        size_t llen = nl ? (size_t)(nl - line) : strlen(line);
        if (llen == 0) { hp += snprintf(hp, left, "<br>\n"); line = nl ? nl + 1 : end; continue; }

        /* Headings */
        if (line[0] == '#') {
            int level = 0;
            while (level < (int)llen && line[level] == '#') level++;
            if (level <= 6 && (llen == (size_t)level || line[level] == ' ')) {
                hp += snprintf(hp, left, "<h%d>", level);
                line += level + (line[level] == ' ' ? 1 : 0);
                llen -= (size_t)(hp - (html + strlen(html) + 1)); /* approximate */
                while (line < end && *line != '\n' && left > 100) {
                    hp += snprintf(hp, left, "%c", *line++);
                }
                hp += snprintf(hp, left, "</h%d>\n", level);
                if (line < end && *line == '\n') line++;
                continue;
            }
        }

        /* Bold */
        if (llen >= 4 && line[0] == '*' && line[1] == '*' && line[llen-2] == '*' && line[llen-1] == '*') {
            hp += snprintf(hp, left, "<strong>");
            for (size_t i = 2; i < llen - 2; i++) hp += snprintf(hp, left, "%c", line[i]);
            hp += snprintf(hp, left, "</strong>\n");
            line += llen + 1;
            continue;
        }

        /* Italic */
        if (llen >= 3 && line[0] == '*' && line[llen-1] == '*' && llen > 3) {
            hp += snprintf(hp, left, "<em>");
            for (size_t i = 1; i < llen - 1; i++) hp += snprintf(hp, left, "%c", line[i]);
            hp += snprintf(hp, left, "</em>\n");
            line += llen + 1;
            continue;
        }

        /* Code block */
        if (llen >= 3 && line[0] == '`' && line[1] == '`' && line[2] == '`') {
            hp += snprintf(hp, left, "<pre><code>");
            line += 3;
            while (line < end && left > 100) {
                const char *eol = strchr(line, '\n');
                if (eol && eol - line >= 3 && line[0] == '`' && line[1] == '`' && line[2] == '`') break;
                if (eol) {
                    for (const char *c = line; c < eol; c++) hp += snprintf(hp, left, "%c", *c);
                    hp += snprintf(hp, left, "\n");
                    line = eol + 1;
                } else {
                    while (line < end) hp += snprintf(hp, left, "%c", *line++);
                    break;
                }
            }
            hp += snprintf(hp, left, "</code></pre>\n");
            if (line < end && *line == '\n') line++;
            continue;
        }

        /* Inline code */
        if (llen >= 2 && line[0] == '`' && line[llen-1] == '`') {
            hp += snprintf(hp, left, "<code>");
            for (size_t i = 1; i < llen - 1; i++) hp += snprintf(hp, left, "%c", line[i]);
            hp += snprintf(hp, left, "</code>\n");
            line += llen + 1;
            continue;
        }

        /* Unordered list */
        if (llen >= 2 && line[0] == '-' && line[1] == ' ') {
            hp += snprintf(hp, left, "<li>");
            for (size_t i = 2; i < llen; i++) hp += snprintf(hp, left, "%c", line[i]);
            hp += snprintf(hp, left, "</li>\n");
            line += llen + 1;
            continue;
        }

        /* Default: paragraph line */
        hp += snprintf(hp, left, "<p>");
        for (size_t i = 0; i < llen; i++) hp += snprintf(hp, left, "%c", line[i]);
        hp += snprintf(hp, left, "</p>\n");
        line += llen + 1;
    }

    hp += snprintf(hp, left, "</body></html>\n");
    free(escaped);
    return html;
}

/* ---- File browser (simple) ---- */

typedef struct {
    char *current_dir;
    char **entries;
    int entry_count;
    int entry_cap;
    int selected;
    int show_all;
} FileBrowser;

static FileBrowser *fb_new(const char *start) {
    FileBrowser *fb = calloc(1, sizeof(*fb));
    if (!fb) return NULL;
    fb->current_dir = xstrdup(start);
    fb->entry_cap = 256;
    fb->entries = malloc(sizeof(char *) * fb->entry_cap);
    return fb;
}

static void fb_free(FileBrowser *fb) {
    if (!fb) return;
    for (int i = 0; i < fb->entry_count; i++) free(fb->entries[i]);
    free(fb->entries);
    free(fb->current_dir);
    free(fb);
}

static void fb_refresh(FileBrowser *fb) {
    for (int i = 0; i < fb->entry_count; i++) free(fb->entries[i]);
    fb->entry_count = 0;
    fb->selected = 0;

    /* Add parent dir */
    if (strlen(fb->current_dir) > 1) {
        char *parent = xstrdup("..");
        fb->entries[fb->entry_count++] = parent;
    }

    /* List directory */
    DIR *dir = opendir(fb->current_dir);
    if (!dir) return;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (fb->entry_count >= fb->entry_cap) {
            fb->entry_cap *= 2;
            fb->entries = realloc(fb->entries, sizeof(char *) * fb->entry_cap);
        }
        char *full = malloc(strlen(fb->current_dir) + strlen(de->d_name) + 2);
        sprintf(full, "%s/%s", fb->current_dir, de->d_name);

        /* Filter for open mode */
        if (!fb->show_all) {
            const char *ext = strrchr(full, '.');
            if (!ext || (strcmp(ext, ".md") != 0 && strcmp(ext, ".markdown") != 0 && strcmp(ext, ".txt") != 0)) {
                free(full);
                continue;
            }
        }
        fb->entries[fb->entry_count++] = full;
    }
    closedir(dir);
}

static const char *fb_selected(FileBrowser *fb) {
    if (fb->selected < 0 || fb->selected >= fb->entry_count) return NULL;
    return fb->entries[fb->selected];
}

/* ---- Export ---- */

static int export_html(const char *path, const char *content) {
    /* Write a complete standalone HTML document */
    FILE *f = fopen(path, "w");
    if (!f) {
        fn_set_error("Cannot create file: %s", path);
        return -1;
    }
    
    fprintf(f, "<!DOCTYPE html>\n<html>\n<head>\n");
    fprintf(f, "<meta charset=\"utf-8\">\n");
    fprintf(f, "<title>FastNote Export</title>\n");
    fprintf(f, "<style>\n");
    fprintf(f, "body { font-family: sans-serif; max-width: 800px; margin: auto; padding: 1em; }\n");
    fprintf(f, "code { background: #f4f4f4; padding: 2px 4px; border-radius: 3px; }\n");
    fprintf(f, "pre { background: #f4f4f4; padding: 1em; overflow-x: auto; border-radius: 4px; }\n");
    fprintf(f, "table { border-collapse: collapse; }\n");
    fprintf(f, "th, td { border: 1px solid #ccc; padding: 4px 8px; }\n");
    fprintf(f, "</style>\n");
    fprintf(f, "</head>\n<body>\n");
    fprintf(f, "%s\n", content);
    fprintf(f, "</body>\n</html>\n");
    
    fclose(f);
    return 0;
}

static int export_pdf(const char *path, const char *html_content) {
    /* Minimal PDF export - write a simple text-based PDF */
    /* For a proper implementation, we'd need a PDF library or wkhtmltopdf */
    /* For now, write a minimal PDF that contains the text content */
    
    FILE *f = fopen(path, "wb");
    if (!f) {
        fn_set_error("Cannot create PDF file: %s", path);
        return -1;
    }
    
    /* Write minimal PDF structure */
    fprintf(f, "%%PDF-1.4\n");
    fprintf(f, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    fprintf(f, "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
    fprintf(f, "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>\nendobj\n");
    
    /* Content stream with text */
    fprintf(f, "4 0 obj\n<< /Length %zu >>\nstream\n", strlen(html_content) + 50);
    fprintf(f, "BT\n/F1 12 Tf\n72 720 Td\n");
    
    /* Write text content (simplified - just write the first few lines) */
    const char *line = html_content;
    int y = 720;
    while (line && *line && y > 50) {
        const char *eol = strchr(line, '\n');
        int len = eol ? (int)(eol - line) : (int)strlen(line);
        if (len > 80) len = 80; /* Truncate long lines */
        
        fprintf(f, "(");
        for (int i = 0; i < len; i++) {
            if (line[i] == '(' || line[i] == ')' || line[i] == '\\') {
                fprintf(f, "\\%c", line[i]);
            } else {
                fprintf(f, "%c", line[i]);
            }
        }
        fprintf(f, ") Tj\n0 -14 Td\n");
        
        line = eol ? eol + 1 : NULL;
        y -= 14;
    }
    
    fprintf(f, "ET\n");
    fprintf(f, "endstream\nendobj\n");
    
    /* Font */
    fprintf(f, "5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n");
    
    /* Cross-reference table */
    long xref = ftell(f);
    fprintf(f, "xref\n0 6\n");
    fprintf(f, "0000000000 65535 f \n");
    fprintf(f, "0000000009 00000 n \n");
    fprintf(f, "0000000058 00000 n \n");
    fprintf(f, "0000000115 00000 n \n");
    fprintf(f, "0000000266 00000 n \n");
    fprintf(f, "0000000400 00000 n \n");
    
    /* Trailer */
    fprintf(f, "trailer\n<< /Size 6 /Root 1 0 R >>\n");
    fprintf(f, "startxref\n%ld\n%%%%EOF\n", xref);
    
    fclose(f);
    return 0;
}

/* ---- UI ---- */

#define WIN_W 1024
#define WIN_H 768
#define TOOLBAR_H 40
#define SIDEBAR_W 240
#define PAD 8

typedef enum {
    VIEW_EDITOR, VIEW_PREVIEW
} ViewMode;

typedef struct {
    FastNoteApp *app;
    FileBrowser *fb;
    int fb_open;
    ViewMode view;
    char path_buf[512];
    int show_all;
    int message;
    char message_text[256];
    int cursor_pos;        /* cursor position in document */
    int editor_focused;    /* whether editor has focus */
    int path_focused;      /* whether path input has focus */
    int close_requested;   /* window close requested */
    int confirm_close;     /* show close confirmation dialog */
} UI;

static void ui_init(UI *ui, FastNoteApp *app) {
    ui->app = app;
    ui->fb = NULL;
    ui->fb_open = 0;
    ui->view = VIEW_EDITOR;
    ui->path_buf[0] = '\0';
    ui->show_all = 0;
    ui->message = 0;
    ui->message_text[0] = '\0';
    ui->cursor_pos = 0;
    ui->editor_focused = 1;
    ui->path_focused = 0;
    ui->close_requested = 0;
    ui->confirm_close = 0;
}

static void ui_open_file_dialog(UI *ui) {
    if (ui->fb) fb_free(ui->fb);
    const char *start = ui->app->current_path ? ui->app->current_path : ui->app->notes_dir;
    ui->fb = fb_new(start);
    if (ui->fb) {
        fb_refresh(ui->fb);
        ui->fb_open = 1;
    }
}

static void ui_open_file(UI *ui, const char *path) {
    size_t len = 0;
    char *content = read_file_all(path, &len);
    if (!content) {
        fn_set_error("Cannot open file: %s", path);
        return;
    }
    
    /* Ensure content fits in buffer */
    if (len >= ui->app->content_cap) {
        ui->app->content_cap = len + 1;
        free(ui->app->document_content);
        ui->app->document_content = malloc(ui->app->content_cap);
    }
    
    memcpy(ui->app->document_content, content, len);
    ui->app->document_content[len] = '\0';
    ui->app->content_len = len;
    free(content);
    
    free(ui->app->current_path);
    ui->app->current_path = xstrdup(path);
    ui->app->dirty = 0;
    ui->cursor_pos = 0;
    ui->fb_open = 0;
    fb_free(ui->fb);
    ui->fb = NULL;
    
    fn_event(ui->app, "open");
}

static void ui_save_file(UI *ui) {
    if (!ui->app->current_path || !ui->app->document_content) return;
    if (write_file_all(ui->app->current_path, ui->app->document_content, ui->app->content_len) == 0) {
        ui->app->dirty = 0;
        fn_event(ui->app, "save");
    }
}

static void ui_save_as_file(UI *ui, const char *path) {
    if (!ui->app->document_content) return;
    if (write_file_all(path, ui->app->document_content, ui->app->content_len) == 0) {
        free(ui->app->current_path);
        ui->app->current_path = xstrdup(path);
        ui->app->dirty = 0;
        fn_event(ui->app, "save-as");
    }
}

static void ui_export(UI *ui, int is_pdf) {
    if (!ui->app->document_content) return;
    char *html = render_markdown(ui->app->document_content, ui->app->content_len);
    if (!html) return;

    char path[512];
    if (ui->app->current_path) {
        strncpy(path, ui->app->current_path, sizeof(path) - 1);
        char *ext = strrchr(path, '.');
        if (ext) *ext = '\0';
    } else {
        strcpy(path, "untitled");
    }
    strcat(path, is_pdf ? ".pdf" : ".html");

    int ok = is_pdf ? export_pdf(path, html) : export_html(path, html);
    free(html);

    if (ok == 0) {
        snprintf(ui->message_text, sizeof(ui->message_text), "Exported to: %s", path);
        fn_event(ui->app, is_pdf ? "export-pdf" : "export-html");
    } else {
        snprintf(ui->message_text, sizeof(ui->message_text), "Export failed: %s", fn_error());
    }
    ui->message = 1;
}

static void draw_editor(UI *ui, int x, int y, int w, int h) {
    /* Simple text editor area with actual editing support */
    DrawRectangle(x, y, w, h, DARKGRAY);
    DrawRectangleLines(x, y, w, h, ui->editor_focused ? BLUE : GRAY);

    const char *text = ui->app->document_content ? ui->app->document_content : "";
    int lines = 0;
    for (const char *p = text; *p; p++) if (*p == '\n') lines++;
    lines++;

    int line_h = 16;
    int max_lines = (h - PAD * 2) / line_h;
    if (max_lines < 1) max_lines = 1;

    /* Calculate which line the cursor is on */
    int cursor_line = 0;
    int cursor_col = 0;
    int pos = 0;
    for (const char *p = text; p < text + ui->cursor_pos && *p; p++) {
        if (*p == '\n') {
            cursor_line++;
            cursor_col = 0;
        } else {
            cursor_col++;
        }
        pos++;
    }

    /* Calculate scroll offset */
    int scroll_offset = 0;
    if (cursor_line >= max_lines) {
        scroll_offset = cursor_line - max_lines + 1;
    }

    /* Draw text lines */
    const char *line = text;
    int drawn = 0;
    int current_line = 0;
    for (int i = 0; drawn < lines; i++) {
        const char *eol = strchr(line, '\n');
        int llen = eol ? (int)(eol - line) : (int)strlen(line);
        
        if (current_line >= scroll_offset && drawn < max_lines) {
            int draw_y = y + PAD + (drawn - scroll_offset) * line_h;
            DrawText(TextSubtext(line, 0, llen), x + PAD, draw_y, 14, WHITE);
            
            /* Draw cursor on current line */
            if (current_line == cursor_line) {
                int cursor_x = x + PAD + cursor_col * 8; /* Approximate char width */
                DrawRectangle(cursor_x, draw_y, 2, line_h, YELLOW);
            }
            drawn++;
        }
        
        current_line++;
        line += llen + (eol ? 1 : 0);
        if (!eol) break;
    }

    /* Handle text input if editor is focused */
    if (ui->editor_focused && !ui->fb_open) {
        /* Handle special keys */
        if (IsKeyPressed(KEY_BACKSPACE)) {
            if (ui->cursor_pos > 0 && ui->app->content_len > 0) {
                /* Delete character before cursor */
                memmove(&ui->app->document_content[ui->cursor_pos - 1],
                        &ui->app->document_content[ui->cursor_pos],
                        ui->app->content_len - ui->cursor_pos);
                ui->cursor_pos--;
                ui->app->content_len--;
                ui->app->document_content[ui->app->content_len] = '\0';
                ui->app->dirty = 1;
            }
        } else if (IsKeyPressed(KEY_DELETE)) {
            if (ui->cursor_pos < (int)ui->app->content_len) {
                /* Delete character at cursor */
                memmove(&ui->app->document_content[ui->cursor_pos],
                        &ui->app->document_content[ui->cursor_pos + 1],
                        ui->app->content_len - ui->cursor_pos - 1);
                ui->app->content_len--;
                ui->app->document_content[ui->app->content_len] = '\0';
                ui->app->dirty = 1;
            }
        } else if (IsKeyPressed(KEY_LEFT)) {
            if (ui->cursor_pos > 0) ui->cursor_pos--;
        } else if (IsKeyPressed(KEY_RIGHT)) {
            if (ui->cursor_pos < (int)ui->app->content_len) ui->cursor_pos++;
        } else if (IsKeyPressed(KEY_UP)) {
            /* Move cursor up one line */
            int line_start = ui->cursor_pos;
            while (line_start > 0 && ui->app->document_content[line_start - 1] != '\n') {
                line_start--;
            }
            if (line_start > 0) {
                int prev_line_start = line_start - 1;
                while (prev_line_start > 0 && ui->app->document_content[prev_line_start - 1] != '\n') {
                    prev_line_start--;
                }
                int col = ui->cursor_pos - line_start;
                int prev_line_len = line_start - 1 - prev_line_start;
                ui->cursor_pos = prev_line_start + (col < prev_line_len ? col : prev_line_len);
            }
        } else if (IsKeyPressed(KEY_DOWN)) {
            /* Move cursor down one line */
            int line_end = ui->cursor_pos;
            while (line_end < (int)ui->app->content_len && ui->app->document_content[line_end] != '\n') {
                line_end++;
            }
            if (line_end < (int)ui->app->content_len) {
                int next_line_end = line_end + 1;
                while (next_line_end < (int)ui->app->content_len && ui->app->document_content[next_line_end] != '\n') {
                    next_line_end++;
                }
                int col = ui->cursor_pos - (line_end > 0 ? line_end - 1 : 0);
                int next_line_len = next_line_end - line_end - 1;
                ui->cursor_pos = line_end + 1 + (col < next_line_len ? col : next_line_len);
            }
        } else {
            /* Handle text input */
            int key = GetCharPressed();
            while (key > 0) {
                if (key >= 32 && key < 127) { /* Printable ASCII */
                    /* Ensure buffer has space */
                    if (ui->app->content_len >= ui->app->content_cap - 1) {
                        ui->app->content_cap *= 2;
                        ui->app->document_content = realloc(ui->app->document_content, ui->app->content_cap);
                    }
                    
                    /* Insert character at cursor */
                    memmove(&ui->app->document_content[ui->cursor_pos + 1],
                            &ui->app->document_content[ui->cursor_pos],
                            ui->app->content_len - ui->cursor_pos);
                    ui->app->document_content[ui->cursor_pos] = (char)key;
                    ui->cursor_pos++;
                    ui->app->content_len++;
                    ui->app->document_content[ui->app->content_len] = '\0';
                    ui->app->dirty = 1;
                }
                key = GetCharPressed();
            }
            
            /* Handle Enter key */
            if (IsKeyPressed(KEY_ENTER)) {
                /* Ensure buffer has space */
                if (ui->app->content_len >= ui->app->content_cap - 1) {
                    ui->app->content_cap *= 2;
                    ui->app->document_content = realloc(ui->app->document_content, ui->app->content_cap);
                }
                
                /* Insert newline at cursor */
                memmove(&ui->app->document_content[ui->cursor_pos + 1],
                        &ui->app->document_content[ui->cursor_pos],
                        ui->app->content_len - ui->cursor_pos);
                ui->app->document_content[ui->cursor_pos] = '\n';
                ui->cursor_pos++;
                ui->app->content_len++;
                ui->app->document_content[ui->app->content_len] = '\0';
                ui->app->dirty = 1;
            }
        }
    }
}

static void draw_preview(UI *ui, int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, DARKGRAY);
    DrawRectangleLines(x, y, w, h, GRAY);

    if (!ui->app->document_content) {
        DrawText("No document loaded", x + PAD, y + h / 2 - 10, 20, GRAY);
        return;
    }

    char *html = render_markdown(ui->app->document_content, ui->app->content_len);
    if (html) {
        /* Show raw HTML as text preview (proper rendering would need WebView) */
        int lines = 0;
        for (const char *p = html; *p; p++) if (*p == '\n') lines++;
        int line_h = 14;
        int max_lines = (h - PAD * 2) / line_h;
        if (max_lines < 1) max_lines = 1;

        const char *line = html;
        for (int i = 0; i < max_lines; i++) {
            const char *eol = strchr(line, '\n');
            int llen = eol ? (int)(eol - line) : (int)strlen(line);
            DrawText(TextSubtext(line, 0, llen), x + PAD, y + PAD + i * line_h, 12, LIGHTGRAY);
            line += llen + (eol ? 1 : 0);
        }
        free(html);
    }
}

static void draw_file_browser(UI *ui) {
    if (!ui->fb) return;

    int x = (WIN_W - SIDEBAR_W) / 2;
    int y = (WIN_H - 400) / 2;
    int w = SIDEBAR_W;
    int h = 400;

    DrawRectangle(x, y, w, h, DARKGRAY);
    DrawRectangleLines(x, y, w, h, LIGHTGRAY);

    /* Path bar */
    DrawText(ui->fb->current_dir, x + PAD, y + PAD, 12, WHITE);

    /* Toggle show all */
    int btn_x = x + w - 90;
    int btn_y = y + PAD;
    GuiButton((Rectangle){btn_x, btn_y, 80, 24}, ui->show_all ? "Show All*" : "Show All");
    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        ui->show_all = !ui->show_all;
        fb_refresh(ui->fb);
    }

    /* File list */
    int item_h = 22;
    int list_y = y + 30;
    for (int i = 0; i < ui->fb->entry_count && i < 15; i++) {
        const char *name = strrchr(ui->fb->entries[i], '/');
        name = name ? name + 1 : ui->fb->entries[i];

        int sel = (i == ui->fb->selected);
        DrawRectangle(x + PAD, list_y + i * item_h, w - PAD * 2, item_h,
                       sel ? BLUE : DARKGRAY);

        /* Directory indicator */
        if (strcmp(name, "..") == 0) {
            DrawText("[..]", x + PAD + 4, list_y + i * item_h + 4, 12,
                     sel ? WHITE : LIGHTGRAY);
        } else {
            DrawText(name, x + PAD + 4, list_y + i * item_h + 4, 12,
                     sel ? WHITE : LIGHTGRAY);
        }
    }

    /* Open/Cancel buttons */
    GuiButton((Rectangle){x + PAD, y + h - 36, (w - PAD * 2) / 2 - 4, 30}, "Open");
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        const char *sel = fb_selected(ui->fb);
        if (sel) {
            ui_open_file(ui, sel);
        }
    }

    GuiButton((Rectangle){x + (w - PAD * 2) / 2 + 4, y + h - 36, (w - PAD * 2) / 2 - 4, 30}, "Cancel");
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        ui->fb_open = 0;
        fb_free(ui->fb);
        ui->fb = NULL;
    }

    /* Mouse selection */
    Vector2 mouse = GetMousePosition();
    if (mouse.x >= x && mouse.x < x + w && mouse.y >= list_y && mouse.y < list_y + 15 * item_h) {
        int idx = (int)((mouse.y - list_y) / item_h);
        if (idx >= 0 && idx < ui->fb->entry_count) {
            ui->fb->selected = idx;
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                const char *sel = fb_selected(ui->fb);
                if (sel) ui_open_file(ui, sel);
            }
        }
    }
}

/* ---- Main loop ---- */

int fastnote_app_run(FastNoteApp *app, int argc, char **argv) {
    /* Parse command-line flags (spec 5.1: only --version and --event-file) */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("fastnote_c_raygui v%s\n", APP_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--event-file") == 0 && i + 1 < argc) {
            free(app->event_file);
            app->event_file = xstrdup(argv[++i]);
            continue;
        }
        /* Unknown flag: reject and exit */
        fprintf(stderr, "fastnote_c_raygui: unknown option: %s\n", argv[i]);
        return 2;
    }

    /* GUI mode */
    UI ui;
    ui_init(&ui, app);

    InitWindow(WIN_W, WIN_H, "FastNote");
    SetTargetFPS(30);

    int first_frame = 1;

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(BLACK);

        /* Write "painted" marker after first frame */
        if (first_frame) {
            fn_event(app, "painted");
            first_frame = 0;
        }

        /* Handle keyboard accelerators (spec 5.2) */
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
            if (IsKeyPressed(KEY_O)) {
                ui_open_file_dialog(&ui);
            } else if (IsKeyPressed(KEY_S)) {
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                    /* Ctrl+Shift+S: Save As - open browser in save mode */
                    if (ui.fb) fb_free(ui.fb);
                    const char *start = app->current_path ? app->current_path : app->notes_dir;
                    ui.fb = fb_new(start);
                    if (ui.fb) {
                        fb_refresh(ui.fb);
                        ui.fb_open = 1;
                        /* TODO: set save mode */
                    }
                } else {
                    /* Ctrl+S: Save */
                    ui_save_file(&ui);
                }
            } else if (IsKeyPressed(KEY_E)) {
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                    /* Ctrl+Shift+E: Export PDF */
                    ui_export(&ui, 1);
                } else {
                    /* Ctrl+E: Export HTML */
                    ui_export(&ui, 0);
                }
            } else if (IsKeyPressed(KEY_L)) {
                /* Ctrl+L: Focus path field (spec 3.2) */
                ui.path_focused = 1;
                ui.editor_focused = 0;
            }
        }

        /* Toolbar */
        DrawRectangle(0, 0, WIN_W, TOOLBAR_H, GRAY);

        GuiButton((Rectangle){PAD, PAD, 70, TOOLBAR_H - PAD * 2}, "Open");
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 mouse = GetMousePosition();
            if (mouse.x >= PAD && mouse.x < PAD + 70 && mouse.y >= PAD && mouse.y < TOOLBAR_H - PAD) {
                ui_open_file_dialog(&ui);
            }
        }

        GuiButton((Rectangle){PAD + 80, PAD, 70, TOOLBAR_H - PAD * 2}, "Save");
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 mouse = GetMousePosition();
            if (mouse.x >= PAD + 80 && mouse.x < PAD + 150 && mouse.y >= PAD && mouse.y < TOOLBAR_H - PAD) {
                ui_save_file(&ui);
            }
        }

        GuiButton((Rectangle){PAD + 160, PAD, 70, TOOLBAR_H - PAD * 2}, "Export");
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 mouse = GetMousePosition();
            if (mouse.x >= PAD + 160 && mouse.x < PAD + 230 && mouse.y >= PAD && mouse.y < TOOLBAR_H - PAD) {
                ui_export(&ui, 0);
            }
        }

        GuiButton((Rectangle){PAD + 240, PAD, 70, TOOLBAR_H - PAD * 2}, "PDF");
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 mouse = GetMousePosition();
            if (mouse.x >= PAD + 240 && mouse.x < PAD + 310 && mouse.y >= PAD && mouse.y < TOOLBAR_H - PAD) {
                ui_export(&ui, 1);
            }
        }

        /* View toggle */
        GuiButton((Rectangle){WIN_W - 120, PAD, 55, TOOLBAR_H - PAD * 2}, "Edit");
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 mouse = GetMousePosition();
            if (mouse.x >= WIN_W - 120 && mouse.x < WIN_W - 65 && mouse.y >= PAD && mouse.y < TOOLBAR_H - PAD) {
                ui.view = VIEW_EDITOR;
                ui.editor_focused = 1;
                ui.path_focused = 0;
            }
        }

        GuiButton((Rectangle){WIN_W - 60, PAD, 55, TOOLBAR_H - PAD * 2}, "Preview");
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 mouse = GetMousePosition();
            if (mouse.x >= WIN_W - 60 && mouse.x < WIN_W - 5 && mouse.y >= PAD && mouse.y < TOOLBAR_H - PAD) {
                ui.view = VIEW_PREVIEW;
                ui.editor_focused = 0;
            }
        }

        /* Title */
        char title[256];
        const char *path = app->current_path ? strrchr(app->current_path, '/') : "Untitled";
        if (path) path++;
        snprintf(title, sizeof(title), "%s%s — FastNote", path, app->dirty ? "*" : "");
        SetWindowTitle(title);
        DrawText(title, PAD + 320, PAD + 8, 16, WHITE);

        /* Main area */
        int main_x = PAD;
        int main_y = TOOLBAR_H + PAD;
        int main_w = WIN_W - PAD * 2;
        int main_h = WIN_H - TOOLBAR_H - PAD * 2;

        if (ui.view == VIEW_EDITOR) {
            draw_editor(&ui, main_x, main_y, main_w, main_h);
        } else {
            draw_preview(&ui, main_x, main_y, main_w, main_h);
        }

        /* File browser overlay */
        if (ui.fb_open) draw_file_browser(&ui);

        /* Message */
        if (ui.message) {
            DrawRectangle(0, WIN_H - 30, WIN_W, 30, BLUE);
            DrawText(ui.message_text, PAD, WIN_H - 25, 14, WHITE);
            ui.message = 0;
        }

        /* FR-9: Close confirmation dialog */
        if (ui.confirm_close) {
            DrawRectangle(0, 0, WIN_W, WIN_H, Fade(BLACK, 0.7f));
            DrawRectangle(WIN_W / 2 - 200, WIN_H / 2 - 100, 400, 200, DARKGRAY);
            DrawRectangleLines(WIN_W / 2 - 200, WIN_H / 2 - 100, 400, 200, LIGHTGRAY);
            DrawText("Document has unsaved changes.", WIN_W / 2 - 180, WIN_H / 2 - 80, 20, WHITE);
            
            GuiButton((Rectangle){WIN_W / 2 - 180, WIN_H / 2 + 40, 100, 40}, "Save");
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Vector2 mouse = GetMousePosition();
                if (mouse.x >= WIN_W / 2 - 180 && mouse.x < WIN_W / 2 - 80 && 
                    mouse.y >= WIN_H / 2 + 40 && mouse.y < WIN_H / 2 + 80) {
                    ui_save_file(&ui);
                    return 0;
                }
            }
            
            GuiButton((Rectangle){WIN_W / 2 - 50, WIN_H / 2 + 40, 100, 40}, "Discard");
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Vector2 mouse = GetMousePosition();
                if (mouse.x >= WIN_W / 2 - 50 && mouse.x < WIN_W / 2 + 50 && 
                    mouse.y >= WIN_H / 2 + 40 && mouse.y < WIN_H / 2 + 80) {
                    return 0;
                }
            }
            
            GuiButton((Rectangle){WIN_W / 2 + 80, WIN_H / 2 + 40, 100, 40}, "Cancel");
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Vector2 mouse = GetMousePosition();
                if (mouse.x >= WIN_W / 2 + 80 && mouse.x < WIN_W / 2 + 180 && 
                    mouse.y >= WIN_H / 2 + 40 && mouse.y < WIN_H / 2 + 80) {
                    ui.confirm_close = 0;
                }
            }
        }

        EndDrawing();
    }

    /* FR-9: Check for unsaved changes before closing */
    if (app->dirty) {
        /* In a real implementation, we'd show the dialog here */
        /* For now, just exit */
    }

    CloseWindow();
    return 0;
}

int main(int argc, char *argv[]) {
    FastNoteApp *app = fastnote_app_new();
    if (!app) return 1;
    int status = fastnote_app_run(app, argc, argv);
    fastnote_app_free(app);
    return status;
}
