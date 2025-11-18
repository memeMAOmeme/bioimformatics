#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#define usleep(x) Sleep((x)/1000)
#else
#include <unistd.h>
#include <termios.h>
#include <poll.h>

int _kbhit() {
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
    return poll(&pfd, 1, 0) > 0;
}
int _getch() {
    struct termios old, new;
    int ch;
    tcgetattr(STDIN_FILENO, &old);
    new = old;
    new.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    return ch;
}
#endif

#define GRID_SIZE 28
#define MAX_ENTITIES (GRID_SIZE * GRID_SIZE)
#define HISTORY_SIZE 50

typedef enum {
    EMPTY = 0, GRASS, RABBIT, WOLF
} EntityType;

typedef struct {
    int x, y;
    int energy;
    int age;
    int max_age;
    EntityType type;
    int grass_regrow_timer;
} Entity;

Entity grid[GRID_SIZE][GRID_SIZE];
int rabbit_count = 0, wolf_count = 0, grass_count = 0;
int tick = 0;
int paused = 0;
int delay_ms = 250;
int season = 0;
int start_season = 0; // 用户选择的起始季节
const char* season_names[4] = { "春季", "夏季", "秋季", "冬季" };

int history_r[HISTORY_SIZE] = { 0 };
int history_w[HISTORY_SIZE] = { 0 };
int hist_index = 0;

int max_rabbits = 0, max_wolves = 0;
int min_rabbits = 9999, min_wolves = 9999;

int init_grass = 250;
int init_rabbits = 50;
int init_wolves = 0;

char message[128] = { 0 };
int message_timeout = 0;

void clear_screen();
void show_welcome();
void prompt_initial_counts();
void prompt_start_season(); // 新增：选择起始季节
int get_valid_input(int min_val, int max_val);
int get_season_input();
void print_map();
void print_status();
void print_history_chart();
void print_legend();
void print_controls();
void initialize_grid();
void spawn_random(EntityType type, int count);
void update_season();
void update_grass();
void update_entities();
int find_nearest_in_original_grid(EntityType me, EntityType target, int x, int y, int* out_x, int* out_y);
void handle_reproduction_in_new_grid(Entity* parent, Entity new_grid[GRID_SIZE][GRID_SIZE]);
int is_valid(int x, int y);
void save_snapshot();
void set_message(const char* msg);

int is_speed_up_key(int key) {
    return (key == '+' || key == '=');
}
int is_speed_down_key(int key) {
    return (key == '-');
}

int main() {
    srand((unsigned int)time(NULL));
    show_welcome();
    prompt_initial_counts();
    prompt_start_season(); // 新增步骤
    initialize_grid();

    int input;
    while (1) {
        clear_screen();
        update_season();
        print_map();
        print_status();
        print_history_chart();
        print_legend();
        print_controls();

        if (message_timeout > 0) {
            printf("\n>>> %s\n", message);
            message_timeout--;
        }

        if (!paused) {
            update_grass();
            update_entities();

            history_r[hist_index] = rabbit_count;
            history_w[hist_index] = wolf_count;
            hist_index = (hist_index + 1) % HISTORY_SIZE;

            if (rabbit_count > max_rabbits) max_rabbits = rabbit_count;
            if (wolf_count > max_wolves) max_wolves = wolf_count;
            if (rabbit_count > 0 && rabbit_count < min_rabbits) min_rabbits = rabbit_count;
            if (wolf_count > 0 && wolf_count < min_wolves) min_wolves = wolf_count;

            tick++;
        }

        if (_kbhit()) {
            input = _getch();
            int handled = 1;
            if (input == ' ') {
                paused = !paused;
                set_message(paused ? "【已暂停】按空格继续" : "【已继续】模拟运行中");
            }
            else if (is_speed_up_key(input)) {
                if (delay_ms > 20) {
                    delay_ms -= 20;
                    set_message("速度加快");
                }
                else {
                    set_message("已达到最快速度！");
                }
            }
            else if (is_speed_down_key(input)) {
                delay_ms += 20;
                set_message("速度减慢");
            }
            else if (input == 'r' || input == 'R') {
                initialize_grid();
                tick = 0;
                set_message("生态系统已重置！");
            }
            else if (input == 's' || input == 'S') {
                save_snapshot();
            }
            else if (input == 'q' || input == 'Q') {
                clear_screen();
                printf("\n感谢使用生态系统模拟器！\n");
                printf("  草按季节再生 | 起始季节: %s\n", season_names[start_season]);
                printf("食物链：青草 -> 兔子 -> 狼\n\n");
                return 0;
            }
            else {
                handled = 0;
            }
        }

        if (!paused) {
            usleep(delay_ms * 1000);
        }
    }
    return 0;
}

