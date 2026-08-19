/*
 * tc-launcher — консольный интерфейс тонкого клиента (ncurses).
 *
 * Рисует список серверов по центру экрана и полосу действий внизу
 * (в стиле htop). Сам ничего не исполняет: выбранное действие пишет
 * в /tmp/tc-choice и завершается, а обёртка tc-menu выполняет его
 * и перезапускает лаунчер.
 *
 * Протокол в /tmp/tc-choice (одна строка):
 *   CONNECT <ip>     подключиться к серверу
 *   ADD <name>;<ip>  добавить сервер в servers.conf
 *   SHELL            консоль Linux
 *   REBOOT           перезагрузка
 *   OFF              выключение
 */
#include <curses.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONF    "/mnt/flash/servers.conf"
#define OUTFILE "/tmp/tc-choice"
#define MAXSRV  64
#define LIST_W  52
#define NAME_W  32

struct srv {
    char name[64];
    char ip[64];
};

static struct srv servers[MAXSRV];
static int nsrv;

enum {
    C_NORM = 1,   /* обычный текст */
    C_IP,         /* ip в списке */
    C_SEL,        /* выбранная строка */
    C_BAR,        /* сегменты полосы действий */
    C_KEY,        /* клавиши в полосе действий */
    C_INFO        /* приглушённый служебный текст */
};

static void load_servers(void)
{
    FILE *f;
    char line[256];

    nsrv = 0;
    f = fopen(CONF, "r");
    if (!f)
        return;
    while (fgets(line, sizeof line, f) && nsrv < MAXSRV) {
        char *sep;

        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#')
            continue;
        sep = strchr(line, ';');
        if (!sep || sep == line || !sep[1])
            continue;
        *sep = 0;
        snprintf(servers[nsrv].name, sizeof servers[nsrv].name, "%s", line);
        snprintf(servers[nsrv].ip, sizeof servers[nsrv].ip, "%s", sep + 1);
        nsrv++;
    }
    fclose(f);
}

/* "tc-aabbccddeeff  192.168.0.5" для правого угла полосы действий */
static void get_info(char *buf, size_t n)
{
    char host[64] = "?";
    char ip[64] = "no ip";
    struct ifaddrs *ifa0, *ifa;

    gethostname(host, sizeof host);
    host[sizeof host - 1] = 0;
    if (getifaddrs(&ifa0) == 0) {
        for (ifa = ifa0; ifa; ifa = ifa->ifa_next) {
            struct sockaddr_in *sa;

            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                continue;
            if (strcmp(ifa->ifa_name, "lo") == 0)
                continue;
            sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof ip);
            break;
        }
        freeifaddrs(ifa0);
    }
    snprintf(buf, n, "%s  %s", host, ip);
}

static void write_choice(const char *fmt, const char *arg1, const char *arg2)
{
    FILE *f = fopen(OUTFILE, "w");

    if (!f)
        return;
    fprintf(f, fmt, arg1 ? arg1 : "", arg2 ? arg2 : "");
    fputc('\n', f);
    fclose(f);
}

/* нижняя полоса: Console | Reboot | Off; по ней ходят стрелками */
#define NBAR 3
static const char *bar_label[NBAR] = { " Console ", " Reboot ", " Off " };

static void draw_bar(int focused, int bsel)
{
    char info[160];
    int y = LINES - 1;
    int x = 1;
    int i;

    attrset(COLOR_PAIR(C_NORM));
    mvhline(y, 0, ' ', COLS);
    for (i = 0; i < NBAR; i++) {
        if (focused && i == bsel)
            attrset(COLOR_PAIR(C_BAR) | A_BOLD);
        else
            attrset(COLOR_PAIR(C_IP));
        mvaddstr(y, x, bar_label[i]);
        x += (int)strlen(bar_label[i]) + 2;
    }
    get_info(info, sizeof info);
    if ((int)strlen(info) + 2 < COLS - x) {
        attrset(COLOR_PAIR(C_INFO));
        mvaddstr(y, COLS - (int)strlen(info) - 2, info);
    }
    attrset(COLOR_PAIR(C_NORM));
}

