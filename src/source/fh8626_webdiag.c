#include "fh8626_webdiag.h"

static const char page[] =
"<!doctype html>\n"
"<html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"<title>FH8626 diagnostics</title>\n"
"<style>\n"
":root{color-scheme:dark;background:#111;color:#e8e8e8;font-family:system-ui,sans-serif}body{margin:0;padding:18px;max-width:1100px}h1{margin:0 0 14px;font-size:24px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px}.card{background:#1b1b1b;border:1px solid #333;border-radius:10px;padding:14px}.label{color:#9da3aa;font-size:12px;text-transform:uppercase}.value{font-family:ui-monospace,monospace;margin-top:4px;word-break:break-all}.ok{color:#72d572}.warn{color:#f0c36b}.bad{color:#ff7373}.muted{color:#8a8f95}video{width:100%;max-height:62vh;background:#000;border-radius:8px;margin-top:10px}button,a.btn{background:#2d5bd1;color:white;border:0;border-radius:6px;padding:8px 11px;text-decoration:none;display:inline-block;cursor:pointer;margin:4px 6px 4px 0}button:disabled,a.btn.disabled{opacity:.45;cursor:not-allowed;pointer-events:none}pre{white-space:pre-wrap;word-break:break-word;font-size:12px;background:#0d0d0d;padding:10px;border-radius:7px}table{width:100%;border-collapse:collapse}td{padding:5px;border-bottom:1px solid #2e2e2e}td:first-child{color:#9da3aa;width:42%}.small{font-size:12px;color:#9da3aa}\n"
"</style></head><body>\n"
"<h1>FH8626V100 live diagnostics</h1>\n"
"<div class=\"grid\">\n"
"<div class=\"card\"><div class=\"label\">Source</div><div id=\"srcstate\" class=\"value muted\">loading</div><table><tr><td>Frames</td><td id=\"frames\">-</td></tr><tr><td>Forwarded</td><td id=\"forwarded\">-</td></tr><tr><td>Tiny frames</td><td id=\"tiny\">-</td></tr><tr><td>Pack errors</td><td id=\"errors\">-</td></tr><tr><td>Last bytes</td><td id=\"bytes\">-</td></tr><tr><td>Age ms</td><td id=\"age\">-</td></tr><tr><td>Generation</td><td id=\"generation\">-</td></tr></table></div>\n"
"<div class=\"card\"><div class=\"label\">Platform</div><div id=\"identity\" class=\"value\">loading</div><table id=\"providers\"></table></div>\n"
"<div class=\"card\"><div class=\"label\">Runtime</div><table><tr><td>Temperature</td><td id=\"temp\">-</td></tr><tr><td>Memory</td><td id=\"memory\">-</td></tr><tr><td>Uptime</td><td id=\"uptime\">-</td></tr><tr><td>Load</td><td id=\"load\">-</td></tr></table></div>\n"
"</div>\n"
"<div class=\"card\" style=\"margin-top:12px\"><div class=\"label\">Live</div><div id=\"liveinfo\" class=\"small\">loading capabilities</div><div><button id=\"start\" type=\"button\" disabled>Start browser preview</button><a id=\"raw\" class=\"btn disabled\" href=\"#\">Raw H.264</a><button id=\"copyrtsp\" type=\"button\" disabled>Copy RTSP URL</button></div><div id=\"rtsp\" class=\"value muted\">loading</div><video id=\"video\" controls muted playsinline></video><div class=\"small\">Browser preview uses the Divinus fMP4 endpoint when enabled. RTSP remains the preferred transport for external players.</div></div>\n"
"<div class=\"card\" style=\"margin-top:12px\"><div class=\"label\">Raw API state</div><pre id=\"rawjson\">loading</pre></div>\n"
"<script>\n"
"const $=id=>document.getElementById(id);let live=null;let previewStarted=false;let refreshBusy=false;\n"
"async function getj(p){const r=await fetch(p,{cache:'no-store'});if(!r.ok)throw new Error(p+' '+r.status);return r.json()}\n"
"function setState(s){const e=$('srcstate');e.textContent=s||'unknown';e.className='value '+(s==='streaming'?'ok':s==='waiting'?'warn':s==='stalled'?'bad':'muted')}\n"
"function providerTable(p){const t=$('providers');t.textContent='';Object.entries(p||{}).forEach(([k,v])=>{const r=t.insertRow();r.insertCell().textContent=k;r.insertCell().textContent=v})}\n"
"function stopPreview(){const v=$('video');previewStarted=false;v.pause();v.removeAttribute('src');v.load()}\n"
"function setRaw(raw){const a=$('raw');const ok=!!(raw&&raw.available&&raw.path);a.classList.toggle('disabled',!ok);a.href=ok?raw.path:'#'}\n"
"function setRtsp(rtsp){const b=$('copyrtsp');const e=$('rtsp');if(rtsp&&rtsp.available){const port=rtsp.port||554;let path=rtsp.path||'/';if(path[0]!=='/')path='/'+path;e.textContent='rtsp://'+location.hostname+':'+port+path;e.className='value';b.disabled=false}else{e.textContent='unavailable';e.className='value muted';b.disabled=true}}\n"
"function setFmp4(f){const ok=!!(f&&f.available&&f.path);$('start').disabled=!ok;if(!ok&&previewStarted)stopPreview();return ok}\n"
"async function copyText(text){if(!text||text==='unavailable')return false;try{if(navigator.clipboard&&window.isSecureContext){await navigator.clipboard.writeText(text);return true}}catch(e){}const ta=document.createElement('textarea');ta.value=text;ta.setAttribute('readonly','');ta.style.position='fixed';ta.style.opacity='0';document.body.appendChild(ta);ta.select();let ok=false;try{ok=document.execCommand('copy')}catch(e){}ta.remove();return ok}\n"
"async function startPreview(){const f=live&&live.http&&live.http.fmp4;if(!setFmp4(f)){$('liveinfo').textContent='fMP4 unavailable; use RTSP/raw H.264';return}const v=$('video');v.src=f.path;$('liveinfo').textContent='browser fMP4 preview requested';try{await v.play();previewStarted=true;$('liveinfo').textContent='browser fMP4 preview active'}catch(e){previewStarted=false;$('liveinfo').textContent='browser preview failed; use RTSP/raw H.264'}}\n"
"$('start').onclick=startPreview;$('copyrtsp').onclick=async()=>{const ok=await copyText($('rtsp').textContent);$('liveinfo').textContent=ok?'RTSP URL copied':'RTSP URL copy unavailable; select it manually'};\n"
"function applyLive(v){live=v;const rtsp=v&&v.rtsp;const http=v&&v.http;const f=http&&http.fmp4;setRtsp(rtsp);setRaw(http&&http.raw_h264);const fmp4ok=setFmp4(f);$('liveinfo').textContent=fmp4ok?(previewStarted?'browser fMP4 preview active':'fMP4 available; preview is manual'):'fMP4 unavailable; use RTSP/raw H.264'}\n"
"function liveUnavailable(){live=null;setRtsp(null);setRaw(null);setFmp4(null);$('liveinfo').textContent='live capability API unavailable'}\n"
"async function refresh(){if(refreshBusy)return;refreshBusy=true;try{const rs=await Promise.allSettled([getj('/api/status'),getj('/api/platform'),getj('/api/fh86'),getj('/api/live')]);const out={};if(rs[0].status==='fulfilled'){const s=rs[0].value;out.status=s;$('temp').textContent=(s.temp!==undefined&&s.temp!==null?s.temp:'unavailable');$('memory').textContent=(s.memory!==undefined?s.memory:'-');$('uptime').textContent=(s.uptime!==undefined?s.uptime:'-');$('load').textContent=Array.isArray(s.loadavg)?s.loadavg.join(' / '):'-'}if(rs[1].status==='fulfilled'){const p=rs[1].value;out.platform=p;$('identity').textContent=(p.chip||'?')+' / '+(p.sensor||'?');providerTable(p.providers)}if(rs[2].status==='fulfilled'){const f=rs[2].value;out.fh86=f;setState(f.state);$('frames').textContent=f.frames_received;$('forwarded').textContent=f.frames_forwarded;$('tiny').textContent=f.tiny_frames;$('errors').textContent=f.pack_errors;$('bytes').textContent=f.last_frame_bytes;$('age').textContent=(f.last_frame_age_ms!==null?f.last_frame_age_ms:'-');$('generation').textContent=f.generation+' / changes '+f.generation_changes}else setState('unavailable');if(rs[3].status==='fulfilled'){out.live=rs[3].value;applyLive(rs[3].value)}else liveUnavailable();$('rawjson').textContent=JSON.stringify(out,null,2)}finally{refreshBusy=false}}\n"
"refresh();setInterval(refresh,2000);\n"
"</script></body></html>\n";

const char *fh8626_webdiag_html(void)
{
    return page;
}