void set_message(const char* msg) {
    strncpy(message, msg, sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';
    message_timeout = 30;
}

void show_welcome() {
    clear_screen();
    printf("\n");
    printf(" ---------季节性草原生态系统模拟器---------  \n\n");
    printf("本程序模拟一个包含青草、兔子和狼的动态生态系统。\n");
    printf("你将观察到：\n");
    printf("    食物链能量流动（草 → 兔 → 狼）\n");
    printf("    种群数量的周期性振荡\n");
    printf("    季节变化对生态的影响\n");
    printf("    智能行为（兔子躲避、狐狸追捕）\n\n");
    printf("  符号说明：\n");
    printf("  g/G = 青草（嫩/熟） | r = 兔子 |  W = 狼 | . = 空地\n\n");
    printf("  操作说明：\n");
    printf("  [空格] 暂停/继续   [+/–] 调整速度\n");
    printf("  [R] 重置生态系统   [S] 保存当前状态\n");
    printf("  [Q] 退出程序\n\n");
    printf(" 特性：\n");
    printf("     草按季节再生（春夏快，秋冬慢）\n");
    printf("     季节对动物的影响\n");
    printf("     可自定义起始季节\n");
    printf("     可自定义生物数量\n\n");
    printf("  按任意键开始模拟...\n");
    printf("\n\n\n注：全屏体验更佳");
    _getch();
}

void prompt_initial_counts() {
    clear_screen();
    printf(" 设置初始数量（总格子: %d）\n", GRID_SIZE * GRID_SIZE);
    printf("（回车使用推荐值）\n\n");

    printf("初始青草数量 (为提高模拟的回合，设置了草的自动再生，此初始设置对程序模拟影响很小): ");
    init_grass = get_valid_input(0, MAX_ENTITIES);
    if (init_grass == -1) init_grass = 250;

    printf("兔子数量 [推荐: 40~60]  : ");
    init_rabbits = get_valid_input(0, MAX_ENTITIES);
    if (init_rabbits == -1) init_rabbits = 50;

    printf("狼的数量 [推荐: 0~8]    : ");
    init_wolves = get_valid_input(0, MAX_ENTITIES);
    if (init_wolves == -1) init_wolves = 0;
}

// 新增：选择起始季节
void prompt_start_season() {
    clear_screen();
    printf(" 选择起始季节：\n");
    printf("  0 = 春季（推荐）\n");
    printf("  1 = 夏季\n");
    printf("  2 = 秋季\n");
    printf("  3 = 冬季\n");
    printf("（直接回车选择春季）\n\n");
    printf("请输入 (0-3): ");

    start_season = get_season_input();
    if (start_season == -1) {
        start_season = 0; // 默认春季
    }
    season = start_season; // 初始化季节
}

// 辅助：读取季节输入
int get_season_input() {
    char buffer[32];
    if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
        if (buffer[0] == '\n') return -1; // 回车默认
        char* end;
        long val = strtol(buffer, &end, 10);
        if (end != buffer && *end == '\n' && val >= 0 && val <= 3) {
            return (int)val;
        }
    }
    printf("无效输入！默认使用春季。\n");
    usleep(800000);
    return -1;
}

int get_valid_input(int min_val, int max_val) {
    char buffer[32];
    if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
        if (buffer[0] == '\n') return -1;
        char* end;
        long val = strtol(buffer, &end, 10);
        if (end != buffer && *end == '\n' && val >= min_val && val <= max_val) {
            return (int)val;
        }
    }
    printf("无效输入！使用默认值。\n");
    usleep(800000);
    return -1;
}

