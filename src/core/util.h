#pragma once

#include "container_types.h"

#include <functional>
#include <stb_image.h>
#include <glad/glad.h>
#include <glm/vec3.hpp>
#include <WFLCG.hh>

#include <filesystem>
#include <random>
#include <string>


namespace RGL
{
	struct ImageMeta
    {
		ImageMeta() = default;

        GLuint width;
		GLuint height { 1 };  // only 2d & 3d textures
		GLuint depth { 1 };   // only 3d textures
		GLuint layers { 0 };  // only for array textures
        GLuint channels;
		GLenum channel_format = GL_RGB;
		GLenum channel_type = GL_UNSIGNED_INT;
    };

	extern WFLCG s_rng;

    class Util
    {
    public:

		static bool FileExist(const std::filesystem::path &filepath);

        /**
        * @brief   Loads a file in a text mode.
        * @param   std::filesystem::path Relative path, with file name
        *                                and extension, to the file that
        *                                needs to be loaded.
        * @returns Full file's source as a std::string.
        */
		static std::tuple<std::string, bool> LoadFile(const std::filesystem::path & filename, bool fail_ok=false);

		static std::tuple<std::string, bool> LoadShaderFile(const std::filesystem::path &filepath);

		// same as LoadFile, but if 'filename' is relative, it attempts open in given search paths, in order.
		static std::optional<std::filesystem::path> FindFileInPaths(const std::filesystem::path &filename, const std::vector<std::filesystem::path> &search_paths);

		static std::streamsize GetFileSize(std::ifstream &strm);

        /**
        * @brief   Loads a file in a binary mode.
        * @param   std::filesystem::path Relative path, with file name
        *                                and extension, to the file that
        *                                needs to be loaded.
        * @returns std::vector<unsigned char> that contains the loaded data.
        */
        static std::vector<uint8_t> LoadFileBinary(const std::filesystem::path& filename);
		
		static void AddShaderSearchPath(const std::filesystem::path &path);
		/**
        * @brief   Loads a file that contains an image data.
        * @param   std::string Relative path, with file name
        *          and extension, to the file that needs to be
        *          loaded.
        * @param   image_data
        * @param   desired_number_of_channels
        * @returns Pointer to unsigned char that contains image's data.
        *          Has to be freed with stbi_image_free(data)!
        */
		using TextureData = std::unique_ptr<void, std::function<void(void *)>>;

		static TextureData LoadTextureData(const std::filesystem::path& filepath,                        ImageMeta& image_data, int desired_number_of_channels = 0);
		static TextureData LoadTextureData(unsigned char*               memory_data, uint32_t data_size, ImageMeta& image_data, int desired_number_of_channels = 0);
		static TextureData LoadTextureDataHdr(const std::filesystem::path& filepath,                     ImageMeta& image_data, int desired_number_of_channels = 0);


		using ImageOptions = uint32_t;
		static constexpr ImageOptions ImageOptionsDefault = 0;
		static constexpr ImageOptions ImageFlipVertical = 0x0001;

		static TextureData jxl_load(const std::filesystem::path &filepath, ImageMeta &image_data, ImageOptions opts=ImageOptionsDefault);

		// static void ReleaseTextureData (const std::filesystem::path &filepath, unsigned char* data);
        static void ReleaseTextureData (float*         data);
		static void ReleaseTextureData (TextureData &data);

        static double RandomDouble()
        {
			// Returns a random real in range [0,1).
			return s_rng.getDouble() - 1.f;
        }

		static float RandomFloat()
		{
			// Returns a random real in range [0,1).
			return s_rng.getFloat() - 1.f;
		}

        static double RandomDouble(double min, double max)
        {
			// Returns a random real in [min, max).
			assert(min < max);
            return min + (max - min) * RandomDouble();
        }

		static float RandomFloat(float min, float max)
		{
			// Returns a random real in range [min, max).
			assert(min < max);
			return min + (max - min) * RandomFloat();
		}

		static uint32_t RandomInt()
		{
			return s_rng();
		}

		static uint32_t RandomInt(uint32_t min, uint32_t max)
        {
			// Returns a random integer in range [min, max].
			assert(min < max);
			return min + (RandomInt() % (max - min));
        }

		static glm::vec3 RandomVec3(float min, float  max)
        {
            // Returns a random vec3 in [min, max).
			return { RandomFloat(min, max), RandomFloat(min, max), RandomFloat(min, max) };
        }

		static glm::vec3 RandomVec3(const glm::vec3 &min, const glm::vec3 &max)
		{
			// Returns a random vec3 inside the box defined by [min, max).
			return { RandomFloat(min.x, max.x), RandomFloat(min.y, max.y), RandomFloat(min.z, max.z) };
		}

	private:
		static std::tuple<std::string, bool> PreprocessShaderSource(const std::filesystem::path &filepath, const std::string &shader_source, dense_set<std::filesystem::path> &visited_files);
	};
}
