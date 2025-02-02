#pragma once
#define MIN_GL_VERSION_MAJOR 4
#define MIN_GL_VERSION_MINOR 6

#define WINDOW_WIDTH  1920
#define WINDOW_HEIGHT 1080

#define GLERROR(str)  { if(const auto ___GL_error___= glGetError(); ___GL_error___) fprintf(stderr, "## GL Error[%s/%s:%d]: %d\n", str, __FILE__, __LINE__, ___GL_error___); }


enum MaterialCtrl
{
	UseMaterials,
	NoMaterials,
};
