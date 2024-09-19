#ifndef _GAME_H_
#define _GAME_H_

enum DIRECTION {
    leftUp = 0,
    leftDown = 1,
    rightUp = 2,
    rightDown = 3
};

typedef enum DIRECTION Direction;

struct Egg {
    int frame;
    Direction direction;
    int posX;
    int posY;
};

struct EggList {
    struct Egg* egg;
    struct EggList* next;
};

typedef struct EggList* List;

extern List list;

extern Direction woldDir;
extern int Score;
extern int lifes;

extern List listInitialization(void);
extern List addEgg(List, struct Egg*);
extern List deleteEgg(List, struct Egg*);
extern int kill(List);
extern void destroy(List);

extern struct Egg* randomEgg(int r);

extern void listFunction(void (*function)(struct Egg*), List);
extern void printEgg(struct Egg*);
extern void move(struct Egg*);

extern void draw(void);
extern int chooseDirection(int);
extern List collision(List, Direction);

#endif
