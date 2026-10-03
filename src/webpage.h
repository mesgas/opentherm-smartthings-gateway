#pragma once
#include <pgmspace.h>

// Statistics page served at "/". Plain HTML + JavaScript, no external libraries.
// It fetches /api/stats and /api/history?step=5 and draws the charts on <canvas> elements.
// Texts are Italian or English depending on the browser language.
static const char STATS_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenTherm</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;padding:12px;background:#f4f5f7;color:#222}
h1{font-size:18px;margin:0 0 10px}h2{font-size:14px;margin:14px 0 4px}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px}
.card{background:#fff;border-radius:8px;padding:8px 10px;box-shadow:0 1px 2px #0002}
.card b{display:block;font-size:20px}.card span{font-size:12px;color:#666}
canvas{width:100%;height:190px;background:#fff;border-radius:8px;box-shadow:0 1px 2px #0002}
.leg{font-size:12px;margin:2px 0}.leg i{display:inline-block;width:10px;height:10px;margin:0 3px 0 8px;border-radius:2px}
small{color:#888}
</style></head><body>
<h1 id="ti">OpenTherm</h1>
<div class="cards" id="cards"></div>
<h2 id="h1"></h2><canvas id="c1"></canvas><div class="leg" id="l1"></div>
<h2 id="h2"></h2><canvas id="c2"></canvas><div class="leg" id="l2"></div>
<h2 id="h3"></h2><canvas id="c3"></canvas><div class="leg" id="l3"></div>
<p><small id="ft"></small></p>
<script>
const IT=(navigator.language||'').startsWith('it');
const T=IT?{ti:'Statistiche caldaia (ultime 24 ore)',h1:'Temperature (°C)',h2:'Modulazione e fiamma',h3:'Riscaldamento e acqua calda',
 flame:'Fiamma accesa',heat:'Riscaldamento attivo',dhw:'Acqua calda attiva',starts:'Accensioni',avgmod:'Modulazione media',cond:'In condensazione',
 room:'Casa',out:'Esterna',flow:'Mandata',ret:'Ritorno',req:'Mandata richiesta',mod:'Modulazione %',fd:'Fiamma (% del tempo)',ch:'Riscaldamento (% del tempo)',dw:'Acqua calda (% del tempo)',
 tot:'Ore fiamma totali',data:'dati disponibili',min:'min',h:'h',ago:'ore fa',now:'ora',upd:'Aggiornato ogni minuto. Lo storico si azzera se l\'ESP si riavvia.',nodata:'Nessun dato'}
:{ti:'Boiler statistics (last 24 hours)',h1:'Temperatures (°C)',h2:'Modulation and flame',h3:'Heating and hot water',
 flame:'Flame on',heat:'Heating active',dhw:'Hot water active',starts:'Burner starts',avgmod:'Average modulation',cond:'Condensing',
 room:'House',out:'Outdoor',flow:'Flow',ret:'Return',req:'Requested flow',mod:'Modulation %',fd:'Flame (% of time)',ch:'Heating (% of time)',dw:'Hot water (% of time)',
 tot:'Total flame hours',data:'data available',min:'min',h:'h',ago:'h ago',now:'now',upd:'Refreshed every minute. History is cleared when the ESP restarts.',nodata:'No data'};
const $=id=>document.getElementById(id);
const fmtMin=m=>m==null?'-':(m>=120?(m/60).toFixed(1)+' '+T.h:Math.round(m)+' '+T.min);
function card(v,l){return '<div class="card"><b>'+v+'</b><span>'+l+'</span></div>'}
function draw(cv,leg,series,opt){
 const dpr=window.devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;cv.width=w*dpr;cv.height=h*dpr;
 const g=cv.getContext('2d');g.scale(dpr,dpr);g.font='11px system-ui';
 const n=series[0].v.length;let lo=Infinity,hi=-Infinity;
 for(const s of series)for(const x of s.v)if(x!=null){if(x<lo)lo=x;if(x>hi)hi=x}
 if(opt.min!=null)lo=opt.min;if(opt.max!=null)hi=opt.max;
 if(!isFinite(lo)){g.fillText(T.nodata,10,20);return}
 if(hi-lo<2){hi+=1;lo-=1}
 const L=34,B=18,R=6,Tp=6,pw=w-L-R,ph=h-B-Tp,X=i=>L+pw*i/Math.max(1,n-1),Y=v=>Tp+ph*(1-(v-lo)/(hi-lo));
 g.strokeStyle='#ddd';g.fillStyle='#888';g.lineWidth=1;
 for(let k=0;k<=4;k++){const v=lo+(hi-lo)*k/4,y=Y(v);g.beginPath();g.moveTo(L,y);g.lineTo(w-R,y);g.stroke();g.fillText(v.toFixed(hi-lo>20?0:1),2,y+4)}
 const hrs=n*opt.step/60;
 for(let k=0;k<=4;k++){const x=L+pw*k/4,hh=hrs*(1-k/4);g.fillText(hh<0.05?T.now:'-'+hh.toFixed(0)+T.h,Math.min(x,w-30),h-4)}
 for(const s of series){
  g.strokeStyle=s.c;g.fillStyle=s.c;g.lineWidth=1.6;
  if(s.bar){const bw=Math.max(1,pw/n-0.5);s.v.forEach((x,i)=>{if(x!=null){const y=Y(x);g.fillRect(X(i)-bw/2,y,bw,Y(lo)-y)}});continue}
  g.setLineDash(s.dash?[4,3]:[]);g.beginPath();let pen=false;
  s.v.forEach((x,i)=>{if(x==null){pen=false;return}if(!pen){g.moveTo(X(i),Y(x));pen=true}else g.lineTo(X(i),Y(x))});
  g.stroke();g.setLineDash([]);
 }
 leg.innerHTML=series.map(s=>'<i style="background:'+s.c+'"></i>'+s.n).join('');
}
async function load(){
 try{
  const st=await (await fetch('/api/stats')).json(),hs=await (await fetch('/api/history?step=5')).json();
  $('ti').textContent=T.ti;$('h1').textContent=T.h1;$('h2').textContent=T.h2;$('h3').textContent=T.h3;
  $('cards').innerHTML=card(fmtMin(st.flameMinutes),T.flame)+card(fmtMin(st.heatingMinutes),T.heat)+card(fmtMin(st.hotWaterMinutes),T.dhw)
   +card(st.burnerStarts,T.starts)+card(st.avgModulation==null?'-':st.avgModulation.toFixed(0)+' %',T.avgmod)
   +card(st.condensingPercent==null?'-':st.condensingPercent.toFixed(0)+' %',T.cond)
   +card(st.totalFlameHours.toFixed(1)+' '+T.h,T.tot)+card(fmtMin(st.windowMinutes),T.data);
  const pct=a=>a.map(x=>x==null?null:Math.min(100,x/(60*hs.step)*100));
  draw($('c1'),$('l1'),[{n:T.room,c:'#e67e22',v:hs.room},{n:T.out,c:'#3498db',v:hs.out},{n:T.flow,c:'#c0392b',v:hs.flow},{n:T.ret,c:'#8e44ad',v:hs.ret},{n:T.req,c:'#999',v:hs.req,dash:1}],{step:hs.step});
  draw($('c2'),$('l2'),[{n:T.fd,c:'#f5b7b1',v:pct(hs.flame),bar:1},{n:T.mod,c:'#c0392b',v:hs.mod}],{step:hs.step,min:0,max:100});
  draw($('c3'),$('l3'),[{n:T.ch,c:'#e67e22',v:pct(hs.ch),bar:1},{n:T.dw,c:'#2e86c1',v:pct(hs.dhw)}],{step:hs.step,min:0,max:100});
  $('ft').textContent=T.upd;
 }catch(e){$('ti').textContent='...'}
}
load();setInterval(load,60000);
</script></body></html>)HTML";
