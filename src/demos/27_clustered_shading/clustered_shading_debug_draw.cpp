#include "clustered_shading.h"

#include "window.h"


using namespace std::literals;

using namespace RGL;

glm::mat3 make_common_space_from_direction(const glm::vec3 &direction);


void ClusteredShading::debugDrawSceneBounds()
{
	const auto view_projection = m_camera.projectionTransform() * m_camera.viewTransform();

	// if using VBO, generate the data into a single VBO then draw using a single call


	glDisable(GL_DEPTH_TEST);
	glEnable(GL_LINE_SMOOTH);
	glDepthMask(GL_FALSE);

	glBindBuffer(GL_ARRAY_BUFFER, m_debug_draw_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);

	// indices are fixed  TODO use an element array buffer
	static const std::array<std::uint16_t, 24> indices { // see AABB::corners() for vertex ordering
		// top
		0, 1,
		1, 2,
		2, 3,
		3, 0,
		// bottom
		4, 5,
		5, 6,
		6, 7,
		7, 4,
		// "walls"
		0, 4,
		1, 5,
		2, 6,
		3, 7
	};

	// TODO: also draw AABBs for lights
	//   a tad "laborious" since the light "animation" is currently done in a compute shader


	m_line_draw_shader->bind();
	m_line_draw_shader->setUniform("u_line_color"sv, glm::vec4(0.3, 1.0, 0.7, 1));
	m_line_draw_shader->setUniform("u_mvp"sv, view_projection); // no model transform needed; we'll generate vertices in world-space

	for(const auto &obj: _scene) // _scenePvs
	{
		// TODO: the transformed AABB should be updated by the model itself, when moved that is.
		bounds::AABB tfm_aabb;
		for(const auto &corner: obj.model->aabb().corners())
			tfm_aabb.expand(obj.transform * glm::vec4(corner, 1));


		const auto &vertices = tfm_aabb.corners();
		// TODO: add UV so the shader can draw gradients?

		glNamedBufferData(m_debug_draw_vbo, GLsizeiptr(vertices.size()*sizeof(vertices[0])), &vertices[0], GL_STREAM_DRAW);
		glDrawElements(GL_LINES, indices.size(), GL_UNSIGNED_SHORT, &indices[0]);
	}


		   // restore some states
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDisableVertexAttribArray(0);

	/*
		for(const auto &spot: m_spot_lights)
		{
			const auto color = glm::vec4(glm::normalize(spot.point.base.color), 1);
			debugDrawSpotLight(spot, color);
		}
	*/
	for(const auto &L: _light_mgr)
	{
		const auto point_ = _light_mgr.to_<PointLight>(L);
		if(not point_)
			continue;
		const auto &point = point_.value();
		const auto color = glm::vec4(glm::normalize(point.color), 1);
		debugDrawSphere(point.position, point.affect_radius, color);
	}
}

void ClusteredShading::debugDrawLine(const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec4 &color)
{
	const auto view_projection = m_camera.projectionTransform() * m_camera.viewTransform();

	m_line_draw_shader->bind();
	m_line_draw_shader->setUniform("u_line_color"sv, color);
	m_line_draw_shader->setUniform("u_mvp"sv, view_projection); // no model transform needed; we'll generate vertices in world-space

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_LINE_SMOOTH);
	glDepthMask(GL_FALSE);

	glBindBuffer(GL_ARRAY_BUFFER, m_debug_draw_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);


	const std::array<glm::vec3, 2> vertices { p1, p2 };

	glNamedBufferData(m_debug_draw_vbo, GLsizeiptr(vertices.size()*sizeof(vertices[0])), &vertices[0], GL_STREAM_DRAW);
	glDrawArrays(GL_LINES, 0, vertices.size());


		   // restore some states
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDisableVertexAttribArray(0);
}

