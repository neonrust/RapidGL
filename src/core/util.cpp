#include "util.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/exponential.hpp>

#include <jxl/decode_cxx.h>
#include <jxl/resizable_parallel_runner_cxx.h>

#include "filesystem.h"

namespace RGL
{
    std::string Util::LoadFile(const std::filesystem::path & filename)
    {
        if (filename.empty())
        {
            return "";
        }

        std::string filetext;
        std::string line;

        std::filesystem::path filepath = FileSystem::rootPath() / filename;
        std::ifstream inFile(filepath);

        if (!inFile)
        {
            fprintf(stderr, "Could not open file %s\n", filepath.string().c_str());
            inFile.close();

            return "";
        }

        while (getline(inFile, line))
        {
            filetext.append(line + "\n");
        }

        inFile.close();

        return filetext;
    }

	std::vector<uint8_t> Util::LoadFileBinary(const std::filesystem::path& filename)
    {
        std::filesystem::path filepath = FileSystem::rootPath() / filename;
        std::ifstream file(filepath, std::ios::binary);

        if (!file)
        {
            fprintf(stderr, "Could not open file %s\n", filepath.string().c_str());
            file.close();

            return {};
        }

        // Determine the file size.
        file.seekg(0, std::ios_base::end);
		auto file_size = file.tellg();
        file.seekg(0, std::ios_base::beg);

        // Allocate storage.
		std::vector<uint8_t> data(size_t(file_size) / sizeof(unsigned char));

        // Read the file contents into the allocated storage.
        file.read((char*)&data[0], file_size);

        return data;
    }

	std::string Util::PreprocessShaderSource(const std::string& shader_code, const std::filesystem::path& dir)
    {
		static const std::string include_phrase = "#include ";

		std::istringstream ss(shader_code);

		std::string line, new_shader_code;

        bool included = false;

        while (std::getline(ss, line))
        {
			if (line.starts_with(include_phrase))
            {
				auto include_file_name = line.substr(include_phrase.size() + 1, line.size() - include_phrase .size() - 2);
                
                line     = LoadFile(dir / include_file_name);
                included = true;
            }

            new_shader_code.append(line + "\n");
        }

		// Preprocess the processed source (i.e. recursively)
        if (included)
			new_shader_code = PreprocessShaderSource(new_shader_code, dir);

        return new_shader_code;
    }

	static Util::TextureData mk_tx_data(void *data);

	Util::TextureData Util::LoadTextureData(const std::filesystem::path& filepath, ImageData & image_data, int desired_number_of_channels)
    {
		// TODO: always try to load .jxl (instead) ?

		void *data = nullptr;


		if(filepath.extension() == ".jxl")
		{
			return jxl_load(filepath, image_data);
		}
		else
		{
			int channels_in_file = desired_number_of_channels;
			data = stbi_load(filepath.generic_string().c_str(), (int *)&image_data.width, (int *)&image_data.height, (int *)&channels_in_file, desired_number_of_channels);
			if (data)
				image_data.channels = desired_number_of_channels == 0 ? GLuint(channels_in_file) : GLuint(desired_number_of_channels);
			// data.size = image_data.width * image_data.height * image_data.channels;
		}

		return mk_tx_data(data);
    }

	Util::TextureData Util::LoadTextureData(unsigned char* memory_data, uint32_t data_size, ImageData& image_data, int desired_number_of_channels)
    {
		int channels_in_file = desired_number_of_channels;
		void* data = stbi_load_from_memory(memory_data, int(data_size), (int *)&image_data.width, (int *)&image_data.height, &channels_in_file, desired_number_of_channels);
        
        if (data)
			image_data.channels = desired_number_of_channels == 0 ? GLuint(channels_in_file) : GLuint(desired_number_of_channels);

		return mk_tx_data(data);
    }

	Util::TextureData Util::LoadTextureDataHdr(const std::filesystem::path &filepath, ImageData &image_data, int desired_number_of_channels)
    {
		void *data = nullptr;

		if(filepath.extension() == ".jxl")
		{
			return jxl_load(filepath, image_data, ImageFlipVertical);
		}
		else
		{
			stbi_set_flip_vertically_on_load(true);
			int channels_in_file = desired_number_of_channels;
			data = stbi_loadf(filepath.generic_string().c_str(), (int *)&image_data.width, (int *)&image_data.height, &channels_in_file, desired_number_of_channels);
			stbi_set_flip_vertically_on_load(false);
			if (data)
				image_data.channels = desired_number_of_channels == 0 ? GLuint(channels_in_file) : GLuint(desired_number_of_channels);
			// data.size = image_data.width * image_data.height * image_data.channels;
			image_data.channel_format = GL_RGB;
			image_data.channel_type = GL_FLOAT;
		}

		return mk_tx_data(data);
	}

