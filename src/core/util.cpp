#include "util.h"

#include "zstr.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/exponential.hpp>

#include <jxl/decode_cxx.h>
#include <jxl/resizable_parallel_runner_cxx.h>

#include "filesystem.h"

#include <print>
using namespace std::literals;

namespace RGL
{

static std::vector<fs::path> s_shader_include_paths;

bool Util::FileExist(const std::filesystem::path &filepath)
{
	return std::filesystem::exists(filepath);
}

std::tuple<std::string, bool> Util::LoadFile(const fs::path & filename, bool fail_ok)
{
	if (filename.empty())
		return { std::string(), false };

	std::string filetext;

	fs::path filepath = FileSystem::rootPath() / filename;
	std::ifstream inFile(filepath);

	if(not inFile)
	{
		if(not fail_ok)
			std::print(stderr, "Could not open file {}\n", filepath.string());
		inFile.close();

		return { std::string(), false };
	}

	const auto file_size = GetFileSize(inFile);
	filetext.reserve(std::string::size_type(file_size + (file_size >> 6))); // add a small margin  ;)

	std::string line;
	line.reserve(256);

	while(std::getline(inFile, line))
		filetext.append(line + "\n");

	inFile.close();

	return { filetext, true };
}

std::tuple<std::string, bool> Util::LoadShaderFile(const std::filesystem::path &filepath)
{
	const auto &[file_content, okf] = Util::LoadFile(filepath);
	if(not okf)
	{
		std::print(stderr, "Load shader failed: {}\n", filepath.string());
		return { std::string(), false };
	}

	// std::print(" >> {}\n", filepath.string());

	static dense_set<std::filesystem::path> visited_files;
	visited_files.reserve(8);
	visited_files.clear();

	auto [code, okp] = PreprocessShaderSource(filepath, file_content, visited_files);
	if(not okp)
	{
		std::print(stderr, "Preprocessing shader failed: {}\n", filepath.string());
		return { std::string(), false };
	}

	return { code, true };
}

std::optional<std::filesystem::path> Util::FindFileInPaths(const fs::path &filename, const std::vector<fs::path> &search_paths)
{
	for(const auto &search_path: search_paths)
	{
		auto full_path = search_path / filename;
		if(FileExist(full_path))
			return std::filesystem::canonical(full_path);
	}

	return std::nullopt;
}

std::streamsize Util::GetFileSize(std::ifstream &strm)
{
	strm.seekg(0, std::ios::end);
	const auto size = strm.tellg();
	strm.seekg(0, std::ios::beg);
	return size;
}

std::vector<uint8_t> Util::LoadFileBinary(const fs::path& filename)
{
	fs::path filepath = FileSystem::rootPath() / filename;
	std::ifstream file(filepath, std::ios::binary);

	if (!file)
	{
		std::print(stderr, "Could not open file %s\n", filepath.string());
		file.close();

		return {};
	}

	const auto file_size = GetFileSize(file);

	std::vector<uint8_t> data;
	data.resize(size_t(file_size) / sizeof(unsigned char));

		   // Read the entire file contents
	file.read(reinterpret_cast<char *>(data.data()), file_size);

	return data;
}

void Util::AddShaderSearchPath(const std::filesystem::path &path)
{
	auto abs_path = std::filesystem::canonical(path);

	if(std::find(s_shader_include_paths.begin(), s_shader_include_paths.end(), abs_path) == s_shader_include_paths.end())
		s_shader_include_paths.push_back(abs_path);
}

std::tuple<std::string, bool> Util::PreprocessShaderSource(const fs::path &filepath, const std::string& shader_source, dense_set<fs::path> &visited_files)
{
	static const auto phrase_include = "#include "sv;

	// add current file's directory first in the search paths
	auto search_paths = s_shader_include_paths;
	if(filepath.has_parent_path())
		search_paths.insert(search_paths.begin(), filepath.parent_path());

	std::istringstream ss(shader_source);

	std::string line, new_source;
	new_source.reserve(shader_source.size());
	size_t line_num = 0;

	while (std::getline(ss, line))
	{
		++line_num;

		const auto non_space = line.find_first_not_of(" \t");
		auto is_preproc = non_space != std::string::npos and line[non_space] == '#';

		std::string_view preproc_instruction; // used when it's a preprocessor instruction
		if(is_preproc)
		{
			preproc_instruction = line;
			{
				// clean up the line to simplify parsing
				const auto comment = preproc_instruction.find("//");
				if(comment != std::string_view::npos)
					preproc_instruction = preproc_instruction.substr(0, comment);
				preproc_instruction = zstr::strip(preproc_instruction);
			}

			if(preproc_instruction.empty())
				continue;

			if (preproc_instruction.starts_with(phrase_include))
			{
				// extract filename, cutting off quotes (or brackets)
				const auto include_file = fs::path(preproc_instruction.substr(phrase_include.size() + 1, preproc_instruction.size() - phrase_include .size() - 2));

				const auto &found_path_ = FindFileInPaths(include_file, search_paths);
				// if not absolute always relative to the current file
				if(not found_path_)
				{
					std::print(stderr, "[{}:{}] \x1b[31;1minclude not found\x1b[m: {}\n", filepath.string(), line_num, preproc_instruction);
					return { std::string(), false };
				}
				const auto found_path = found_path_.value();
				if(not visited_files.contains(found_path))
				{
					// std::print("  .. {}\n", found_path.string());
					const auto &[include_data, ok] = LoadFile(found_path);
					if(not ok)
					{
						std::print(stderr, "[{}:{}] \x1b[31;1minclude failed\x1b[m: {}\n", filepath.string(), line_num, preproc_instruction);
						return { std::string(), false };
					}

					// std::print(" -> \x1b[34;1m{}\x1b[m\n", found_path.string());
					if(not include_data.empty())
					{
						// TODO: this recursion should probably modify the same string object
						//   now, potentially _many_ strings will be allocated, depending on how deep the includes recursion goes
						fs::path parent_path;
						if(found_path.has_parent_path())
							parent_path = found_path.parent_path();
						else
							parent_path = filepath.parent_path();

						const auto &[include_data_p, ok] = PreprocessShaderSource(found_path, include_data, visited_files);
						if(not ok)
							return { std::string(), false }; // error message already output
						// NOTE: don't write #line, so we can show the context lines
						//   maybe use a custom #pragma instead
						new_source.append(std::format("#pragma include start {}\n", found_path.string()));
						new_source.append(include_data_p);
						new_source.append("\n"sv);

						// NOTE: don't write #line, so we can show the context lines
						//   maybe use a custom #pragma instead
						new_source.append(std::format("#pragma include end {}\n", found_path.string()));
						// new_source.append("#line ");
						// new_source.append(std::to_string(line_num + 1));
						new_source.append("\n");
					}
				}
				else
					std::print(stderr, "[{}:{}] already included {}\n", filepath.string(), line_num, found_path.string());
			}
			else
				is_preproc = false; // make sure the non-processed line get forwarded to 'new_source'
		}


		if(not is_preproc)
		{
			new_source.append(line);
			new_source.append("\n"sv);
		}
	}

	return { new_source, true };
}

static Util::TextureData mk_tx_data(void *data);

Util::TextureData Util::LoadTextureData(const fs::path& filepath, ImageMeta & image_data, int desired_number_of_channels)
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
		else
			std::print(stderr, "Load failed: {}\n", stbi_failure_reason());
		// data.size = image_data.width * image_data.height * image_data.channels;
	}

	return mk_tx_data(data);
}

