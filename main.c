#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <math.h>

#define MIN_SCALE 0.25
#define MAX_SCALE 2.5
#define MAX_UNITS 75
#define MAX_TEAMS 8
#define PI 3.14159
#define STROBE (SDL_GetTicks()/3)

struct unit {
	int x, y, px, py;
	int type, team, moves;
	float progress;
};

struct team {
	int cash, income, flag;
};

SDL_Texture *tileset = NULL;
SDL_Texture *unitSheet = NULL;
SDL_Texture *flagSheet = NULL;
SDL_Texture *uiSheet = NULL;
SDL_Texture *fontSheet = NULL;

SDL_Renderer *renderer = NULL;
SDL_Window *window = NULL;

int *map = NULL, mapW, mapH;
float scale = 1.5;
float cameraX = 0, cameraY = 0;
struct unit *units[MAX_UNITS] = {NULL};
struct team *teams[MAX_TEAMS] = {NULL};
SDL_Event event;
bool redraw, quit, panning;
int oldMouseX, oldMouseY;
int lastUpdate;
int cursorX, cursorY;
int selectedUnit;
float bounce = 0, bounceV = 0.01;
bool cursor;
int *vmap;
const int unitRanges[] = {
	2, 4, 2, 6,
	2, 2, 2,
};

void initSDL() {
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	assert(SDL_Init(SDL_INIT_EVERYTHING) >= 0);
	window = SDL_CreateWindow("MiniHex",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			640, 480,
			SDL_WINDOW_RESIZABLE);
	assert(window);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	assert(renderer);
	SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0xff);
}

void endSDL() {
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}

SDL_Texture *loadTexture(const char *filename) {
	SDL_Surface *loadedSurface = SDL_LoadBMP(filename);
	assert(loadedSurface);
	SDL_SetColorKey(loadedSurface, SDL_TRUE,
			SDL_MapRGB(loadedSurface->format, 0xff, 0x00, 0xff));
	SDL_Texture *newTexture = SDL_CreateTextureFromSurface(
			renderer, loadedSurface);
	SDL_FreeSurface(loadedSurface);
	assert(newTexture);
	return newTexture;
}

void loadMedia() {
	tileset = loadTexture("data/tileset.bmp");
	unitSheet = loadTexture("data/units.bmp");
	flagSheet = loadTexture("data/flags.bmp");
	SDL_SetTextureAlphaMod(flagSheet, 0xc0);
	uiSheet = loadTexture("data/ui.bmp");
	fontSheet = loadTexture("data/font.bmp");
}

void freeMedia() {
	SDL_DestroyTexture(fontSheet);
	SDL_DestroyTexture(uiSheet);
	SDL_DestroyTexture(flagSheet);
	SDL_DestroyTexture(unitSheet);
	SDL_DestroyTexture(tileset);
}

void loadLevel(const char *filename) {
	FILE *fp = fopen(filename, "r");
	assert(fp);
	fscanf(fp, "%d%d", &mapW, &mapH);
	map = malloc(sizeof(int)*mapW*mapH);
	vmap = malloc(sizeof(int)*mapW*mapH);
	for(int i = 0; i < mapW*mapH; i++)
		fscanf(fp, "%d", &map[i]);

	int t;
	fscanf(fp, "%d", &t);
	for(int i = 0; i < t; i++) {
		teams[i] = malloc(sizeof(struct team));
		fscanf(fp, "%d%d%d", &teams[i]->flag,
				&teams[i]->cash, &teams[i]->income);
	}

	fscanf(fp, "%d", &t);
	for(int i = 0; i < t; i++) {
		units[i] = malloc(sizeof(struct unit));
		fscanf(fp, "%d%d%d%d", &units[i]->team, &units[i]->type,
				&units[i]->x, &units[i]->y);
		units[i]->moves = 0;
		units[i]->px = units[i]->x;
		units[i]->py = units[i]->y;
		units[i]->progress = 1;
	}
	fclose(fp);
}

