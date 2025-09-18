#include "ktx_loader.h"

#include "ktx.h"
#include "zstr.h"
#include <GLFW/glfw3.h>

#include <print>

using namespace std::literals;

namespace RGL
{

static struct KTX_Loader
{
	bool support_astc  { false };
	bool support_bc1   { false };
	bool support_bc3   { false };
	bool support_bc6   { false };
	bool support_bc7   { false };
	bool support_pvrtc { false };

	ktx_transcode_fmt_e pick_format(ktxTexture2 *tex);

} s_ktx_loader;


static void init_ktx_loader();

GLuint ktx_load(const std::filesystem::path &filepath, ImageMeta &meta, size_t  dimensions, bool array)
{
	static struct InitKtx { InitKtx() { init_ktx_loader(); }; } _init_ktx;

	ktxTexture2 *ktx_tex = nullptr;
	ktxTextureCreateFlags flags = KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT | KTX_TEXTURE_CREATE_SKIP_KVDATA_BIT;
	auto rc = ktxTexture2_CreateFromNamedFile(filepath.c_str(), flags, &ktx_tex);
	if(rc != KTX_SUCCESS)
	{
		std::print("[{}] KTX open failed: {}\n", filepath.string(), int(rc));
		return 0;
	}

	assert(ktx_tex->classId == ktxTexture2_c);
	assert(ktx_tex->isVideo == KTX_FALSE);

	if(dimensions != ktx_tex->numDimensions)
	{
		std::print("[{}] KTX expected {} dimensions, got {}\n", filepath.string(), dimensions, ktx_tex->numDimensions);
		return 0;
	}
	if(array != bool(ktx_tex->isArray))
	{
		std::print("[{}] KTX {}expected array\n", filepath.string(), array?"":"un");
		ktxTexture2_Destroy(ktx_tex);
		return 0;
	}

	if(ktx_tex->numLevels == 1 and ktx_tex->baseHeight > 1 and ktx_tex->baseWidth > 1)
		ktx_tex->generateMipmaps = KTX_TRUE;

	if(ktxTexture2_NeedsTranscoding(ktx_tex))
	{
		ktx_transcode_fmt_e format = s_ktx_loader.pick_format(ktx_tex);
		const auto flags = KTX_TF_HIGH_QUALITY;

		rc = ktxTexture2_TranscodeBasis(ktx_tex, format, flags);
		if(rc != KTX_SUCCESS)
		{
			std::print(stderr, "[{}] KTX transcode failed: {}\n", filepath.string(), int(rc));
			ktxTexture2_Destroy(ktx_tex);
			return false;
		}
	}

	GLuint tex_id { 0 }; // GLUpload will generate
	// TODO: should create the texture ourselves to be able to use glTextureView() (cube & array textures)
	// glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &tex);
	// glTextureStorage3D(tex, mipLevels, internalFormat, width, height, depth);

	GLenum target { 0 };
	GLenum glerror { 0 };
	rc = ktxTexture_GLUpload(reinterpret_cast<ktxTexture *>(ktx_tex), &tex_id, &target, &glerror);
	const bool ok = rc == KTX_SUCCESS;
	if(not ok)
		std::print(stderr, "[{}] KTX upload failed! RC: {}   GL error: {}\n", filepath.string(), uint32_t(rc), glerror);

	meta.width = ktx_tex->baseWidth;
	meta.height = ktx_tex->baseHeight;
	meta.depth = ktx_tex->baseDepth;
	meta.layers = ktx_tex->numLayers;

	ktxTexture2_Destroy(ktx_tex);

	return tex_id;
}


void init_ktx_loader()
{
	std::print("KTX loader init...\n");

	ktxLoadOpenGL(glfwGetProcAddress);

	// detect supported texture compression methods

	std::vector<std::string> supported;
	supported.reserve(6);

	if(glfwExtensionSupported("GL_KHR_texture_compression_astc_ldr"))
	{
		s_ktx_loader.support_astc = true;
		supported.push_back("ASTC"s);
	}

	if(glfwExtensionSupported("GL_EXT_texture_compression_s3tc") or glfwExtensionSupported("GL_EXT_texture_compression_dxt1"))
	{
		// BC1/BC3 available
		s_ktx_loader.support_bc1 = true;
		s_ktx_loader.support_bc3 = true;
		supported.push_back("BC1"s);
		supported.push_back("BC3"s);
	}

	if(glfwExtensionSupported("GL_ARB_texture_compression_bptc") or glfwExtensionSupported("GL_EXT_texture_compression_bptc"))
	{
		// BC6/BC7 available
		s_ktx_loader.support_bc6 = true;
		s_ktx_loader.support_bc7 = true;
		supported.push_back("BC6"s);
		supported.push_back("BC7"s);
	}

	if(glfwExtensionSupported("GL_IMG_texture_compression_pvrtc")) // mostly iOS
	{
		s_ktx_loader.support_pvrtc = true;
		supported.push_back("PVRTC"s);
	}

	std::print("KTX texture compressions supported: {}\n", zstr::join(supported, ", "sv));

	std::print("KTX loader init DONE\n");
}

ktx_transcode_fmt_e KTX_Loader::pick_format(ktxTexture2 *)
{
	// NOTE: these options mean the result will always be RGBA

	if(support_bc7)
		return KTX_TTF_BC7_RGBA;

	// TODO: maybe use BC1/4/5

	return KTX_TTF_BC3_RGBA;
}

} // RGL