void clear_screen() {
#ifdef _WIN32
    system("cls");
#else
    printf("\033[2J\033[H");
#endif
}

void print_map() {
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            switch (grid[i][j].type) {
            case EMPTY:  printf("."); break;
            case GRASS:
                if (grid[i][j].grass_regrow_timer < 4)
                    printf("\033[32mg\033[0m");
                else
                    printf("\033[32mG\033[0m");
                break;
            case RABBIT: printf("\033[36mr\033[0m"); break;
            case WOLF:   printf("\033[35mW\033[0m"); break;
            }
        }
        printf("\n");
    }
}

void print_status() {
    printf("\n");
    printf("【当前状态】 回合: %4d | 季节: %s\n", tick, season_names[season]);
    printf("青草: %3d | 兔子: %3d | 狼: %3d\n",
        grass_count, rabbit_count, wolf_count);
    printf("历史峰值 (兔/狼): %3d/%3d | 谷值: %3d/%3d\n",
        max_rabbits, max_wolves,
        (min_rabbits == 9999 ? 0 : min_rabbits),
        (min_wolves == 9999 ? 0 : min_wolves));
    printf("模拟速度: %d 毫秒/回合 | 状态: %s\n",
        delay_ms, paused ? "【已暂停】" : "运行中");
}

void print_history_chart() {
    int max_val = 1;
    for (int i = 0; i < HISTORY_SIZE; i++) {
        if (history_r[i] > max_val) max_val = history_r[i];
        if (history_w[i] > max_val) max_val = history_w[i];
    }
    if (max_val == 0) max_val = 1;

    int height = 6;
    printf("\n【种群历史】最近 %d 回合数量变化:\n", HISTORY_SIZE);
    for (int h = height - 1; h >= 0; h--) {
        printf("| ");
        for (int i = 0; i < HISTORY_SIZE; i++) {
            int idx = (hist_index + i) % HISTORY_SIZE;
            char c = ' ';
            if (history_r[idx] * height / max_val > h) c = 'r';
            if (history_w[idx] * height / max_val > h) c = 'W';
            printf("%c", c);
        }
        printf("\n");
    }
    printf("+");
    for (int i = 0; i < HISTORY_SIZE; i++) printf("-");
    printf("\n");
}

void print_legend() {
    printf("\n【图例】 ");
    printf("\033[32mg\033[0m=嫩草 ");
    printf("\033[32mG\033[0m=熟草 ");
    printf("\033[36mr\033[0m=兔子 ");
    printf("\033[35mW\033[0m=狼 ");
    printf(".=空地\n");
}

void print_controls() {
    printf("\n【操作】 [空格]=暂停/继续  [+/=]=加速  [-]=减速  [R]=重置  [S]=保存  [Q]=退出\n");
}

void initialize_grid() {
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            grid[i][j].type = EMPTY;
            grid[i][j].x = i;
            grid[i][j].y = j;
            grid[i][j].energy = 0;
            grid[i][j].age = 0;
            grid[i][j].max_age = 0;
            grid[i][j].grass_regrow_timer = 0;
        }
    }
    rabbit_count = wolf_count = grass_count = 0;
    max_rabbits = max_wolves = 0;
    min_rabbits = min_wolves = 9999;
    hist_index = 0;
    memset(history_r, 0, sizeof(history_r));
    memset(history_w, 0, sizeof(history_w));

    spawn_random(GRASS, init_grass);
    spawn_random(RABBIT, init_rabbits);
    spawn_random(WOLF, init_wolves);

    // 设置初始季节
    season = start_season;
    tick = 0;
}