	void Util::ReleaseTextureData(TextureData &data)
	{
		data.reset();
	}

	// void Util::ReleaseTextureData(const std::filesystem::path& filepath, unsigned char* data)
 //    {
	// 	if(filepath.extension() == ".jxl")
	// 		::free(data);
	// 	else
	// 		stbi_image_free(data);
 //    }

    void Util::ReleaseTextureData(float* data)
    {
        stbi_image_free(data);
    }

	Util::TextureData Util::jxl_load(const fs::path &filepath, ImageData &image_data, ImageOptions options)
	{
		// see https://github.com/libjxl/libjxl/blob/main/examples/decode_oneshot.cc

		// for logging
		auto short_name = filepath.filename();

		JxlDecoderStatus res;

		auto runner = JxlResizableParallelRunnerMake(nullptr);

		auto dec = JxlDecoderMake(nullptr);

		res = JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO /*| JXL_DEC_COLOR_ENCODING*/ | JXL_DEC_FULL_IMAGE);
		if(res != JXL_DEC_SUCCESS)
		{
			fprintf(stderr, "[%s] JxlDecoderSubscribeEvents failed: %d\n", short_name.c_str(), res);
			return {};
		}
		res = JxlDecoderSetParallelRunner(dec.get(), JxlResizableParallelRunner, runner.get());
		if(res != JXL_DEC_SUCCESS)
		{
			fprintf(stderr, "[%s] JxlDecoderSetParallelRunner failed: %d\n", short_name.c_str(), res);
			return {};
		}

		JxlBasicInfo info;
		JxlPixelFormat format = {
			.num_channels = 0, // set by JXL_DEC_BASIC_INFO message below
			.data_type = JXL_TYPE_UINT8, // updated by JXL_DEC_BASIC_INFO message below
			.endianness = JXL_NATIVE_ENDIAN,
			.align = 0,
		};

		auto jxlData = LoadFileBinary(filepath);

		res = JxlDecoderSetInput(dec.get(), jxlData.data(), jxlData.size());
		if(res != JXL_DEC_SUCCESS)
		{
			fprintf(stderr, "[%s] JxlDecoderSetInput failed: %d\n", short_name.c_str(), res);
			return {};
		}
		JxlDecoderCloseInput(dec.get());

		void *data = nullptr;

		std::vector<uint8_t> icc_profile;
		size_t channel_size = 1;

		bool decodingDone = false;

