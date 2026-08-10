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
    return app;
}

void fastnote_app_free(FastNoteApp *app) {
    if (!app) return;
    free(app->notes_dir);
    free(app->current_path);
    free(app->document_content);
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
    return write_file_all(path, content, strlen(content));
}

static int export_pdf(const char *path, const char *html_content) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "wkhtmltopdf --quiet - '%s' 2>/dev/null", path);
    FILE *pid = popen(cmd, "w");
    if (!pid) {
        fn_set_error("wkhtmltopdf not available for PDF export");
        return -1;
    }
    fprintf(pid, "%s", html_content);
    return pclose(pid) == 0 ? 0 : -1;
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
    free(ui->app->document_content);
    ui->app->document_content = content;
    ui->app->content_len = len;
    free(ui->app->current_path);
    ui->app->current_path = xstrdup(path);
    ui->app->dirty = 0;
    ui->fb_open = 0;
    fb_free(ui->fb);
    ui->fb = NULL;
}

static void ui_save_file(UI *ui) {
    if (!ui->app->current_path || !ui->app->document_content) return;
    if (write_file_all(ui->app->current_path, ui->app->document_content, ui->app->content_len) == 0) {
        ui->app->dirty = 0;
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
    } else {
        snprintf(ui->message_text, sizeof(ui->message_text), "Export failed: %s", fn_error());
    }
    ui->message = 1;
}

static void draw_editor(UI *ui, int x, int y, int w, int h) {
    /* Simple text editor area */
    DrawRectangle(x, y, w, h, DARKGRAY);
    DrawRectangleLines(x, y, w, h, GRAY);

    const char *text = ui->app->document_content ? ui->app->document_content : "";
    int lines = 0;
    for (const char *p = text; *p; p++) if (*p == '\n') lines++;
    lines++;

    int line_h = 16;
    int max_lines = (h - PAD * 2) / line_h;
    if (max_lines < 1) max_lines = 1;

    const char *line = text;
    int drawn = 0;
    for (int i = 0; i < max_lines && drawn < lines; i++) {
        const char *eol = strchr(line, '\n');
        int llen = eol ? (int)(eol - line) : (int)strlen(line);
        DrawText(TextSubtext(line, 0, llen), x + PAD, y + PAD + i * line_h, 14, WHITE);
        line += llen + (eol ? 1 : 0);
        drawn++;
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
    /* CLI handling */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("fastnote-c-raygui v1.0\n");
            return 0;
        }
        if (strcmp(argv[i], "--headless") == 0) {
            /* Headless mode: process remaining args without display */
            for (int j = i + 1; j < argc; j++) {
                if (strcmp(argv[j], "--open") == 0 && j + 1 < argc) {
                    size_t len = 0;
                    char *content = read_file_all(argv[j + 1], &len);
                    if (content) {
                        app->document_content = content;
                        app->content_len = len;
                        free(app->current_path);
                        app->current_path = xstrdup(argv[j + 1]);
                    }
                }
                if (strcmp(argv[j], "--save") == 0) {
                    if (app->current_path && app->document_content) {
                        write_file_all(app->current_path, app->document_content, app->content_len);
                    }
                }
                if (strcmp(argv[j], "--export") == 0 && j + 1 < argc) {
                    char *html = render_markdown(app->document_content, app->content_len);
                    if (html) {
                        export_html(argv[j + 1], html);
                        free(html);
                    }
                }
                if (strcmp(argv[j], "--selftest") == 0) {
                    /* Basic self-test */
                    char *html = render_markdown("# Hello\n**World**", 17);
                    if (html && strstr(html, "<h1>") && strstr(html, "<strong>")) {
                        free(html);
                        printf("selftest: pass\n");
                        return 0;
                    }
                    if (html) free(html);
                    printf("selftest: fail\n");
                    return 1;
                }
            }
            return 0;
        }
        if (strcmp(argv[i], "--notes-dir") == 0 && i + 1 < argc) {
            free(app->notes_dir);
            app->notes_dir = xstrdup(argv[++i]);
        }
    }

    /* GUI mode */
    UI ui;
    ui_init(&ui, app);

    InitWindow(WIN_W, WIN_H, "FastNote");
    SetTargetFPS(30);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(BLACK);

        /* Toolbar */
        DrawRectangle(0, 0, WIN_W, TOOLBAR_H, GRAY);

        GuiButton((Rectangle){PAD, PAD, 70, TOOLBAR_H - PAD * 2}, "Open");
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) ui_open_file_dialog(&ui);

        GuiButton((Rectangle){PAD + 80, PAD, 70, TOOLBAR_H - PAD * 2}, "Save");
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) ui_save_file(&ui);

        GuiButton((Rectangle){PAD + 160, PAD, 70, TOOLBAR_H - PAD * 2}, "Export");
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) ui_export(&ui, 0);

        GuiButton((Rectangle){PAD + 240, PAD, 70, TOOLBAR_H - PAD * 2}, "PDF");
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) ui_export(&ui, 1);

        /* View toggle */
        GuiButton((Rectangle){WIN_W - 120, PAD, 55, TOOLBAR_H - PAD * 2}, "Edit");
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) ui.view = VIEW_EDITOR;

        GuiButton((Rectangle){WIN_W - 60, PAD, 55, TOOLBAR_H - PAD * 2}, "Preview");
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) ui.view = VIEW_PREVIEW;

        /* Title */
        char title[256];
        const char *path = app->current_path ? strrchr(app->current_path, '/') : "Untitled";
        if (path) path++;
        snprintf(title, sizeof(title), "%s%s", path, app->dirty ? " *" : "");
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

        EndDrawing();
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
