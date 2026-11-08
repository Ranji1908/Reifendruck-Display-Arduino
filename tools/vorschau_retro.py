# Retro-Design-Vorschau fuer das DinMeter (240x135). Geometrie identisch zur Firmware.
import sys, math
from PIL import Image, ImageDraw, ImageFont
OUT=sys.argv[1]
W,H=240,135
def c565(v): r=(v>>11)&31; g=(v>>5)&63; b=v&31; return (r*255//31,g*255//63,b*255//31)
BG=(0,0,0); AMB=c565(0xFDA0); MID=c565(0xABC0); GHOST=c565(0x1880)
MONO="/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
class Scr:
  def __init__(s): s.im=Image.new("RGB",(W,H),BG); s.d=ImageDraw.Draw(s.im); s.d.fontmode="1"
  def rect(s,x,y,w,h,col,fill=True):
    if w<=0 or h<=0: return
    if fill: s.d.rectangle([x,y,x+w-1,y+h-1],fill=col)
    else: s.d.rectangle([x,y,x+w-1,y+h-1],outline=col)
  def rrect(s,x,y,w,h,r,col): s.d.rounded_rectangle([x,y,x+w-1,y+h-1],r,outline=col)
  def line(s,x0,y0,x1,y1,col): s.d.line([x0,y0,x1,y1],fill=col)
  def circle(s,x,y,r,col,fill=False):
    if fill: s.d.ellipse([x-r,y-r,x+r,y+r],fill=col)
    else: s.d.ellipse([x-r,y-r,x+r,y+r],outline=col)
  def tri(s,pts,col): s.d.polygon(pts,fill=col)
  # Pixelschrift wie Font0 (6x8 Zelle) * size
  def text(s,t,x,y,size,col,align="l"):
    f=ImageFont.truetype(MONO,int(9*size)); w=len(t)*6*size
    if align=="r": x-=w
    elif align=="c": x-=w//2
    for i,ch in enumerate(t): s.d.text((x+i*6*size+ (6*size)//2, y+(8*size)//2),ch,font=f,fill=col,anchor="mm")
  # 7-Segment
  def segm(s,h): t=max(2,h//9); return int(h*0.5), t, t+2
  def segw(s,txt,h):
    w,t,sp=s.segm(h); tot=0
    for ch in txt: tot+= (t+sp) if ch in ".:" else (w+sp)
    return tot-sp if txt else 0
  def seg(s,txt,x,y,h,on,ghost=GHOST,align="l"):
    w,t,sp=s.segm(h); tw=s.segw(txt,h)
    if align=="r": x-=tw
    elif align=="c": x-=tw//2
    B={'0':0x3F,'1':0x06,'2':0x5B,'3':0x4F,'4':0x66,'5':0x6D,'6':0x7D,'7':0x07,'8':0x7F,'9':0x6F,'-':0x40,' ':0}
    for ch in txt:
      if ch=='.': s.rect(x,y+h-t,t,t,on); x+=t+sp; continue
      if ch==':': s.rect(x,y+h//3-t//2,t,t,on); s.rect(x,y+2*h//3-t//2,t,t,on); x+=t+sp; continue
      bits=B.get(ch,0); mid=y+h//2-t//2
      segs=[(x+t,y,w-2*t,t),(x+w-t,y+t,t,mid-(y+t)),(x+w-t,mid+t,t,(y+h-t)-(mid+t)),(x+t,y+h-t,w-2*t,t),(x,mid+t,t,(y+h-t)-(mid+t)),(x,y+t,t,mid-(y+t)),(x+t,mid,w-2*t,t)]
      for i,(sx,sy,sw,sh) in enumerate(segs):
        col=on if bits>>i&1 else ghost
        if col is not None: s.rect(sx,sy,sw,sh,col)
      x+=w+sp
  def header(s,left,right=None,rcol=None):
    s.text(left,1,1,1,AMB)
    if right: s.text(right,239,1,1,rcol or MID,"r")
    for x in range(0,W,3): s.d.point((x,11),fill=MID)

# Transporter quer, Front rechts. Rad-Reihenfolge VL,VR,HL,HR
def van(s,cx,cy,alarm_wheel=None,blink=True):
  x0,y0,w,h=cx-28,cy-17,56,34
  s.rrect(x0,y0,w,h,4,AMB)
  s.line(x0+42,y0+3,x0+42,y0+h-4,AMB); s.line(x0+36,y0+3,x0+36,y0+h-4,MID)
  s.tri([(x0+w+3,cy-4),(x0+w+8,cy),(x0+w+3,cy+4)],MID)   # Fahrtrichtung
  wpos={0:(x0+40,y0-4),1:(x0+40,y0+h),2:(x0+6,y0-4),3:(x0+6,y0+h)}
  for k,(wx,wy) in wpos.items():
    if k==alarm_wheel and not blink: s.rect(wx,wy,12,4,AMB,False)
    else: s.rect(wx,wy,12,4,AMB)

QX={0:196,1:196,2:44,3:44}        # Spaltenmitte
QTOP={0:True,1:False,2:True,3:False}
NAMES={0:"VL",1:"VR",2:"HL",3:"HR"}
def quad_box(w): return (0 if QX[w]<120 else 152, 12 if QTOP[w] else 72, 88, 60 if QTOP[w] else 63)

def tires(mode="STRASSE",alarm=False,blink=True,temps=False):
  s=Scr()
  if temps: s.header("REIFENTEMP  GRAD C", "SENSOR-BATT")
  elif alarm: s.header("DRUCK  "+mode, "!! HL LECK", AMB if blink else BG)
  else: s.header("DRUCK  "+mode, "BAR")
  van(s,120,67,2 if alarm else None,blink)
  pres=["3.6","3.5","3.1" if alarm else "4.1","4.0"]; soll=["3.5","3.5","4.0","4.0"]
  tmp=["19","20","21","22"]; batt=["85%","85%","80%","15%"]
  for w in range(4):
    inv = alarm and w==2 and blink and not temps
    bx,by,bw,bh=quad_box(w)
    fg,gh=(BG,None) if inv else (AMB,GHOST)
    if inv: s.rect(bx,by+1,bw,bh,AMB)
    top=QTOP[w]; ly=15 if top else 124; sy=25 if top else 76
    if temps: lab=NAMES[w]+" BATT "+batt[w]
    elif alarm and w==2: lab="HL LECK!"
    else: lab=NAMES[w]+" SOLL "+soll[w]
    s.text(lab,bx+4,ly,1,BG if inv else MID)
    s.seg(tmp[w] if temps else pres[w],QX[w],sy,44,fg,gh,"c")
  return s

def tilt(roll=4.0,pitch=-7.0):
  s=Scr(); s.header("NEIGUNG","GRAD")
  def rot(cx,cy,pts,deg,k):
    a=math.radians(deg); ca,sa=math.cos(a),math.sin(a)
    return [(cx+k*(x*ca-y*sa),cy+k*(x*sa+y*ca)) for x,y in pts]
  def poly(pts,col):
    for i in range(len(pts)): s.line(*pts[i],*pts[(i+1)%len(pts)],col)
  # Heck (links) und Seite (rechts), Drahtmodell
  for x in range(4,116,4): s.d.point((x,62),fill=MID)
  for x in range(124,236,4): s.d.point((x,62),fill=MID)
  k=0.8
  poly(rot(60,42,[(-24,-24),(24,-24),(26,16),(-26,16)],roll,k),AMB)
  poly(rot(60,42,[(-18,-18),(18,-18),(18,-8),(-18,-8)],roll,k),AMB)
  for wx in (-22,12): poly(rot(60,42,[(wx,16),(wx+10,16),(wx+10,24),(wx,24)],roll,k),AMB)
  pd=-pitch
  poly(rot(180,42,[(-38,-24),(18,-24),(30,-10),(40,-6),(40,16),(-38,16)],pd,k),AMB)
  poly(rot(180,42,[(18,-20),(26,-10),(18,-10)],pd,k),AMB)
  for wx in (-30,20): poly(rot(180,42,[(wx,16),(wx+12,16),(wx+12,24),(wx,24)],pd,k),AMB)
  s.seg(f"{abs(roll):.0f}",60,70,44,AMB,GHOST,"c"); s.seg(f"{abs(pitch):.0f}",180,70,44,AMB,GHOST,"c")
  s.text("SEITE R-TIEF" if roll>0 else "SEITE L-TIEF",60,124,1,MID,"c")
  s.text("BERGAUF" if pitch>0 else "BERGAB",180,124,1,MID,"c")
  return s

def compass(hdg=247):
  s=Scr(); s.header("KOMPASS","KURS")
  txt=f"{hdg:03d}"
  s.seg(txt,100,18,48,AMB,GHOST,"c")
  pts=["N","NO","O","SO","S","SW","W","NW"]; cp=pts[int((hdg+22.5)//45)%8]
  s.text(cp,200,30,3,AMB,"c")
  # Kompassband
  y0=86; s.line(0,y0,239,y0,MID); s.line(0,y0+40,239,y0+40,MID)
  for d in range(hdg-70,hdg+71):
    x=120+(d-hdg)*1.6
    dd=d%360
    if dd%10==0: s.line(x,y0+30,x,y0+39,AMB if dd%30==0 else MID)
    if dd%45==0:
      lab={0:"N",45:"NO",90:"O",135:"SO",180:"S",225:"SW",270:"W",315:"NW"}[dd]
      s.text(lab,int(x),y0+8,2,AMB,"c")
  s.tri([(114,y0+1),(126,y0+1),(120,y0+8)],AMB)
  return s

def bord():
  s=Scr(); s.header("BORD","14:32")
  cells=[("HOEHE M","487",4,14,"l"),("UHR","14:32",236,14,"r"),("LUFTDRUCK HPA","1013",4,76,"l"),("INNEN GRAD C","23",236,76,"r")]
  for lab,val,x,y,a in cells:
    s.text(lab,x,y,1,MID,a); s.seg(val,x,y+11,36,AMB,GHOST,a)
  s.text("TREND: FAELLT",4,126,1,AMB)
  for x in range(120,121): pass
  return s

def level(roll=1.6,pitch=-0.8):
  s=Scr(); s.header("AUSRICHTEN","CM UNTERLEGEN")
  cx,cy,r=120,73,40
  s.circle(cx,cy,r,AMB); s.circle(cx,cy,8,MID)
  for x in range(cx-r,cx+r+1,4): s.d.point((x,cy),fill=MID)
  for y in range(cy-r,cy+r+1,4): s.d.point((cx,y),fill=MID)
  # Front rechts: Blase wandert zur hoeheren Seite.  links tiefer/rechts hoeher -> Blase oben? (links = oben im Bild)
  bx=cx+pitch*6; by=cy-roll*6   # Front rechts: Front hoeher -> Blase rechts; links hoeher -> Blase oben
  s.circle(int(bx),int(by),7,AMB,True)
  L,T=367,171; tp,tr=math.tan(math.radians(pitch)),math.tan(math.radians(roll))
  xs=[L/2,L/2,-L/2,-L/2]; ys=[T/2,-T/2,T/2,-T/2]; hh=[xs[i]*tp+ys[i]*tr for i in range(4)]
  lifts={i:("0" if max(hh)-hh[i]<0.5 else f"{max(hh)-hh[i]:.0f}") for i in range(4)}
  for w in range(4):
    x=QX[w]; top=QTOP[w]; ly=15 if top else 124; sy=26 if top else 78
    s.text(NAMES[w]+" CM",x,ly,1,MID,"c"); s.seg(lifts[w],x,sy,36,AMB,GHOST,"c")
  return s

def menu(sel=1):
  s=Scr(); s.header("MENUE","2/27")
  items=[("SOLL STR VO","3.5"),("SOLL STR HI","4.0"),("SOLL OFF VO","2.5"),("SOLL OFF HI","2.8"),("WARNUNG","-10%")]
  for i,(l,v) in enumerate(items):
    y=15+i*24
    if i==sel: s.rect(0,y-2,W,22,AMB); col=BG
    else: col=AMB
    s.text(l,4,y+1,2,col); s.text(v,236,y+1,2,col,"r")
  return s

def edit():
  s=Scr(); s.header("SOLL STRASSE HINTEN","BAR")
  s.seg("4.0",120,28,64,AMB,GHOST,"c")
  s.text("DREHEN = WERT   DRUECKEN = OK",120,124,1,MID,"c")
  return s

def pair():
  s=Scr(); s.header("SENSOREN ANLERNEN","1/4")
  s.text("VL",4,20,4,AMB)
  s.text("GEFUNDEN:",70,22,1,MID); s.text("TPMSII 10:8A:78",70,34,1,AMB)
  s.text("3.52 BAR  -61 DBM",70,48,1,AMB)
  s.text("SENSOR AUF DAS RAD",4,78,2,MID)
  s.text("VORNE LINKS SCHRAUBEN",4,98,1,MID)
  s.text("KURZ = UEBERNEHMEN   LANG = ABBRUCH",120,124,1,AMB,"c")
  return s

shots=[("1_reifen",tires(),"REIFENDRUCK"),("2_alarm",tires("OFFROAD",True),"ALARM (BLINKT)"),("3_temperatur",tires(temps=True),"REIFENTEMPERATUR"),
 ("4_neigung",tilt(),"NEIGUNG"),("5_kompass",compass(),"KOMPASS"),("6_bord",bord(),"HOEHE / WETTER / UHR"),("7_ausrichten",level(),"AUSRICHTEN"),
 ("8_menue",menu(),"MENUE"),("9_wert",edit(),"WERT AENDERN"),("10_anlernen",pair(),"SENSOR ANLERNEN")]
SC=3
def framed(im):
  big=im.resize((W*SC,H*SC),Image.NEAREST)
  pad=14*SC//3*2; out=Image.new("RGB",(big.width+2*pad,big.height+2*pad),(18,18,18))
  ImageDraw.Draw(out).rounded_rectangle([0,0,out.width-1,out.height-1],18,fill=(26,26,26),outline=(60,60,60),width=2)
  out.paste(big,(pad,pad)); return out
tiles=[]
for name,s,cap in shots:
  f=framed(s.im); f.save(f"{OUT}/{name}.png"); tiles.append((f,cap))
# Uebersicht 2 Spalten
tw,th=tiles[0][0].size; capH=40; cols=2; rows=(len(tiles)+1)//2
sheet=Image.new("RGB",(cols*tw+(cols+1)*30, rows*(th+capH)+(rows+1)*24),(12,12,12)); d=ImageDraw.Draw(sheet)
fcap=ImageFont.truetype(MONO,26)
for i,(f,cap) in enumerate(tiles):
  cx=30+(i%2)*(tw+30); cy=24+(i//2)*(th+capH+24)
  d.text((cx,cy),f"{i+1}. {cap}",font=fcap,fill=AMB); sheet.paste(f,(cx,cy+capH))
sheet.save(f"{OUT}/uebersicht.png")
