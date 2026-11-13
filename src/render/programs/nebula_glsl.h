#pragma once

// Shared by native background shaders; preserve the approved procedural baseline.
inline constexpr char kNebulaGlsl[] = R"(
float hazeHash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float hazeNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    // Continuous second derivatives avoid curvature seams in the warped sheets.
    f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return mix(mix(hazeHash(i), hazeHash(i + vec2(1.0, 0.0)), f.x),
               mix(hazeHash(i + vec2(0.0, 1.0)), hazeHash(i + vec2(1.0)), f.x), f.y);
}

float hazeField(vec2 p) {
    return hazeNoise(p) * 0.57 + hazeNoise(p * 2.03 + 7.1) * 0.29
         + hazeNoise(p * 4.01 + 17.4) * 0.14;
}

vec4 nebulaBackground(vec2 hazeUv, vec2 hazeSize, float hazeTime, float hazeSeed, float backingAlpha, vec3 base) {
    // Isotropic coordinates preserve the formations on wide and tall panels.
    vec2 p = (hazeUv - 0.5) * vec2(hazeSize.x / hazeSize.y, 1.0) * 3.0;
    float t = hazeTime * 0.035;
    vec2 pattern = vec2(hazeSeed, hazeSeed * 0.6180339);
    vec2 drift = vec2(t * 0.16, -t * 0.09);
    vec2 warp = vec2(hazeField(p * 0.68 + drift + 3.7 + pattern),
                     hazeField(p * 0.81 - drift + 19.2 + pattern)) - 0.5;
    vec2 q = p + warp * 1.8;
    float density = hazeField(q * vec2(0.75, 1.2) + vec2(-t * 0.08, t * 0.13) + pattern);
    float detail = hazeNoise(q * 3.4 + vec2(t * 0.09, -t * 0.07) + pattern);
    float sheets = 0.0;
    float filaments = 0.0;
    for (int i = 0; i < 5; ++i) {
        float layer = float(i);
        float ridge = q.y + 0.38 * sin(q.x * (0.85 + layer * 0.18) + layer * 2.1 + t * 0.35 + hazeSeed * 0.2)
                    + 0.16 * sin(q.x * 2.7 - layer * 1.7 - t * 0.21)
                    - (layer - 2.0) * 0.22;
        float width = 0.06 + 0.12 * density;
        // A translucent sheet trails each fold. Vary its thickness and break up
        // the fine edge so this reads as smoke rather than a set of sine waves.
        float sheetDistance = ridge - width * 0.7;
        float veil = exp(-(sqrt(sheetDistance * sheetDistance + 0.0001) - 0.01) / (width * 1.6));
        sheets += veil * 0.19;
        float foldDistance = sqrt(ridge * ridge + 0.000016) - 0.004;
        // Share the original fold's energy with a faint, irregular secondary wisp.
        float fineRidge = ridge + (detail - 0.5) * 0.13 + 0.045;
        float fineDistance = sqrt(fineRidge * fineRidge + 0.000009) - 0.003;
        filaments += exp(-foldDistance / (0.013 + 0.012 * density)) * 0.125;
        filaments += exp(-fineDistance / (0.007 + 0.005 * density)) * 0.025;
    }
    float broken = smoothstep(0.22, 0.74, density);
    float envelope = exp(-p.y * p.y * 0.7);
    float haze = (sheets * 0.75 + filaments) * broken * envelope * 0.97;
    vec3 red = vec3(0.34, 0.025, 0.045);
    // Composite the haze over the dark backing independently. Lowering the
    // backing's opacity must not attenuate the crimson contribution or content.
    float hazeAlpha = clamp(haze * 0.65, 0.0, 1.0);
    float alpha = hazeAlpha + backingAlpha * (1.0 - hazeAlpha);
    // Fixed, neutral, triangular dither before 8-bit quantisation. Never animate
    // this noise: even the static mode must remain pixel-stable, without sparkle.
    float dither = (hazeHash(gl_FragCoord.xy) - hazeHash(gl_FragCoord.yx + 37.2)) / 255.0;
    vec3 premultiplied = (base + dither) * alpha + red * haze;
    return vec4(clamp(premultiplied, 0.0, alpha), alpha);
}

)";