Util::TextureData Util::LoadTextureData(unsigned char* memory_data, uint32_t data_size, ImageMeta& image_data, int desired_number_of_channels)
{
	int channels_in_file = desired_number_of_channels;
	void* data = stbi_load_from_memory(memory_data, int(data_size), (int *)&image_data.width, (int *)&image_data.height, &channels_in_file, desired_number_of_channels);

	if (data)
		image_data.channels = desired_number_of_channels == 0 ? GLuint(channels_in_file) : GLuint(desired_number_of_channels);

	return mk_tx_data(data);
}

Util::TextureData Util::LoadTextureDataHdr(const fs::path &filepath, ImageMeta &image_meta, int desired_number_of_channels)
{
	void *data = nullptr;

	if(filepath.extension() == ".jxl")
	{
		return jxl_load(filepath, image_meta, ImageFlipVertical);
	}
	else
	{
		stbi_set_flip_vertically_on_load(true);
		int channels_in_file = desired_number_of_channels;
		data = stbi_loadf(filepath.generic_string().c_str(), (int *)&image_meta.width, (int *)&image_meta.height, &channels_in_file, desired_number_of_channels);
		stbi_set_flip_vertically_on_load(false);
		if (data)
			image_meta.channels = desired_number_of_channels == 0 ? GLuint(channels_in_file) : GLuint(desired_number_of_channels);
		// data.size = image_data.width * image_data.height * image_data.channels;
		image_meta.channel_format = GL_RGB;
		image_meta.channel_type = GL_FLOAT;
	}

	return mk_tx_data(data);
}

