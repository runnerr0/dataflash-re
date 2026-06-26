#!/usr/bin/env python3
"""Analyze, auto-name, and build a web preview of the captured Dataflash programs.

Reads the sniffer captures (captures/sniff/prog-*.txt), extracts each program's
animation, classifies the motion, and writes:
  - captures/sniff/_catalog.json   (machine-readable, includes frame data)
  - captures/sniff/_catalog.md     (human table + ASCII filmstrips)
  - captures/sniff/preview.html    (self-contained web visualizer — just open it)

Frame model (from the live capture + the "8 fixtures linear" label on the gear):
each frame = `0x55 0x40` + N data bytes, **one byte per fixture** (default 8 heads,
8-bit value). The sequence of frames over the capture is the animation. Auto-names
are a FIRST PASS — open preview.html, watch each program, and rename via the UI.

Usage:
    python3 tools/pattern_catalog.py
    python3 tools/pattern_catalog.py --fixtures 8 --maxframes 160
"""
import argparse, glob, json, os, re, sys
from collections import Counter

TOKEN = re.compile(r'([0-9A-Fa-f]{2})([Cd])')


def load_frames(path, nheads):
    """Real frame = `55 40` (both 9th=0) + nheads fixture bytes, then a 00 + a run of
    heartbeats until the next header. We anchor on the header in the *tagged* token
    stream (robust to the sniffer's flaky 9th bit on heartbeats, which sprinkles
    stray 00d/00C through the gap) and take the 8 fixture bytes by position. Then we
    collapse consecutive identical frames so the animation steps through actual
    program STATES, not heartbeat-paced re-sends."""
    toks = TOKEN.findall(open(path, encoding="utf-8", errors="replace").read())
    hdr = [i for i in range(len(toks) - 1)
           if toks[i] == ('55', 'd') and toks[i + 1] == ('40', 'd')]
    frames = []
    for i in hdr:
        if i + 1 + nheads < len(toks):
            fr = [int(toks[i + 2 + j][0], 16) for j in range(nheads)]
            if not frames or frames[-1] != fr:        # collapse heartbeat re-sends
                frames.append(fr)
    return frames


def classify(frames):
    if len(frames) < 2:
        return "unknown", {"frames": len(frames)}
    n = len(frames)
    bg = Counter(v for fr in frames for v in fr).most_common(1)[0][0]
    uniform = [len(set(fr)) == 1 for fr in frames]
    n_uni = sum(uniform)
    uni_vals = {fr[0] for fr, u in zip(frames, uniform) if u}
    active = [tuple(j for j, v in enumerate(fr) if v != bg) for fr in frames]
    avg_active = sum(len(s) for s in active) / n
    meta = {"frames": n, "bg": bg, "uniform_frames": n_uni, "avg_active": round(avg_active, 2)}
    if all(len(set(fr[0::2])) == 1 and len(set(fr[1::2])) == 1 and fr[0] != fr[1] for fr in frames):
        return "alternate", meta
    if n_uni >= 0.6 * n:
        return ("wash-cycle" if len(uni_vals) > 2 else "wash-static"), meta
    if 0 < avg_active <= 2:                               # a single moving fixture
        cents = [sum(s) / len(s) for s in active if s]
        if len(cents) >= 4:
            d = [b - a for a, b in zip(cents, cents[1:])]
            up, dn = sum(x > 0 for x in d), sum(x < 0 for x in d)
            if up > 2 * max(1, dn): return "chase-fwd", meta
            if dn > 2 * max(1, up): return "chase-rev", meta
            if up + dn > 2 and abs(up - dn) <= 2: return "bounce", meta
        return "chase", meta
    if sum(fr == sorted(fr) or fr == sorted(fr, reverse=True) for fr in frames) >= 0.5 * n:
        return "ramp", meta
    cnts = [len(s) for s in active]
    if cnts and max(cnts) - min(cnts) >= n // 2 and cnts[-1] < cnts[len(cnts) // 2]:
        return "build", meta
    if avg_active >= 3:
        return "sparkle", meta
    return "complex", meta


