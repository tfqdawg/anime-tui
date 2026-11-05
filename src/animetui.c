#define _GNU_SOURCE
#include <fcntl.h>
#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct{
  int id;
  char title[128];
  char kind[8];
  int progress;
  char url[512];
}Entry;

static sqlite3 *db;
static Entry *entries;
static int count, cap, sel, top;

// database 
static void db_open(void){
  char dir[512], path[600];
  const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
  if (xdg && *xdg) snprintf(dir, sizeof dir, "%s/animetui", xdg);
  else snprintf(dir, sizeof dir, "%s/.local/share/animetui", home ? home : ".");

  for (char *p = dir + 1; *p; p++)
    if (*p == '/') { *p = 0; mkdir(dir, 0755); *p = '/'; }
  mkdir(dir, 0755);

  snprintf(path, sizeof path, "%s/tracker.db", dir);
  if(sqlite3_open(path, &db) != SQLITE_OK){
    fprintf(stderr, "cannot open %s: %s\n", path, sqlite3_errmsg(db));
    exit(1);
  }
  sqlite3_exec(db,
		"CREATE TABLE IF NOT EXISTS entries ("
		" id INTEGER PRIMARY KEY,"
		" title TEXT NOT NULL,"
		" kind TEXT NOT NULL CHECK (kind IN ('anime','manga')),"
		" progress INTEGER NOT NULL DEFAULT 0,"
		" url TEXT NOT NULL DEFAULT '')",
		0, 0, 0);
}

static const char *txt(sqlite3_stmt *s, int col){
  const unsigned char *t = sqlite3_column_text(s, col);
  return t ? (const char *)t : "";
}

static void reload(int keep_id){
  sqlite3_stmt *s;
  count = 0;
  sqlite3_prepare_v2(db,
      "SELECT id, title, kind, progress, url FROM entries"
      " ORDER BY kind, title COLLATE NOCASE", -1, &s, 0);
  while(sqlite3_step(s) == SQLITE_ROW){
    if(count == cap){
      cap = cap ? cap * 2 : 32;
      entries = realloc(entries, cap * sizeof *entries);
      if (!entries) exit(1);
    }
    Entry *e = &entries[count++];
    e->id = sqlite3_column_int(s, 0);
    snprintf(e->title, sizeof e->title, "%s", txt(s, 1));
    snprintf(e->kind, sizeof e->kind, "%s", txt(s, 2));
    e->progress = sqlite3_column_int(s, 3);
    snprintf(e->url, sizeof e->url, "%s", txt(s, 4));
  }
  sqlite3_finalize(s);

  sel = 0;
  for (int i = 0; i < count; i++)
    if (entries[i].id == keep_id) sel = i;
  if (sel >= count) sel = count ? count - 1 : 0;
}

static int db_insert(const char *title, const char *kind, const char *url){
  sqlite3_stmt *s;
  sqlite3_prepare_v2(db, "INSERT INTO entries (title, kind, url) VALUES (?1, ?2, ?3)", -1, &s, 0);
    sqlite3_bind_text(s, 1, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, kind, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 3, url, -1, SQLITE_TRANSIENT);
    sqlite3_step(s);
    sqlite3_finalize(s);
  return (int)sqlite3_last_insert_rowid(db);
}

static void db_bump(int id, int delta){
	sqlite3_stmt *s;
	sqlite3_prepare_v2(db,
			"UPDATE entries SET progress = MAX(0, progress + ?1) WHERE id = ?2", -1, &s, 0);
	sqlite3_bind_int(s, 1, delta);
	sqlite3_bind_int(s, 2, id);
	sqlite3_step(s);
	sqlite3_finalize(s);
}

static void db_set_text(const char *sql, int id, const char *val){
	sqlite3_stmt *s; /* sql: "UPDATE entries SET <col> = ?1 WHERE id = ?2" */
	sqlite3_prepare_v2(db, sql, -1, &s, 0);
	sqlite3_bind_text(s, 1, val, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(s, 2, id);
	sqlite3_step(s);
	sqlite3_finalize(s);
}

static void db_delete(int id){
	sqlite3_stmt *s;
	sqlite3_prepare_v2(db, "DELETE FROM entries WHERE id = ?1", -1, &s, 0);
	sqlite3_bind_int(s, 1, id);
	sqlite3_step(s);
	sqlite3_finalize(s);
}

// helpers
static void open_url(const char *url, char *msg, size_t n){
	if (!*url) { snprintf(msg, n, "No link set - press e to add one"); return; }

	char full[600];
	if (strstr(url, "://")) snprintf(full, sizeof full, "%s", url);
	else snprintf(full, sizeof full, "https://%s", url);

	pid_t p = fork();
	if(p == 0){
		int fd = open("/dev/null", O_RDWR);
		if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); }
		setsid();
		execlp("xdg-open", "xdg-open", full, (char *)NULL);
		_exit(127);
	}
	if (p < 0) snprintf(msg, n, "fork failed");
	else snprintf(msg, n, "Opening %s", full);
}

/* line editor on the bottom row. buf holds the initial text.
 * returns 1 on enter, 0 on esc. ctrl-u clears the line. */