void Util::ReleaseTextureData(TextureData &data)
{
	data.reset();
}

	   // void Util::ReleaseTextureData(const fs::path& filepath, unsigned char* data)
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

Util::TextureData Util::jxl_load(const fs::path &filepath, ImageMeta &image_meta, ImageOptions options)
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
		std::print(stderr, "[{}] JxlDecoderSubscribeEvents failed: {}\n", short_name.string(), int(res));
		return {};
	}
	res = JxlDecoderSetParallelRunner(dec.get(), JxlResizableParallelRunner, runner.get());
	if(res != JXL_DEC_SUCCESS)
	{
		std::print(stderr, "[{}] JxlDecoderSetParallelRunner failed: {}\n", short_name.string(), int(res));
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
		std::print(stderr, "[{}] JxlDecoderSetInput failed: {}\n", short_name.string(), int(res));
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
			std::print(stderr, "[{}] Jxl: Decoder error\n", short_name.c_str());
			return {};
		case JXL_DEC_NEED_MORE_INPUT:
			std::print(stderr, "[{}] Jxl: Error, already provided all input\n", short_name.string());
			return {};
		case JXL_DEC_BASIC_INFO:
		{
			// std::print("[{}] JXL_DEC_BASIC_INFO\n", short_name.string());
			res = JxlDecoderGetBasicInfo(dec.get(), &info);
			if(res != JXL_DEC_SUCCESS)
			{
				std::print(stderr, "[{}] Jxl: JxlDecoderGetBasicInfo failed: {}\n", short_name.string(), int(res));
				return {};
			}
			// update our initial guesses regarding format
			image_meta.width    = info.xsize;
			image_meta.height   = info.ysize;
			image_meta.channels = info.num_color_channels + info.num_extra_channels;

			format.num_channels = image_meta.channels;
			if(image_meta.channels == 4)
				image_meta.channel_format = GL_RGBA;
			else
				image_meta.channel_format = GL_RGB;
			channel_size = info.bits_per_sample >> 3;
			// TODO: how to discern between FLOAT and UINT types ?  (2 and 4 bytes)
			//   exponent_bits_per_sample can be 0 in both cases
			switch(channel_size)
			{
			case 1: format.data_type = JXL_TYPE_UINT8; channel_size = 1; image_meta.channel_type = GL_UNSIGNED_INT; break;
			case 2: format.data_type = JXL_TYPE_FLOAT; channel_size = 4; image_meta.channel_type = GL_FLOAT; break;
			case 4: format.data_type = JXL_TYPE_FLOAT; channel_size = 4; image_meta.channel_type = GL_FLOAT; break;
			}

				   // std::print(stderr, "[{}] Jxl: {} x {}  channel size: %zu\n", short_name.string(), image_data.width, image_data.height, channel_size);

			const auto num_threads = JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize);
			JxlResizableParallelRunnerSetThreads(runner.get(), num_threads);
			break;
		}
		case JXL_DEC_COLOR_ENCODING:
			// std::print("[{}] JXL_DEC_COLOR_ENCODING\n", short_name.c_str());
			// // Get the ICC color profile of the pixel data
			// size_t icc_size;
			// res = JxlDecoderGetICCProfileSize(dec.get(), nullptr, JXL_COLOR_PROFILE_TARGET_DATA, &icc_size);
			// if(res != JXL_DEC_SUCCESS)
			// {
			// 	std::print(stderr, "[{}] Jxl: JxlDecoderGetICCProfileSize failed\n", short_name.c_str());
			// 	return {};
			// }
			// icc_profile.resize(icc_size);
			// res = JxlDecoderGetColorAsICCProfile(dec.get(), nullptr, JXL_COLOR_PROFILE_TARGET_DATA, icc_profile.data(), icc_profile.size());
			// if(res != JXL_DEC_SUCCESS)
			// {
			// 	std::print(stderr, "[{}] Jxl: JxlDecoderGetColorAsICCProfile failed\n", short_name.string());
			// 	return {};
			// }
			break;
		case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
			// std::print("[{}] JXL_DEC_NEED_IMAGE_OUT_BUFFER\n", short_name.string());
			size_t buffer_size;
			res = JxlDecoderImageOutBufferSize(dec.get(), &format, &buffer_size);
			if(res != JXL_DEC_SUCCESS)
			{
				std::print(stderr, "[{}] Jxl: JxlDecoderImageOutBufferSize failed: {}\n", short_name.string(), int(res));
				return {};
			}
			{
				const auto expected_size = image_meta.width * image_meta.height * channel_size * format.num_channels;
				if(buffer_size != expected_size)
				{
					std::print(stderr, "[{}] Jxl: Invalid out buffer size %ld, expected %ld\n", short_name.string(), buffer_size, expected_size);
					return {};
				}
			}
			// std::fprint(stderr, "[{}] Jxl: buffer size: {}\n", short_name.string(), buffer_size);
			data = ::malloc(buffer_size);
			res = JxlDecoderSetImageOutBuffer(dec.get(), &format, data, buffer_size);
			if(res != JXL_DEC_SUCCESS)
			{
				std::print(stderr, "[{}] Jxl: JxlDecoderSetImageOutBuffer failed: {}\n", short_name.c_str(), int(res));
				return {};
			}
			break;
		case JXL_DEC_FULL_IMAGE:
			// std::print("[{}] JXL_DEC_FULL_IMAGE\n", short_name.string());
			// Nothing to do. Do not yet return. If the image is an animation, more
			// full frames may be decoded. This example only keeps the last one.
			break;
		case JXL_DEC_SUCCESS:
			// std::print("[{}] JXL_DEC_SUCCESS\n", short_name.string());
			// All decoding successfully finished.
			// It's not required to call JxlDecoderReleaseInput(dec.get()) here since
			// the decoder will be destroyed.

				   // data = (unsigned char *)malloc(pixels.size());
				   // std::memcpy(data, pixels.data(), pixels.size());
			decodingDone = true;
			break;

		default:
			std::print(stderr, "[{}] Jxl: unhandled decoder status: {}\n", short_name.string(), int(status));
			break;
		}
	}

	if(not decodingDone and data)
		return {};

	if(options & ImageFlipVertical)
	{
		// TODO: swap pixel rows
		size_t row_stride = image_meta.width * image_meta.channels * channel_size;

		void *temp_row = ::malloc(row_stride);

		auto *rows = static_cast<uint8_t *>(data);
		for(auto row = 0u; row < image_meta.height >> 1; ++row)
		{
			auto *row0_start = &rows[row * row_stride];
			auto *row1_start = &rows[(image_meta.height - row - 1) * row_stride];
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

} // RGL
