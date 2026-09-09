/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */

/* Gaussian rectangle coverage; dynamic shadow pixels are evaluated on GPU. */
static const char *const DwmGpuShadowSource =
    "uniform vec2 uOwner, uSize, uSigma, uOpacity;\n"
    "uniform float uScreenHeight, uOffset, uWindowAlpha;\n"
    "float cdf(float x) {\n"
    "    float z=abs(x), t=1.0/(1.0+0.2316419*z);\n"
    "    float p=0.39894228*exp(-0.5*z*z)*t*(0.319381530+t*(-0.356563782+t*(1.781477937+t*(-1.821255978+t*1.330274429))));\n"
    "    return x<0.0 ? p : 1.0-p;\n"
    "}\n"
    "float coverage(float p, float n, float sigma) {\n"
    "    return clamp(cdf((p+0.5)/sigma)-cdf((p-n+0.5)/sigma),0.0,1.0);\n"
    "}\n"
    "void main() {\n"
    "    vec2 p=vec2(gl_FragCoord.x-0.5,uScreenHeight-gl_FragCoord.y-0.5)-uOwner;\n"
    "    float wx=floor(coverage(p.x,uSize.x,uSigma.x)*256.0+0.5);\n"
    "    float tx=floor(coverage(p.x,uSize.x,uSigma.y)*256.0+0.5);\n"
    "    float wy=floor(coverage(p.y-uOffset,uSize.y,uSigma.x)*uOpacity.x*uWindowAlpha/1000.0);\n"
    "    float ty=floor(coverage(p.y-uOffset,uSize.y,uSigma.y)*uOpacity.y*uWindowAlpha/1000.0);\n"
    "    float wa=floor(wx*wy/256.0), ta=floor(tx*ty/256.0);\n"
    "    float a=min(wa+ta-floor(wa*ta/256.0),255.0)/256.0;\n"
    "    gl_FragColor=vec4(0.0,0.0,0.0,a);\n"
    "}\n";