void freeLevel() {
	if(map) {
		free(map);
		free(vmap);
		map = NULL;
	}
	for(int i = 0; i < MAX_TEAMS; i++)
		if(teams[i]) {
			free(teams[i]);
			teams[i] = NULL;
		}
	for(int i = 0; i < MAX_UNITS; i++)
		if(units[i]) {
			free(units[i]);
			units[i] = NULL;
		}
}

void drawText(const char *text, int x, int y) {
	SDL_Rect src, dst;
	dst.x = x;
	dst.y = y;
	dst.w = 8;
	dst.h = 16;
	for(; *text; text++) {
		src.x = (*text%16)*8;
		src.y = (*text/16)*16;
		src.w = 8;
		src.h = 16;
		SDL_RenderCopy(renderer, fontSheet, &src, &dst);
		dst.x += 8;
		if(*text == '\n') {
			dst.y += 16;
			dst.x = x;
		}
	}
}

void drawMap(int xo, int yo) {
	SDL_Rect src, dst;
	src.w = 32;
	src.h = 32;
	dst.w = 32*scale + 0.5;
	dst.h = 32*scale + 0.5;
	for(int x = 0; x < mapW; x++)
		for(int y = 0; y < mapH; y++) {
			int t = map[y*mapW+x];
			src.x = (t%4)*32;
			src.y = (t/4)*32;
			dst.x = x*32*scale + !(y%2)*16*scale - xo - 0.5;
			dst.y = y*24*scale - yo - 0.5;
			SDL_RenderCopy(renderer, tileset, &src, &dst);
		}
}

void drawUnits(int xo, int yo) {
	for(int i = 0; i < MAX_UNITS; i++) {
		if(!units[i])
			continue;
		SDL_Rect src, dst;
		src.x = (units[i]->type % 4)*24;
		src.y = (units[i]->type / 4)*24;
		src.w = 24;
		src.h = 24;
		int px, py, x, y;
		px = units[i]->px*32*scale + !(units[i]->py%2)*16*scale
			+ 4*scale - xo;
		py = units[i]->py*24*scale - yo + 4*scale;
		x = units[i]->x*32*scale + !(units[i]->y%2)*16*scale
			+ 4*scale - xo;
		y = units[i]->y*24*scale - yo + 4*scale;
		dst.x = x + (px - x) * (1.0 - units[i]->progress);
		dst.y = y + (py - y) * (1.0 - units[i]->progress);
		if(units[i]->moves)
			dst.y -= bounce*8*scale;
		dst.w = 24*scale;
		dst.h = 24*scale;
		SDL_RenderCopy(renderer, unitSheet, &src, &dst);

		src.x = (teams[units[i]->team]->flag%4)*8;
		src.y = (teams[units[i]->team]->flag/4)*8;
		src.w = 8;
		src.h = 8;
		dst.w = 8*scale;
		dst.h = 8*scale;
		SDL_RenderCopy(renderer, flagSheet, &src, &dst);
	}
}

void drawCursor(int xo, int yo) {
	if(!cursor)
		return;
	SDL_Rect src, dst;
	src.x = 0;
	src.y = 0;
	src.w = 32;
	src.h = 32;
	dst.x = cursorX*32*scale + !(cursorY%2)*16*scale - 0.5 - xo;
	dst.y = cursorY*24*scale - 0.5 - yo;
	dst.w = 32*scale + 0.5;
	dst.h = 32*scale + 0.5;
	SDL_SetTextureAlphaMod(uiSheet, STROBE);
	SDL_RenderCopy(renderer, uiSheet, &src, &dst);
	SDL_SetTextureAlphaMod(uiSheet, 0xff);
}

void drawVmap(int xo, int yo) {
	SDL_SetTextureAlphaMod(uiSheet, STROBE);
	SDL_Rect src, dst;
	src.x = 0;
	src.y = 0;
	src.w = 32;
	src.h = 32;
	dst.w = 32*scale + 0.5;
	dst.h = 32*scale + 0.5;
	for(int x = 0; x < mapW; x++)
		for(int y = 0; y < mapH; y++) {
			if(!vmap[y*mapW+x])
				continue;
			dst.x = x*32*scale + !(y%2)*16*scale - xo - 0.5;
			dst.y = y*24*scale - yo - 0.5;
			SDL_RenderCopy(renderer, uiSheet, &src, &dst);
		}
	SDL_SetTextureAlphaMod(uiSheet, 0xff);
}

