#include "AI.h"
#include "Constants.h"

extern const bool asynchronous = true;

#include <random>
#include <iostream>
#include <thread>
#include <ctime>
#include <cmath>

extern const THUAI4::JobType playerJob = THUAI4::JobType::Job1;

namespace
{
    [[maybe_unused]] std::uniform_real_distribution<double> direction(0, 2 * 3.1415926);
    [[maybe_unused]] std::default_random_engine e{std::random_device{}()};
}

static bool first = true;
static int area = 0;
static uint32_t askhelpclock = clock();
bool g_attack(GameApi &g, int X, int Y);
void g_defence(GameApi &g);
bool (GameApi::*move[4])(uint32_t) = {&GameApi::MoveDown, &GameApi::MoveRight, &GameApi::MoveUp, &GameApi::MoveLeft};
std::default_random_engine r((unsigned)time(NULL));
bool g_coloring_4(GameApi &g);
void g_flee(GameApi &g, int teamID);

void AI::play(GameApi &g)
{
    static THUAI4::ColorType selfcolor = g.GetSelfTeamColor();
    auto self = g.GetSelfInfo();
    if (self->isDying)
    {
        first = true;
        return;
    }
    if (first) //从起始点到达中间位置
    {
        if (!self->teamID)
        {
            for (int i = 0; i < 20; i++)
            {
                g_defence(g);
                g.MoveDown(50);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        else
        {
            for (int i = 0; i < 20; i++)
            {
                g_defence(g);
                g.MoveUp(50);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        for (int i = 0; i < 110; i++)
        {
            g_defence(g);
            g.MoveLeft(50);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        first = false;
    } //初始移动（如果该队员相对位置发生变化，需要考虑预设移动的方位）
    if (self->bulletNum == 0 && g.GetCellColor(self->x / 1000, self->y / 1000) != g.GetSelfTeamColor() && (clock() > askhelpclock + 5 * CLOCKS_PER_SEC))
    {
        std::string message = "1";
        message += char(self->x / 1000);
        message += char(self->y / 1000);
        message += char(1);
        g.Send(0, message);
        askhelpclock = clock();
    }

    self = g.GetSelfInfo();
    int nowBulletNum = self->bulletNum;
    while (nowBulletNum > 5)
    {
        if (nowBulletNum > 10)
        {
            g_attack(g, 47 - self->teamID * 45, 2);
            g_attack(g, 47 - self->teamID * 45, 47);
        }
        if (g_coloring_4(g))
            --nowBulletNum;
    }
    for (int i = 0; i < 80; i++)
    {
        g_defence(g);
        if (g.GetCellColor(self->x / 1000, self->y / 1000) != g.GetSelfTeamColor())
            g.Attack(0, (area + 1) % 4 * 3.1415926 / 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool g_attack(GameApi &g, int X, int Y) //攻击函数
{
    auto self = g.GetSelfInfo();
    if (self->bulletNum)
    {
        int X0 = self->x / 1000, Y0 = self->y / 1000;
        double distance = sqrt((X - X0) * (X - X0) + (Y - Y0) * (Y - Y0));
        double fly_time = distance / 12.0 * 1000.0;
        double fly_angle = atan(double(Y - Y0) / double(X - X0));
        if (X - X0 < 0)
            fly_angle += 3.1415926;
        g.Attack(fly_time, fly_angle);
        return true;
    }
    else
        return false;
}
void g_defence(GameApi &g)
{
    static int playerID = 3;
    static uint32_t messageclock = clock();
    static uint32_t defenceclock = clock();
    auto ch = g.GetCharacters();
    if (ch.size() > 1)
    {
        auto self = g.GetSelfInfo();
        int bulletnum = self->bulletNum;
        for (int i = 0; i < ch.size(); i++)
            if (ch[i]->teamID != g.GetSelfInfo()->teamID && !ch[i]->isDying && ch[i]->jobType != THUAI4::JobType::Job6) //发现敌人(不是铁扇公主)
            {
                if (bulletnum >= 2 && (clock() > defenceclock + CLOCKS_PER_SEC / 5)) //防御间隔至少0.2秒
                {
                    for (int k = 0; k < ch[i]->hp / 750 + 1 && k < 6 && k < bulletnum - 2; k++) //此处有修改k！！！！！！！！
                    {
                        g_attack(g, ch[i]->x / 1000, ch[i]->y / 1000);
                    }
                    defenceclock = clock();
                }
                if (clock() >= messageclock + 5 * CLOCKS_PER_SEC) //发送消息间隔至少5秒
                {
                    std::string message = "";
                    message += "1";
                    message += char(ch[i]->x / 1000);
                    message += char(ch[i]->y / 1000);
                    message += char((ch[i]->hp / 750 - 5) / 3 + 1);
                    for (int j = 0; j < 4; j++)
                        if (j != playerID)
                            g.Send(j, message);
                    messageclock = clock();
                }
                g_flee(g, self->teamID);
                break;
            }
    }
    if (g.MessageAvailable())
    {
        int bulletnum = g.GetSelfInfo()->bulletNum;
        std::string message;
        g.TryGetMessage(message);
        if (bulletnum >= 2)
            for (int i = 0; i < int(message[3]) && i < 2; i++)
                g_attack(g, int(message[1]), int(message[2]));
    }
}
bool g_coloring_4(GameApi &g)
{
    static int colorcount = -1;
    static int team = g.GetSelfInfo()->teamID;
    static THUAI4::ColorType teamcolor = g.GetSelfTeamColor();
    colorcount++;
    int p = 3 * ((colorcount * 5 % 48) % 3) + 41;
    int q = 3 * ((colorcount * 5 % 48) / 3) + 2;
    if (g.GetCellColor(team * 49 - (2 * team - 1) * p, q) != teamcolor ||
        g.GetCellColor(team * 49 - (2 * team - 1) * p + 1, q) != teamcolor ||
        g.GetCellColor(team * 49 - (2 * team - 1) * p - 1, q) != teamcolor ||
        g.GetCellColor(team * 49 - (2 * team - 1) * p, q + 1) != teamcolor ||
        g.GetCellColor(team * 49 - (2 * team - 1) * p, q - 1) != teamcolor)
    {
        g_attack(g, team * 49 - (2 * team - 1) * p, q);
        return true;
    }
    else
        return false;
}
void g_flee(GameApi &g, int teamID)
{
    static uint32_t fleeclock = 0;
    if (clock() - fleeclock > 2 * CLOCKS_PER_SEC)
    {
        fleeclock = clock();
        auto self = g.GetSelfInfo();
        int r = 2 * (rand() % 2);
        for (int j = 0; j < 24; j++)
        {
            g_defence(g);
            (g.*move[area])(50);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}