		while(not decodingDone)
		{
			const auto status = JxlDecoderProcessInput(dec.get());

			switch(status)
			{
			case JXL_DEC_ERROR:
				std::fprintf(stderr, "[%s] Jxl: Decoder error\n", short_name.c_str());
				return {};
			case JXL_DEC_NEED_MORE_INPUT:
				std::fprintf(stderr, "[%s] Jxl: Error, already provided all input\n", short_name.c_str());
				return {};
			case JXL_DEC_BASIC_INFO:
			{
				// std::printf("[%s] JXL_DEC_BASIC_INFO\n", short_name.c_str());
				res = JxlDecoderGetBasicInfo(dec.get(), &info);
				if(res != JXL_DEC_SUCCESS)
				{
					fprintf(stderr, "[%s] Jxl: JxlDecoderGetBasicInfo failed: %d\n", short_name.c_str(), res);
					return {};
				}
				// update our initial guesses regarding format
				image_data.width = info.xsize;
				image_data.height = info.ysize;
				image_data.channels = info.num_color_channels + info.num_extra_channels;

				format.num_channels = image_data.channels;
				if(image_data.channels == 4)
					image_data.channel_format = GL_RGBA;
				else
					image_data.channel_format = GL_RGB;
				channel_size = info.bits_per_sample >> 3;
				// TODO: how to discern between FLOAT and UINT types ?  (2 and 4 bytes)
				//   exponent_bits_per_sample can be 0 in both cases
				switch(channel_size)
				{
				case 1: format.data_type = JXL_TYPE_UINT8;   channel_size = 1; image_data.channel_type = GL_UNSIGNED_INT; break;
				case 2: format.data_type = JXL_TYPE_FLOAT; channel_size = 4; image_data.channel_type = GL_FLOAT; break;
				case 4: format.data_type = JXL_TYPE_FLOAT;   channel_size = 4; image_data.channel_type = GL_FLOAT; break;
				}

				// std::fprintf(stderr, "[%s] Jxl: %u x %u  channel size: %zu\n", short_name.c_str(), image_data.width, image_data.height, channel_size);

				const auto num_threads = JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize);
				JxlResizableParallelRunnerSetThreads(runner.get(), num_threads);
				break;
			}
			case JXL_DEC_COLOR_ENCODING:
				// std::printf("[%s] JXL_DEC_COLOR_ENCODING\n", short_name.c_str());
				// // Get the ICC color profile of the pixel data
				// size_t icc_size;
				// res = JxlDecoderGetICCProfileSize(dec.get(), nullptr, JXL_COLOR_PROFILE_TARGET_DATA, &icc_size);
				// if(res != JXL_DEC_SUCCESS)
				// {
				// 	std::fprintf(stderr, "[%s] Jxl: JxlDecoderGetICCProfileSize failed\n", short_name.c_str());
				// 	return {};
				// }
				// icc_profile.resize(icc_size);
				// res = JxlDecoderGetColorAsICCProfile(dec.get(), nullptr, JXL_COLOR_PROFILE_TARGET_DATA, icc_profile.data(), icc_profile.size());
				// if(res != JXL_DEC_SUCCESS)
				// {
				// 	std::fprintf(stderr, "[%s] Jxl: JxlDecoderGetColorAsICCProfile failed\n", short_name.c_str());
				// 	return {};
				// }
				break;
			case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
				// std::printf("[%s] JXL_DEC_NEED_IMAGE_OUT_BUFFER\n", short_name.c_str());
				size_t buffer_size;
				res = JxlDecoderImageOutBufferSize(dec.get(), &format, &buffer_size);
				if(res != JXL_DEC_SUCCESS)
				{
					std::fprintf(stderr, "[%s] Jxl: JxlDecoderImageOutBufferSize failed: %d\n", short_name.c_str(), res);
					return {};
				}
				{
					const auto expected_size = image_data.width * image_data.height * channel_size * format.num_channels;
					if(buffer_size != expected_size)
					{
						std::fprintf(stderr, "[%s] Jxl: Invalid out buffer size %ld, expected %ld\n", short_name.c_str(), buffer_size, expected_size);
						return {};
					}
				}
				// std::fprintf(stderr, "[%s] Jxl: buffer size: %lu\n", short_name.c_str(), buffer_size);
				data = ::malloc(buffer_size);
				res = JxlDecoderSetImageOutBuffer(dec.get(), &format, data, buffer_size);
				if(res != JXL_DEC_SUCCESS)
				{
					std::fprintf(stderr, "[%s] Jxl: JxlDecoderSetImageOutBuffer failed: %d\n", short_name.c_str(), res);
					return {};
				}
				break;
			case JXL_DEC_FULL_IMAGE:
				// std::printf("[%s] JXL_DEC_FULL_IMAGE\n", short_name.c_str());
				// Nothing to do. Do not yet return. If the image is an animation, more
				// full frames may be decoded. This example only keeps the last one.
				break;
			case JXL_DEC_SUCCESS:
				// std::printf("[%s] JXL_DEC_SUCCESS\n", short_name.c_str());
				// All decoding successfully finished.
				// It's not required to call JxlDecoderReleaseInput(dec.get()) here since
				// the decoder will be destroyed.

				// data = (unsigned char *)malloc(pixels.size());
				// std::memcpy(data, pixels.data(), pixels.size());
				decodingDone = true;
				break;

			default:
				std::fprintf(stderr, "[%s] Jxl: unhandled decoder status: %d", short_name.c_str(), status);
				break;
			}
		}

		if(not decodingDone and data)
			return {};

		if(options & ImageFlipVertical)
		{
			// TODO: swap pixel rows
			size_t row_stride = image_data.width * image_data.channels * channel_size;

			void *temp_row = ::malloc(row_stride);

			auto *rows = static_cast<uint8_t *>(data);
			for(auto row = 0u; row < image_data.height >> 1; ++row)
			{
				auto *row0_start = &rows[row * row_stride];
				auto *row1_start = &rows[(image_data.height - row - 1) * row_stride];
				std::memcpy(temp_row, row0_start, row_stride);
				std::memcpy(row0_start, row1_start, row_stride);
				std::memcpy(row1_start, temp_row, row_stride);
			}
		}

		return mk_tx_data(data);
	}

	static void _tx_data_deleter(void *data)
	{
		::free(data);
	}
	Util::TextureData mk_tx_data(void *data)
	{
		return Util::TextureData(data, _tx_data_deleter);
	}

}
