#include "zigapp.h"

int main()
{
	ZigApp app;
	app.init(1920, 1080, "Zig App" /*title*/, 60/*framerate*/);
	return app.run();
}
