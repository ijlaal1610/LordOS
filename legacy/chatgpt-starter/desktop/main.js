// LordOS desktop mockup; no native window integration yet.
const windows = [...document.querySelectorAll('.window')];
let z = 5;
function focus(w) {w.style.zIndex = ++z;}
function clock(){document.querySelector('#clock').textContent=new Date().toLocaleTimeString('es-AR',{hour:'2-digit',minute:'2-digit'});}
clock();setInterval(clock,10000);
windows.forEach(w=>w.addEventListener('pointerdown',()=>focus(w)));
document.querySelectorAll('[data-open]').forEach(b=>b.addEventListener('click',()=>{const w=document.getElementById(b.dataset.open);w.style.display='flex';focus(w);}));
document.querySelectorAll('[data-close]').forEach(b=>b.addEventListener('click',()=>document.getElementById(b.dataset.close).style.display='none'));
document.querySelectorAll('[data-minimize]').forEach(b=>b.addEventListener('click',()=>document.getElementById(b.dataset.minimize).style.display='none'));
document.querySelectorAll('[data-maximize]').forEach(b=>b.addEventListener('click',()=>{const w=document.getElementById(b.dataset.maximize); if(w.dataset.max){w.style.cssText=w.dataset.max;delete w.dataset.max;}else{w.dataset.max=w.style.cssText;w.style.left='15px';w.style.top='15px';w.style.width='calc(100% - 30px)';w.style.height='calc(100% - 95px)';}focus(w); }));
document.querySelectorAll('[data-drag]').forEach(bar=>{
  bar.addEventListener('pointerdown',ev=>{
    if(ev.target.closest('button'))return;
    const w=document.getElementById(bar.dataset.drag),r=w.getBoundingClientRect(),d=document.querySelector('#desktop').getBoundingClientRect();
    let offsetX=ev.clientX-r.left,offsetY=ev.clientY-r.top;
    focus(w);bar.setPointerCapture(ev.pointerId);
    const move=e=>{w.style.left=Math.max(0,Math.min(d.width-w.offsetWidth,e.clientX-d.left-offsetX))+'px';w.style.top=Math.max(0,Math.min(d.height-w.offsetHeight,e.clientY-d.top-offsetY))+'px';};
    const end=()=>{bar.removeEventListener('pointermove',move);bar.removeEventListener('pointerup',end);bar.removeEventListener('pointercancel',end);};
    bar.addEventListener('pointermove',move);bar.addEventListener('pointerup',end);bar.addEventListener('pointercancel',end);
  });
});
document.querySelector('#about').addEventListener('click',()=>alert('LordOS Desktop Prototype — maqueta HTML/JS. Proximo hito: compositor Wayland real en Debian 13.'));