def filmstrip(frames, rows=8):
    if not frames:
        return []
    step = max(1, len(frames) // rows)
    return [" ".join("%02X" % v for v in fr) for fr in frames[::step][:rows]]


PREVIEW_HTML = r"""<!doctype html><html><head><meta charset=utf-8><title>Dataflash strobe preview</title>
<style>
 body{font:14px system-ui,sans-serif;background:#0b0d12;color:#e6edf3;margin:0;padding:18px}
 h2{margin:0 0 12px} .lab{color:#8b949e;font-size:12px}
 select,button,input{font:14px system-ui;background:#161b22;color:#e6edf3;border:1px solid #30363d;border-radius:6px;padding:6px}
 #stage{display:flex;gap:12px;margin:22px 0}
 .col{text-align:center} .head{width:60px;height:60px;border-radius:10px;background:#000;box-shadow:0 0 0 1px #30363d inset;transition:background .015s}
 .hx{font:11px ui-monospace,monospace;color:#8b949e;margin-top:4px}
 pre{background:#0d1117;border:1px solid #30363d;border-radius:6px;padding:8px;max-height:160px;overflow:auto}
</style></head><body>
<h2>Dataflash strobe preview <span class=lab>(8 fixtures, 1 byte each)</span></h2>
<div>program <select id=sel></select>
 <button id=play>&#9208; pause</button>
 speed <input id=spd type=range min=1 max=40 value=10>
 <span id=info class=lab></span></div>
<div id=stage></div>
<div class=lab>name <input id=nm style="width:260px"> <button id=save>set name</button> <button id=exp>export catalog</button></div>
<pre id=out class=lab></pre>
<script>
const CAT = /*__DATA__*/[];
const sel=document.getElementById('sel'),stage=document.getElementById('stage'),info=document.getElementById('info'),nm=document.getElementById('nm');
let heads=[],hexs=[],cur=0,fi=0,playing=true,timer=null;
CAT.forEach((c,i)=>{let o=document.createElement('option');o.value=i;o.textContent=`${c.program} — ${c.name}`;sel.appendChild(o);});
function buildHeads(n){stage.innerHTML='';heads=[];hexs=[];for(let i=0;i<n;i++){let col=document.createElement('div');col.className='col';let d=document.createElement('div');d.className='head';let h=document.createElement('div');h.className='hx';h.textContent='00';col.appendChild(d);col.appendChild(h);stage.appendChild(col);heads.push(d);hexs.push(h);}}
function render(){const c=CAT[cur],fd=c.frames_data||[];if(!fd.length)return;const fr=fd[fi%fd.length];
 // 0x00 = off (dark). Nonzero = flashing in some mode/intensity; floor keeps even small
 // values visible, then scale by value so mode differences (E6 vs E0 vs 86) still read.
 fr.forEach((v,i)=>{const a=v===0?0:0.42+0.58*(v/255);heads[i].style.background=v===0?'#000':`rgba(255,255,${(165+90*(v/255))|0},${a})`;heads[i].style.boxShadow=v?`0 0 ${(24*a)|0}px rgba(255,255,190,${a})`:'0 0 0 1px #30363d inset';hexs[i].textContent=v.toString(16).toUpperCase().padStart(2,'0');});
 info.textContent=`${c.kind} · state ${(fi%fd.length)+1}/${fd.length}`;}
function load(i){cur=i;fi=0;const c=CAT[i];buildHeads((c.frames_data&&c.frames_data[0]||new Array(8)).length);nm.value=c.name;render();}
function setSpeed(){clearInterval(timer);timer=setInterval(()=>{if(playing){fi++;render();}},1000/document.getElementById('spd').value);}
sel.onchange=()=>load(+sel.value);
document.getElementById('play').onclick=e=>{playing=!playing;e.target.innerHTML=playing?'&#9208; pause':'&#9654; play';};
document.getElementById('spd').oninput=setSpeed;
document.getElementById('save').onclick=()=>{CAT[cur].name=nm.value;sel.options[cur].textContent=`${CAT[cur].program} — ${CAT[cur].name}`;};
document.getElementById('exp').onclick=()=>{document.getElementById('out').textContent=JSON.stringify(CAT.map(c=>({program:c.program,name:c.name,kind:c.kind})));};
load(0);setSpeed();
</script></body></html>"""


def main():
    ap = argparse.ArgumentParser(description="Auto-name + web-preview captured Dataflash programs")
    ap.add_argument("--indir", default="captures/sniff")
    ap.add_argument("--fixtures", type=int, default=8, help="heads = data bytes per frame (1 byte/fixture)")
    ap.add_argument("--maxframes", type=int, default=1000, help="distinct states per program embedded in preview")
    a = ap.parse_args()
    files = sorted(glob.glob(os.path.join(a.indir, "prog-*.txt")))
    if not files:
        sys.exit(f"no {a.indir}/prog-*.txt found — run tools/sniff_sampler.py first")

    catalog = []
    for f in files:
        label = os.path.basename(f)[5:-4]
        frames = load_frames(f, a.fixtures)
        kind, meta = classify(frames)
        catalog.append({"program": label, "name": kind, "kind": kind,
                        "frames": meta.get("frames", 0), "avg_active": meta.get("avg_active"),
                        "filmstrip": filmstrip(frames), "frames_data": frames[:a.maxframes]})

    json.dump(catalog, open(os.path.join(a.indir, "_catalog.json"), "w"), indent=1)
    md = os.path.join(a.indir, "_catalog.md")
    with open(md, "w") as m:
        m.write("# Dataflash program catalog (auto-generated)\n\n"
                f"Frame = `55 40` + {a.fixtures} data bytes = {a.fixtures} fixtures (1 byte each, 8-bit). "
                "Filmstrip rows are sampled frames (per-fixture hex). Names are a first pass.\n\n"
                "| Prog | Name | Frames | AvgActive | Filmstrip (8 fixtures) |\n|---|---|---|---|---|\n")
        for c in catalog:
            m.write(f"| {c['program']} | {c['name']} | {c['frames']} | {c['avg_active']} "
                    f"| `{'`<br>`'.join(c['filmstrip']) or '-'}` |\n")
    open(os.path.join(a.indir, "preview.html"), "w").write(
        PREVIEW_HTML.replace("/*__DATA__*/[]", json.dumps(catalog)))

    kinds = Counter(c["kind"] for c in catalog)
    print(f"cataloged {len(catalog)} programs ({a.fixtures} fixtures, up to {a.maxframes} frames) "
          f"-> {a.indir}/_catalog.md, _catalog.json, preview.html")
    print("kind distribution:", " ".join(f"{k}:{v}" for k, v in kinds.most_common()))
    print(f"open the visualizer:  open {a.indir}/preview.html")


if __name__ == "__main__":
    main()
