//
// Created by projector on 7/9/22.
//

#include "Utils.h"

//0--not callback 1--leader callback 2--follower  callback
static thread_local int g_CallBackFlag = 0;

int IsCallBack() {
    return g_CallBackFlag;
}

void SetCallBackFlag(int type) {
    g_CallBackFlag = type;
}

void ClearCallBackFlag() {
    g_CallBackFlag = 0;
}

int IsLeaderCallBack() {
    return g_CallBackFlag == 1;
}

int IsFollowerCallBack() {
    return g_CallBackFlag == 2;
}