void drawUI() {
	char cashStr[35];
	sprintf(cashStr, "$: %d (+%d)", teams[0]->cash, teams[0]->income);
	drawText(cashStr, 0, 0);
}

void draw() {
	int w, h;
	SDL_GetWindowSize(window, &w, &h);
	int xo, yo;
	xo = cameraX*scale - w/2;
	yo = cameraY*scale - h/2;

	SDL_RenderClear(renderer);
	drawMap(xo, yo);
	drawVmap(xo, yo);
	drawCursor(xo, yo);
	drawUnits(xo, yo);
	drawUI();
	SDL_RenderPresent(renderer);
}

void restrictCamera() {
	if(cameraX < 0)
		cameraX = 0;
	if(cameraY < 0)
		cameraY = 0;
	if(cameraX > mapW*32 + 16)
		cameraX = mapW*32 + 16;
	if(cameraY > mapH*24)
		cameraY = mapH*24;
}

void pan() {
	int mx, my;
	SDL_GetMouseState(&mx, &my);
	cameraX += (oldMouseX - mx) / scale;
	cameraY += (oldMouseY - my) / scale;
	restrictCamera();
}

void restrictScale() {
	if(scale < MIN_SCALE)
		scale = MIN_SCALE;
	if(scale > MAX_SCALE)
		scale = MAX_SCALE;
}

bool restrictCursor() {
	bool r = false;
	if(cursorX < 0) {
		cursorX = 0;
		r = true;
	}
	if(cursorY < 0) {
		cursorY = 0;
		r = true;
	}
	if(cursorX >= mapW) {
		cursorX = mapW-1;
		r = true;
	}
	if(cursorY >= mapH) {
		cursorY = mapH-1;
		r = true;
	}
	return r;
}

int unitAt(int x, int y) {
	for(int i = 0; i < MAX_UNITS; i++) {
		if(!units[i])
			continue;
		if(units[i]->x == x && units[i]->y == y)
			return i;
	}
	return -1;
}

void updateVmap() {
	for(int i = 0; i < mapW*mapH; i++)
		vmap[i] = false;
	if(selectedUnit == -1)
		return;
	int t = units[selectedUnit]->type - 8;
	if(t < 0)
		return;

	bool blocks[4];
	Uint8 bblk = 0x90;
	if(t == 3)
		bblk = 0x70;
	for(int i = 0; i < 4; i++)
		blocks[i] = (0x80 >> i) & bblk;

	int *wvmap = malloc(sizeof(int)*mapW*mapH);
	for(int i = 0; i < mapW*mapH; i++)
		wvmap[i] = blocks[map[i]]*-1;
	for(int i = 0; i < MAX_UNITS; i++) {
		if(!units[i])
			continue;
		wvmap[units[i]->y*mapW+units[i]->x] = -1;
	}
	wvmap[units[selectedUnit]->y*mapW+units[selectedUnit]->x] = 1;

	const int dirs[] = {-1,-1, 0,-1, 1,0, 0,1, -1,1, -1,0};
	int maplen = mapW*mapH;
	for(int i = 1; i <= units[selectedUnit]->moves; i++) {
		for(int *t = wvmap; t < wvmap+maplen; t++) {
			if(*t != i)
				continue;
			for(const int *d = dirs; d < dirs+6*2; d+=2) {
				int x, y;
				y = *(d+1);
				x = *d;
				if(y)
					x += !(((t-wvmap)/mapW)%2);
				int *tt = t + x + y * mapW;
				if(tt < wvmap || tt >= wvmap+maplen)
					continue;
				if(*tt)
					continue;
				*tt = i + 1;
			}
		}
	}
	wvmap[units[selectedUnit]->y*mapW+units[selectedUnit]->x] = -1;

	for(int i = 0; i < maplen; i++) {
		vmap[i] = (wvmap[i] - 1);
		if(vmap[i] < 0)
			vmap[i] = 0;
	}

	free(wvmap);
}

