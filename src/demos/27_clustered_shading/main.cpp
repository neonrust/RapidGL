#include <memory>

#include "clustered_shading.h"

using namespace RGL;

int main()
{
	ClusteredShading app;
	app.init(1920, 1080, "Clustered Shading Demo" /*title*/, 60/*framerate*/);
	app.start();

    return 0;
}
