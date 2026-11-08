# V2 "modern" – Vorschau fuer das DinMeter (240x135). Geometrie identisch zur Firmware.
import sys, math
from PIL import Image, ImageDraw, ImageFont
OUT=sys.argv[1]; W,H=240,135; SS=4   # intern 4x rendern, dann verkleinern (wie Antialiasing-Schrift)
def c565(v): r=(v>>11)&31; g=(v>>5)&63; b=v&31; return (r*255//31,g*255//63,b*255//31)
BG=c565(0x0841); CARD=c565(0x18E3); LINE=c565(0x31A6); TX=c565(0xFFFF); SUB=c565(0x8C92)
OK=c565(0x3666); WARN=c565(0xFD00); CRIT=c565(0xFA28); ROAD=c565(0x041F); OFF=c565(0xFD00); ACC=c565(0x2E7B)
FR="/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"; FB="/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
LB="/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
# Groesse -> (Schrift, Em-Hoehe in px). 24/40 entsprechen FreeSansBold9pt7b / FreeSansBold18pt7b der Firmware
FONTS={9:(FR,7.0),12:(FR,9.4),18:(FR,14.0),24:(LB,17.6),40:(LB,33.0)}
class Scr:
  def __init__(s): s.im=Image.new("RGB",(W*SS,H*SS),BG); s.d=ImageDraw.Draw(s.im)
  def P(s,v): return int(round(v*SS))
  def rrect(s,x,y,w,h,r,fill=None,outline=None,width=1):
    s.d.rounded_rectangle([s.P(x),s.P(y),s.P(x+w)-1,s.P(y+h)-1],s.P(r),fill=fill,outline=outline,width=s.P(width) if outline else 0)
  def rect(s,x,y,w,h,fill): s.d.rectangle([s.P(x),s.P(y),s.P(x+w)-1,s.P(y+h)-1],fill=fill)
  def line(s,x0,y0,x1,y1,col,w=1): s.d.line([s.P(x0),s.P(y0),s.P(x1),s.P(y1)],fill=col,width=s.P(w))
  def circle(s,x,y,r,fill=None,outline=None,w=1): s.d.ellipse([s.P(x-r),s.P(y-r),s.P(x+r),s.P(y+r)],fill=fill,outline=outline,width=s.P(w) if outline else 0)
  def arc(s,x,y,r,a0,a1,col,w): s.d.arc([s.P(x-r),s.P(y-r),s.P(x+r),s.P(y+r)],a0,a1,fill=col,width=s.P(w))
  def poly(s,pts,fill): s.d.polygon([(s.P(a),s.P(b)) for a,b in pts],fill=fill)
  def text(s,t,x,y,size,col,anchor="lm"):
    f=ImageFont.truetype(FONTS[size][0],s.P(FONTS[size][1])); s.d.text((s.P(x),s.P(y)),t,font=f,fill=col,anchor=anchor)
  def tw(s,t,size):
    f=ImageFont.truetype(FONTS[size][0],s.P(FONTS[size][1])); return f.getlength(t)/SS
  def deg(s,x,y,size,col): r=max(1.5,size*0.09); s.circle(x+r+1,y-size*0.22,r,outline=col,w=max(1,size*0.05))
  def done(s): return s.im.resize((W,H),Image.LANCZOS)

def pill(s,x,y,txt,col):
  w=s.tw(txt,12)+12; s.rrect(x,y,w,14,7,fill=col); s.text(txt,x+6,y+7,12,BG if col in (OFF,WARN) else TX,"lm"); return w
def header(s,mode_or_title,status=None,scol=SUB,is_mode=True):
  if is_mode: pill(s,4,3,mode_or_title,ROAD if mode_or_title=="Strasse" else OFF)
  else: s.text(mode_or_title,5,10,12,TX,"lm")
  if status:
    s.circle(236-s.tw(status,12)-7,10,3,fill=scol); s.text(status,236,10,12,scol if scol!=SUB else SUB,"rm")

# Fahrzeug quer (Front rechts), klein in der Mitte
def van(s,cx,cy,wheelcols):
  s.rrect(cx-20,cy-11,40,22,5,fill=LINE); s.rrect(cx+6,cy-9,10,18,3,fill=CARD)
  pos=[(cx+8,cy-14),(cx+8,cy+11),(cx-16,cy-14),(cx-16,cy+11)]
  for (x,y),c in zip(pos,wheelcols): s.rrect(x,y,9,3.5,1.5,fill=c)
CARDS=[(144,20,92,55),(144,78,92,55),(4,20,92,55),(4,78,92,55)]  # VL,VR,HL,HR
NAMES=["VL","VR","HL","HR"]
def card(s,i,big,unit,foot,stcol,ratio=None,alarm=False,degsign=False):
  x,y,w,h=CARDS[i]
  s.rrect(x,y,w,h,8,fill=CARD,outline=CRIT if alarm else None,width=2)
  s.text(NAMES[i],x+8,y+10,12,SUB,"lm"); s.circle(x+w-10,y+10,3.5,fill=stcol)
  vc=CRIT if alarm else TX
  s.text(big,x+8,y+31,24,vc,"lm"); bw=s.tw(big,24)
  if degsign: s.deg(x+8+bw,y+31,24,vc)
  elif unit: s.text(unit,x+11+bw,y+35,9,SUB,"lm")
  s.text(foot,x+8,y+h-12,9,CRIT if alarm else SUB,"lm")
  if ratio is not None:  # Fuellbalken: Druck relativ zum Soll (Soll = Markierung)
    bx,by,bw2=x+50,y+h-13,w-58
    s.rrect(bx,by,bw2,3,1.5,fill=LINE); fill=max(0.0,min(1.0,ratio/1.25))
    s.rrect(bx,by,max(3,bw2*fill),3,1.5,fill=stcol); tx=bx+bw2*(1/1.25); s.rect(tx,by-2,1,7,TX)

def tires(alarm=False):
  s=Scr()
  if alarm: header(s,"Offroad","HL Leck",CRIT)
  else: header(s,"Strasse","Alles OK",OK)
  p=[3.6,3.5,3.1 if alarm else 4.1,4.0]; so=[3.5,3.5,4.0,4.0]
  cols=[OK,OK,CRIT if alarm else OK,OK]
  for i in range(4):
    al = alarm and i==2
    card(s,i,f"{p[i]:.1f}","bar","Leck!" if al else f"Soll {so[i]:.1f}",cols[i],p[i]/so[i],al)
  van(s,120,77,cols); return s.done()

def temps():
  s=Scr(); header(s,"Reifentemperatur",None,is_mode=False)
  t=[19,20,21,22]; b=[85,85,80,15]
  for i in range(4):
    card(s,i,str(t[i]),None,f"Batterie {b[i]}%",WARN if b[i]<20 else OK,None,False,True)
    x,y,w,h=CARDS[i]; s.rrect(x+w-26,y+h-16,18,8,2,outline=SUB,width=1); s.rect(x+w-8,y+h-14,2,4,SUB)
    s.rect(x+w-24.5,y+h-14.5,15*b[i]/100,5,WARN if b[i]<20 else OK)
  van(s,120,77,[LINE]*4); return s.done()

def tilt(roll=4.0,pitch=-7.0):
  s=Scr(); header(s,"Neigung",None,is_mode=False)
  def rotp(cx,cy,pts,deg,k=1.0):
    a=math.radians(deg); ca,sa=math.cos(a),math.sin(a); return [(cx+k*(x*ca-y*sa),cy+k*(x*sa+y*ca)) for x,y in pts]
  for i,(cx,val,cap,deg,shape) in enumerate([(61,roll,"Seite - rechts tief" if roll>0 else "Seite - links tief",roll,"rear"),(179,pitch,"Laengs - bergab" if pitch<0 else "Laengs - bergauf",-pitch,"side")]):
    x=4 if i==0 else 122; s.rrect(x,20,114,113,8,fill=CARD)
    s.line(x+10,62,x+104,62,LINE,1)
    if shape=="rear":
      s.poly(rotp(cx,46,[(-22,-20),(22,-20),(24,14),(-24,14)],deg,0.8),ACC)
      s.poly(rotp(cx,46,[(-16,-15),(16,-15),(16,-6),(-16,-6)],deg,0.8),CARD)
      for wx in (-21,11): s.poly(rotp(cx,46,[(wx,14),(wx+10,14),(wx+10,21),(wx,21)],deg,0.8),SUB)
    else:
      s.poly(rotp(cx,46,[(-38,-22),(16,-22),(28,-9),(38,-5),(38,14),(-38,14)],deg,0.8),ACC)
      s.poly(rotp(cx,46,[(17,-18),(25,-9),(17,-9)],deg,0.8),CARD)
      for wx in (-30,20): s.poly(rotp(cx,46,[(wx,14),(wx+11,14),(wx+11,21),(wx,21)],deg,0.8),SUB)
    v=f"{abs(val):.0f}"; tw=s.tw(v,40); s.text(v,cx-tw/2-4,96,40,TX,"lm"); s.deg(cx+tw/2-4,96,40,TX)
    s.text(cap,cx,124,9,SUB,"mm")
  return s.done()

def compass(h=247):
  s=Scr(); header(s,"Kompass",None,is_mode=False)
  s.rrect(4,20,232,113,8,fill=CARD)
  cx,cy,r=62,77,48
  s.circle(cx,cy,r,outline=LINE,w=2)
  for d in range(0,360,15):
    a=math.radians(d-h-90); r0=r-(7 if d%45==0 else 4)
    s.line(cx+r0*math.cos(a),cy+r0*math.sin(a),cx+(r-1)*math.cos(a),cy+(r-1)*math.sin(a),TX if d%90==0 else SUB,1.5 if d%90==0 else 1)
  for d,l in [(0,"N"),(90,"O"),(180,"S"),(270,"W")]:
    a=math.radians(d-h-90); s.text(l,cx+(r-15)*math.cos(a),cy+(r-15)*math.sin(a),12,CRIT if l=="N" else TX,"mm")
  s.poly([(cx,cy-r-3),(cx-5,cy-r+7),(cx+5,cy-r+7)],ACC)   # Fahrtrichtung oben
  s.poly([(cx,cy-20),(cx-5,cy),(cx,cy+3),(cx+5,cy)],ACC)
  v=f"{h}"; tw=s.tw(v,40); s.text(v,128,70,40,TX,"lm"); s.deg(128+tw,70,40,TX)
  s.text("SW",130,104,24,ACC,"lm"); s.text("Suedwest",172,105,12,SUB,"lm")
  return s.done()

def bord():
  s=Scr(); header(s,"Bord",None,is_mode=False); s.text("14:32",236,10,12,SUB,"rm")
  tiles=[(4,20,"Hoehe","487","m"),(122,20,"Uhrzeit","14:32",""),(4,78,"Luftdruck","1013","hPa"),(122,78,"Innen","23","C")]
  for x,y,lab,val,unit in tiles:
    s.rrect(x,y,114,55,8,fill=CARD); s.text(lab,x+8,y+10,12,SUB,"lm")
    s.text(val,x+8,y+33,24,TX,"lm"); tw=s.tw(val,24)
    if unit: s.text(unit,x+11+tw,y+37,12,SUB,"lm")
  # Trend-Pfeil im Luftdruck-Kachel
  s.poly([(104,108),(96,96),(112,96)],WARN); s.text("faellt",104,122,9,WARN,"mm")
  return s.done()

def level(roll=1.6,pitch=-0.8):
  s=Scr(); header(s,"Ausrichten",None,is_mode=False); s.text("cm unterlegen",236,10,12,SUB,"rm")
  L,T=367,171; tp,tr=math.tan(math.radians(pitch)),math.tan(math.radians(roll))
  xs=[L/2,L/2,-L/2,-L/2]; ys=[T/2,-T/2,T/2,-T/2]; hh=[xs[i]*tp+ys[i]*tr for i in range(4)]
  lift=[max(hh)-v for v in hh]
  chips=[(162,22),(162,104),(4,22),(4,104)]
  for i,(x,y) in enumerate(chips):
    s.rrect(x,y,74,28,8,fill=CARD); s.text(NAMES[i],x+8,y+14,12,SUB,"lm")
    v="0" if lift[i]<0.5 else f"+{lift[i]:.0f}"; col=OK if lift[i]<0.5 else TX
    s.text(v,x+66,y+14,18,col,"rm")
  cx,cy=120,77
  s.circle(cx,cy,42,fill=CARD); s.circle(cx,cy,28,outline=LINE,w=1); s.circle(cx,cy,10,outline=LINE,w=1)
  s.line(cx-42,cy,cx+42,cy,LINE); s.line(cx,cy-42,cx,cy+42,LINE)
  bx,by=cx+pitch*6,cy-roll*6; s.circle(bx,by,8,fill=WARN)
  s.text("1.6 / 0.8 Grad",cx,128,9,SUB,"mm")
  return s.done()

def menu():
  s=Scr(); header(s,"Einstellungen",None,is_mode=False); s.text("2 / 27",236,10,12,SUB,"rm")
  items=[("Soll Strasse vorn","3.5 bar"),("Soll Strasse hinten","4.0 bar"),("Soll Offroad vorn","2.5 bar"),("Soll Offroad hinten","2.8 bar")]
  for i,(l,v) in enumerate(items):
    y=21+i*28
    if i==1: s.rrect(4,y,226,26,7,fill=ACC)
    s.text(l,12,y+13,12,BG if i==1 else TX,"lm"); s.text(v,222,y+13,12,BG if i==1 else ACC,"rm")
  s.rrect(234,21,3,112,1.5,fill=LINE); s.rrect(234,21+112*1/27,3,112*4/27,1.5,fill=SUB)
  return s.done()

def edit():
  s=Scr(); header(s,"Soll Strasse hinten",None,is_mode=False)
  s.rrect(4,20,232,113,8,fill=CARD)
  v="4.0"; tw=s.tw(v,40); s.text(v,120-tw/2-10,66,40,TX,"lm"); s.text("bar",120+tw/2-6,72,12,SUB,"lm")
  bx,by,bw=24,98,192; s.rrect(bx,by,bw,4,2,fill=LINE); f=(4.0-0.5)/(8.0-0.5); s.rrect(bx,by,bw*f,4,2,fill=ACC); s.circle(bx+bw*f,by+2,6,fill=TX)
  s.text("drehen: aendern - druecken: OK",120,121,9,SUB,"mm")
  return s.done()

def pair():
  s=Scr(); header(s,"Sensoren anlernen",None,is_mode=False); s.text("1 / 4",236,10,12,SUB,"rm")
  s.rrect(4,20,232,113,8,fill=CARD)
  s.text("VL",14,48,40,ACC,"lm"); s.text("vorne links",16,78,12,SUB,"lm")
  s.rrect(118,30,108,58,7,fill=LINE)
  s.circle(128,42,3.5,fill=OK); s.text("Gefunden",136,42,12,TX,"lm")
  s.text("TPMSII 10:8A:78",126,60,9,SUB,"lm"); s.text("3.52 bar  -61 dBm",126,76,9,SUB,"lm")
  s.text("kurz: uebernehmen - lang: Abbruch",120,118,9,SUB,"mm")
  return s.done()

shots=[("1_reifen",tires(),"Reifendruck"),("2_alarm",tires(True),"Alarm (blinkt)"),("3_temperatur",temps(),"Reifentemperatur"),
 ("4_neigung",tilt(),"Neigung"),("5_kompass",compass(),"Kompass"),("6_bord",bord(),"Höhe / Wetter / Uhr"),("7_ausrichten",level(),"Ausrichten"),
 ("8_menue",menu(),"Menü"),("9_wert",edit(),"Wert ändern"),("10_anlernen",pair(),"Sensor anlernen")]
SC=3
def framed(im):
  big=im.resize((W*SC,H*SC),Image.LANCZOS); pad=28; out=Image.new("RGB",(big.width+2*pad,big.height+2*pad),(10,10,10))
  ImageDraw.Draw(out).rounded_rectangle([0,0,out.width-1,out.height-1],18,fill=(24,24,26),outline=(60,60,64),width=2)
  out.paste(big,(pad,pad)); return out
tiles=[]
for name,im,cap in shots:
  f=framed(im); f.save(f"{OUT}/{name}.png"); tiles.append((f,cap))
tw,th=tiles[0][0].size; capH=40; rows=(len(tiles)+1)//2
sheet=Image.new("RGB",(2*tw+90,rows*(th+capH)+(rows+1)*24),(12,12,14)); d=ImageDraw.Draw(sheet)
fc=ImageFont.truetype(FB,26)
for i,(f,cap) in enumerate(tiles):
  cx=30+(i%2)*(tw+30); cy=24+(i//2)*(th+capH+24); d.text((cx,cy),f"{i+1}. {cap}",font=fc,fill=(220,220,225)); sheet.paste(f,(cx,cy+capH))
sheet.save(f"{OUT}/uebersicht.png")
