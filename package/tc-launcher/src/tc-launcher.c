/*
 * tc-launcher — консольный интерфейс тонкого клиента (ncurses).
 *
 * Рисует список серверов по центру экрана и полосу действий внизу
 * (в стиле htop). Сам ничего не исполняет: выбранное действие пишет
 * в /tmp/tc-choice и завершается, а обёртка tc-menu выполняет его
 * и перезапускает лаунчер.
 *
 * Протокол в /tmp/tc-choice (одна строка):
 *   CONNECT <ip>            подключиться к серверу
 *   MANAGE                  экран управления серверами (add/edit/delete)
 *   ADD <name>;<ip>         добавить сервер в servers.conf
 *   EDIT <old>\t<new>       заменить запись сервера
 *   DELETE <name>;<ip>      удалить сервер
 *   BACK                    выйти из экрана управления
 *   SHELL                   консоль Linux
 *   DIAG                    экран диагностики (сеть/ping/логи)
 *   SETTINGS                экран настроек (сеть, RDP-дефолты) в tc.conf
 *   REBOOT                  перезагрузка
 *   POWEROFF                выключение
 *
 * ВАЖНО: набор действий должен совпадать со списком case в tc-menu.
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
    C_INFO,       /* приглушённый служебный текст */
    C_HOST        /* hostname и ip в углу */
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

/* сервисные кнопки — вертикальным столбиком внизу. Console показывается
 * всегда; пароль на неё (опционально) ставит админ файлом shell.pass —
 * проверку делает tc-menu. */
#define NBAR 5
static const char *bar_label[NBAR] = {
    " Console ", " Diagnostics ", " Settings ", " Reboot ", " PowerOff "
};
static int nbar = NBAR;   /* 0 в режиме управления серверами */
static int manage = 0;    /* 1 = экран «Manage servers» (add/edit/delete) */

static void draw_buttons(int sel, int x0)
{
    int base = LINES - 1 - nbar;
    int i;

    for (i = 0; i < nbar; i++) {
        if (sel == nsrv + 1 + i)
            attrset(COLOR_PAIR(C_SEL));
        else
            attrset(COLOR_PAIR(C_IP));
        mvaddstr(base + i, x0 + 1, bar_label[i]);
    }
    attrset(COLOR_PAIR(C_NORM));
}

/*
 * Единая цепочка выбора: 0..nsrv-1 — серверы, nsrv — "+ Add server",
 * nsrv+1..nsrv+NBAR — кнопки нижней полосы. Стрелки вверх/вниз ходят
 * по всей цепочке по кругу.
 */
static void draw(int sel)
{
    char info[160];
    int gap = nsrv ? 1 : 0;
    int height = nsrv + gap + 1;
    int top = (LINES - 1 - height) / 2;
    int x0 = (COLS - LIST_W) / 2;
    int i, y;

    if (top < 1)
        top = 1;
    if (x0 < 0)
        x0 = 0;

    erase();

    /* hostname и ip — в левом верхнем углу */
    get_info(info, sizeof info);
    attrset(COLOR_PAIR(C_HOST));
    mvaddstr(0, 1, info);

    /* версия образа (из /etc/tc-release) — в правом верхнем углу; читаем раз */
    {
        static char ver[48] = "\1";     /* \1 = ещё не читали */
        if (ver[0] == '\1') {
            FILE *vf = fopen("/etc/tc-release", "r");
            ver[0] = 0;
            if (vf) {
                if (fgets(ver, sizeof ver, vf))
                    ver[strcspn(ver, "\r\n")] = 0;
                fclose(vf);
            }
        }
        if (ver[0]) {
            attrset(COLOR_PAIR(C_INFO));
            mvaddstr(0, COLS - (int)strlen(ver) - 1, ver);
        }
    }

    if (!nsrv) {
        attrset(COLOR_PAIR(C_INFO));
        mvaddstr(top - 2, (COLS - 21) / 2, "No servers configured");
    }

    for (i = 0; i < nsrv; i++) {
        y = top + i;
        attrset(COLOR_PAIR(i == sel ? C_SEL : C_NORM));
        mvhline(y, x0, ' ', LIST_W);
        mvaddnstr(y, x0 + 1, servers[i].name, NAME_W);
        if (i != sel)
            attrset(COLOR_PAIR(C_IP));
        mvaddstr(y, x0 + LIST_W - (int)strlen(servers[i].ip) - 1,
                 servers[i].ip);
    }

    y = top + nsrv + gap;
    attrset(sel == nsrv ? COLOR_PAIR(C_SEL) : COLOR_PAIR(C_INFO));
    if (sel == nsrv)
        mvhline(y, x0, ' ', LIST_W);
    mvaddstr(y, x0 + 1, manage ? "+ Add server" : "Manage servers");

    if (manage) {
        attrset(COLOR_PAIR(C_HOST));
        mvaddstr(0, (COLS - 14) / 2, "Manage servers");
        attrset(COLOR_PAIR(C_INFO));
        mvaddstr(LINES - 1, 1, "Enter/e edit   d delete   a add   q back");
    }

    draw_buttons(sel, x0);
    refresh();
}