void ClusteredShading::debugDrawLine(const glm::uvec2 &p1, const glm::uvec2 &p2, const glm::vec4 &color, float thickness)
{
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_2d_line_shader->bind();

	m_2d_line_shader->setUniform("u_start"sv, p1);
	m_2d_line_shader->setUniform("u_end"sv, p2);
	m_2d_line_shader->setUniform("u_line_color"sv, color);
	m_2d_line_shader->setUniform("u_thickness", std::max(1.f, thickness));

	glBindVertexArray(_empty_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

void ClusteredShading::debugDrawRect(const glm::uvec2 &top_left, const glm::uvec2 &size, const glm::vec4 &color, float thickness)
{
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_2d_rect_shader->bind();

	m_2d_rect_shader->setUniform("u_rect_min"sv, top_left);
	m_2d_rect_shader->setUniform("u_rect_max"sv, top_left + size);
	m_2d_rect_shader->setUniform("u_line_color"sv, color);
	m_2d_rect_shader->setUniform("u_thickness", thickness); // 0 = filled

	glBindVertexArray(_empty_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

void ClusteredShading::debugDrawNumber(uint32_t number, const glm::uvec2 &bottom_right, float height, const glm::vec4 &color, float thickness)
{
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_2d_7segment_shader->bind();

	m_2d_7segment_shader->setUniform("u_number"sv, number);
	m_2d_7segment_shader->setUniform("u_bottom_right"sv, bottom_right);
	m_2d_7segment_shader->setUniform("u_height"sv, height);
	m_2d_7segment_shader->setUniform("u_color"sv, color);
	m_2d_7segment_shader->setUniform("u_thickness"sv, thickness);

	glBindVertexArray(_empty_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

void ClusteredShading::debugDrawSphere(const glm::vec3 &center, float radius, const glm::vec4 &color)
{
	debugDrawSphere(center, radius, 8, 10, color);
}

void ClusteredShading::debugDrawSphere(const glm::vec3 &center, float radius, size_t rings, size_t slices, const glm::vec4 &color)
{
	auto view_projection = m_camera.projectionTransform() * m_camera.viewTransform();
	auto transform = view_projection * glm::translate(glm::mat4(1), center) * glm::scale(glm::mat4(1), { radius, radius, radius });

	m_line_draw_shader->bind();
	m_line_draw_shader->setUniform("u_line_color"sv, color);
	m_line_draw_shader->setUniform("u_mvp"sv, transform);

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_LINE_SMOOTH);
	glDepthMask(GL_FALSE);

	glBindBuffer(GL_ARRAY_BUFFER, m_debug_draw_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);


	static std::vector<glm::vec3> vertices;
	vertices.reserve(6 * (rings + 1) * slices);
	vertices.clear();

		   // this bit was "borrowed" from Raylib's DrawSphereEx()  (c++-ified)

	const auto ring_step = glm::radians(180.f/float(rings + 1));
	const auto slice_step = glm::radians(360.f/float(slices));

		   // const auto ring_rot = glm::angleAxis(ring_step, AXIS_Y);

	const auto ring_cos = std::cos(ring_step);
	const auto ring_sin = std::sin(ring_step);
	const auto slice_cos = std::cos(slice_step);
	const auto slice_sin = std::sin(slice_step);

		   // rotation matrix around z axis
		   // const auto ring_rotate = glm::rotate(glm::mat4(1),  ring_step, AXIS_Z);
		   // rotation matrix around y axis
		   // const auto slice_rotate = glm::rotate(glm::mat4(1), slice_step, AXIS_Y);

	std::array<glm::vec3, 4> base_verts;
	base_verts[2] = AXIS_Y;
	base_verts[3] = { ring_sin, ring_cos, 0 };

	for(auto ring = 0u; ring < rings + 1; ++ring)
	{
		for(auto slice = 0u; slice < slices; ++slice)
		{
			// rotate around y axis to set up vertices for next face
			base_verts[0] = base_verts[2];
			base_verts[1] = base_verts[3];
			// base_verts[2] = slice_rotate * glm::vec4(base_verts[2], 0);
			// base_verts[3] = slice_rotate * glm::vec4(base_verts[3], 0);
			base_verts[2] = {
				slice_cos*base_verts[2].x - slice_sin*base_verts[2].z,
				base_verts[2].y,
				slice_sin*base_verts[2].x + slice_cos*base_verts[2].z
			};
			base_verts[3] = {
				slice_cos*base_verts[3].x - slice_sin*base_verts[3].z,
				base_verts[3].y,
				slice_sin*base_verts[3].x + slice_cos*base_verts[3].z
			};

			vertices.push_back(base_verts[0]);
			vertices.push_back(base_verts[3]);
			vertices.push_back(base_verts[1]);

			vertices.push_back(base_verts[0]);
			vertices.push_back(base_verts[2]);
			vertices.push_back(base_verts[3]);
		}

			   // rotate around z axis to set up  starting vertices for next ring
		base_verts[2] = base_verts[3];
		// base_verts[3] = ring_rotate * glm::vec4(base_verts[3], 0);
		base_verts[3] = { // rotation matrix around z axis
			ring_cos*base_verts[3].x + ring_sin*base_verts[3].y,
			-ring_sin*base_verts[3].x + ring_cos*base_verts[3].y,
			base_verts[3].z
		};
	}

		   // TODO: cache vertices? (key: rings + slices)

	glNamedBufferData(m_debug_draw_vbo, GLsizeiptr(vertices.size()*sizeof(vertices[0])), &vertices[0], GL_STREAM_DRAW);
	glDrawArrays(GL_LINES, 0, GLsizei(vertices.size()));


		   // restore some states
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDisableVertexAttribArray(0);
}

void ClusteredShading::debugDrawSpotLight(const SpotLight &light, const glm::vec4 &color)
{
	const auto &L = light;

	const auto dir_space = make_common_space_from_direction(L.direction);
	const auto line_rot = glm::rotate(glm::mat4(1), -L.outer_angle, dir_space[0]);
	const glm::vec3 dir_line = line_rot * glm::vec4(L.direction, 0)*L.affect_radius;

	static constexpr auto num_lines = 24;
	const auto rot_angle = glm::radians(360.f / float(num_lines));
	glm::vec3 first_end;
	glm::vec3 last_end;
	for(int idx = 0; idx < num_lines; ++idx)
	{
		glm::vec3 end_point = L.position + glm::vec3(glm::rotate(glm::mat4(1), rot_angle*float(idx), L.direction) * glm::vec4(dir_line, 0));

		debugDrawLine(L.position, end_point, color);
		if(idx > 0)
			debugDrawLine(end_point, last_end, color);
		else
			first_end = end_point;
		last_end = end_point;
	}

		   // center/axis
	debugDrawLine(first_end, last_end, color);
	debugDrawLine(L.position, L.position + L.direction*L.affect_radius, color);

		   // TODO: draw cap
}

void ClusteredShading::debugDrawClusterGrid()
{
	const glm::vec4 grid_color{ 0.1f, 1.f, 0.6f, m_debug_clusters_blend_factor };
	const glm::vec4 cluster_color{ 0.7f, 0.3f, 0.2f, m_debug_clusters_blend_factor };
	const glm::vec4 text_color { 1, 0.9f, 0.2f, m_debug_clusters_blend_factor };
	const glm::vec4 grid2d_color{ 0.1f, 0.3f, 0.9f, 0.8f * m_debug_clusters_blend_factor };

	const auto ww = Window::width();
	const auto wh = Window::height();
	const auto aspect = float(ww)/float(wh);


		   // draw screen grid, starting bottom left
	const auto stride = glm::vec2(m_cluster_block_size, m_cluster_block_size);
	for(auto x = 0.f; x < float(Window::width()); x += stride.x)
		debugDrawLine({ uint32_t(x), 0u }, { uint32_t(x), Window::height() - 1 }, grid2d_color);
	for(auto y = float(Window::height() - 1); y > 0.f; y -= stride.y)
		debugDrawLine({ 0u, uint32_t(y) }, { Window::width() - 1, uint32_t(y) }, grid2d_color);



	auto draw_grid = [this, &grid_color](const glm::uvec2 &top_left, const glm::uvec2 &size, const glm::uvec2 &dims) {

		auto x_stride = float(size.x) / float(dims.x);
		auto y_stride = float(size.y) / float(dims.y);

			   // vertical lines (keft to right)
		auto x = float(top_left.x) + x_stride;
		while(x < float(top_left.x + size.x))
		{
			debugDrawLine({ uint32_t(x), top_left.y }, { uint32_t(x), top_left.y + size.y }, grid_color);
			x += x_stride;
		}

			   // horizontallines (top to bottom)
		auto y = float(top_left.y) + y_stride;
		while(y < float(top_left.y + size.y))
		{
			debugDrawLine({ top_left.x, uint32_t(y) }, { top_left.x + size.x, uint32_t(y) }, grid_color);
			y += y_stride;
		}

		debugDrawRect(top_left, size, grid_color, 4.f);
	};


	const auto base_size = uint32_t(float(Window::width())/6.4f);
	const auto pad = base_size/30u;

	const glm::uvec2 front_rect { base_size, uint32_t(float(base_size)/aspect) };
	const glm::uvec2 side_rect { uint32_t(float(base_size)*1.8f), front_rect.y };
	const glm::uvec2 top_rect   { front_rect.x, side_rect.x };

	const glm::uvec2 front_top_left { pad, wh - pad - front_rect.y };
	const glm::uvec2 side_top_left  { pad + front_rect.x + pad, wh - pad - front_rect.y };
	const glm::uvec2 top_top_left   { pad, wh - pad - front_rect.y - pad - top_rect.y };

	draw_grid(front_top_left, front_rect, { m_cluster_resolution.x, m_cluster_resolution.y });
	draw_grid(side_top_left,  side_rect,  { m_cluster_resolution.z, m_cluster_resolution.y });
	draw_grid(top_top_left,   top_rect,   { m_cluster_resolution.x, m_cluster_resolution.z });

	const auto front_cell_size = glm::uvec2 { front_rect.x / m_cluster_resolution.x, front_rect.y / m_cluster_resolution.y };
	const auto side_cell_size  = glm::uvec2 { side_rect.x  / m_cluster_resolution.z, side_rect.y  / m_cluster_resolution.y };
	const auto top_cell_size   = glm::uvec2 { top_rect.x   / m_cluster_resolution.x, top_rect.y   / m_cluster_resolution.z };


	auto discovery_view = m_cluster_discovery_ssbo.view();
	const auto &discovery = *discovery_view;
	const auto num_nonempty = discovery[0]; // num_active

	static std::vector<uint32_t> nonempty_clusters;
	nonempty_clusters.reserve(num_nonempty);
	nonempty_clusters.clear();

	constexpr auto nonempty_offset = 1u; // skipn num_active

	for(auto idx = 0u; idx < m_cluster_count; ++idx)
	{
		if(discovery[idx + nonempty_offset] == 1)
			nonempty_clusters.push_back(idx);
	}

	// verify that the active clusters (second half of the nonempty_clusters array) contain no duplicates
	const auto active_offset = 1u + m_cluster_count;
	static dense_set<uint32_t> seen_nonempty;
	seen_nonempty.clear();
	for(auto idx = 0u; idx < nonempty_clusters.size(); ++idx)
	{
		auto cluster_index = discovery[idx + active_offset];
		assert(not seen_nonempty.contains(cluster_index));
		seen_nonempty.insert(cluster_index);
	}
	// verify that the active clusters collected are the same ones that were flagged as nonempty
	assert(seen_nonempty.size() == nonempty_clusters.size());
	for(const auto cluster_index: nonempty_clusters)
		assert(seen_nonempty.contains(cluster_index));

	std::sort(nonempty_clusters.begin(), nonempty_clusters.end());

	auto index2coord = [this](auto index) -> glm::uvec3 {
		return glm::uvec3 {
			index % m_cluster_resolution.x,
			index % (m_cluster_resolution.x * m_cluster_resolution.y) / m_cluster_resolution.x,
			index / (m_cluster_resolution.x * m_cluster_resolution.y)
		};
	};

	auto draw_cell = [this, cluster_color](glm::uvec2 top_left, glm::uvec2 coord, glm::uvec2 size) {
		top_left += coord*size + glm::uvec2{ 1, 1 };
		size -= glm::uvec2{ 2, 2 };
		debugDrawRect(top_left, size, cluster_color, 0.f);
	};

	auto c_lights_view = m_cluster_lights_range_ssbo.view();
	auto clusters_view = m_cluster_aabb_ssbo.view();
	// auto points_view = m_point_lights_ssbo.view();
	// auto points_idx_view = m_point_lights_index_ssbo.view();

	static uvec2_map<uint32_t> visited_front;
	visited_front.reserve(m_cluster_resolution.x * m_cluster_resolution.y);
	visited_front.clear();
	static uvec2_map<uint32_t> visited_side;
	visited_side.reserve(m_cluster_resolution.z * m_cluster_resolution.y);
	visited_side.clear();
	static uvec2_map<uint32_t> visited_top;
	visited_top.reserve(m_cluster_resolution.x * m_cluster_resolution.z);
	visited_top.clear();

	std::print("non-empty: {} - ", nonempty_clusters.size());

	struct ClusterLightRange
	{
		uint32_t cluster_Index;
		uint32_t start_index;
		uint32_t count;
	};
	static std::vector<ClusterLightRange> light_ranges;
	light_ranges.reserve(nonempty_clusters.size());
	light_ranges.clear();

	auto all_lights = m_all_lights_index_ssbo.view();
	const auto &all_light_index = *all_lights;
	constexpr auto index_offset = 1u; // skip all_lights_start_index

	for(const auto &index: nonempty_clusters)
	{
		const auto coord = index2coord(index);

			   // first uint is number of lights
		const auto start_index = (*c_lights_view)[index].start_index;
		const auto num_lights = (*c_lights_view)[index].count;

		const auto front_coord = glm::uvec2{ coord.x, m_cluster_resolution.y - 1 - coord.y };
		if(not visited_front.contains(front_coord))
			draw_cell(front_top_left, front_coord, front_cell_size);
		visited_front[front_coord] = std::max(visited_front[front_coord], num_lights);

		const auto side_coord = glm::uvec2{ coord.z, m_cluster_resolution.y - 1 - coord.y };
		if(not visited_side.contains(side_coord))
			draw_cell(side_top_left, side_coord, side_cell_size);
		visited_side[side_coord] = std::max(visited_side[side_coord], num_lights);

		const auto top_coord = glm::uvec2{ coord.x, m_cluster_resolution.z - 1 - coord.z };
		if(not visited_top.contains(top_coord))
			draw_cell(top_top_left, top_coord, top_cell_size);
		visited_top[top_coord] = std::max(visited_top[top_coord], num_lights);

		if(num_lights > 0)
		{
			std::print("  [{}]:", index);
			light_ranges.push_back({ index, start_index, num_lights });

			// std::print("{}+{}", start_index, num_lights);
			static small_vec<uint32_t> light_indices;
			light_indices.clear();
			for(auto idx = start_index; idx < start_index + num_lights; ++idx)
			{
				const auto light_index = all_light_index[index_offset + idx];
				light_indices.push_back(light_index);
				assert(light_index < _light_mgr.num_lights<PointLight>());
			}
			std::sort(light_indices.begin(), light_indices.end());
			auto first = true;
			for(auto light_index: light_indices)
			{
				if(not first)
					std::print(",");
				first = false;
				std::print("{}", light_index);
			}
		}
	}
	std::puts("");

	std::sort(light_ranges.begin(), light_ranges.end(), [](const ClusterLightRange &A, const ClusterLightRange &B) {
		return A.start_index < B.start_index;
	});

	uint32_t current = 0;
	for(const auto &range: light_ranges)
	{
		if(range.start_index > current)
			std::print("\x1b[33;1mGAP\x1b[m {} > {}\n", range.start_index, current);
		else if(range.start_index < current)
			std::print("\x1b[31;1mOVERLAP\x1b[m {} < {}\n", range.start_index, current);
		current = range.start_index + range.count;
	}


	auto draw_light_counts = [this, &text_color](auto top_left, auto cell_size, auto visited) {

		float text_size = float(cell_size.y) * 0.4f;
		float thickness = text_size/10.f;

		for(const auto &[coord, num_lights]: visited)
		{
			const auto bottom_right = top_left + cell_size + coord*cell_size - glm::uvec2{8, 8};
			debugDrawNumber(num_lights, bottom_right, text_size, text_color, thickness);
		}
	};

	draw_light_counts(front_top_left, front_cell_size, visited_front);
	draw_light_counts(side_top_left,  side_cell_size,  visited_side);
	draw_light_counts(top_top_left,   top_cell_size,   visited_top);
}
