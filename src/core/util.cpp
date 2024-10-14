#include "util.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/exponential.hpp>

#include <jxl/decode.h>
#include <jxl/resizable_parallel_runner.h>

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

        std::filesystem::path filepath = FileSystem::getRootPath() / filename;
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

    std::vector<unsigned char> Util::LoadFileBinary(const std::filesystem::path& filename)
    {
        std::filesystem::path filepath = FileSystem::getRootPath() / filename;
        std::ifstream file(filepath, std::ios::binary);

        if (!file)
        {
            fprintf(stderr, "Could not open file %s\n", filepath.string().c_str());
            file.close();

            return {};
        }

        // Determine the file size.
        file.seekg(0, std::ios_base::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios_base::beg);

        // Allocate storage.
        std::vector<unsigned char> data(file_size / sizeof(unsigned char));

        // Read the file contents into the allocated storage.
        file.read((char*)&data[0], file_size);

        return data;
    }

    std::string Util::LoadShaderIncludes(const std::string& shader_code, const std::filesystem::path& dir)
    {
        std::istringstream ss(shader_code);

        std::string line, new_shader_code = "";
        std::string include_phrase        = "#include";

        bool included = false;

        while (std::getline(ss, line))
        {
            if (line.substr(0, include_phrase.size()) == include_phrase)
            {
                std::string include_file_name = line.substr(include_phrase.size() + 2, line.size() - include_phrase .size() - 3);
                
                line     = LoadFile(dir / include_file_name);
                included = true;
            }

            new_shader_code.append(line + "\n");
        }

        // Parse #include in the included files
        if (included)
        {
            new_shader_code = LoadShaderIncludes(new_shader_code, dir);
        }

        return new_shader_code;
    }


    unsigned char* Util::LoadTextureData(const std::filesystem::path& filepath, ImageData & image_data, int desired_number_of_channels)
    {
        int width, height, channels_in_file;

		unsigned char* data = nullptr;

		if(filepath.extension() == ".jxl")
		{
			// see https://github.com/libjxl/libjxl/blob/main/examples/decode_oneshot.cc

			static const auto initJxl = true;
			static JxlDecoder *dec;
			static void *runner;

			JxlDecoderStatus res;

			if(initJxl)
			{
				runner = JxlResizableParallelRunnerCreate(nullptr);
				dec = JxlDecoderCreate(nullptr);;

				res = JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE);
				if(res != JXL_DEC_SUCCESS)
				{
					fprintf(stderr, "JxlDecoderSubscribeEvents failed\n");
					return {};
				}
				res = JxlDecoderSetParallelRunner(dec, JxlResizableParallelRunner, runner);
				if(res != JXL_DEC_SUCCESS)
				{
					fprintf(stderr, "JxlDecoderSetParallelRunner failed\n");
					return {};
				}
			}
			JxlBasicInfo info;
			JxlPixelFormat format = {
				.num_channels = 0, // set by JXL_DEC_BASIC_INFO message below
				.data_type = JXL_TYPE_UINT8, // budated by JXL_DEC_BASIC_INFO message below
				.endianness = JXL_NATIVE_ENDIAN,
				.align = 0,
			};

			auto jxlData = LoadFileBinary(filepath);

			JxlDecoderSetInput(dec, jxlData.data(), jxlData.size());
			JxlDecoderCloseInput(dec);

			// std::vector<uint8_t> icc_profile;
			int channel_size { 0 };

			std::vector<uint8_t> pixels; // TODO: might use the same (static) buffers for all textures (can't upload in parallel anyway)

			bool decodingDone = false;

			while(not decodingDone)
			{
				const auto status = JxlDecoderProcessInput(dec);

				switch(status)
				{
				case JXL_DEC_ERROR:
					fprintf(stderr, "Jxl: Decoder error\n");
					return {};
				case JXL_DEC_NEED_MORE_INPUT:
					fprintf(stderr, "Jxl: Error, already provided all input\n");
					return {};
				case JXL_DEC_BASIC_INFO:
					// std::cout << "JXL_DEC_BASIC_INFO\n";
					res = JxlDecoderGetBasicInfo(dec, &info);
					if(res != JXL_DEC_SUCCESS)
					{
						fprintf(stderr, "Jxl: JxlDecoderGetBasicInfo failed\n");
						return {};
					}
					width = int(info.xsize);
					height = int(info.ysize);
					// update our initial guesses regarding format
					format.num_channels = info.num_color_channels + info.num_extra_channels;
					channel_size = info.bits_per_sample >> 3;
					if(channel_size == sizeof(float) and info.exponent_bits_per_sample > 0)
						format.data_type = JXL_TYPE_FLOAT;
					else if(channel_size == 2)
					{
						if(info.exponent_bits_per_sample > 0)
							format.data_type = JXL_TYPE_FLOAT16;
						else
							format.data_type = JXL_TYPE_UINT16;
					}

					JxlResizableParallelRunnerSetThreads(runner, JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize));
					break;
				case JXL_DEC_COLOR_ENCODING:
					// std::cout << "JXL_DEC_COLOR_ENCODING\n";
					// Get the ICC color profile of the pixel data
					// size_t icc_size;
					// res = JxlDecoderGetICCProfileSize(dec, JXL_COLOR_PROFILE_TARGET_DATA, &icc_size);
					// if(res != JXL_DEC_SUCCESS)
					// {
					// 	std::cerr << "Jxl: JxlDecoderGetICCProfileSize failed\n";
					// 	return {};
					// }
					// icc_profile.resize(icc_size);
					// res = JxlDecoderGetColorAsICCProfile(dec, JXL_COLOR_PROFILE_TARGET_DATA, icc_profile.data(), icc_profile.size());
					// if(res != JXL_DEC_SUCCESS)
					// {
					// 	std::cerr << "Jxl: JxlDecoderGetColorAsICCProfile failed\n";
					// 	return {};
					// }
					break;
				case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
					// std::cout << "JXL_DEC_NEED_IMAGE_OUT_BUFFER\n";
					size_t buffer_size;
					res = JxlDecoderImageOutBufferSize(dec, &format, &buffer_size);
					if(res != JXL_DEC_SUCCESS)
					{
						fprintf(stderr, "Jxl: JxlDecoderImageOutBufferSize failed\n");
						return {};
					}
					{
						const auto expected_size = (size_t(width * height * channel_size) * format.num_channels);
						if(buffer_size != expected_size)
						{
							fprintf(stderr, "Jxl: Invalid out buffer size %ld: %ld\n", buffer_size, expected_size);
							return {};
						}
						pixels.resize(buffer_size);
					}
					void *pixels_buffer;
					pixels_buffer = static_cast<void*>(pixels.data());
					size_t pixels_buffer_size;
					pixels_buffer_size = pixels.size() * size_t(channel_size); // don't need format.num_channels ?
					res = JxlDecoderSetImageOutBuffer(dec, &format, pixels_buffer, pixels_buffer_size);
					if(res != JXL_DEC_SUCCESS)
					{
						fprintf(stderr, "Jxl: JxlDecoderSetImageOutBuffer failed\n");
						return {};
					}
					break;
				case JXL_DEC_FULL_IMAGE:
					// std::cout << "JXL_DEC_FULL_IMAGE\n";
					// Nothing to do. Do not yet return. If the image is an animation, more
					// full frames may be decoded. This example only keeps the last one.
					break;
				case JXL_DEC_SUCCESS:
					// std::cout << "JXL_DEC_SUCCESS\n";
					// All decoding successfully finished.
					// It's not required to call JxlDecoderReleaseInput(dec.get()) here since
					// the decoder will be destroyed.

					channels_in_file = int(format.num_channels);
					data = (unsigned char *)malloc(pixels.size());
					std::memcpy(data, pixels.data(), pixels.size());
					decodingDone = true;
					break;

				default:
					fprintf(stderr, "Jxl: unhandled decoder status: %d", status);
					break;
				}
			}
		}
		else
			data = stbi_load(filepath.generic_string().c_str(), &width, &height, &channels_in_file, desired_number_of_channels);

        if (data)
        {
            image_data.width    = width;
            image_data.height   = height;
            image_data.channels = desired_number_of_channels == 0 ? channels_in_file : desired_number_of_channels;
        }

        return data;
    }

    unsigned char* Util::LoadTextureData(unsigned char* memory_data, uint32_t data_size, ImageData& image_data, int desired_number_of_channels)
    {
        int width, height, channels_in_file;
        unsigned char* data = stbi_load_from_memory(memory_data, data_size, &width, &height, &channels_in_file, desired_number_of_channels);
        
        if (data)
        {
            image_data.width    = width;
            image_data.height   = height;
            image_data.channels = desired_number_of_channels == 0 ? channels_in_file : desired_number_of_channels;
        }

        return data;
    }

    float* Util::LoadTextureDataHdr(const std::filesystem::path& filepath, ImageData& image_data, int desired_number_of_channels)
    {
        stbi_set_flip_vertically_on_load(true);
            int width, height, channels_in_file;
            float* data = stbi_loadf(filepath.generic_string().c_str(), &width, &height, &channels_in_file, desired_number_of_channels);
        stbi_set_flip_vertically_on_load(false);
        if (data)
        {
            image_data.width    = width;
            image_data.height   = height;
            image_data.channels = desired_number_of_channels == 0 ? channels_in_file : desired_number_of_channels;
        }

        return data;
    }

	void Util::ReleaseTextureData(const std::filesystem::path& filepath, unsigned char* data)
    {
		if(filepath.extension() == ".jxl")
			::free(data);
		else
			stbi_image_free(data);
    }

    void Util::ReleaseTextureData(float* data)
    {
        stbi_image_free(data);
    }
}
