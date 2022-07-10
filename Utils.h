//
// Created by projector on 7/9/22.
//

#ifndef RAFTFUSEFS_UTILS_H
#define RAFTFUSEFS_UTILS_H

#include <iostream>
#include <string>
using namespace std;
#define LVAR(var)   " " << #var << "="<< var << " "

class Utils {

};

extern int IsCallBack();
extern int IsLeaderCallBack();
extern int IsFollowerCallBack();
extern void SetCallBackFlag(int type=1);
extern void ClearCallBackFlag();

class CallBackLock {
public:
    CallBackLock() {
        SetCallBackFlag();
    }
    CallBackLock(int type) {
        SetCallBackFlag(type);
    }
    ~CallBackLock() {
        ClearCallBackFlag();
    }
};

#endif //RAFTFUSEFS_UTILS_H
