#!/usr/bin/env python3
"""Chromium frame timing on Canvas, through DevTools on 127.0.0.1:9222.

    cperf.py gpu                 chrome://gpu's feature status and GL strings
    cperf.py run SCENARIO SECS   navigate to the scenario's page, then rAF intervals for SECS seconds as one JSON line
"""
import json
import os
import sys
import time

PORT = int(os.environ.get("CPERF_PORT", "9222"))

sys.path.insert(0, "/tmp/cperf")
from wayland_chromium import Cdp  # noqa: E402

MEASURE = r"""
new Promise(done => {
  const t = [];
  const until = performance.now() + %d;
  function step(now) { t.push(now); if (now < until) requestAnimationFrame(step); else finish(); }
  function finish() {
    const d = [];
    for (let i = 1; i < t.length; i++) d.push(t[i] - t[i-1]);
    d.sort((a, b) => a - b);
    const q = p => d.length ? d[Math.min(d.length - 1, Math.floor(p * d.length))] : 0;
    const p50 = q(0.5);
    done({frames: d.length, fps: d.length / ((t[t.length-1] - t[0]) / 1000), p50: p50, p90: q(0.9), p99: q(0.99),
          max: d.length ? d[d.length-1] : 0, over2x: d.filter(x => x > 2 * p50).length,
          dpr: devicePixelRatio, w: innerWidth, h: innerHeight});
  }
  requestAnimationFrame(step);
})
"""

PAGES = {
    "idle": "<body style='margin:0;background:#fff'></body>",
    "scroll": "<body style='margin:0;font:16px sans-serif'><div id=c></div><script>"
              "let h='';for(let i=0;i<4000;i++)h+='<p>Paragraph '+i+' lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor.</p>';"
              "c.innerHTML=h;let y=0;function s(){y=(y+12)%(document.body.scrollHeight-innerHeight);scrollTo(0,y);requestAnimationFrame(s)}requestAnimationFrame(s)</script></body>",
    "canvas2d": "<body style='margin:0'><canvas id=c width=1280 height=720></canvas><script>"
                "const x=c.getContext('2d');let f=0;function d(){f++;x.fillStyle='#fff';x.fillRect(0,0,1280,720);"
                "for(let i=0;i<300;i++){x.fillStyle='hsl('+((i*7+f)%360)+',70%,50%)';x.fillRect((i*37+f*3)%1280,(i*53+f*2)%720,40,40)}"
                "requestAnimationFrame(d)}requestAnimationFrame(d)</script></body>",
    "css": "<body style='margin:0;overflow:hidden'><style>.b{position:absolute;width:24px;height:24px;background:#3a8;border-radius:4px;"
           "animation:m 2s linear infinite alternate}@keyframes m{to{transform:translate(400px,200px) rotate(360deg)}}</style><script>"
           "for(let i=0;i<1500;i++){const e=document.createElement('div');e.className='b';e.style.left=(i*13%1800)+'px';"
           "e.style.top=(i*29%900)+'px';e.style.animationDelay=(-(i%40)/20)+'s';document.body.appendChild(e)}</script></body>",
    "webgl": "<body style='margin:0;overflow:hidden'><canvas id=c></canvas><script>"
             "c.width=innerWidth;c.height=innerHeight;const g=c.getContext('webgl2');"
             "if(!g){document.title='nowebgl'}else{"
             "const vs=`#version 300 es\nin vec2 p;void main(){gl_Position=vec4(p,0,1);}`;"
             "const fs=`#version 300 es\nprecision highp float;uniform float t;uniform vec2 r;out vec4 o;void main(){vec2 u=gl_FragCoord.xy/r;"
             "float v=0.;for(int i=0;i<24;i++){v+=sin(u.x*float(i)*3.+t)*cos(u.y*float(i)*2.-t);}o=vec4(.5+.5*sin(v),.5+.5*cos(v*.7),.5,1);}`;"
             "function sh(k,s){const o=g.createShader(k);g.shaderSource(o,s);g.compileShader(o);return o}"
             "const pr=g.createProgram();g.attachShader(pr,sh(g.VERTEX_SHADER,vs));g.attachShader(pr,sh(g.FRAGMENT_SHADER,fs));g.linkProgram(pr);g.useProgram(pr);"
             "const b=g.createBuffer();g.bindBuffer(g.ARRAY_BUFFER,b);g.bufferData(g.ARRAY_BUFFER,new Float32Array([-1,-1,3,-1,-1,3]),g.STATIC_DRAW);"
             "g.enableVertexAttribArray(0);g.vertexAttribPointer(0,2,g.FLOAT,false,0,0);const ut=g.getUniformLocation(pr,'t'),ur=g.getUniformLocation(pr,'r');"
             "function d(n){g.viewport(0,0,c.width,c.height);g.uniform1f(ut,n/1000);g.uniform2f(ur,c.width,c.height);g.drawArrays(g.TRIANGLES,0,3);requestAnimationFrame(d)}"
             "requestAnimationFrame(d)}</script></body>",
}


class BrowserCdp(Cdp):
    """The browser target's websocket (SystemInfo is only there)."""

    def __init__(self, port):
        import base64, os, socket, urllib.request
        url = json.load(urllib.request.urlopen("http://127.0.0.1:%d/json/version" % port, timeout=5))["webSocketDebuggerUrl"]
        host, _, path = url[len("ws://"):].partition("/")
        address, _, number = host.partition(":")
        self.sock = socket.create_connection((address, int(number)), timeout=30)
        self.sock.sendall(("GET /%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n"
                           % (path, host, base64.b64encode(os.urandom(16)).decode())).encode())
        seen = b""
        while b"\r\n\r\n" not in seen:
            seen += self.sock.recv(1)
        self.next = 1
        self.pending = b""


def main():
    if sys.argv[1] == "gpu":
        cdp = BrowserCdp(PORT)
        info = cdp.call("SystemInfo.getInfo", timeout=30)
        gpu = info.get("result", {}).get("gpu", {})
        print(json.dumps({"featureStatus": gpu.get("featureStatus"),
                          "devices": gpu.get("devices"),
                          "driverBugWorkarounds": gpu.get("driverBugWorkarounds"),
                          "aux": {k: v for k, v in (gpu.get("auxAttributes") or {}).items()
                                  if k in ("glRenderer", "glVersion", "glImplementationParts", "displayType",
                                           "gpuCompositingDisabled", "rasterScheme", "skiaBackendType",
                                           "visibilityCallbackCalled", "softwareRendering", "passthroughCmdDecoder",
                                           "glWsVendor", "glWsVersion", "eglExtensions")},
                          "error": info.get("error")}, indent=1))
        return
    cdp = Cdp(PORT)
    cdp.call("Page.enable")
    name, secs = sys.argv[2], int(sys.argv[3])
    import urllib.parse
    url = "data:text/html," + urllib.parse.quote(PAGES[name])
    cdp.call("Page.navigate", {"url": url})
    time.sleep(2)
    result = cdp.eval(MEASURE % (secs * 1000))
    title = cdp.eval("document.title")
    if isinstance(result, dict):
        result["scenario"] = name
        result["title"] = title
    print(json.dumps(result))


if __name__ == "__main__":
    main()
