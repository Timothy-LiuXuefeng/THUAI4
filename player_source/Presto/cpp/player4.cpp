#include "AI.h"
#include "Constants.h"
#include <iomanip>
#include <thread>
#define pi 3.1415926535

//为假则play()调用期间游戏状态更新阻塞，为真则只保证当前游戏状态不会被状态更新函数与GameApi的方法同时访问
extern const bool asynchronous = true;

#include <random>
#include <iostream>

/* 请于 VS2019 项目属性中开启 C++17 标准：/std:c++17 */

extern const THUAI4::JobType playerJob = THUAI4::JobType::HappyMan; //选手职业，选手 !!必须!! 定义此变量来选择职业

namespace
{
	[[maybe_unused]] std::uniform_real_distribution<double> direction(0, 2 * pi);
	[[maybe_unused]] std::default_random_engine e{ std::random_device{}() };
}

static int x00, y00;//出生点坐标
static int calledcount = 0;//调用次数
static int myid;//我方teamID
static int enmid;//敌方teamID
static THUAI4::ColorType mycolor;//我方颜色
static int indanger;//是否被瞄准
static int seekingprp;//是否正在寻找道具
static int beingchased;//是否遇见敌人
static int blocked;//是否卡住
static int restingflag;//是否停下休息
static int enmpoint1_x, enmpoint1_y, enmpoint2_x, enmpoint2_y;//敌人的出生点区块中心坐标
static int firepoint_x, firepoint_y;//轰炸点
static int deltax, deltay;//轰炸点坐标变化

int distance(uint32_t x, uint32_t y, GameApi& g) {//坐标(x,y)与我间距
	return (int)sqrt(((int)x - (int)g.GetSelfInfo()->x) * ((int)x - (int)g.GetSelfInfo()->x) + ((int)y - (int)g.GetSelfInfo()->y) * ((int)y - (int)g.GetSelfInfo()->y));
}
double angle(uint32_t x, uint32_t y, GameApi& g) {//我到坐标(x,y)的角度
	return atan2((int)y - (int)g.GetSelfInfo()->y, (int)x - (int)g.GetSelfInfo()->x);
}
std::string space(int k)
{
	std::string s;
	for (int i = 0; i < k; i++)
		s = s + " ";
	return s;
}
std::string infobox(int x, int y, bool isDying, int typeofmessage)
{
	std::string sx = std::to_string(x);
	std::string sy = std::to_string(y);
	std::string sisDying = std::to_string(isDying);
	std::string stypeofmessage = std::to_string(typeofmessage);
	std::string buffer = sx + space(8 - sx.size()) + sy + space(8 - sy.size()) + sisDying + space(1) + stypeofmessage + space(1);//注意长度限制
	return buffer;
}//写入信息，末尾的信息类型计划是用来辨别信息是关于敌还是友的，但若如此需再加一条信息，即队友的编号
struct MessageGroup
{
	int x;
	int y;
	bool isDying;
	int typeofmessage;
};
MessageGroup extractinfo(std::string buffer)
{
	char* cbuffer = new char[buffer.size()];
	cbuffer = buffer.data();
	int ox = atoi(&cbuffer[0]);
	int oy = atoi(&cbuffer[8]);
	bool oisDying = atoi(&cbuffer[16]);
	int otypeofmessage = atoi(&cbuffer[18]);
	MessageGroup r = { ox,oy,oisDying,otypeofmessage };
	return r;
}


