#include "zigapp.h"

int main(int argc, char *argv[])
{
	ZigApp app;
	app.parse_arguments({ &argv[1], &argv[argc] });
	app.init(1920, 1080, "Zig App" /*title*/, 60/*framerate*/);
	return app.run();
}
