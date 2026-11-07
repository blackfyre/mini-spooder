function $(id){return document.getElementById(id)}
function say(text,tone){var s=$('status');s.textContent=text;s.className=tone||''}
// One message per outcome of "Navigation and command feedback".
function outcome(x,what){
  var b=(x.responseText||'').trim();
  if(x.status==200)return say('Sent: '+what+'.','ok');
  if(x.status==409&&b=='remote in control')return say('Not run: the remote is driving the robot. The portal is locked until its session ends.','warn');
  if(x.status==409&&b=='motion playing')return say('Not saved: a motion is playing. Try again when it has ended.','warn');
  if(x.status==503&&b=='servo output off')return say('Not run: servo output did not accept the servo command.','bad');
  if(x.status==503&&b=='servo output fault')return say('Not run: calibration fault. Repair the trims on the Servo calibration page.','bad');
  if(x.status==400)return say('Invalid: the robot refused '+what+'. Nothing was run.','bad');
  say('Unexpected answer '+x.status+(b?' ('+b+')':'')+' to '+what+'.','bad');
}
// Sends one GET without reloading the page; no answer within 5 s is reported.
function send(url,what){
  var x=new XMLHttpRequest(),done=false;
  function noAnswer(){if(done)return;done=true;say('No answer from the robot within 5 s: '+what+' may still have run.','bad')}
  x.open('GET',url,true);
  x.timeout=5000;
  x.ontimeout=noAnswer;
  x.onerror=noAnswer;
  x.onload=function(){if(done)return;done=true;outcome(x,what)};
  say('Sending: '+what+'...');
  x.send();
}
// The shared click handler: a control's data-pm, or data-servo with data-value.
function control(e){
  var b=e.target.closest('button');
  if(!b)return;
  var d=b.dataset;
  if(d.pm!==undefined)send('controller?pm='+d.pm,b.textContent);
  else if(d.servo!==undefined&&d.value!==undefined)send('controller?servo='+d.servo+'&value='+d.value,b.textContent);
}