/* поле ввода по центру.
 *   allow_empty=0: пустая строка = отмена (возврат 0);
 *   allow_empty=1: пустая строка допустима (возврат 1, buf==""). */
static int prompt(const char *label, char *buf, int n, int allow_empty)
{
    int y = LINES / 2;
    int x = (COLS - 44) / 2;
    int r;

    if (x < 0)
        x = 0;
    attrset(COLOR_PAIR(C_NORM));
    erase();
    mvaddstr(y - 1, x, label);
    attrset(COLOR_PAIR(C_INFO));
    mvaddstr(y + 2, x, allow_empty ? "empty = keep current" : "empty input cancels");
    attrset(COLOR_PAIR(C_NORM));
    move(y, x);
    echo();
    curs_set(1);
    /* блокирующий ввод: общий секундный таймаут перерисовки иначе
       обрывает getnstr через 1с и это выглядит как отмена */
    timeout(-1);
    r = getnstr(buf, n - 1);
    timeout(1000);
    noecho();
    curs_set(0);
    if (r == ERR)
        return 0;
    return allow_empty || buf[0] != 0;
}

/* убрать из поля символы, ломающие формат "имя;ip" и протокол:
 * ';' (разделитель), а также tab и любые control-символы. */
static void strip_bad(char *s)
{
    char *p, *q;

    for (p = q = s; *p; p++)
        if (*p != ';' && (unsigned char)*p >= 0x20)
            *q++ = *p;
    *q = 0;
}

static int do_add(void)
{
    char name[64], ip[64];

    if (prompt("Server name:", name, sizeof name, 0) &&
        prompt("IP address (ip or ip:port):", ip, sizeof ip, 0)) {
        strip_bad(name);
        strip_bad(ip);
        if (!name[0] || !ip[0])
            return 0;
        endwin();
        write_choice("ADD %s;%s", name, ip);
        return 1;
    }
    return 0;
}

/* редактировать сервер i: пустое поле = оставить старое значение.
 * Пишем "EDIT old_name;old_ip \t new_name;new_ip" (tab-разделитель). */
static int do_edit(int i)
{
    char name[64] = "", ip[64] = "";
    char lbl1[128], lbl2[128], oldp[136], newp[136];

    snprintf(lbl1, sizeof lbl1, "New name [%s]:", servers[i].name);
    snprintf(lbl2, sizeof lbl2, "New IP [%s]:", servers[i].ip);
    if (!prompt(lbl1, name, sizeof name, 1))
        return 0;
    if (!prompt(lbl2, ip, sizeof ip, 1))
        return 0;
    strip_bad(name);
    strip_bad(ip);
    if (!name[0])
        snprintf(name, sizeof name, "%s", servers[i].name);
    if (!ip[0])
        snprintf(ip, sizeof ip, "%s", servers[i].ip);
    snprintf(oldp, sizeof oldp, "%s;%s", servers[i].name, servers[i].ip);
    snprintf(newp, sizeof newp, "%s;%s", name, ip);
    endwin();
    write_choice("EDIT %s\t%s", oldp, newp);
    return 1;
}

