#include "clustered_shading.h"

#include "gl_lookup.h"
#include "gui/gui.h"   // IWYU pragma: keep

// #include "implot.h"

#include "filesystem.h"
#include "constants.h"

using namespace std::literals;

using namespace RGL;


void ImGui_ImageEx(ImTextureID texture_id, ImVec2 size, ImVec2 uv1, ImVec2 uv0, GLuint shader_id);

void visualize_3d_texture(const Texture3D &t3, RenderTarget::Texture2d &out, int major_axis, float level);


inline ImVec2 operator + (const ImVec2 &A, const ImVec2 &B)
{
	return { A.x + B.x, A.y + B.y };
}

inline ImVec2 operator * (const ImVec2 &A, float scale)
{
	return { A.x * scale, A.y * scale };
}

inline ImVec2 operator / (const ImVec2 &A, float div)
{
	return { A.x / div, A.y / div };
}

void ClusteredShading::render_gui()
{
	/* This method is responsible for rendering GUI using ImGUI. */

	/*
	 * It's possible to call render_gui() from the base class.
	 * It renders performance info overlay.
	 */
	CoreApp::render_gui();

	/* Create your own GUI using ImGUI here. */
	ImVec2 window_pos       = ImVec2(float(Window::width()) - 10.f, 10.f);
	ImVec2 window_pos_pivot = ImVec2(1.0f, 0.0f);

	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
	ImGui::SetNextWindowSize({ 400, 1024 });

	ImGui::Text("    Culling: %4ld µs", m_cull_scene_time.average().count());
	ImGui::Text("    Shadows: %4ld µs", m_shadow_time.average().count());
	ImGui::Text("     Z-pass: %4ld µs", m_depth_time.average().count());
	ImGui::Text(" Clstr.find: %4ld µs", m_cluster_find_time.average().count());
	ImGui::Text(" Clstr.coll: %4ld µs", m_cluster_index_time.average().count());
	ImGui::Text(" Light cull: %4ld µs", m_light_cull_time.average().count());
	ImGui::Text("    Shading: %4ld µs", m_shading_time.average().count());
	ImGui::Text("     Skybox: %4ld µs", m_skybox_time.average().count());
	// ImGui::Text("        PP: %3ld µs", m_pp_time.count());
	ImGui::Text(" Scattering: %4ld µs", m_scatter_time.average().count());
	ImGui::Text("Tonemapping: %4ld µs", m_tonemap_time.average().count());
	ImGui::Text(" Debug draw: %4ld µs", m_debug_draw_time.average().count());
	// ImGui::Text("   PP blur: %4ld µs", m_pp_blur_time.average().count());

	// if (ImPlot::BeginPlot("plots"))
	// {
	// 	const float x[] = { 0, 1, 2, 3 };
	// 	const float v[] = { 3.5f, 1.f, 2.f, 4.4f };
	// 	ImPlot::SetupAxesLimits(0, 100, 0, 100);
	// 	ImPlot::PlotShaded("Shadows", x, v, 4);

	// 	ImPlot::EndPlot();
	// }

	ImGui::Begin("Settings");
	{
		ImGui::Text("T: %6.2f", _running_time.count());

		if (ImGui::CollapsingHeader("Camera Info", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const auto cam_pos   = m_camera.position();
			const auto cam_fwd   = m_camera.forwardVector();
			const auto cam_right = m_camera.rightVector();
			const auto cam_up    = m_camera.upVector();

			const auto fwd_xz = glm::normalize(glm::vec3(cam_fwd.x, 0.f, cam_fwd.z));
			const float heading_angle = std::acos(glm::clamp(glm::dot(AXIS_Z, fwd_xz), -1.f, 1.f));


			ImGui::Text("     Yaw : %6.1f° Pitch : %5.1f°\n"
						"Position : %5.1f ; %5.1f ; %5.1f\n"
						"Forward  : %5.2f ; %5.2f ; %5.2f   %5.1f°\n"
						"Right    : %5.2f ; %5.2f ; %5.2f\n"
						"Up       : %5.2f ; %5.2f ; %5.2f",
						glm::degrees(m_camera.yaw()), glm::degrees(m_camera.pitch()),
						cam_pos.x, cam_pos.y, cam_pos.z,
						cam_fwd.x, cam_fwd.y, cam_fwd.z, glm::degrees(heading_angle),
						cam_right.x, cam_right.y, cam_right.z,
						cam_up.x, cam_up.y, cam_up.z);
			ImGui::Text("PVS size : %lu", _scenePvs.size());

			ImGui::Checkbox("Draw AABB", &m_debug_draw_aabb);
			ImGui::Checkbox("Draw Lights", &m_debug_draw_light_markers);

			if(ImGui::SliderFloat("FOV", &m_camera_fov, 25.f, 150.f))
				calculateShadingClusterGrid();
		}

		if (ImGui::CollapsingHeader("Lights"))//, ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);

			ImGui::Text("Cluster  resolution: %u x %u x %u", m_cluster_resolution.x, m_cluster_resolution.y, m_cluster_resolution.z);
			ImGui::Checkbox("Draw cluster grid (slow!)  [c]", &m_debug_draw_cluster_grid);
			if (ImGui::Checkbox("Show cluster geom", &m_debug_cluster_geom))
				m_debug_clusters_occupancy = false;

			if (ImGui::Checkbox("Show cluster occupancy", &m_debug_clusters_occupancy))
				m_debug_cluster_geom = false;

			if (m_debug_cluster_geom or m_debug_clusters_occupancy or m_debug_draw_cluster_grid)
				ImGui::SliderFloat("Cluster debug blending", &m_debug_clusters_blend_factor, 0.0f, 1.0f);

			ImGui::Checkbox   ("Animate Lights",    &m_animate_lights);
			ImGui::SliderFloat("Animation Speed",   &m_animation_speed, 0.0f, 15.0f, "%.1f");

			ImGui::PopItemWidth();
		}

		if (ImGui::CollapsingHeader("Tonemapper"))
		{
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
			ImGui::SliderFloat("Exposure",             &m_exposure,             0.0, 10.0, "%.1f");
			ImGui::SliderFloat("Gamma",                &m_gamma,                0.0, 10.0, "%.1f");
			ImGui::SliderFloat("Background LOD level", &m_background_lod_level, 0.0, glm::log2(float(m_env_cubemap_rt->width())), "%.1f");

			if (ImGui::BeginCombo("HDR map", m_hdr_maps_names[m_current_hdr_map_idx].data()))
			{
				for (uint8_t idx = 0; idx < std::size(m_hdr_maps_names); ++idx)
				{
					const bool is_selected = (m_current_hdr_map_idx == idx);
					if (ImGui::Selectable(m_hdr_maps_names[idx].data(), is_selected))
					{
						m_current_hdr_map_idx = idx;
						PrecomputeIndirectLight(FileSystem::getResourcesPath() / "textures/skyboxes/IBL" / m_hdr_maps_names[m_current_hdr_map_idx]);
					}

					if (is_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::PopItemWidth();
		}

		if (ImGui::CollapsingHeader("Bloom"))
		{
			ImGui::Checkbox   ("Bloom enabled",        &m_bloom_enabled);
			ImGui::SliderFloat("Bloom threshold",      &m_bloom_threshold,      0, 15.f, "%.1f");
			ImGui::SliderFloat("Bloom knee",           &m_bloom_knee,           0,  1.f, "%.1f");
			ImGui::SliderFloat("Bloom intensity",      &m_bloom_intensity,      0,  2.f, "%.1f");
			ImGui::SliderFloat("Bloom dirt intensity", &m_bloom_dirt_intensity, 0, 10.f, "%.1f");
		}
		if(ImGui::CollapsingHeader("Fog / Scattering", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderFloat("Fog density", &m_fog_density, 0.f, 1.f);
		}

		if(ImGui::CollapsingHeader("Shadows"))//, ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderFloat("Bias constant",    &m_shadow_bias_constant,       -0.03f,   0.02f, "%.3f");
			ImGui::SliderFloat("Bias slope scale", &m_shadow_bias_slope_scale,     0.f,    5.f,   "%.1f");
			ImGui::SliderFloat("Bias slope power", &m_shadow_bias_slope_power,     0.01f,  5.f,   "%.2f");
			ImGui::SliderFloat("Bias dist. scale", &m_shadow_bias_distance_scale, -0.01f,    0.01f, "%.3f");
			ImGui::SliderFloat("Bias scale",       &m_shadow_bias_scale,          -2.f,    2.f,   "%.1f");

			static std::string size_line(64, ' ');
			size_line.clear();
			for(const auto &[size, count]: _shadow_atlas.allocated_counts())
				size_line += std::format("  {:4}: {}", size, count);
			if(size_line.empty())
				ImGui::Text("  -- no shadow maps");
			else
				ImGui::Text("  %s", size_line.c_str());

			ImGui::Text("Lights rendered: %lu  slots: %lu", _light_shadow_maps_rendered, _shadow_atlas_slots_rendered);
		}

		if(ImGui::CollapsingHeader("Images", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static const char *rt_names[] = {
				"-- No selection",
				// cube
				"env_cubemap_rt  [cube]",
				"irradiance_cubemap_rt  [cube]",
				"prefiltered_env_map_rt  [cube]",

				// 2d
				"depth_pass_rt",
				"rt",
				"pp_low_rt",
				"pp_full_rt",
				"final_rt",
				"shadow_atlas",

				// 3d
				"scattering froxels [3d]",
			};
			static int current_image = 6;
			ImGui::Combo("Render target", &current_image, rt_names, std::size(rt_names));

			RenderTarget::Texture2d *rt = nullptr;
			RenderTarget::Cube *rtc = nullptr;
			const Texture3D *t3 = nullptr;
			switch(current_image)
			{
			case  1: rtc = m_env_cubemap_rt.get(); break;
			case  2: rtc = m_irradiance_cubemap_rt.get(); break;
			case  3: rtc = m_prefiltered_env_map_rt.get(); break;
			case  4: rt = &m_depth_pass_rt; break;
			case  5: rt = &_rt; break;
			case  6: rt = &_pp_low_rt; break;
			case  7: rt = &_pp_full_rt; break;
			case  8: rt = &_final_rt; break;
			case  9: rt = &_shadow_atlas; break;
			case 10: t3 = &m_volumetrics_pp.froxel_texture(); break;
			}
			// const bool is_cube = current_image >= 1 and current_image <= 3;
			// const bool is_depth = current_image == 4;

			ImVec2 top_left { 0, 0 };
			ImVec2 bottom_right { 1, 1 };

			auto zoom_uv0 = [](float zoom, const ImVec2 &center) -> ImVec2 {
				float half_inv_zoom = 0.5f / zoom;
				return ImVec2(center.x - half_inv_zoom, center.y - half_inv_zoom);
			};

			auto zoom_uv1 = [](float zoom, const ImVec2 &center) -> ImVec2 {
				float half_inv_zoom = 0.5f / zoom;
				return ImVec2(center.x + half_inv_zoom, center.y + half_inv_zoom);
			};

			const auto vMin = ImGui::GetWindowContentRegionMin();
			const auto vMax = ImGui::GetWindowContentRegionMax();
			const auto win_width = std::min(vMax.x - vMin.x, 512.f);
			static std::string meta_info;
			meta_info.clear();

			if(t3)
			{
				const auto &meta = t3->GetMetadata();
				const float aspect = float(meta.width) / float(meta.height);

				const ImVec2 img_size { win_width, float(win_width)/aspect };

				static RenderTarget::Texture2d tex3d_rt;
				if(not tex3d_rt)
					tex3d_rt.create("tex3d-preview", 512, 512, RenderTarget::Color::Texture, RenderTarget::Depth::None);

				static int major_axis { 2 };
				ImGui::Combo("Major axis", &major_axis, "X\0Y\0Z\0");
				static float level { 0 };
				ImGui::SliderFloat("Major axis value", &level, 0, 1, "%.2f");

				visualize_3d_texture(*t3, tex3d_rt, major_axis, level);

				rt = &tex3d_rt;
				const auto color_f = meta.channel_format;
				meta_info = std::format("Color: {} x {} x {}  {}", meta.width, meta.height, meta.depth, gl_lookup::enum_name(color_f).substr(3));
			}

			if(rt)
			{
				float aspect = float(rt->width()) / float(rt->height());

				const ImVec2 img_size { win_width, float(win_width)/aspect };

				static ImVec2 center { 0.5f, 0.5f };
				static float magnification = 1.f;
/*
				if(rt->color_texture() or rt->depth_texture())
				{
					ImGui::SliderFloat("Magnification", &magnification, 1.f, 16.f, "%.1f");
					ImGui::SliderFloat("X-center", &center.x, 0, 1.f, "%.2f");
					ImGui::SliderFloat("Y-center", &center.y, 0, 1.f, "%.2f");
				}
*/
				if(rt->has_color() and rt->color_texture())
				{
					const auto &texture = rt->color_texture();

					top_left = zoom_uv0(magnification, center);
					bottom_right = zoom_uv1(magnification, center);

					ImGui_ImageEx(texture.texture_id(), img_size, top_left, bottom_right, 0);

					if(not meta_info.empty())
						ImGui::Text("%s", meta_info.c_str());
					else
					{
						const auto color_f = rt->color_format();
						ImGui::Text("Color: %u x %u  %s", rt->width(), rt->height(), gl_lookup::enum_name(color_f).substr(3).data());
					}
				}

				if(rt->has_depth() and rt->depth_texture())
				{
					const auto &texture = rt->depth_texture();

					// render with shader to show as gray scale
					ImGui_ImageEx(texture.texture_id(), img_size, top_left, bottom_right, m_imgui_depth_texture_shader->program_id());

					const auto depth_f = rt->depth_format();
					ImGui::Text("Depth: %u x %u  %s", rt->width(), rt->height(), gl_lookup::enum_name(depth_f).substr(3).data());
				}
			}

			if(rtc)
			{
				float aspect = float(rtc->width()) / float(rtc->height());

				const ImVec2 img_size { win_width / 2, float(win_width / 2)/aspect };

				if(rtc->has_color() and rtc->color_texture())
				{
					const auto &texture = rtc->color_texture();

					const char *names[] = { "right", "left", "up", "down", "front", "back" };
					for(auto face = 0u; face < 6; ++face)
					{
						ImGui_ImageEx(texture.texture_face_id(CubeFace(face)), img_size, top_left, bottom_right, 0);
						ImGui::Text("%u: %s", face, names[face]);
					}
				}
			}
		}
	}
	ImGui::End();
}

void ImGui_ImageEx(ImTextureID texture_id, ImVec2 size, ImVec2 uv0, ImVec2 uv1, GLuint shader_id)
{
	struct CB_args
	{
		GLuint program_id;
		GLuint texture_id;
	};

	auto *args = new CB_args{
		shader_id,
		GLuint(texture_id),
	};

	static TextureSampler clamp0_sampler;
	if(not clamp0_sampler)
	{
		clamp0_sampler.Create();
		clamp0_sampler.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::Linear);
		clamp0_sampler.SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
	}

	auto dl = ImGui::GetWindowDrawList();

	dl->AddCallback([](const ImDrawList *, const ImDrawCmd *cmd) {
		CB_args *args = static_cast<CB_args *>(cmd->UserCallbackData);
		if(args->program_id)
		{
			const auto *draw_data = ImGui::GetDrawData();
			float left = draw_data->DisplayPos.x;
			float right = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
			float top = draw_data->DisplayPos.y;
			float bottom = draw_data->DisplayPos.y + draw_data->DisplaySize.y;

			const auto ortho_proj = glm::orthoLH(left, right, bottom, top, 1.f, -1.f);

			glUseProgram(args->program_id);
			const auto projLoc = glGetUniformLocation(args->program_id, "u_projection");
			glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(ortho_proj));
		}
		glBindTextureUnit(0, args->texture_id);
		delete args;

		clamp0_sampler.Bind(0);
	}, args);

	dl->AddImage(texture_id, ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + size, uv0, uv1);
	ImGui::Dummy(size);  // Reserve layout space *before* drawing

	dl->AddCallback([](const ImDrawList *, const ImDrawCmd *) {
		glUseProgram(0);
		glBindTextureUnit(0, 0);
		glBindSampler(0, 0);
	}, nullptr);

		   // reset all ImGui state
	dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void visualize_3d_texture(const Texture3D &t3, RenderTarget::Texture2d &out, int major_axis, float level)
{
	// TODO: visualize the 3d texture, somehow.
	//   render to 'out'

	GLint prev_fbo { 0 };
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

	out.bindRenderTarget();

	static GLuint _empty { 0 };
	if(not _empty)
		glCreateVertexArrays(1, &_empty);

	const auto shader_path = FileSystem::getResourcesPath() / "shaders";
	static Shader shader(shader_path / "imgui_3d_texture.vert", shader_path / "imgui_3d_texture.frag");
	if(not shader)
		shader.link();

	t3.Bind(0);

	// shader.setUniform("u_projection"sv, glm::mat4(1));
	shader.setUniform("u_axis", major_axis);
	shader.setUniform("u_level", level);

	shader.bind();
	glBindVertexArray(_empty);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(prev_fbo));
}
