#version 330 core
layout(location = 0) in vec2 aPos; // (lon, lat) in degrees
uniform mat4 uVP;
uniform float uRadius;
void main() {
    float lon = radians(aPos.x);
    float lat = radians(aPos.y);
    float cl = cos(lat);
    vec3 dir = vec3(cl * sin(lon), sin(lat), cl * cos(lon));
    // Slightly above the terrain radius to avoid z-fighting with the sphere.
    gl_Position = uVP * vec4(dir * (uRadius + 0.15), 1.0);
}
