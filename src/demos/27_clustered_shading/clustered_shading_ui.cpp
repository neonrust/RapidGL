#include "clustered_shading.h"

#include "gl_lookup.h"
#include "gui/gui.h"   // IWYU pragma: keep

// #include "implot.h"

#include "filesystem.h"
#include "constants.h"
#include <ranges>

using namespace std::literals;

using namespace RGL;


void ImGui_ImageEx(ImTextureID texture_id, ImVec2 size, ImVec2 uv1, ImVec2 uv0, GLuint shader_id);

void visualize_3d_texture(const Texture3D &t3, RenderTarget::Texture2d &out, Shader &shader);


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

#define ROW() ImGui::TableNextRow();
#define COL(n) ImGui::TableSetColumnIndex(n)
#define TIMING(label, time) ROW();\
COL(0); ImGui::Text(label); \
COL(1); ImGui::Text("%4ld µs", (time).count())

	if(ImGui::BeginTable("Timings", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable))
	{
		ImGui::TableSetupColumn("Phase",    ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();

		TIMING("Culling",  m_cull_scene_time.average());
		TIMING("Z-pass", m_depth_time.average());

		TIMING("Shadow", m_shadow_time.average() + m_shadow_alloc_time.average());
		TIMING("  alloc", m_shadow_alloc_time.average());
		TIMING("  render", m_shadow_time.average());

		TIMING("Clusters", m_cluster_find_time.average() + m_cluster_index_time.average() + m_light_cull_time.average());
		TIMING("  find", m_cluster_find_time.average());
		TIMING("  collect", m_cluster_index_time.average());
		TIMING("  cull", m_light_cull_time.average());

		TIMING("Shading", m_shading_time.average());
		TIMING("Skybox", m_skybox_time.average());

		TIMING("Volumetrics", m_volumetrics_cull_time.average() + m_volumetrics_inject_time.average() + m_volumetrics_accum_time.average() + m_volumetrics_render_time.average());
		TIMING("  cull", m_volumetrics_cull_time.average());
		TIMING("  inject", m_volumetrics_inject_time.average());
		TIMING("  accum",  m_volumetrics_accum_time.average());
		TIMING("  render", m_volumetrics_render_time.average());

		TIMING("Tonemapping", m_tonemap_time.average());
		TIMING("Debug draw", m_debug_draw_time.average());
		// TIMING("PP blur", m_pp_blur_time.average());

		ImGui::EndTable();
	}

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
			ImGui::Text("Lights PVS size : %lu", _lightsPvs.size());

			ImGui::Checkbox("Draw AABB", &m_debug_draw_aabb);

			if(ImGui::SliderFloat("FOV", &m_camera_fov, 25.f, 150.f))
				calculateShadingClusterGrid();
			static float target_fps = static_cast<float>(1.0 / m_frame_time);
			if(ImGui::SliderFloat("Target FPS", &target_fps, 5.f, 200.f))
				m_frame_time = 1.f / target_fps;
		}

		if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f); // less wide sliders

			ImGui::ColorEdit3("Ambient", glm::value_ptr(_ambient_radiance));
			ImGui::SliderFloat("IBL strength", &_ibl_strength, 0.f, 2.f, "%.1f");

			static float falloff_power { _light_mgr.falloff_power() };
			if(ImGui::SliderFloat("Falloff power", &falloff_power, 10.f, 1000.f, "%.0f"))
				_light_mgr.set_falloff_power(falloff_power);
			static float spec_distance { m_camera.farPlane()*0.1f };
			if(ImGui::SliderFloat("Specular distance", &spec_distance, .5f, 100.f, "%.1f"))
				m_clustered_pbr_shader->setUniform("u_specular_max_distance"sv, spec_distance);
			if(auto sun_id = _shadow_atlas.sun_id(); sun_id != NO_LIGHT_ID)
				ImGui::SliderFloat("Sun size", &_sun_size, 0.1f, 5.f, "%.1f");
			ImGui::Text("Cluster  resolution: %u x %u x %u", m_cluster_resolution.x, m_cluster_resolution.y, m_cluster_resolution.z);
			ImGui::Checkbox("Draw cluster grid (slow!)  [c]", &m_debug_draw_cluster_grid);
			if(ImGui::Checkbox("Show cluster geom", &m_debug_cluster_geom) and m_debug_cluster_geom)
			{
				m_debug_clusters_occupancy = false;
				m_debug_tile_occupancy = false;
			}
			if(ImGui::Checkbox("Show cluster occupancy", &m_debug_clusters_occupancy) and m_debug_clusters_occupancy)
			{
				m_debug_cluster_geom = false;
				m_debug_tile_occupancy = false;
			}
			if(ImGui::Checkbox("Show tile occupancy", &m_debug_tile_occupancy) and m_debug_tile_occupancy)
			{
				m_debug_cluster_geom = false;
				m_debug_clusters_occupancy = false;
			}
			static bool show_unshaded { false };
			if(ImGui::Checkbox("Show unshaded as red", &show_unshaded))
				m_clustered_pbr_shader->setUniform("u_debug_unshaded_clusters"sv, show_unshaded);

			if (m_debug_cluster_geom or m_debug_clusters_occupancy or m_debug_draw_cluster_grid or m_debug_tile_occupancy)
				ImGui::SliderFloat("Debug overlay blend", &m_debug_coverlay_blend, 0.0f, 1.0f);

			ImGui::Checkbox("Draw light icons", &m_debug_draw_light_markers);
			ImGui::Checkbox("Draw surface light geometry", &m_draw_surface_lights_geometry);

			ImGui::Checkbox   ("Animate Lights",    &m_animate_lights);
			ImGui::SliderFloat("Animation Speed",   &m_animation_speed, 0.0f, 15.0f, "%.1f");

			ImGui::PopItemWidth();
		}

		if (ImGui::CollapsingHeader("Tonemapper"))
		{
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
			static float exposure = m_camera.exposure();
			if(ImGui::SliderFloat("Exposure",             &exposure,             0.f, 10.f, "%.1f"))
				m_camera.setExposure(exposure);
			ImGui::SliderFloat("Gamma",                &m_gamma,                0.f, 10.f, "%.1f");
			static float saturation { 1.f };
			if(ImGui::SliderFloat("Saturation",        &saturation,             0.f, 5.0f, "%.1f"))
				m_tmo_pp.setSaturation(saturation);

			// TODO: move these settings somewhere else?  (not actually tonemapping settings)
			ImGui::SliderFloat("IBL MIP level", &_ibl_mip_level, 0.0, glm::log2(float(m_env_cubemap_rt->width())), "%.1f");

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
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::PopItemWidth();
		}

		static std::string bloom_label = "Bloom  b:";
		static const auto bloom_label_size = 9u;
		if(m_bloom_enabled and bloom_label.size() != bloom_label_size + 5)
		{
			bloom_label.resize(bloom_label_size);
			bloom_label.append(" [ON]");
		}
		else if(not m_bloom_enabled and bloom_label.size() != bloom_label_size + 6)
		{
			bloom_label.resize(bloom_label_size);
			bloom_label.append(" [off]");
		}
		if (ImGui::CollapsingHeader(bloom_label.c_str()))
		{
			ImGui::Checkbox("Enabled (b)",        &m_bloom_enabled);
			if(m_bloom_enabled)
			{
				ImGui::SliderFloat("Threshold",      &m_bloom_threshold,      0, 15.f, "%.1f");
				ImGui::SliderFloat("Knee",           &m_bloom_knee,           0,  1.f, "%.1f");
				ImGui::SliderFloat("Intensity",      &m_bloom_intensity,      0,  2.f, "%.1f");
				ImGui::SliderFloat("Dirt intensity", &m_bloom_dirt_intensity, 0, 10.f, "%.1f");
			}
		}

		static std::string fog_label = "Fog / Volumetrics  f:";
		static const auto fog_label_size = 21u;
		if(_fog_enabled and fog_label.size() != fog_label_size + 5)
		{
			fog_label.resize(fog_label_size);
			fog_label.append(" [ON]");
		}
		else if(not _fog_enabled and fog_label.size() != fog_label_size + 6)
		{
			fog_label.resize(fog_label_size);
			fog_label.append(" [off]");
		}
		if(ImGui::CollapsingHeader(fog_label.c_str()))//, ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Enabled (f)",    &_fog_enabled);
			if(_fog_enabled)
			{
				ImGui::SliderFloat("Strength", &_fog_strength, 0.f, 4.f);
				ImGui::SliderFloat("Density", &_fog_density, 0.f, 1.f);
				static float anisotropy { 0.7f };
				if(ImGui::SliderFloat("Anisotropy", &anisotropy, -1.f, 1.f))
					m_volumetrics_pp.setAnisotropy(anisotropy);
				static bool fog_noise { false };
				if(ImGui::Checkbox("Fog noise", &fog_noise))
					m_volumetrics_pp.setNoiseEnabled(fog_noise);
				if(fog_noise)
				{
					static glm::vec3 noise_offset(0);
					if(ImGui::SliderFloat("Noise offset X", &noise_offset.x, 0.f, 100.f))
						m_volumetrics_pp.setNoiseOffset(noise_offset);
					if(ImGui::SliderFloat("Noise offset Y", &noise_offset.y, 0.f, 100.f))
						m_volumetrics_pp.setNoiseOffset(noise_offset);
					if(ImGui::SliderFloat("Noise offset Z", &noise_offset.z, 0.f, 100.f))
						m_volumetrics_pp.setNoiseOffset(noise_offset);
					static glm::vec3 noise_freq{ 0.2f };
					if(ImGui::SliderFloat("Noise frequency X", &noise_freq.x, 0.f, 5.f))
						m_volumetrics_pp.setNoiseFrequency(noise_freq);
					if(ImGui::SliderFloat("Noise frequency Y", &noise_freq.y, 0.f, 5.f))
						m_volumetrics_pp.setNoiseFrequency(noise_freq);
					if(ImGui::SliderFloat("Noise frequency Z", &noise_freq.z, 0.f, 5.f))
						m_volumetrics_pp.setNoiseFrequency(noise_freq);
				}
				static bool z_noise_enabled { true };
				if(ImGui::Checkbox("Z-Noise", &z_noise_enabled))
					m_volumetrics_pp.setFroxelNoiseEnabled(z_noise_enabled);
				static bool blend_enabled { true };
				if(ImGui::Checkbox("Temporal blending", &blend_enabled))
					m_volumetrics_pp.setTemporalBlending(blend_enabled);
				if(blend_enabled)
					ImGui::SliderFloat("Temporal blend", &_fog_blend_weight, 0.f, 0.99f, "%.2f");  // lerp: <current> - <previous>
				static bool blur3_enabled { true };
				if(ImGui::Checkbox("3D Blur", &blur3_enabled))
					m_volumetrics_pp.setFroxelBlurEnabled(blur3_enabled);
				static bool blur2_enabled { false };
				if(ImGui::Checkbox("2D Blur", &blur2_enabled))
					m_volumetrics_pp.setPostBlurEnabled(blur2_enabled);
			}
		}

		if(ImGui::CollapsingHeader("Shadows"))//, ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Colorize shadow slots", &_debug_colorize_shadows);
			ImGui::SliderFloat("Shadow occlusion",   &m_shadow_occlusion,            0.f,    1.f,   "%.2f");
			auto backoff = _shadow_atlas.csm_backoff();
			if(ImGui::SliderFloat("CSM backoff distance", &backoff, 1.f, 100.f, "%.1f"))
				_shadow_atlas.set_csm_backoff(backoff);
			ImGui::SliderFloat("Shadow occlusion",   &m_shadow_occlusion,            0.05f,  1.f,   "%.2f");
			ImGui::SliderFloat("Bias constant",      &m_shadow_bias_constant,       -0.03f,  0.03f, "%.4f");
			ImGui::SliderFloat("Bias slope scale",   &m_shadow_bias_slope_scale,    -5.f,    5.f,   "%.2f");
			ImGui::SliderFloat("Bias slope power",   &m_shadow_bias_slope_power,     0.01f,  5.f,   "%.3f");
			ImGui::SliderFloat("Bias dist. scale",   &m_shadow_bias_distance_scale, -0.01f,  0.01f, "%.4f");
			ImGui::SliderFloat("Bias texel sz mix",  &m_shadow_bias_texel_size_mix,  0.f,    1.f,   "%.2f");
			ImGui::SliderFloat("Bias scale",         &m_shadow_bias_scale,          -2.f,    2.f,   "%.2f");

			static std::string size_line(64, ' ');
			size_line.clear();
			for(const auto &[size, count]: _shadow_atlas.allocated_counts())
				size_line += std::format("  {:4}: {}", size, count);
			if(size_line.empty())
				ImGui::Text("  -- no shadow maps");
			else
				ImGui::Text("  %s", size_line.c_str());

			ImGui::Text("Rendered:  Lights: %3lu  Slots: %lu", _light_shadow_maps_rendered, _shadow_atlas_slots_rendered);
		}

		if(ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static const char *rt_names[] = {
				"-- No selection",
				// cube
				"env_cubemap_rt  [cube]",
				"irradiance_cubemap_rt  [cube]",
				"prefiltered_env_map_rt  [cube]",

				// 2d
				"depth_pass_rt",   // 4
				"rt",
				"pp_low_rt",
				"pp_full_rt",
				"final_rt",
				"shadow_atlas",

				// 3d
				"volumetric froxels [3d]",  // 10
				"volumetric froxels back [3d]",
				"volumetric froxels acc [3d]",
			};
			static int current_image = 9;
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
			case 10: t3 = &m_volumetrics_pp.froxel_texture(0); break;
			case 11: t3 = &m_volumetrics_pp.froxel_texture(1); break;
			case 12: t3 = &m_volumetrics_pp.froxel_texture(2); break;
			}
			// const bool is_cube = current_image >= 1 and current_image <= 3;
			// const bool is_depth = current_image == 4;

			static ImVec2 default_top_left { 0, 0 };
			static ImVec2 default_bottom_right { 1, 1 };

			static ImVec2 top_left = default_top_left;
			static ImVec2 bottom_right = default_bottom_right;

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
				const char *axis_names[] = { "X", "Y", "Z" };
				ImGui::SliderFloat(axis_names[major_axis], &level, 0, 1, "%.2f");

				static float brightness { 4 };
				ImGui::SliderFloat("Brightness", &brightness, 0, 10, "%.1f");

				static float alpha_boost { 5 };
				ImGui::SliderFloat("Alpha boost", &alpha_boost, 1, 10, "%.1f");

				const auto shader_path = FileSystem::getResourcesPath() / "shaders";
				static Shader shader(shader_path / "imgui_3d_texture.vert", shader_path / "imgui_3d_texture.frag");
				if(not shader)
					shader.link();

				// shader.setUniform("u_projection"sv, glm::mat4(1));
				shader.setUniform("u_axis", major_axis);
				shader.setUniform("u_level", level);
				shader.setUniform("u_brightness"sv, brightness);
				shader.setUniform("u_alpha_boost"sv, alpha_boost);

				visualize_3d_texture(*t3, tex3d_rt, shader);

				rt = &tex3d_rt;
				const auto color_f = meta.channel_format;
				meta_info = std::format("Color: {} x {} x {}  {}", meta.width, meta.height, meta.depth, gl_lookup::enum_name(color_f).substr(3));
			}

			if(rt)
			{
				float aspect = float(rt->width()) / float(rt->height());

				if(rt == &_shadow_atlas)
				{
					glm::vec2 atlas_size { rt->width(), rt->height() };
					static std::string selected_label = "< whole atlas >";
					selected_label.reserve(32);
					static LightID selected_light { NO_LIGHT_ID };
					static uint32_t selected_slot { std::numeric_limits<uint32_t>::max() };
					static std::string label;
					label.reserve(32);
					if(ImGui::BeginCombo("Atlas light", selected_label.c_str()))
					{
						label = "< whole atlas >";
						if(ImGui::Selectable(label.c_str(), selected_label == label))
						{
							selected_label = label;
							top_left = default_top_left;
							bottom_right = default_bottom_right;
							Log::debug("  no region");
						}

						for(const auto &[light_id, atlas_light]: _shadow_atlas.allocated_lights())
						{
							auto is_selected = selected_light == light_id;
							label.clear();
							const auto &L = _light_mgr.get_by_id(light_id);
							std::format_to(std::back_inserter(label), "[{}] {}: {} slots", light_id, _light_mgr.type_name(L), atlas_light.num_slots);
							if(light_id == _pov_light_id)
								label.append(" pov");
							ImGui::Selectable(label.c_str(), is_selected, ImGuiSelectableFlags_Disabled);

							static const char *face_names[] = {
								" left",
								" right",
								" up",
								" down",
								" forw.",
								" backw.",

							};

							for(auto slot = 0u; slot < atlas_light.num_slots; ++slot)
							{
								// ImGui::PushID(123);
								auto is_slot_selected = is_selected and slot == selected_slot;
								label.clear();
								// TODO: instead of "slot", use "cascade N" or "+X", etc. (depending on light type)
								std::format_to(std::back_inserter(label), "  {}: slot {} ({})", light_id, slot, atlas_light.slots[slot].size);
								if(IS_POINT_LIGHT(L))
									label.append(face_names[slot]);
								if(ImGui::Selectable(label.c_str(), is_slot_selected))
								{
									selected_light = light_id;
									selected_slot = slot;
									selected_label = label;
									// std::format_to(std::back_inserter(selected_label), "{}, slot {} ({})", light_id, slot, atlas_light.slots[slot].size);

									const auto rect = glm::vec4(atlas_light.slots[slot].rect);
									top_left = ImVec2{ rect.x / atlas_size.x, rect.y / atlas_size.y };
									bottom_right = ImVec2{ (rect.x + rect.z) / atlas_size.x, (rect.y + rect.w) / atlas_size.y };
									Log::debug("  region {:.2f}; {:.2f}  {:.2f}x{:.2f}", top_left.x, top_left.y, bottom_right.x, bottom_right.y);
								}
								if(is_selected)
									ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}
				}
				else
				{
					top_left = default_top_left;
					bottom_right = default_bottom_right;
					Log::debug("  no region");
				}


				const ImVec2 img_size { win_width, float(win_width)/aspect };

				if(rt->has_color() and rt->color_texture())
				{
					const auto &texture = rt->color_texture();

					// TODO: use custom shader to enable brightness and alpha boost controls

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

					static float depth_brightness { 1.f };
					if(ImGui::SliderFloat("Brightness", &depth_brightness, 1, 100, "%.1f"))
						m_imgui_depth_texture_shader->setUniform("u_brightness"sv, depth_brightness);
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

	ImGui::Begin("Lights");
	{
		if(ImGui::Button("+ point"))
		{
			_light_mgr.add(PointLightParams{
				.color = { 1, 1, 1 },
				.intensity = 50.f,
				.fog = 1.f,
				.shadow_caster = true,
				.position = m_camera.position(),
			});
		}
		ImGui::SameLine();
		if(ImGui::Button("+ spot"))
		{
			_light_mgr.add(SpotLightParams{
				.color = { 1, 1, 1 },
				.intensity = 50.f,
				.fog = 1.f,
				.shadow_caster = true,
				.position = m_camera.position(),
				.direction = m_camera.forwardVector(),
				.outer_angle = glm::radians(m_camera.verticalFov() - 10) / 2,
				.inner_angle = glm::radians(m_camera.verticalFov() - 10) / 3,
			});
		}

		static std::string selected_light_label = "< select light >";
		static LightID selected_light_id = NO_LIGHT_ID;
		static GPULight Lmut;

		if(ImGui::BeginCombo("Properties", selected_light_label.c_str()))
		{
			for(auto idx = 0u; idx < _light_mgr.size(); ++idx)
			{
				const auto &[light_id, L] = _light_mgr.at(idx);
				static std::string label;
				label.reserve(32);
				label.clear();
				std::format_to(std::back_inserter(label), "[{}] {}", light_id, _light_mgr.type_name(L));
				if(light_id == _pov_light_id)
					label.append(" POV");
				const auto is_selected = selected_light_id == light_id;
				if(ImGui::Selectable(label.c_str(), is_selected))
				{
					selected_light_id = light_id;
					selected_light_label = label;
					Lmut = _light_mgr.get_by_id(selected_light_id); // a copy, to modify it
				}

				if(is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		if(selected_light_id != NO_LIGHT_ID)
		{
			// NOTE: this will not be updated by changes to lights made elsewhere

			auto enabled = IS_ENABLED(Lmut);
			if(ImGui::Checkbox("Enabled", &enabled))
			{
				_light_mgr.set_enabled(selected_light_id, enabled);
				Lmut = _light_mgr.get_by_id(selected_light_id);
			}

			if(ImGui::ColorEdit3("Color", glm::value_ptr(Lmut.color)))
			{
				const auto color = Lmut.color;
				Lmut = _light_mgr.get_by_id(selected_light_id); // a copy, to modify it
				Lmut.color = color;
				_light_mgr.set(selected_light_id, Lmut);
			}
			if(ImGui::SliderFloat("Intensity", &Lmut.intensity, 1, 1000, "%.0f"))
			{
				const auto intensity = Lmut.intensity;
				Lmut = _light_mgr.get_by_id(selected_light_id); // a copy, to modify it
				_light_mgr.set_intensity(Lmut, intensity);
				_light_mgr.set(selected_light_id, Lmut);
			}
			if(IS_DIR_LIGHT(Lmut))
			{
				auto direction = Lmut.direction;
				float elevation = -glm::degrees(std::asin(direction.y));
				float azimuth   =  glm::degrees(std::atan2(direction.x, direction.z));

				auto changed =  ImGui::SliderFloat("Azimuth",   &azimuth,    -180.f, 180.f, "%.1f");
					 changed |= ImGui::SliderFloat("Elevation", &elevation,  -15.f,  89.9f, "%.1f");

				if(changed)
				{
					elevation   = glm::radians(-elevation);
					azimuth     = glm::radians(azimuth);
					direction.x = std::sin(azimuth) * std::cos(elevation);
					direction.y = std::sin(elevation);
					direction.z = std::cos(azimuth) * std::cos(elevation);
					_light_mgr.set_direction(Lmut, direction);
					_light_mgr.set(selected_light_id, Lmut);
				}
			}
			if(IS_SPOT_LIGHT(Lmut))
			{
				float outer_angle = glm::degrees(Lmut.outer_angle);
				if(ImGui::SliderFloat("Angle", &outer_angle, 0.1f, 89.9f, "%.1f"))
				{
					_light_mgr.set_spot_angle(Lmut, glm::radians(outer_angle));
					_light_mgr.set(selected_light_id, Lmut);
				}
				float inner_angle = 100.f * glm::degrees(Lmut.inner_angle) / outer_angle;
				if(ImGui::SliderFloat("Inner angle", &inner_angle, 0, 100, "%.1f%%"))
				{
					Lmut.inner_angle = glm::radians(outer_angle * inner_angle / 100.f);
					_light_mgr.set(selected_light_id, Lmut);
				}
			}

			auto cast_shadows = IS_SHADOW_CASTER(Lmut);
			if(ImGui::Checkbox("Cast shadow", &cast_shadows))
			{
				Lmut = _light_mgr.get_by_id(selected_light_id); // a copy, to modify it
				if(cast_shadows)
					Lmut.type_flags |= LIGHT_SHADOW_CASTER;
				else
					Lmut.type_flags &= ~LIGHT_SHADOW_CASTER;
				_light_mgr.set(selected_light_id, Lmut);
			}

			auto is_volumetric = IS_VOLUMETRIC(Lmut);
			if(ImGui::Checkbox("Volumetric", &is_volumetric))
			{
				Lmut = _light_mgr.get_by_id(selected_light_id); // a copy, to modify it
				if(is_volumetric)
					Lmut.type_flags |= LIGHT_VOLUMETRIC;
				else
					Lmut.type_flags &= ~LIGHT_VOLUMETRIC;
				_light_mgr.set(selected_light_id, Lmut);
			}

			if(selected_light_id == _pov_light_id)
				ImGui::SliderFloat("Distance", &_pov_light_distance, -5.f, 5.f, "%.1f");

				   // more properties...

			if(ImGui::Button("Move / rotate"))
				Log::info("Not yet");
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
		.program_id = shader_id,
		.texture_id = GLuint(texture_id),
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

void visualize_3d_texture(const Texture3D &t3, RenderTarget::Texture2d &out, Shader &shader)
{
	// TODO: visualize the 3d texture, somehow.
	//   render to 'out'

	GLint prev_fbo { 0 };
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

	out.bindRenderTarget();

	static GLuint _empty { 0 };
	if(not _empty)
		glCreateVertexArrays(1, &_empty);

	t3.Bind(0);

	shader.bind();
	glBindVertexArray(_empty);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(prev_fbo));
}