static void draw(int focus_bar, int sel, int bsel)
{
    int gap = nsrv ? 1 : 0;
    int height = nsrv + gap + 1;
    int top = (LINES - 1 - height) / 2;
    int x0 = (COLS - LIST_W) / 2;
    int list_focused = !focus_bar;
    int i, y;

    if (top < 1)
        top = 1;
    if (x0 < 0)
        x0 = 0;

    erase();

    if (!nsrv) {
        attrset(COLOR_PAIR(C_INFO));
        mvaddstr(top - 2, (COLS - 21) / 2, "No servers configured");
    }

    for (i = 0; i < nsrv; i++) {
        y = top + i;
        attrset(COLOR_PAIR(list_focused && i == sel ? C_SEL : C_NORM));
        mvhline(y, x0, ' ', LIST_W);
        mvaddnstr(y, x0 + 1, servers[i].name, NAME_W);
        if (!(list_focused && i == sel))
            attrset(COLOR_PAIR(C_IP));
        mvaddstr(y, x0 + LIST_W - (int)strlen(servers[i].ip) - 1,
                 servers[i].ip);
    }

    y = top + nsrv + gap;
    attrset(list_focused && sel == nsrv ? COLOR_PAIR(C_SEL)
                                        : COLOR_PAIR(C_INFO));
    if (list_focused && sel == nsrv)
        mvhline(y, x0, ' ', LIST_W);
    mvaddstr(y, x0 + 1, "+ Add server");

    draw_bar(focus_bar, bsel);
    refresh();
}

/* простое поле ввода по центру; пустая строка = отмена */
static int prompt(const char *label, char *buf, int n)
{
    int y = LINES / 2;
    int x = (COLS - 40) / 2;
    int r;

    if (x < 0)
        x = 0;
    attrset(COLOR_PAIR(C_NORM));
    erase();
    mvaddstr(y - 1, x, label);
    attrset(COLOR_PAIR(C_INFO));
    mvaddstr(y + 2, x, "empty input cancels");
    attrset(COLOR_PAIR(C_NORM));
    move(y, x);
    echo();
    curs_set(1);
    r = getnstr(buf, n - 1);
    noecho();
    curs_set(0);
    return r != ERR && buf[0] != 0;
}

static int do_add(void)
{
    char name[64], ip[64];

    if (prompt("Server name:", name, sizeof name) &&
        prompt("IP address (ip or ip:port):", ip, sizeof ip)) {
        endwin();
        write_choice("ADD %s;%s", name, ip);
        return 1;
    }
    return 0;
}

static void bar_action(int bsel)
{
    static const char *act[NBAR] = { "SHELL", "REBOOT", "OFF" };

    endwin();
    write_choice(act[bsel], NULL, NULL);
}

int main(void)
{
    int sel = 0;        /* позиция в списке */
    int bsel = 0;       /* позиция в нижней полосе */
    int focus_bar = 0;  /* 0 — список, 1 — полоса действий */
    int total;

    load_servers();
    total = nsrv + 1;

    initscr();
    start_color();
    init_pair(C_NORM, COLOR_WHITE, COLOR_BLACK);
    init_pair(C_IP,   COLOR_CYAN,  COLOR_BLACK);
    init_pair(C_SEL,  COLOR_BLACK, COLOR_CYAN);
    init_pair(C_BAR,  COLOR_BLACK, COLOR_CYAN);
    init_pair(C_KEY,  COLOR_WHITE, COLOR_BLACK);
    init_pair(C_INFO, COLOR_BLUE,  COLOR_BLACK);
    bkgd(COLOR_PAIR(C_NORM));
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    timeout(1000);          /* раз в секунду перерисовка: обновляет ip в углу */

    for (;;) {
        int c;

        draw(focus_bar, sel, bsel);
        c = getch();
        switch (c) {
        case ERR:
            break;
        case KEY_UP:
        case 'k':
            if (focus_bar)
                focus_bar = 0, sel = total - 1;
            else if (sel > 0)
                sel--;
            else
                focus_bar = 1;
            break;
        case KEY_DOWN:
        case 'j':
            if (!focus_bar) {
                if (sel < total - 1)
                    sel++;
                else
                    focus_bar = 1;
            } else {
                focus_bar = 0;
                sel = 0;
            }
            break;
        case KEY_LEFT:
        case 'h':
            if (focus_bar)
                bsel = (bsel + NBAR - 1) % NBAR;
            else
                focus_bar = 1;
            break;
        case KEY_RIGHT:
        case 'l':
            if (focus_bar)
                bsel = (bsel + 1) % NBAR;
            else
                focus_bar = 1;
            break;
        case '\t':
            focus_bar = !focus_bar;
            break;
        case '\n':
        case '\r':
        case KEY_ENTER:
            if (focus_bar) {
                bar_action(bsel);
                return 0;
            }
            if (sel < nsrv) {
                endwin();
                write_choice("CONNECT %s", servers[sel].ip, NULL);
                return 0;
            }
            if (do_add())
                return 0;
            break;
        /* F-клавиши оставлены как шорткаты */
        case KEY_F(2):
            if (do_add())
                return 0;
            break;
        case KEY_F(3):
            bar_action(0);
            return 0;
        case KEY_F(4):
            bar_action(1);
            return 0;
        case KEY_F(5):
            bar_action(2);
            return 0;
        default:
            break;
        }
    }
}
