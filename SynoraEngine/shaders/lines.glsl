#ifdef VERTEX_SRC
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aPointA;
layout(location = 2) in vec3 aPointB;
layout(location = 3) in vec4 aColorAndThickness;

#include "common.glsl"

uniform vec2 u_resolution;

out vec3 fragColor;

// Implementation from: https://wwwtyro.net/2019/11/18/instanced-lines.html
void main() {
  fragColor = aColorAndThickness.xyz;
  float width = aColorAndThickness.w;

  vec4 clip0 = u_ViewProjection * vec4(aPointA, 1.0);
  vec4 clip1 = u_ViewProjection * vec4(aPointB, 1.0);

  vec2 screen0 = u_resolution * (0.5 * clip0.xy / clip0.w + 0.5);
  vec2 screen1 = u_resolution * (0.5 * clip1.xy / clip1.w + 0.5);

  vec2 dir = screen1 - screen0;
  vec2 xBasis = dot(dir, dir) > 1e-12 ? normalize(dir) : vec2(1.0, 0.0);
  vec2 yBasis = vec2(-xBasis.y, xBasis.x);

  vec2 pt0 = screen0 + width * (aPos.x * xBasis + aPos.y * yBasis);
  vec2 pt1 = screen1 + width * (aPos.x * xBasis + aPos.y * yBasis);

  vec2 pt = mix(pt0, pt1, aPos.z);
  vec4 clip = mix(clip0, clip1, aPos.z);

  gl_Position = vec4(clip.w * ((2.0 * pt) / u_resolution - 1.0), clip.z, clip.w);
}
#endif
#ifdef FRAGMENT_SRC

out vec4 finalColor;

in vec3 fragColor;

void main() {
  finalColor = vec4(fragColor, 1.0f);
}

#endif
