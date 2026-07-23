#version 330 core
in float vElev;
in vec3 vNormal;
out vec4 fragColor;

// Hypsometric tint with fixed elevation breakpoints, identical to the region
// view's colormap so both screens read the same height the same way.
vec3 colormap(float h) {
    if (h < 0.0) {
        float d = -h; // depth, metres
        if (d < 200.0)  return mix(vec3(0.51, 0.74, 0.86), vec3(0.33, 0.62, 0.83), d / 200.0);
        if (d < 1000.0) return mix(vec3(0.33, 0.62, 0.83), vec3(0.18, 0.45, 0.76), (d - 200.0) / 800.0);
        if (d < 3000.0) return mix(vec3(0.18, 0.45, 0.76), vec3(0.09, 0.28, 0.58), (d - 1000.0) / 2000.0);
        return mix(vec3(0.09, 0.28, 0.58), vec3(0.03, 0.13, 0.38), clamp((d - 3000.0) / 3000.0, 0.0, 1.0));
    } else {
        if (h < 100.0)  return mix(vec3(0.27, 0.55, 0.27), vec3(0.45, 0.66, 0.32), h / 100.0);
        if (h < 300.0)  return mix(vec3(0.45, 0.66, 0.32), vec3(0.72, 0.73, 0.39), (h - 100.0) / 200.0);
        if (h < 600.0)  return mix(vec3(0.72, 0.73, 0.39), vec3(0.78, 0.66, 0.45), (h - 300.0) / 300.0);
        if (h < 1200.0) return mix(vec3(0.78, 0.66, 0.45), vec3(0.62, 0.48, 0.36), (h - 600.0) / 600.0);
        if (h < 2500.0) return mix(vec3(0.62, 0.48, 0.36), vec3(0.48, 0.40, 0.36), (h - 1200.0) / 1300.0);
        return mix(vec3(0.48, 0.40, 0.36), vec3(0.95, 0.95, 0.96), clamp((h - 2500.0) / 1500.0, 0.0, 1.0));
    }
}

void main() {
    // Simple fixed-direction Lambertian term so the sphere reads as a globe
    // (shaded terminator) instead of a flat-shaded disc; colours stay the
    // same hypsometric tint used by the region view.
    vec3 n = normalize(vNormal);
    vec3 lightDir = normalize(vec3(0.4, 0.6, 0.7));
    float diff = 0.55 + 0.45 * max(dot(n, lightDir), 0.0);
    fragColor = vec4(colormap(vElev) * diff, 1.0);
}
