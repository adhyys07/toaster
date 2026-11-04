#version 120

// Single-pass CRT filter: barrel distortion, aperture-grille scanlines,
// a subtle RGB phosphor mask and vignetting.

varying vec2 v_texcoord0;

uniform sampler2D sampler0;
uniform vec2 u_sourceSize;   // emulator frame size in pixels
uniform vec2 u_outputSize;   // viewport size in pixels
uniform vec4 u_setting;      // x: curvature, y: scanline, z: mask, w: unused

vec2 curve(vec2 uv, float amount) {
    uv = uv * 2.0 - 1.0;
    vec2 offset = abs(uv.yx) / vec2(6.0, 5.0);
    uv += uv * offset * offset * amount;
    return uv * 0.5 + 0.5;
}

void main() {
    const float kCurvatureScale = 1.5;

    float curvature = clamp(u_setting.x, 0.0, 1.0) * kCurvatureScale;
    float scanline_strength = clamp(u_setting.y, 0.0, 1.0);
    float mask_strength = clamp(u_setting.z, 0.0, 1.0);

    vec2 uv = curve(v_texcoord0, curvature);

    // Outside the tube: black bezel rather than smeared edge texels.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 color = texture2D(sampler0, uv).rgb;

    // Scanlines follow the source scanline count, not the output resolution,
    // so the pattern stays stable as the window is resized.
    float scan_phase = uv.y * u_sourceSize.y * 3.14159265;
    float scan = 1.0 - scanline_strength * 0.5 * (1.0 - cos(2.0 * scan_phase));
    color *= scan;

    // Aperture-grille mask: tint successive output columns R, G, B.
    float column = floor(gl_FragCoord.x);
    vec3 mask = vec3(1.0);
    float phase = mod(column, 3.0);
    if (phase < 1.0)      mask = vec3(1.0, 0.7, 0.7);
    else if (phase < 2.0) mask = vec3(0.7, 1.0, 0.7);
    else                  mask = vec3(0.7, 0.7, 1.0);
    color *= mix(vec3(1.0), mask, mask_strength);

    // Gentle vignette towards the corners of the tube.
    vec2 vignette_uv = uv * (1.0 - uv.yx);
    float vignette = clamp(pow(vignette_uv.x * vignette_uv.y * 32.0, 0.25),
                           0.0, 1.0);
    color *= mix(1.0, vignette, 0.4);

    // Scanlines and the mask both lose energy; restore some brightness.
    color *= 1.0 + 0.25 * (scanline_strength + mask_strength);

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
