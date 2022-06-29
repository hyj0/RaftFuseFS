//
// Created by projector on 6/28/22.
//

#include "Server.h"
#include "FuseFS.h"

int main(int argc, char ** argv)
{
    FuseFS fuseFs;
    fuseFs.main(argc, argv);
}
