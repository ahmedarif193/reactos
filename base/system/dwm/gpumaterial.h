/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */

/* GPU counterpart of DwmBlitWindow's transient brush-color replacement.
 * GDI BGRX alpha is undefined unless the window explicitly requests it. */
static const char *const DwmGpuMaterialSource =
    "uniform sampler2D uWindow, uBackdrop;\n"
    "uniform vec2 uSize, uCaptureOrigin, uCaptureSize, uClientMin, uClientMax;\n"
    "uniform vec3 uBrush, uColorization, uKey;\n"
    "uniform float uOpacity, uAlpha, uRadius;\n"
    "uniform int uGlass, uWhole, uPixelAlpha, uUseKey;\n"
    "varying vec2 vTexCoord;\n"
    "float distanceRGB(vec3 a, vec3 b) {\n"
    "    vec3 d = abs(a-b); return max(d.r,max(d.g,d.b));\n"
    "}\n"
    "void main() {\n"
    "    vec4 s = texture2D(uWindow, vTexCoord);\n"
    "    if (uUseKey != 0 && distanceRGB(s.rgb,uKey) < 0.5/255.0) discard;\n"
    "    vec2 p = vTexCoord*uSize;\n"
    "    float coverage = 1.0;\n"
    "#ifndef DWM_MATERIAL_INTERIOR\n"
    "    if (uRadius > 0.0) {\n"
    "        vec2 q = abs(p-uSize*0.5) - (uSize*0.5-vec2(uRadius));\n"
    "        float d = length(max(q,vec2(0.0))) + min(max(q.x,q.y),0.0)-uRadius;\n"
    "        coverage = clamp(0.5-d,0.0,1.0);\n"
    "    }\n"
    "#endif\n"
    "    if (uGlass != 0 && (uWhole != 0 || p.x < uClientMin.x ||\n"
    "        p.y < uClientMin.y || p.x >= uClientMax.x || p.y >= uClientMax.y)) {\n"
    "        float a = distanceRGB(s.rgb,uBrush);\n"
    "        float b = distanceRGB(s.rgb,uColorization);\n"
    "        vec3 key = b < a ? uColorization : uBrush;\n"
    "        float weight = clamp((96.0-min(a,b)*255.0)/48.0,0.0,1.0);\n"
    "        vec3 base = texture2D(uBackdrop,\n"
    "            (gl_FragCoord.xy-uCaptureOrigin)/uCaptureSize).rgb;\n"
    "        s.rgb = clamp(s.rgb+(base-key)*(1.0-uOpacity)*weight,0.0,1.0);\n"
    "    }\n"
    "    gl_FragColor = vec4(s.rgb,uAlpha*coverage*(uPixelAlpha != 0 ? s.a : 1.0));\n"
    "}\n";
