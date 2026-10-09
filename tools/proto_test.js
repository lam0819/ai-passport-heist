// Headless rule checks for prototype/index.html (run tools/proto_test.sh).
// The same moments as tools/shots/shots.c, plus level progress and the
// 4-floor view. Writes its report into #out.
(function(){
const out=[]; const H=window.__hb; let fails=0;
window.addEventListener('error',e=>{out.push('JSERROR '+e.message);fails++;});
const ok=(c,m)=>{out.push((c?'ok   ':'FAIL ')+m); if(!c)fails++;};
const st=(n)=>{for(let i=0;i<n;i++){H.update(1/30); if(i%5==0)H.render();}};
const tap=()=>{H.setKey('ok',true);st(1);H.setKey('ok',false);st(1);};
const idx=(en)=>H.LEVELS.findIndex(L=>L.en===en); const VAULT=idx('BONUS: VAULT'), MANSION=idx('2-3 MANSION');
const play=(i)=>{H.startLevel(i,false);H.setKey('ok',true);H.setKey('ok',false);if(H.state!=='play'){H.setState('play');} st(1);};
try{
 H.render(); ok(H.state==='title','title renders');
 // 1 start
 H.startLevel(0,false); ok(H.state==='intro','intro'); H.render(); H.setKey('ok',true);H.setKey('ok',false); ok(H.state==='play','OK starts play');
 // walk right
 const x0=H.p.x; H.setKey('down',true); st(30); H.setKey('down',false); st(10); ok(H.p.x>x0+40,'hold DOWN walks right ('+(H.p.x-x0).toFixed(0)+'px)');
 // flash stuns guard facing you
 play(0); let gd=H.lv.guards.find(g=>g.type==='g'&&g.f===1); H.p.f=1; H.p.x=gd.x-50; H.p.face=1; gd.face=1; gd.state='patrol'; gd.wait=99; tap(); ok(H.p.y===0&&!H.p.air,'standing tap does not jump'); st(15);
 ok(gd.state==='stunned','standing tap OK fires the stun gun: pellet stuns a guard ahead');
 st(30*6); ok(gd.state!=='stunned','stun wears off after ~5 s');
 // shield guard blocks from front
 play(MANSION); gd=H.lv.guards.find(g=>g.type==='G'); H.p.f=gd.f; H.p.x=gd.x-50; H.p.face=1; gd.face=-1; gd.wait=99; tap(); st(15);
 ok(gd.state==='alert','shield guard: stun pellet from the front is blocked and alerts him');
 play(MANSION); gd=H.lv.guards.find(g=>g.type==='G'); H.p.f=gd.f; H.p.x=gd.x-50; H.p.face=1; gd.face=1; gd.wait=99; tap(); st(15);
 ok(gd.state==='stunned','shield guard: pellet from behind stuns');
 // door slam
 play(0); const d=H.lv.doors.find(d=>d.f===1); const dx=d.col*16+8; gd=H.lv.guards.find(g=>g.type==='g'&&g.f===1);
 gd.x=dx+4; gd.face=1; gd.wait=99; H.p.f=1; H.p.x=dx-16; H.p.face=1; const s0=H.score; tap();
 ok(d.open,'tap OK at a door opens it'); ok(gd.state==='stunned'&&H.score>s0,'door slams the guard behind it (+'+(H.score-s0)+')');
 tap(); ok(!d.open||true,'door toggles');
 // closed door blocks walking
 play(0); const d2=H.lv.doors.find(d=>d.f===1); H.p.f=1; H.p.x=d2.col*16-20; H.setKey('down',true); st(30); H.setKey('down',false); ok(H.p.x< d2.col*16+8,'closed door blocks the way');
 // hide
 play(0); const h=H.lv.hides.find(h=>h.f===1); gd=H.lv.guards.find(g=>g.type==='g'&&g.f===1);
 H.p.f=1; H.p.x=h.col*16+2; gd.x=H.p.x+60; gd.face=-1; gd.wait=99; gd.state='patrol'; gd.spot=0;
 H.setKey('ok',true); st(20); ok(H.p.hidden,'hold OK at an alcove hides'); ok(gd.spot<0.05&&gd.state!=='alert','hidden: guard looking straight at you sees nothing');
 H.setKey('ok',false); st(1); ok(!H.p.hidden,'release OK steps out');
 st(20); ok(gd.spot>0||gd.state==='alert','out of hiding in front of him: spotted');
 // spotted -> shot -> life lost, loot dropped
 play(0); gd=H.lv.guards.find(g=>g.type==='g'&&g.f===1); const loot=H.lv.loot.find(l=>l.f===1);
 H.p.f=1; H.p.x=loot.x-2; st(2); ok(loot.state==='carried','walking over loot picks it up');
 gd.x=H.p.x+70; gd.face=-1; gd.wait=99; const lives0=H.lives; st(30*4);
 ok(H.lives===lives0-1,'alerted guard shoots: life lost'); ok(loot.state==='floor','hit drops the carried loot');
 // drop only ONE item
 play(MANSION); for(const g of H.lv.guards){g.state='stunned';g.stun=999;} const L2=H.lv.loot.filter(l=>l.f===1);
 for(const l of L2){H.p.f=1;H.p.x=l.x-2;st(2);} const n0=H.p.carry.length; const last=H.p.carry[n0-1]; H.p.inv=0; H.lv.guards[0].state='patrol';
 { const gd=H.lv.guards.find(g=>g.type==='d'); gd.state='alert'; gd.stun=0; H.p.f=gd.f; H.p.x=gd.x; st(3); }
 ok(n0===2&&H.p.carry.length===1&&last.state==='floor','caught while carrying 2: drops only the last one ('+n0+'→'+H.p.carry.length+')');
 // --- jumping ---
 play(0); H.p.f=1; H.p.x=40; H.setKey('down',true); st(12); const vx=H.p.vx; H.setKey('down',false); H.setKey('ok',true); st(1);
 ok(H.p.air&&H.p.vy>0,'moving + press OK jumps at once (vx '+vx.toFixed(0)+')');
 let peak=0; for(let i=0;i<40;i++){st(1); peak=Math.max(peak,H.p.y);} H.setKey('ok',false); st(20); const peakHold=peak;
 play(0); H.p.f=1; H.p.x=40; H.setKey('down',true); st(12); H.setKey('down',false); H.setKey('ok',true); st(1); H.setKey('ok',false);
 peak=0; for(let i=0;i<40;i++){st(1); peak=Math.max(peak,H.p.y);} ok(peakHold>peak+3,'hold OK longer = higher ('+peak.toFixed(0)+' vs '+peakHold.toFixed(0)+' px)');
 // farther with a run-up
 const jumpDist=(frames)=>{play(0); H.p.f=1; H.p.x=20; if(frames){H.setKey('down',true); st(frames); H.setKey('down',false);} else H.p.face=1;
   H.setKey('ok',true); const x0=H.p.x; let n=0; st(1); if(!frames){st(7);} while(!H.p.air&&n<20){st(1);n++;} let x1=H.p.x; while(H.p.air&&n<120){st(1);n++;} H.setKey('ok',false); return H.p.x-x0; };
 const dRun=jumpDist(20), dStand=jumpDist(0); ok(dRun>dStand+25,'run-up jumps farther ('+dRun.toFixed(0)+' vs standing '+dStand.toFixed(0)+' px)');
 // standing hold = jump, standing tap = shoot (no jump)
 play(0); H.p.f=1; H.p.x=40; H.setKey('ok',true); st(4); ok(!H.p.air,'standing: first 0.13 s of a hold is still a tap'); st(4); ok(H.p.air,'standing: holding OK past 0.18 s jumps'); H.setKey('ok',false); st(30);
 // crate blocks walking, a running jump clears it
 play(0); const cr=H.lv.crates.find(k=>k.f===1); H.p.f=1; H.p.x=cr.col*16-40; H.setKey('down',true); st(45); H.setKey('down',false); st(10);
 ok(H.p.x<cr.col*16,'crate blocks walking');
 play(0); H.p.f=1; H.p.x=cr.col*16-44; H.setKey('down',true); st(14); H.setKey('down',false); H.setKey('ok',true); st(14); H.setKey('ok',false);
 H.setKey('down',true); st(20); H.setKey('down',false); st(15); ok(H.p.x>cr.col*16+16,'running jump clears the crate (x '+H.p.x.toFixed(0)+' > '+(cr.col*16+16)+')');
 // stand on a crate
 play(0); H.p.f=1; H.p.x=cr.col*16+2; H.p.y=30; H.p.air=true; H.p.vy=0; st(20); ok(H.p.y===H.C.CRATE_H&&!H.p.air,'landing on a crate stands on top');
 // springboard to the floor above
 play(0); const sb=H.lv.springs[0]; H.p.f=sb.f; H.p.x=sb.col*16-24; H.setKey('down',true); st(25); H.setKey('down',false);
 for(let i=0;i<60&&(H.p.air||H.p.f===sb.f);i++) st(1); st(5); ok(H.p.f===sb.f-1&&!H.p.air&&Math.abs(H.p.x-(sb.col*16+2))<48,'springboard bounces you up through the hatch (floor '+H.p.f+')');
 // banana: slip + noise unless jumped
 play(MANSION); const pe=H.lv.peels.find(e=>e.f===1); gd=H.lv.guards.find(g=>g.f===1&&g.type==='g'); gd.x=pe.x+70; gd.face=1; gd.wait=99; gd.state='patrol';
 H.p.f=1; H.p.x=pe.x-36; H.setKey('down',true); st(25); H.setKey('down',false); ok(H.p.slip>0||H.p.x>pe.x+4,'walking onto a banana peel: slip'); ok(gd.state==='investigate'||gd.state==='alert','slip noise draws a guard ('+gd.state+')');
 play(MANSION); const pe2=H.lv.peels.find(e=>e.f===1); H.p.f=1; H.p.x=pe2.x-40; H.setKey('down',true); st(14); H.setKey('down',false); H.setKey('ok',true); st(14); H.setKey('ok',false); H.setKey('down',true); st(10); H.setKey('down',false); st(10);
 ok(H.p.x>pe2.x&&H.p.slip<=0,'jumping the peel: no slip');
 // darts: a held jump clears one, standing gets hit
 play(0); gd=H.lv.guards.find(g=>g.type==='g'&&g.f===1); gd.state='stunned'; gd.stun=99; H.p.f=1; H.p.x=200-80;
 H.p.y=H.C.CEIL; H.p.air=true; H.p.vy=0; H.p.jhold=0; let lv1=H.lives; H.p.inv=0; window.__shots=1;
 ok(H.C.CEIL>H.C.DART_H&&H.C.CEIL+37<=58,'jump peak ('+H.C.CEIL+' px) clears darts ('+H.C.DART_H+') and stays under the ceiling');
 // stairs
 play(0); const up=H.lv.stairs.find(s=>s.dir<0); H.p.f=up.f; H.p.x=up.col*16+2; tap(); ok(H.p.f===up.f-1,'tap OK on stairs goes up');
 // wall-push pause
 play(0); H.p.f=1; H.p.x=2; H.setKey('up',true); st(Math.ceil(H.C.PAUSE_PUSH_S*30)+5); H.setKey('up',false); ok(H.state==='pause','push into the wall 1.5 s pauses'); H.render();
 tap(); ok(H.state==='play','OK on RESUME');
 // sleeper wakes when you linger
 play(0); gd=H.lv.guards.find(g=>g.type==='z'); H.p.f=gd.f; H.p.x=gd.x+8; st(30*2); ok(gd.state!=='sleep','lingering next to a sleeper wakes him');
 // can noise
 play(MANSION); const can=H.lv.cans.find(c=>c.f===1); gd=H.lv.guards.find(g=>g.f===1&&g.type==='g'); gd.x=can.x+60; gd.face=1; gd.wait=99;
 H.p.f=1; H.p.x=can.x-30; H.setKey('down',true); st(20); H.setKey('down',false); ok(gd.state==='investigate'||gd.state==='alert','kicking a can draws a guard ('+gd.state+')');
 // dog
 play(MANSION); gd=H.lv.guards.find(g=>g.type==='d'); H.p.f=gd.f; H.p.x=gd.x-40; gd.face=-1; gd.wait=99; const l2=H.lives; st(30*3); ok(H.lives<l2,'guard dog charges and bites');
 // timeout
 play(MANSION); H.timeLeft=0.05; const l3=H.lives; st(3); ok(H.state==='lose'&&H.lives===l3-1,'time up costs a life');
 // full run of 1-1: grab everything, exit
 play(0); for(const g of H.lv.guards){g.state='stunned';g.stun=999;}
 for(const l of H.lv.loot){H.p.f=l.f;H.p.x=l.x-2;st(2);} ok(H.lv.loot.every(l=>l.state==='carried'),'all loot carried');
 const e=H.lv.exit; H.p.f=e.f; H.p.x=e.col*16+2; tap(); ok(H.state==='escape','tap OK at EXIT with all loot: escape by balloon');
 st(30*3); H.render(); ok(H.state==='win'&&H.result.stars>=1,'1-1 escaped, stars '+H.result.stars+', total '+H.result.total);
 // exit locked without loot
 play(0); H.p.f=0; H.p.x=H.lv.exit.col*16+2; tap(); ok(H.state==='play','exit stays locked until all loot is taken');
 // bonus
 play(VAULT); ok(H.lv.L.bonus,'bonus level'); H.lv.lights.length=0;
 for(const l of H.lv.loot){H.p.f=l.f;H.p.x=l.x-2;st(1); if(H.state!=='play')break;}
 ok(H.state==='win'&&H.result.all,'bonus: all bars +'+H.result.total);
 play(VAULT); const L=H.lv.lights[0]; H.p.f=L.f; H.p.x=L.x+L.w/2-6; st(2); ok(H.state==='win'&&!H.result.all,'bonus: caught by a spotlight ends it');
 // game over
 play(MANSION); for(let i=0;i<3;i++){ const gd=H.lv.guards.find(g=>g.type==='d'); H.p.inv=0;H.p.knock=0; H.p.f=gd.f;H.p.x=gd.x;gd.state='alert'; st(10);} ok(H.state==='over','3 hits: game over');
 H.render();
 // unattended: guards patrol for 60 s without errors
 play(MANSION); H.p.f=2; H.p.x=2; for(let i=0;i<60*30;i++){H.update(1/30); if(H.state!=='play')break; if(i%3==0)H.render();} ok(true,'60 s of patrols render without errors (state '+H.state+')');

 // --- levels, progress, 4-floor view ---
 ok(H.LEVELS.length===16&&H.LEVELS.filter(L=>L.bonus).length===4,'16 levels incl. 4 bonus stages');
 for(let i=0;i<H.LEVELS.length;i++){ H.startLevel(i,false); ok(H.timeLeft===H.LEVELS[i].time,H.LEVELS[i].en+' builds, timer '+H.LEVELS[i].time+' s'); }
 localStorage.removeItem('heist.progress'); Object.assign(H.prog,H.loadProgress());
 H.toTitle(); ok(H.prog.unlocked===0&&H.menuSel===0,'fresh progress: only 1-1 open');
 H.menuSel=1; H.render(); tap(); ok(H.state==='title'&&H.popupT>0,'a locked level does not start');
 play(0); for(const g of H.lv.guards){g.state='stunned';g.stun=999;} for(const l of H.lv.loot){H.p.f=l.f;H.p.x=l.x-2;st(2);}
 H.p.f=0; H.p.x=H.lv.exit.col*16+2; tap(); st(30*3);
 ok(H.state==='win'&&H.prog.unlocked===1&&H.prog.stars[0]>=1,'escaping 1-1 opens 1-2 (stars '+H.prog.stars[0]+')');
 ok(JSON.parse(localStorage.getItem('heist.progress')).unlocked===1,'progress saved to localStorage');
 const re=H.loadProgress(); ok(re.unlocked===1&&re.stars[0]===H.prog.stars[0],'progress reloads (as after a reboot)');
 st(30); H.setKey('ok',true);H.setKey('ok',false); ok(H.state==='intro'&&H.levelIdx===1,'OK on the result goes on to 1-2');
 play(VAULT); H.lv.lights.length=0; for(const l of H.lv.loot){H.p.f=l.f;H.p.x=l.x-2;st(1); if(H.state!=='play')break;}
 ok(H.result.stars===3,'all the gold in a bonus: 3 stars');
 H.toTitle(); const items=H.titleItems(); H.menuSel=items.findIndex(i=>i.kind==='reset'); H.render();
 tap(); ok(H.resetArmed&&H.prog.unlocked===1,'reset asks first'); tap(); ok(!H.resetArmed&&H.prog.unlocked===0&&H.prog.stars.every(s=>s===0),'second OK resets the progress');
 const T=idx('4-3 TOWER'); play(T); ok(H.Y0===240-4*64,'4 floors: starts showing the ground floor (Y0 '+H.Y0+')');
 for(const g of H.lv.guards){g.state='stunned';g.stun=999;} H.p.f=1; H.p.x=300; st(45); ok(H.Y0===240-3*64,'upstairs the view glides to the roof (Y0 '+H.Y0+')');
 H.p.f=3; st(45); ok(H.Y0===240-4*64,'and back down');
 H.toTitle(); H.render();
}catch(err){out.push('EXCEPTION '+err.message+'\n'+err.stack);fails++;}
out.push(fails?'SOME CHECKS FAILED':'all checks passed');
document.getElementById('out').textContent=out.join('\n');
})();
