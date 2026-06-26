#!/usr/bin/env python3
"""Analyze, auto-name, and build a web preview of the captured Dataflash programs.

Reads the sniffer captures (captures/sniff/prog-*.txt), extracts each program's
animation, classifies the motion, and writes:
  - captures/sniff/_catalog.json   (machine-readable, includes frame data)
  - captures/sniff/_catalog.md     (human table + ASCII filmstrips)
  - captures/sniff/preview.html    (self-contained web visualizer — just open it)

Frame model (from the live capture): each frame = `0x55 0x40` + 8 data bytes =
16 heads (2 per byte, 4-bit intensity each). Auto-names are a FIRST PASS — open
preview.html, watch each program, and rename via the UI (it exports the catalog).

Usage:
    python3 tools/pattern_catalog.py
    python3 tools/pattern_catalog.py --indir captures/sniff --heads 16 --maxframes 40
"""
import argparse, glob, json, os, re, sys
from collections import Counter

TOKEN = re.compile(r'([0-9A-Fa-f]{2})([Cd])')


def load_frames(path, nbytes):
    txt = open(path, encoding="utf-8", errors="replace").read()
    data = [int(v, 16) for v, t in TOKEN.findall(txt) if t == 'd']   # data plane only
    frames, i = [], 0
    while i < len(data) - (nbytes + 1):
        if data[i] == 0x55 and data[i + 1] == 0x40:
            heads = []
            for b in data[i + 2:i + 2 + nbytes]:
                heads += [(b >> 4) & 0xF, b & 0xF]
            frames.append(heads)
            i += 2 + nbytes
        else:
            i += 1
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
    # alternate: every frame splits into two interleaved values (e.g. 80 80 -> heads 8,0,8,0...)
    alt = all(len(set(fr[0::2])) == 1 and len(set(fr[1::2])) == 1 and fr[0] != fr[1] for fr in frames)
    if alt:
        return "alternate", meta
    if n_uni >= 0.6 * n:
        return ("wash-cycle" if len(uni_vals) > 2 else "wash-static"), meta
    if 0 < avg_active <= 3:
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
    # build = active count trends upward then snaps back
    if cnts and max(cnts) - min(cnts) >= 6 and cnts[-1] < cnts[len(cnts) // 2]:
        return "build", meta
    if avg_active >= 4:
        return "sparkle", meta
    return "complex", meta


def filmstrip(frames, rows=6):
    if not frames:
        return []
    step = max(1, len(frames) // rows)
    return ["".join("%X" % v for v in fr) for fr in frames[::step][:rows]]


PREVIEW_HTML = r"""<!doctype html><html><head><meta charset=utf-8><title>Dataflash strobe preview</title>
<style>
 body{font:14px system-ui,sans-serif;background:#0b0d12;color:#e6edf3;margin:0;padding:18px}
 h2{margin:0 0 12px} .lab{color:#8b949e;font-size:12px}
 select,button,input{font:14px system-ui;background:#161b22;color:#e6edf3;border:1px solid #30363d;border-radius:6px;padding:6px}
 #stage{display:flex;gap:8px;margin:20px 0;flex-wrap:wrap}
 .head{width:46px;height:46px;border-radius:8px;background:#000;box-shadow:0 0 0 1px #30363d inset;transition:background .02s}
 pre{background:#0d1117;border:1px solid #30363d;border-radius:6px;padding:8px;max-height:160px;overflow:auto}
</style></head><body>
<h2>Dataflash strobe preview</h2>
<div>program <select id=sel></select>
 <button id=play>⏸ pause</button>
 speed <input id=spd type=range min=1 max=40 value=10>
 <span id=info class=lab></span></div>
<div id=stage></div>
<div class=lab>name <input id=nm style="width:260px"> <button id=save>set name</button> <button id=exp>export catalog</button></div>
<pre id=out class=lab></pre>
<script>
const CAT = /*__DATA__*/[];
const sel=sel0(),stage=document.getElementById('stage'),info=document.getElementById('info'),nm=document.getElementById('nm');
function sel0(){return document.getElementById('sel');}
let heads=[],cur=0,fi=0,playing=true,timer=null;
CAT.forEach((c,i)=>{let o=document.createElement('option');o.value=i;o.textContent=`${c.program} — ${c.name}`;sel.appendChild(o);});
function buildHeads(n){stage.innerHTML='';heads=[];for(let i=0;i<n;i++){let d=document.createElement('div');d.className='head';stage.appendChild(d);heads.push(d);}}
function render(){const c=CAT[cur],fr=(c.frames_data||[])[fi%((c.frames_data||[1]).length)];if(!fr)return;
 fr.forEach((v,i)=>{const a=v/15;heads[i].style.background=`rgba(255,255,${(180+75*a)|0},${a})`;heads[i].style.boxShadow=a>0.05?`0 0 ${(16*a)|0}px rgba(255,255,190,${a})`:'0 0 0 1px #30363d inset';});
 info.textContent=`${c.kind} · frame ${fi%fr.length===0?'…':''}${(fi%(c.frames_data.length))+1}/${c.frames_data.length} · ${c.frames} captured`;}
function load(i){cur=i;fi=0;const c=CAT[i];buildHeads((c.frames_data&&c.frames_data[0]||new Array(16)).length);nm.value=c.name;render();}
function setSpeed(){clearInterval(timer);timer=setInterval(()=>{if(playing){fi++;render();}},1000/document.getElementById('spd').value);}
sel.onchange=()=>load(+sel.value);
document.getElementById('play').onclick=e=>{playing=!playing;e.target.textContent=playing?'⏸ pause':'▶ play';};
document.getElementById('spd').oninput=setSpeed;
document.getElementById('save').onclick=()=>{CAT[cur].name=nm.value;sel.options[cur].textContent=`${CAT[cur].program} — ${CAT[cur].name}`;};
document.getElementById('exp').onclick=()=>{document.getElementById('out').textContent=JSON.stringify(CAT.map(c=>({program:c.program,name:c.name,kind:c.kind})));};
load(0);setSpeed();
</script></body></html>"""


def main():
    ap = argparse.ArgumentParser(description="Auto-name + web-preview captured Dataflash programs")
    ap.add_argument("--indir", default="captures/sniff")
    ap.add_argument("--heads", type=int, default=16)
    ap.add_argument("--maxframes", type=int, default=40, help="frames per program embedded in preview")
    a = ap.parse_args()
    files = sorted(glob.glob(os.path.join(a.indir, "prog-*.txt")))
    if not files:
        sys.exit(f"no {a.indir}/prog-*.txt found — run tools/sniff_sampler.py first")
    nbytes = a.heads // 2

    catalog = []
    for f in files:
        label = os.path.basename(f)[5:-4]
        frames = load_frames(f, nbytes)
        kind, meta = classify(frames)
        catalog.append({"program": label, "name": kind, "kind": kind,
                        "frames": meta.get("frames", 0), "avg_active": meta.get("avg_active"),
                        "filmstrip": filmstrip(frames), "frames_data": frames[:a.maxframes]})

    json.dump(catalog, open(os.path.join(a.indir, "_catalog.json"), "w"), indent=1)
    md = os.path.join(a.indir, "_catalog.md")
    with open(md, "w") as m:
        m.write("# Dataflash program catalog (auto-generated)\n\n"
                "Frame = `55 40` + 8 data bytes = 16 heads (2/byte, 4-bit). Filmstrip chars are "
                "per-head hex intensity (0–F). Names are a first pass — refine in preview.html.\n\n"
                "| Prog | Name | Frames | AvgActive | Filmstrip (16 heads) |\n|---|---|---|---|---|\n")
        for c in catalog:
            m.write(f"| {c['program']} | {c['name']} | {c['frames']} | {c['avg_active']} "
                    f"| `{'`<br>`'.join(c['filmstrip']) or '-'}` |\n")
    html = PREVIEW_HTML.replace("/*__DATA__*/[]", json.dumps(catalog))
    open(os.path.join(a.indir, "preview.html"), "w").write(html)

    kinds = Counter(c["kind"] for c in catalog)
    print(f"cataloged {len(catalog)} programs -> {a.indir}/_catalog.md, _catalog.json, preview.html")
    print("kind distribution:", " ".join(f"{k}:{v}" for k, v in kinds.most_common()))
    print(f"\nopen the visualizer:  open {a.indir}/preview.html")


if __name__ == "__main__":
    main()