static int prompt(const char *label, char *buf, size_t size){
	int len = (int)strlen(buf), c;
	curs_set(1);
	for(;;){
		int avail = COLS - (int)strlen(label) - 1;
		if (avail < 1) avail = 1;
		move(LINES - 1, 0);
		clrtoeol();
		mvprintw(LINES - 1, 0, "%s%s", label, len > avail ? buf + (len - avail) : buf);
		refresh();

		c = getch();
		if (c == '\n' || c == KEY_ENTER) { curs_set(0); return 1; }
		if (c == 27) { curs_set(0); return 0; }
		if(c == KEY_BACKSPACE || c == 127 || c == 8){
			while (len && (buf[len - 1] & 0xC0) == 0x80) len--;
			if (len) len--;
			buf[len] = 0;
		}
		else if(c == 21){ /* Ctrl-U */
			len = 0;
			buf[0] = 0;
		}
		else if(c >= 32 && c < 256 && c != 127 && len < (int)size - 1){
			buf[len++] = (char)c;
			buf[len] = 0;
		}
	}
}

static int confirm(const char *question){
	move(LINES - 1, 0);
	clrtoeol();
	mvprintw(LINES - 1, 0, "%s (y/N) ", question);
	refresh();
	return getch() == 'y';
}

static void add_entry(void){
	char title[128] = "", url[512] = "";
	const char *kind;

	if (!prompt("Title: ", title, sizeof title) || !*title) return;

	move(LINES - 1, 0);
	clrtoeol();
	mvprintw(LINES - 1, 0, "Type: [a]nime / [m]anga ");
	refresh();
	int c = getch();
	if (c == 27) return;
	kind = (c == 'm' || c == 'M') ? "manga" : "anime";

	if (!prompt("Link: ", url, sizeof url)) return;

	reload(db_insert(title, kind, url));
}

// drawing
static void draw(const char *msg){
	erase();
	attron(A_BOLD);
	mvprintw(0, 1, "Anime & Manga Tracker  (%d Entries)", count);
	attroff(A_BOLD);

	int rows = LINES - 3;
	if (rows < 1) rows = 1;
	if (sel < top) top = sel;
	if (sel >= top + rows) top = sel - rows + 1;

	int tw = COLS - 26;
	if (tw < 10) tw = 10;

	for(int i = 0; i < rows && top + i < count; i++){
		Entry *e = &entries[top + i];
		int y = 1 + i, hl = (top + i == sel);
		if (hl) attron(A_REVERSE);
		mvhline(y, 0, ' ', COLS);
		mvprintw(y, 1, "%-5s", e->kind[0] == 'a' ? "ANIME" : "MANGA");
		mvaddnstr(y, 7, e->title, tw);
		mvprintw(y, 7 + tw + 1, "%s %-4d %s",
						 e->kind[0] == 'a' ? "Ep" : "Ch", e->progress, *e->url ? "link" : "");
		if (hl) attroff(A_REVERSE);
	}

	if(!count) mvprintw(2, 2, "No entry yet!  press [a] to add an entry.");

	if(msg && *msg) mvaddnstr(LINES - 2, 1, msg, COLS - 2);
	attron(A_DIM);
	mvaddnstr(LINES - 1, 1,
			"[Enter] open  |  [+/-] progress  |  [a]dd  |  [e] link  |  [t]itle  |  [d]elete  |  [q]uit", COLS - 2);
	attroff(A_DIM);
	refresh();
}

// main
int main(void) {
	setlocale(LC_ALL, "");
	signal(SIGCHLD, SIG_IGN);
	db_open();
	reload(-1);

	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);
	set_escdelay(25);

	char msg[700] = "";
	for(;;){
		draw(msg);
		msg[0] = 0;

		int c = getch();
		Entry *e = count ? &entries[sel] : NULL;

		if (c == 'q') break;
		else if ((c == 'j' || c == KEY_DOWN) && sel < count - 1) sel++;
		else if ((c == 'k' || c == KEY_UP) && sel > 0) sel--;
		else if (c == 'g') sel = 0;
		else if (c == 'G' && count) sel = count - 1;
		else if (c == 'a') add_entry();
		else if (!e) continue;
		else if (c == '\n' || c == KEY_ENTER) open_url(e->url, msg, sizeof msg);
		else if (c == '+' || c == '=' || c == 'l' || c == KEY_RIGHT) { db_bump(e->id, 1); reload(e->id); }
		else if (c == '-' || c == 'h' || c == KEY_LEFT) { db_bump(e->id, -1); reload(e->id); }
		else if(c == 'e'){
			char buf[512];
			snprintf(buf, sizeof buf, "%s", e->url);
			if (prompt("Link: ", buf, sizeof buf)){
					db_set_text("UPDATE entries SET url = ?1 WHERE id = ?2", e->id, buf);
					reload(e->id);
			}
		}
		else if(c == 't'){
			char buf[128];
			snprintf(buf, sizeof buf, "%s", e->title);
			if(prompt("Title: ", buf, sizeof buf) && *buf){
				db_set_text("UPDATE entries SET title = ?1 WHERE id = ?2", e->id, buf);
				reload(e->id);
			}
		}
		else if(c == 'd'){
			char q[200];
			snprintf(q, sizeof q, "Delete \"%s\"?", e->title);
			if(confirm(q)){
				db_delete(e->id);
				int keep = sel;
				reload(-1);
				sel = keep < count ? keep : (count ? count - 1 : 0);
			}
		}
	}

	endwin();
	sqlite3_close(db);
	free(entries);
	return 0;
}