void click() {
	int mx, my;
	SDL_GetMouseState(&mx, &my);
	int w, h;
	SDL_GetWindowSize(window, &w, &h);
	int cx, cy;
	cy = ((cameraY*scale - h/2 + my)/scale)/24;
	cx = ((cameraX*scale - w/2 - !(cy%2)*16*scale + mx)/scale)/32;
	if(cursor) {
		if(cx == cursorX && cy == cursorY)
			cursor = false;
	}
	else
		cursor = true;
	cursorX = cx;
	cursorY = cy;
	if(restrictCursor())
		cursor = false;
	if(cursor) {
		int oldUnit = selectedUnit;
		selectedUnit = unitAt(cursorX, cursorY);
		if(selectedUnit == -1 && oldUnit != -1)
			if(vmap[cursorY*mapW+cursorX]) {
				units[oldUnit]->x = cursorX;
				units[oldUnit]->y = cursorY;
				units[oldUnit]->moves -=
					vmap[cursorY*mapW+cursorX];
				units[oldUnit]->progress = 0;
				cursor = false;
			}
	}
	else
		selectedUnit = -1;
	updateVmap();
}

void updateBounce() {
	bounce -= 0.5;
	int s = 1 - (bounceV < 0)*2;
	bounceV += 0.005*s;
	bounce += bounceV;
	if(bounce > 0.5)
		bounceV = -0.005;
	if(bounce < -0.5)
		bounceV = 0.005;
	bounce += 0.5;
}

void initTurn(int team) {
	for(int i = 0; i < MAX_UNITS; i++) {
		if(!units[i])
			continue;
		if(units[i]->type > 8)
			continue;
		if(units[i]->team == team)
			units[i]->moves = unitRanges[units[i]->type-8];
		else
			units[i]->moves = 0;
	}
}

void updateUnits() {
	for(int i = 0; i < MAX_UNITS; i++) {
		struct unit *u = units[i];
		if(!u)
			continue;
		if(u->progress < 1.0) {
			u->progress += 0.1;
			if(u->progress >= 1.0) {
				u->px = u->x;
				u->py = u->y;
			}
		}
	}
}

void update() {
	if(panning)
		pan();
	updateBounce();
	updateUnits();
	SDL_GetMouseState(&oldMouseX, &oldMouseY);
}

void initGame() {
	loadLevel("data/level1.lvl");
	redraw = true;
	quit = false;
	lastUpdate = SDL_GetTicks();
	SDL_GetMouseState(&oldMouseX, &oldMouseY);
	cursorX = 0;
	cursorY = 0;
	cursor = false;
	selectedUnit = -1;
	initTurn(0);
	updateVmap();
}

void endGame() {
	freeLevel();
}

void mainLoop() {
	while(SDL_PollEvent(&event)) {
		switch(event.type) {
		case SDL_QUIT:
			quit = true;
			break;
		case SDL_MOUSEBUTTONDOWN:
			switch(event.button.button) {
			case SDL_BUTTON_RIGHT:
				panning = true;
				break;
			case SDL_BUTTON_LEFT:
				click();
				break;
			}
			break;
		case SDL_MOUSEBUTTONUP:
			switch(event.button.button) {
			case SDL_BUTTON_RIGHT:
				panning = false;
				break;
			}
			break;
		case SDL_MOUSEWHEEL:
			scale += 0.25 * event.wheel.y;
			restrictScale();
			break;
		}
	}
	
	int currentTime = SDL_GetTicks();
	while(currentTime - lastUpdate > 10) {
		redraw = true;
		update();
		lastUpdate += 10;
	}

	if(redraw) {
		draw();
		redraw = false;
	}
}

int main(int argc, char **args) {
	initSDL();
	loadMedia();

	initGame();
	while(!quit)
		mainLoop();
	endGame();

	freeMedia();
	endSDL();

	return 0;
}