void AI::play(GameApi& g)
{
	calledcount++;
	auto guid = g.GetPlayerGUIDs();

	if (calledcount == 1) {//初始化
		myid = g.GetSelfInfo()->teamID;
		enmid = 1 ^ myid;
		mycolor = g.GetSelfTeamColor();
		x00 = (int)g.GetSelfInfo()->x;
		y00 = (int)g.GetSelfInfo()->y;
		if (myid == 0) {
			enmpoint1_x = 50000 - 2500;
			enmpoint1_y = 2500;
			enmpoint2_x = 50000 - 2500;
			enmpoint2_y = 50000 - 2500;
			deltax = -3000;
			deltay = 3000;
			g.MoveDown(200);
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			g.Attack(0, 0);
		}
		else {
			enmpoint1_x = 2500;
			enmpoint1_y = 2500;
			enmpoint2_x = 2500;
			enmpoint2_y = 50000 - 2500;
			deltax = 3000;
			deltay = 3000;
			g.MoveUp(200);
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			g.Attack(0, pi);
		}
		firepoint_x = enmpoint1_x;
		firepoint_y = enmpoint1_y;
		g.Attack(distance(enmpoint1_x, enmpoint1_y, g) / 12, angle(enmpoint1_x, enmpoint1_y, g));
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		g.Attack(distance(enmpoint2_x, enmpoint2_y, g) / 12, angle(enmpoint2_x, enmpoint2_y, g));
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}

	int xnow = (int)g.GetSelfInfo()->x;
	int ynow = (int)g.GetSelfInfo()->y;

	if (beingchased) goto enm;
	if (indanger) goto blt;
	if (seekingprp) goto prp;

	{//子弹检测与闪躲
	blt:
		auto blt = g.GetBullets();
		indanger = 0;
		for (int i = 0; i < blt.size(); i++) {
			if ((blt[i]->teamID != myid) && (blt[i]->bulletType == (THUAI4::BulletType)3) && (g.GetSelfInfo()->bulletNum > 0)) {
				g.Attack(distance(blt[i]->x, blt[i]->y, g) / 12, angle(blt[i]->x, blt[i]->y, g));
			}
			int dst = distance(blt[i]->x, blt[i]->y, g);
			double angle_blt = -angle(blt[i]->x, blt[i]->y, g);//子弹对我方的角度
			if ((blt[i]->teamID != myid) && (abs((double)dst * sin(angle_blt - blt[i]->facingDirection)) < 1250)) {//有敌方子弹瞄准自己所在区域
				if (angle_blt > blt[i]->facingDirection) {
					g.MovePlayer(200, blt[i]->facingDirection + pi / 3);
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
				}
				else {
					g.MovePlayer(200, blt[i]->facingDirection - pi / 3);
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
				}
				indanger = 1;
			}
		}
	}
	if (indanger) goto end;
	xnow = (int)g.GetSelfInfo()->x;
	ynow = (int)g.GetSelfInfo()->y;

	{//敌人检测与射击
	enm:
		auto oth = g.GetCharacters();
		beingchased = 0;
		for (int i = 0; i < oth.size(); i++) {
			int dst = distance(oth[i]->x, oth[i]->y, g);
			if (g.GetSelfInfo()->bulletNum <= 3 && oth[i]->teamID == myid && dst > 2000) {
				g.MovePlayer(5, angle(oth[i]->x, oth[i]->y, g));
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			if (!oth[i]->isDying && oth[i]->teamID != myid && (oth[i]->propType != (THUAI4::PropType)5 || g.GetSelfInfo()->propType == (THUAI4::PropType)7)) {
				g.Send(2, infobox((int)oth[i]->x, (int)oth[i]->y, 0, 2));
				g.Send(1, infobox((int)oth[i]->x, (int)oth[i]->y, 0, 2));
				//作为playerid=3
				if (g.GetSelfInfo()->bulletNum <= 3) {
					g.MovePlayer(distance(oth[i]->x, oth[i]->y, g) / 4, -angle(oth[i]->x, oth[i]->y, g));
					std::this_thread::sleep_for(std::chrono::milliseconds(distance(oth[i]->x, oth[i]->y, g) / 4 / 3));
				}
				else
					while (g.GetSelfInfo()->bulletNum > 0 && !oth[i]->isDying) {
						g.Attack(distance(oth[i]->x, oth[i]->y, g) / 12, angle(oth[i]->x, oth[i]->y, g));
						g.Wait();
					}
				beingchased = 1;
			}
		}
		if (g.MessageAvailable() == true && g.GetSelfInfo()->bulletNum > 0) {
			std::string info;
			g.TryGetMessage(info);
			MessageGroup INFO = extractinfo(info);
			g.Attack(distance(INFO.x, INFO.y, g) / 12, angle(INFO.x, INFO.y, g));
		}
	}
	if (beingchased) goto end;
	xnow = (int)g.GetSelfInfo()->x;
	ynow = (int)g.GetSelfInfo()->y;

	{//非特殊情况
		if (g.GetSelfInfo()->bulletNum >= 12) {
			restingflag = 0;
		}
		static int cell[9][9] = { -1 };//视野内情况
		if (restingflag == 0) {
			for (int i = -4; i <= 4; i++) {
				for (int j = -4; j <= 4; j++) {
					int k(-1);
					if (xnow / 1000 + i < 1 || xnow / 1000 + i >= 49 || ynow / 1000 + j < 1 || ynow / 1000 + j >= 49) k = -1;//边界以外
					else if (g.GetCellColor(xnow / 1000 + i, ynow / 1000 + j) == THUAI4::ColorType::Invisible) k = -2;//视野以外
					else if (g.GetCellColor(xnow / 1000 + i, ynow / 1000 + j) == mycolor) k = 1;//已涂色区域
					else if (g.GetCellColor(xnow / 1000 + i, ynow / 1000 + j) != mycolor) k = 0;//可涂色区域
					cell[i + 4][j + 4] = k;
				}
			}
			auto wal = g.GetWalls();
			for (int i = 0; i < wal.size(); i++) {
				int xx = (int)wal[i]->x / 1000 - xnow / 1000 + 4, yy = (int)wal[i]->y / 1000 - ynow / 1000 + 4;
				if (xx < 0 || xx>8 || yy < 0 || yy>8) continue;
				cell[xx][yy] = -3;
			}//记录墙的位置
			auto bir = g.GetBirthPoints();
			for (int i = 0; i < bir.size(); i++) {
				int xx = (int)bir[i]->x / 1000 - xnow / 1000 + 4, yy = (int)bir[i]->y / 1000 - ynow / 1000 + 4;
				if (xx < 0 || xx>8 || yy < 0 || yy>8) continue;
				cell[xx][yy] = -3;
			}//记录出生点的位置
			cell[0][0] = cell[0][1] = cell[1][0]
				= cell[0][8] = cell[0][7] = cell[1][8]
				= cell[8][0] = cell[7][0] = cell[8][1]
				= cell[8][8] = cell[8][7] = cell[7][8] = -2;//避免视野边界获取信息出错而撇去四个角的信息
		}

		int change = 0;
		if (g.GetSelfInfo()->bulletNum > 1 && restingflag == 0) {
			if (g.GetSelfInfo()->bulletNum > 1) {
				if (firepoint_x + deltax > 48500 && deltax == 3000) { deltax = -3000; change = 1; }
				if (firepoint_y + deltay > 48500 && deltay == 3000) { deltay = -3000; change = 1; }
				if (firepoint_x + deltax < 1500 && deltax == -3000) { deltax = 3000; change = 1; }
				if (firepoint_y + deltay < 1500 && deltay == -3000) { deltay = 3000; change = 1; }
				if (change) {
					firepoint_x += deltax;
					firepoint_y -= deltay;
				}
				firepoint_y += deltay;
				g.Attack(distance(firepoint_x, firepoint_y, g) / 12, angle(firepoint_x, firepoint_y, g));
			}
			g.Wait();
		}

		if (restingflag == 1 || g.GetSelfInfo()->bulletNum <= 1) {
			if (g.GetCellColor((int)g.GetSelfInfo()->x / 1000, (int)g.GetSelfInfo()->y / 1000) == g.GetSelfTeamColor()) {
				restingflag = 1;
			}
			else {
				restingflag = 0;
				int x = -1, y = -1;
				for (int i = -4; i <= 4; i++) {
					for (int j = -4; j <= 4; j++) {
						if (cell[i + 4][j + 4] == 1) {
							x = xnow + i * 1000;
							y = ynow + j * 1000;
							break;
						}
						else continue;
					}
				}
				if (x > 0 && y > 0) g.MovePlayer(200, angle(x, y, g));//向染色区域前进
				else {
					if (restingflag == 1 && g.GetSelfInfo()->bulletNum >= 1) {
						g.Attack(0, 0);
						g.MovePlayer(0, 0);
					}
					else {
						g.MovePlayer(100, g.GetSelfInfo()->facingDirection + direction(e) / 108);
						std::this_thread::sleep_for(std::chrono::milliseconds(100));
					}
				}
			}
		}
		if (restingflag == 0) {
			if ((cell[3][4] == -3 || cell[5][4] == -3) && cell[4][3] != -3 && cell[4][5] != -3) g.MovePlayer(50, pi / 2);
			if ((cell[4][3] == -3 || cell[4][5] == -3) && cell[3][4] != -3 && cell[5][4] != -3) g.MovePlayer(50, 0);
			if ((cell[5][4] == -3 && cell[4][5] == -3) && cell[4][3] != -3 && cell[3][4] != -3) g.MovePlayer(50, 3 * pi / 2);
			if ((cell[5][4] == -3 && cell[4][3] == -3) && cell[4][5] != -3 && cell[3][4] != -3) g.MovePlayer(50, pi);
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		if (restingflag == 1) goto end;
	}

	{//道具拾取
	prp:
		auto prp = g.GetProps();
		seekingprp = 0;
		for (int i = 0; i < prp.size(); i++) {
			int dst = distance(prp[i]->x, prp[i]->y, g);
			if (dst == 0) g.Pick(prp[i]->propType);
			if (prp.size() == 1) seekingprp = 1;
			else break;
			xnow = (int)g.GetSelfInfo()->x;
			ynow = (int)g.GetSelfInfo()->y;
			if (restingflag != 1 && dst != 0) {
				g.MovePlayer(dst / 4 > 200 ? 200 : dst / 4, angle(prp[i]->x, prp[i]->y, g));
				std::this_thread::sleep_for(std::chrono::milliseconds(dst / 4 > 200 ? 200 : dst / 4));
				g.Pick(prp[i]->propType);
			}
			if (g.GetSelfInfo()->propType != (THUAI4::PropType)0) {
				g.Use();
				seekingprp = 0;
			}
		}
		if (g.GetSelfInfo()->bulletNum <= 1 || g.GetSelfInfo()->bulletNum >= 11) seekingprp = 0;
	}
	if (seekingprp) goto end;

end:
	if (distance(xnow, ynow, g) <= 100 && restingflag == 0) blocked += 1;
	else blocked = 0;
	while (blocked) {
		xnow = (int)g.GetSelfInfo()->x;
		ynow = (int)g.GetSelfInfo()->y;
		g.MovePlayer(600, g.GetSelfInfo()->facingDirection + (double)blocked * pi / 4);
		std::this_thread::sleep_for(std::chrono::milliseconds(600));
		if (distance(xnow, ynow, g) <= 100) blocked += 1;
		else blocked = 0;
	}
	g.Wait();
	if (g.GetCounterOfFrames() % 100 == 0 && g.GetSelfInfo()->bulletNum > 2) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		g.Attack(distance(enmpoint1_x, enmpoint1_y, g) / 12, angle(enmpoint1_x, enmpoint1_y, g));
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		g.Attack(distance(enmpoint2_x, enmpoint2_y, g) / 12, angle(enmpoint2_x, enmpoint2_y, g));
		g.Wait();
	}
}