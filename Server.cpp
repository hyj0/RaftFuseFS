//
// Created by projector on 6/28/22.
//

#include "Server.h"
#include "FuseFS.h"

/*
mkdir -p  /tmp/FuseSrc/  /tmp/FuseTarget
sudo ./cmake-build-debug/Server -d -o uid=1003 -o gid=1003 -o allow_other /tmp/FuseTarget

 */
int main(int argc, char ** argv)
{
    FuseFS fuseFs;
    fuseFs.main(argc, argv);
}
