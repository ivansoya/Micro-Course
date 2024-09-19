#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include "frames.h"
#include "game.h"

int eggSpeed = 12;
int startFrame = 0;
int killFrame = 4;

List list;

Direction woldDir = leftUp;
int Score = 0;

int lifes = 3;

List listInitialization(void) {
    List temp;
    temp = (List)malloc(sizeof(struct EggList));
    temp->egg = randomEgg(0);
    return temp;
}

struct Egg* randomEgg(int r) {
    if (r > 3 || r < 0) r = 0;
    struct Egg* egg = (struct Egg*)malloc(sizeof(struct Egg));
    egg->direction = (Direction)r;
    egg->frame = startFrame;
    egg->posX = staticPosX[r];
    egg->posY = staticPosY[r];
    return egg;
}

List addEgg(List list, struct Egg* egg) {
    List temp = (List)malloc(sizeof(struct EggList));
    temp->egg = egg;
    temp->next = list;
    list = temp;
    return list;
}

List deleteEgg(List list, struct Egg* egg) {
    if (list == NULL) {
        return list;
    }
    if (list->egg == egg) {
        List temp = list;
        list = list->next;
        free(temp->egg);
        free(temp);
        return list;
    } 
    else {
        List temp = list;
        List prev = NULL;
        while (temp->egg != egg && temp != NULL) {
            prev = temp;
            temp = temp->next;
        }
        prev->next = temp->next;
        free(temp->egg);
        free(temp);
        return list;
    }
}

void printEgg(struct Egg* egg) {
    printf("[%i, %i, %i, %i]", egg->frame, (int)egg->direction, egg->posX, egg->posY);
}

void move(struct Egg* egg) {
    switch (egg->direction) {
        case leftUp:
        case leftDown:
            egg->posX += eggSpeed;
            egg->posY += eggSpeed / 2;
            break;
        case rightUp:
        case rightDown:
            egg->posX -= eggSpeed;
            egg->posY += eggSpeed / 2;
            break;
    }
    egg->frame++;
    if (egg->frame > killFrame) {
        egg->frame = startFrame;
    }
}

List collision(List list, Direction dir) {
    if (list == NULL) return NULL;
    List temp = list;
    while (temp != NULL) {
        if (temp->egg->frame == killFrame) {
            if (temp->egg->direction == dir) {
                Score++;
            }
            else {
                lifes--;
            }
            return deleteEgg(list, temp->egg);
        }
        temp = temp->next;
    }
    return list;
}

int chooseDirection(int dir) {
    if (dir >= 0 && dir <= 3) {
        return (Direction)dir;
    } 
    else {
        return woldDir;
    }
}

void listFunction(void (*function)(struct Egg*), List list) {
    if (list == NULL) return;
    List temp = list;
    while (temp != NULL) {
        (*function)(temp->egg);
        temp = temp->next;
    }
}

void destoy(List list) {
    while(list != NULL) {
        list = list->next;
        free(list->egg);
        free(list);
    }
}

int findDeadEgg(List list, struct Egg** egg){
    if (list == NULL) return 0;
    List temp = list;
    while (temp != NULL) {
        if (temp->egg->frame == killFrame) {
            (*egg) = temp->egg;
            return 1;
        }
        temp = temp->next;
    }
    return 0;
}
