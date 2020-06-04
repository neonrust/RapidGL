#include <iostream>
#include <memory>

#include "simple_3d.h"

using namespace RapidGL;

int main()
{
    std::shared_ptr<CoreApp> app = std::make_shared<Simple3d>();
    app->init(800 /*width*/, 600 /*height*/, "Simple 3D Demo" /*title*/, 60.0 /*framerate*/);
    app->start();

    return 0;
}