void spawn_random(EntityType type, int count) {
    int placed = 0;
    int attempts = 0;
    while (placed < count && attempts < MAX_ENTITIES * 2) {
        int x = rand() % GRID_SIZE;
        int y = rand() % GRID_SIZE;
        if (grid[x][y].type == EMPTY) {
            grid[x][y].type = type;
            grid[x][y].age = 0;
            switch (type) {
            case RABBIT:
                grid[x][y].energy = 12;
                grid[x][y].max_age = 30 + rand() % 20;
                rabbit_count++;
                break;
            case WOLF:
                grid[x][y].energy = 25;
                grid[x][y].max_age = 50 + rand() % 20;
                wolf_count++;
                break;
            case GRASS:
                grid[x][y].grass_regrow_timer = 0;
                grass_count++;
                break;
            }
            placed++;
        }
        attempts++;
    }
}

void update_season() {
    // 从 start_season 开始，每100回合换季
    season = (start_season + tick / 100) % 4;
}

void update_grass() {
    double spawn_prob = 0.0;
    switch (season) {
    case 0: spawn_prob = 0.12; break; // 春
    case 1: spawn_prob = 0.10; break; // 夏
    case 2: spawn_prob = 0.03; break; // 秋
    case 3: spawn_prob = 0.005; break; // 冬
    }

    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            if (grid[i][j].type == EMPTY) {
                if (rand() / (double)RAND_MAX < spawn_prob) {
                    grid[i][j].type = GRASS;
                    grid[i][j].grass_regrow_timer = 0;
                    grass_count++;
                }
            }
            else if (grid[i][j].type == GRASS) {
                grid[i][j].grass_regrow_timer++;
            }
        }
    }
}

int find_nearest_in_original_grid(EntityType me, EntityType target, int x, int y, int* out_x, int* out_y) {
    int min_dist = GRID_SIZE * 2;
    int found = 0;
    int best_x = -1, best_y = -1;
    int radius = (me == RABBIT) ? 4 : 6;

    for (int dx = -radius; dx <= radius; dx++) {
        for (int dy = -radius; dy <= radius; dy++) {
            int nx = x + dx, ny = y + dy;
            if (!is_valid(nx, ny)) continue;
            if (grid[nx][ny].type == target) {
                int dist = abs(dx) + abs(dy);
                if (dist < min_dist) {
                    min_dist = dist;
                    best_x = nx;
                    best_y = ny;
                    found = 1;
                }
            }
        }
    }
    if (found) {
        *out_x = best_x;
        *out_y = best_y;
        return 1;
    }
    return 0;
}

void update_entities() {
    Entity new_grid[GRID_SIZE][GRID_SIZE];
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            new_grid[i][j].type = EMPTY;
            new_grid[i][j].grass_regrow_timer = 0;
        }
    }

    rabbit_count = wolf_count = grass_count = 0;

    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            if (grid[i][j].type == GRASS) {
                new_grid[i][j] = grid[i][j];
                grass_count++;
            }
        }
    }

    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            if (grid[i][j].type == RABBIT || grid[i][j].type == WOLF) {
                Entity* e = &grid[i][j];
                int new_x = i, new_y = j;
                int energy_gain = 0;

                int move_cost = (e->type == WOLF) ? 2 : 1;
                if (season == 3) move_cost *= 2;

                int dx = 0, dy = 0;
                if (e->type == RABBIT) {
                    int tx, ty;
                    if (find_nearest_in_original_grid(RABBIT, GRASS, i, j, &tx, &ty)) {
                        dx = (tx - i > 0) ? 1 : (tx - i < 0) ? -1 : 0;
                        dy = (ty - j > 0) ? 1 : (ty - j < 0) ? -1 : 0;
                    }
                    else {
                        int dirs[8][2] = { {-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1} };
                        int idx = rand() % 8;
                        dx = dirs[idx][0];
                        dy = dirs[idx][1];
                    }
                }
                else if (e->type == WOLF) {
                    int tx, ty;
                    if (find_nearest_in_original_grid(WOLF, RABBIT, i, j, &tx, &ty)) {
                        dx = (tx - i > 0) ? 1 : (tx - i < 0) ? -1 : 0;
                        dy = (ty - j > 0) ? 1 : (ty - j < 0) ? -1 : 0;
                    }
                    else {
                        int dirs[8][2] = { {-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1} };
                        int idx = rand() % 8;
                        dx = dirs[idx][0];
                        dy = dirs[idx][1];
                    }
                }

                int nx = i + dx;
                int ny = j + dy;
                if (is_valid(nx, ny) && (new_grid[nx][ny].type == EMPTY || new_grid[nx][ny].type == GRASS)) {
                    new_x = nx;
                    new_y = ny;
                }

                if (e->type == RABBIT && grid[new_x][new_y].type == GRASS) {
                    energy_gain = 10;
                }
                else if (e->type == WOLF && grid[new_x][new_y].type == RABBIT) {
                    energy_gain = 25;
                }

                new_grid[new_x][new_y].type = e->type;
                new_grid[new_x][new_y].x = new_x;
                new_grid[new_x][new_y].y = new_y;
                new_grid[new_x][new_y].age = e->age + 1;
                new_grid[new_x][new_y].max_age = e->max_age;
                new_grid[new_x][new_y].energy = e->energy - move_cost + energy_gain;
                new_grid[new_x][new_y].grass_regrow_timer = 0;
            }
        }
    }

    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            Entity* e = &new_grid[i][j];
            if (e->type == GRASS) {
                grass_count++;
            }
            else if (e->type == RABBIT || e->type == WOLF) {
                if (e->energy <= 0 || e->age > e->max_age) {
                    e->type = EMPTY;
                    continue;
                }

                if (e->type == RABBIT) rabbit_count++;
                else if (e->type == WOLF) wolf_count++;

                double breed_prob = 0.0;
                int min_energy = 0;
                if (e->type == RABBIT) {
                    breed_prob = 0.35;
                    min_energy = 22;
                }
                else if (e->type == WOLF) {
                    breed_prob = 0.20;
                    min_energy = 35;
                }

                if (e->energy >= min_energy && rand() / (double)RAND_MAX < breed_prob) {
                    handle_reproduction_in_new_grid(e, new_grid);
                }
            }
        }
    }

    memcpy(grid, new_grid, sizeof(grid));
}

