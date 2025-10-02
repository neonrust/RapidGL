#pragma once

#include "container_types.h"

#include <functional>
#include <stb_image.h>
#include <glad/glad.h>
#include <glm/vec3.hpp>

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
		GLuint channel_format = GL_RGB;
		GLuint channel_type = GL_UNSIGNED_INT;
    };

    class Util
    {
    public:
        /**
        * @brief   Loads a file in a text mode.
        * @param   std::filesystem::path Relative path, with file name
        *                                and extension, to the file that
        *                                needs to be loaded.
        * @returns Full file's source as a std::string.
        */
		static std::tuple<std::string, bool> LoadFile(const std::filesystem::path & filename);

		// same as LoadFile, but if 'filename' is relative, it attempts open in given search paths, in order.
		static std::tuple<std::string, bool> LoadFileInPaths(const std::filesystem::path &filename, const small_vec<std::filesystem::path, 16> &search_paths);

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
		static std::tuple<std::string, bool> PreprocessShaderSource(const std::string & shader_code, const std::filesystem::path& dir="shaders");
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
            // Returns a random real in [0,1).
            static std::uniform_real_distribution<double> distribution(0.0, 1.0);
            static std::mt19937 generator;
            return distribution(generator);
        }

        static double RandomDouble(double min, double max)
        {
            // Returns a random real in [min, max).
            return min + (max - min) * RandomDouble();
        }

        static int RandomInt(int min, int max)
        {
            // Returns a random integer in [min, max].
            return static_cast<int>(RandomDouble(min, max + 1));
        }

        static glm::vec3 RandomVec3(double min, double max)
        {
            // Returns a random vec3 in [min, max).
            return glm::vec3(RandomDouble(min, max), RandomDouble(min, max), RandomDouble(min, max));
        }

		static glm::vec3 RandomVec3(const glm::vec3 &min, const glm::vec3 &max)
		{
			// Returns a random vec3 inside the box defined by [min, max).
			return glm::vec3(RandomDouble(min.x, max.x), RandomDouble(min.y, max.y), RandomDouble(min.z, max.z));
		}
	};
}
