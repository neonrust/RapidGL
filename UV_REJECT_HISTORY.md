# UV-Reject Code History in volumetrics_inject.comp

## Overview
This document shows what the UV-reject code looked like in the history blending section before it was removed.

## Commit Information
- **Commit Hash**: d7056f2382b2f527d960cb66504abaeccb6f5056
- **Author**: André Jonsson
- **Date**: Sun Oct 26 11:54:10 2025 +0100
- **Commit Message**: "Just rely on clamp-to-edge"
- **Description**: Instead of skipping blending for froxels that are outside the grid; blend with the nearest froxel.

## Code Before Removal (with UV-reject)

The UV-reject code was located in the history blending section of the `main()` function, around line 195:

```glsl
if(u_froxel_blend_previous)
{
	// same as above, but without noise
	vec3 world_pos = froxelWorldPos(froxel, u_froxel_zexp, u_inv_view_projection);

	// find the corresponding UV in the previous froxel grid
	vec3 prev_uv = worldToUV(world_pos, u_prev_view);

	// only mix with UVs that coincide with the current froxel grid
	if(all(greaterThanEqual(prev_uv, vec3(0))) && all(lessThanEqual(prev_uv, vec3(1))))
	{
		// read previous froxel
		vec3 prev_scattered = textureLod(u_prev_scatter, prev_uv, 0).rgb;
		total_scattered = mix(total_scattered, prev_scattered, u_froxel_blend_weight);
	}
	// else
	// 	total_scattered = vec3(1, 0, 1);
}
```

### Key UV-Reject Logic

The UV-reject check consisted of:
```glsl
if(all(greaterThanEqual(prev_uv, vec3(0))) && all(lessThanEqual(prev_uv, vec3(1))))
```

This condition verified that all three components (x, y, z) of `prev_uv` were within the valid [0, 1] range. Only when this condition was true would the code:
1. Sample the previous scatter texture at `prev_uv`
2. Blend the current and previous values using `u_froxel_blend_weight`

If the UV coordinates were outside this range (meaning the world position didn't map to a valid froxel in the previous grid), the blending step was **skipped entirely**, and the froxel would only use the newly calculated `total_scattered` value.

The commented-out `else` branch (`total_scattered = vec3(1, 0, 1)`) appears to have been a debug visualization that would have shown rejected UVs in magenta.

## Code After Removal (current)

```glsl
if(u_froxel_blend_previous)
{
	// same as above, but without noise
	vec3 world_pos = froxelWorldPos(froxel, u_froxel_zexp, u_inv_view_projection);

	// find the corresponding UV in the previous froxel grid
	vec3 prev_uv = worldToUV(world_pos, u_prev_view);
	// NOTE: relying on "camp to edge" here

	// read previous froxel
	vec3 prev_scattered = textureLod(u_prev_scatter, prev_uv, 0).rgb;
	total_scattered = mix(total_scattered, prev_scattered, u_froxel_blend_weight);
}
```

## Changes Made

The UV-reject check was **completely removed**. The new implementation:
1. Unconditionally samples from `u_prev_scatter` at `prev_uv`
2. Relies on OpenGL's "clamp-to-edge" texture sampling mode to handle out-of-range UVs
3. Always performs the blend operation when `u_froxel_blend_previous` is true

This simplification means that when a froxel's world position maps to UV coordinates outside [0, 1], instead of skipping the blend, the texture sampler will clamp to the nearest edge texel and blend with that value.

## Rationale

The commit message indicates the change was made to simplify the logic by relying on the texture sampler's built-in edge handling rather than manually rejecting out-of-bounds UVs.