/* удалить сервер i с подтверждением */
static int do_delete(int i)
{
    char oldp[136];
    int y = LINES / 2, x = (COLS - 44) / 2, c;

    if (x < 0)
        x = 0;
    attrset(COLOR_PAIR(C_NORM));
    erase();
    mvprintw(y, x, "Delete server \"%s\" (%s)?", servers[i].name, servers[i].ip);
    attrset(COLOR_PAIR(C_INFO));
    mvaddstr(y + 2, x, "y = delete, any other key = cancel");
    refresh();
    timeout(-1);
    c = getch();
    timeout(1000);
    if (c != 'y' && c != 'Y')
        return 0;
    snprintf(oldp, sizeof oldp, "%s;%s", servers[i].name, servers[i].ip);
    endwin();
    write_choice("DELETE %s", oldp, NULL);
    return 1;
}

static void bar_action(int bsel)
{
    static const char *act[NBAR] = { "SHELL", "DIAG", "SETTINGS", "REBOOT", "POWEROFF" };

    endwin();
    write_choice(act[bsel], NULL, NULL);
}

int main(int argc, char **argv)
{
    int sel = 0;
    int total;

    if (argc > 1 && strcmp(argv[1], "--manage") == 0) {
        manage = 1;
        nbar = 0;               /* в режиме управления нет сервис-кнопок */
    }

    load_servers();
    total = nsrv + 1 + nbar;    /* серверы + Add/Manage (+ кнопки в main) */

    initscr();
    start_color();
    init_pair(C_NORM, COLOR_WHITE, COLOR_BLACK);
    init_pair(C_IP,   COLOR_CYAN,  COLOR_BLACK);
    init_pair(C_SEL,  COLOR_BLACK, COLOR_CYAN);
    init_pair(C_BAR,  COLOR_BLACK, COLOR_CYAN);
    init_pair(C_KEY,  COLOR_WHITE, COLOR_BLACK);
    init_pair(C_INFO, COLOR_BLUE,  COLOR_BLACK);
    init_pair(C_HOST, COLOR_YELLOW, COLOR_BLACK);
    bkgd(COLOR_PAIR(C_NORM));
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    timeout(1000);          /* раз в секунду перерисовка: обновляет ip в углу */

    for (;;) {
        int c;

        draw(sel);
        c = getch();
        switch (c) {
        case ERR:
            break;
        case KEY_UP:
        case 'k':
            sel = (sel + total - 1) % total;
            break;
        case KEY_DOWN:
        case 'j':
        case '\t':
            sel = (sel + 1) % total;
            break;
        case '\n':
        case '\r':
        case KEY_ENTER:
            if (sel < nsrv) {
                if (manage) {
                    if (do_edit(sel))
                        return 0;
                    break;
                }
                endwin();
                write_choice("CONNECT %s", servers[sel].ip, NULL);
                return 0;
            }
            if (sel == nsrv) {
                if (manage) {
                    if (do_add())
                        return 0;
                } else {
                    endwin();
                    write_choice("MANAGE", NULL, NULL);
                    return 0;
                }
                break;
            }
            bar_action(sel - nsrv - 1);   /* сервис-кнопки (только main) */
            return 0;
        /* --- клавиши режима управления серверами --- */
        case 'e':
        case 'E':
            if (manage && sel < nsrv && do_edit(sel))
                return 0;
            break;
        case 'a':
        case 'A':
            if (manage && do_add())
                return 0;
            break;
        case 'd':
        case 'D':
        case KEY_DC:
            if (manage && sel < nsrv && do_delete(sel))
                return 0;
            break;
        case 'q':
        case 'Q':
        case 27:               /* Esc */
            if (manage) {
                endwin();
                write_choice("BACK", NULL, NULL);
                return 0;
            }
            break;
        /* F-клавиши: сервис (только main):
         * Console/Diagnostics/Settings/Reboot/PowerOff */
        case KEY_F(3):
            if (!manage) { bar_action(0); return 0; }
            break;
        case KEY_F(4):
            if (!manage) { bar_action(1); return 0; }
            break;
        case KEY_F(5):
            if (!manage) { bar_action(2); return 0; }
            break;
        case KEY_F(6):
            if (!manage) { bar_action(3); return 0; }
            break;
        case KEY_F(7):
            if (!manage) { bar_action(4); return 0; }
            break;
        default:
            break;
        }
    }
}
