#version 330 core
layout(location = 0) in vec2 aPos;   // (lon, lat) in degrees
layout(location = 1) in float aElev; // metres

uniform mat4 uVP;
uniform float uRadius;

out float vElev;
out vec3 vNormal;

void main() {
    vElev = aElev;
    float lon = radians(aPos.x);
    float lat = radians(aPos.y);
    float cl = cos(lat);
    // East (+lon) -> +X, north pole (+lat=90) -> +Y, (lon=0, lat=0) -> +Z, so
    // that with the camera's default azimuth=0 the sub-camera point is at
    // lon=0/lat=0 and east reads left-to-right, matching the old flat map.
    vec3 dir = vec3(cl * sin(lon), sin(lat), cl * cos(lon));
    vNormal = dir;
    gl_Position = uVP * vec4(dir * uRadius, 1.0);
}