void handle_reproduction_in_new_grid(Entity* parent, Entity new_grid[GRID_SIZE][GRID_SIZE]) {
    for (int attempt = 0; attempt < 12; attempt++) {
        int dx = (rand() % 3) - 1;
        int dy = (rand() % 3) - 1;
        if (dx == 0 && dy == 0) continue;
        int nx = parent->x + dx;
        int ny = parent->y + dy;
        if (is_valid(nx, ny) && new_grid[nx][ny].type == EMPTY) {
            new_grid[nx][ny].type = parent->type;
            new_grid[nx][ny].x = nx;
            new_grid[nx][ny].y = ny;
            new_grid[nx][ny].age = 0;
            new_grid[nx][ny].max_age = parent->max_age;
            new_grid[nx][ny].energy = (parent->type == RABBIT) ? 10 : 15;
            new_grid[nx][ny].grass_regrow_timer = 0;
            parent->energy -= (parent->type == RABBIT) ? 10 : 15;
            if (parent->energy < 5) parent->energy = 5;
            break;
        }
    }
}

int is_valid(int x, int y) {
    return x >= 0 && x < GRID_SIZE && y >= 0 && y < GRID_SIZE;
}

void save_snapshot() {
    char filename[128];
    sprintf(filename, "生态系统_回合_%d.txt", tick);
    FILE* f = fopen(filename, "w");
    if (!f) {
        set_message("保存失败！无法创建文件");
        return;
    }
    fprintf(f, "生态系统快照 - 回合 %d（季节：%s）\n", tick, season_names[season]);
    fprintf(f, "青草: %d, 兔子: %d, 狼: %d\n\n",
        grass_count, rabbit_count, wolf_count);
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            switch (grid[i][j].type) {
            case EMPTY: fputc('.', f); break;
            case GRASS: fputc('G', f); break;
            case RABBIT: fputc('r', f); break;
            case WOLF: fputc('W', f); break;
            }
        }
        fputc('\n', f);
    }
    fclose(f);

    clear_screen();
    print_map();
    print_status();
    print_history_chart();
    print_legend();
    print_controls();
    printf("\n 快照已保存至: %s\n", filename);
    printf("按任意键继续...\n");
    _getch();
    set_message("快照已保存");
}