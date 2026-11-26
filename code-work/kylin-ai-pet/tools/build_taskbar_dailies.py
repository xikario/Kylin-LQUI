"""Author the four small idle atlases. Only finished PNG frames run in the panel."""
import json
import math
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageOps

ROOT = Path(__file__).resolve().parents[1]
PETS = ROOT / 'assets' / 'pets'
CELL = 80
WORK = 240
FILTER = Image.Resampling.LANCZOS
BICUBIC = Image.Resampling.BICUBIC
# Stable square portrait cameras in a 320×180 reference frame (left, top, side).
CAMERA = {'claude': (101, 1, 115), 'fish': (115, 35, 87),
          'gpt': (109, 17, 91), 'gemini': (121, 15, 82)}
# Mouth, sleeve, cuff and accent, in the existing taskbar portrait's coordinates.
STYLE = {
    'claude': ((46, 66), '#40292b', '#fcf3df', '#ef955e'),
    'fish': ((47, 77), '#283653', '#f1f0ff', '#72c8e6'),
    'gpt': ((49, 73), '#dad8ed', '#fffaff', '#b8a3da'),
    'gemini': ((46, 73), '#584276', '#e9dcff', '#e78bbb'),
}


def ease(t, lo, hi):
    u = min(1., max(0., (t-lo)/(hi-lo)))
    return u*u*(3-2*u)


def arc(t, lo, hi):
    return math.sin(math.pi*ease(t, lo, hi)) ** 2


def sheet_frame(im):
    return im.convert('RGBa').resize((CELL, CELL), FILTER).convert('RGBA')


def clip(role, path, keys, wide=0., dx=0., dy=0.):
    """Select the meaningful beats, using one smoothly pulled-back camera per scene."""
    base = sheet_frame(Image.open(PETS / role / 'avatar.png'))
    x, y, side = CAMERA[role]
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-threads', '2', '-c:v', 'libvpx-vp9',
                        '-i', str(ROOT / path), '-vf', 'fps=6,scale=640:360', '-frames:v', str(max(keys)+1),
                        '-threads', '1', str(Path(tmp) / '%03d.png')], check=True)
        frames = []
        for i, key in enumerate(keys):
            t = i/(len(keys)-1)
            open_camera = ease(t, 0, .22)*(1-ease(t, .79, 1))
            width = side*(1+wide*open_camera)
            left = x + (side-width)/2 + dx*open_camera
            top = y + dy*open_camera
            with Image.open(Path(tmp) / f'{key+1:03}.png') as original:
                box = tuple(round(v*2) for v in (left,top,left+width,top+width))
                frames.append(sheet_frame(original.convert('RGBA').crop(box)))
    # Cut on neutral poses. Cross-dissolving two different eye positions ghosts the face.
    return [base, *frames, frames[0], base]


# Props and the hands holding them are drawn on a supersampled, transparent stage.
# Coordinates are percentages so they use the same camera as the portrait.
def blank():
    return Image.new('RGBA', (WORK, WORK))


def xy(values):
    return tuple(round(v*WORK/100) for v in values)


def ellipse(d, box, fill, ink=None, width=1):
    d.ellipse(xy(box), fill, ink, max(1, round(width*WORK/100)))


def line(d, points, fill, width):
    d.line([xy(p) for p in points], fill=fill, width=max(1, round(width*WORK/100)), joint='curve')


def polygon(d, points, fill):
    d.polygon([xy(p) for p in points], fill=fill)


def stamp(stage, prop, center, size, angle=0):
    if size <= .2:
        return
    n = max(1, round(size*WORK/100))
    im = prop.resize((n,n), FILTER).rotate(angle, BICUBIC, expand=True)
    x,y = xy(center)
    stage.alpha_composite(im, (x-im.width//2, y-im.height//2))


def mitten(d, x, y, sleeve, cuff):
    ellipse(d, (x-9,y+3,x+11,y+20), sleeve, '#5d5060', 1.7)
    ellipse(d, (x-7,y+2,x+9,y+11), cuff, '#8f7581', 1.4)
    ellipse(d, (x-7,y-7,x+8,y+7), '#ffdfc9', '#a87a75', 1.9)


def mirror_glass(role, base):
    prop = blank();d = ImageDraw.Draw(prop)
    _, sleeve,cuff,accent = STYLE[role]
    line(d, [(49,55),(52,90)], '#685165', 10)
    line(d, [(49,54),(52,90)], '#f4c17c', 6)
    ellipse(d, (13,4,87,72), '#d8a261', '#694959', 2.8)
    ellipse(d, (18,9,82,67), '#a6dce6', '#fff1c8', 2)
    # A reflected eye and fringe identify this as a mirror at icon size.
    face=ImageOps.mirror(base.crop(xy((20,35,76,84)))).resize(xy((59,53)),FILTER)
    blue=Image.new('RGBA',face.size,'#b1e2e8')
    blue.alpha_composite(face)
    face=Image.blend(blue,Image.new('RGBA',face.size,'#b1e2e8'),.46)
    mask=Image.new('L',face.size);ImageDraw.Draw(mask).ellipse((0,0,face.width-1,face.height-1),fill=255)
    prop.paste(face,xy((20,11)),mask)
    line(d,[(26,37),(44,17)],'#f4ffff',3.5)
    if role=='gemini':
        polygon(d,[(77,3),(80,12),(88,15),(80,18),(77,27),(74,18),(66,15),(74,12)],'#fff0a8')
    ellipse(d,(45,60,54,68),accent)
    mitten(d,54,77,sleeve,cuff)
    return prop


def tumbler(role, amount):
    prop=blank();d=ImageDraw.Draw(prop);_,sleeve,cuff,accent=STYLE[role]
    line(d,[(24,25),(40,27),(50,64)],'#655777',6)
    line(d,[(24,25),(40,27),(50,64)],'#f9cfca',3.2)
    polygon(d,[(23,35),(80,35),(72,76),(32,76)],'#507a9d')
    polygon(d,[(27,37),(76,37),(69,72),(35,72)],'#d6f4f4')
    level=45+14*amount
    polygon(d,[(31,level),(73,level-1),(68,71),(35,71)],'#65cbd9')
    line(d,[(27,36),(77,36)],'#fcffff',4)
    if role=='fish':
        ellipse(d,(44,53,59,62),accent);polygon(d,[(45,57),(38,52),(38,62)],accent)
    else:
        polygon(d,[(51,50),(54,55),(60,58),(54,60),(51,66),(49,60),(43,58),(49,55)],accent)
    line(d,[(33,42),(36,53)],'#ffffff',2.7)
    mitten(d,76,60,sleeve,cuff)
    return prop


def sweet(role, unwrap, eaten):
    prop=blank();d=ImageDraw.Draw(prop);_,sleeve,cuff,accent=STYLE[role]
    spread=12*unwrap
    if unwrap<.98:
        for direction in (-1,1):
            def point(x,y):
                return (50+direction*(x+spread),y)
            polygon(d,[point(9,47),point(34,31),point(29,43),point(35,56),point(12,55)],'#805574')
            polygon(d,[point(10,48),point(31,35),point(26,44),point(31,52),point(12,52)],'#ffdb91')
    if not eaten:
        ellipse(d,(31,31,67,64),accent,'#834756',2.4)
        d.arc(xy((39,36,60,57)),190,500,fill='#fff3d9',width=round(3.2*WORK/100))
        ellipse(d,(38,37,45,43),'#fff9e7')
    mitten(d,56,67,sleeve,cuff)
    return prop


def hair_spring(base, role, t):
    # Bend only the tuft; its roots, the eyes and the silhouette below the bangs stay fixed.
    cx={'fish':36.,'gpt':44.}[role]
    swing=(7*arc(t,.04,.35)-10*arc(t,.26,.60)+5*arc(t,.52,.78)-2*arc(t,.74,.95))
    def source(x,y):
        u,v=x*100/WORK,y*100/WORK
        weight=max(0.,1-((u-cx)/23)**2)*(1-ease(v,4,18))
        return (x-swing*weight*WORK/100, y-abs(swing)*.45*weight*WORK/100)
    result=base.copy()
    # Only the upper strip needs to be resampled.
    stop=round(.19*WORK)
    mesh=[]
    for y in range(0,stop,6):
        b=min(y+6,stop)
        for x in range(0,WORK,8):
            r=min(x+8,WORK)
            mesh.append(((x,y,r,b),tuple(v for pt in ((x,y),(x,b),(r,b),(r,y)) for v in source(*pt))))
    strip=base.transform((WORK,stop),Image.Transform.MESH,mesh,BICUBIC)
    result.paste(strip,(0,0));return result


def performed(role, action):
    base=Image.open(PETS/role/'avatar.png').convert('RGBA').resize((WORK,WORK),FILTER)
    mx,my=STYLE[role][0]
    mirror=mirror_glass(role,base) if action=='mirror' else None
    frames=[]
    for i in range(21):
        t=i/20
        if action=='hair':
            im=hair_spring(base,role,t)
        else:
            raised=ease(t,.04,.35)*(1-ease(t,.75,.99))
            tilt={'mirror':-5.,'sip':3.,'candy':-3.}[action]*raised
            im=base.rotate(tilt,BICUBIC,center=xy((50,78)))
            if action=='mirror':
                stamp(im,mirror,(82+2*raised,123-66*raised),48,12-22*raised+8*arc(t,.44,.72))
            elif action=='sip':
                sip=ease(t,.39,.70);size=52
                stamp(im,tumbler(role,sip),(mx+.26*size+3*(1-raised),122-(122-my-.25*size)*raised),
                      size,5*arc(t,.38,.69))
            else:
                # Tug the wrapper apart at the chest, lift to the lips, then lower an empty hand.
                unwrap=ease(t,.26,.48);bite=ease(t,.46,.67);lower=ease(t,.72,.98)
                center=(67+(mx-67)*bite,123-37*raised+(my-86)*bite*(1-lower))
                shake=5*math.sin(7*math.pi*t)*arc(t,.15,.48)
                stamp(im,sweet(role,unwrap,t>=.70),center,48,shake)
                blush=arc(t,.62,.93)
                if blush>.04:
                    pink=blank();d=ImageDraw.Draw(pink)
                    for x in (mx-15,mx+16):ellipse(d,(x-4,my-3,x+3,my),'#ff93a2')
                    pink.putalpha(pink.getchannel('A').point(lambda a:round(a*.68*blush)));im.alpha_composite(pink)
        frames.append(sheet_frame(im))
    frames[0]=frames[-1]=sheet_frame(base)
    return frames


# Clip positions are in sixths of a second. Each shot includes the pickup,
# the main pose and a return to the registered resting portrait.
def build(role):
    rows=[]
    def movie(name,path,positions,wide,dx=0,dy=0):
        rows.append((name,clip(role,path,positions,wide,dx,dy)))
    def prop(name,action):
        rows.append((name,performed(role,action)))
    if role=='claude':
        movie('捧杯喝茶','clips/C01.webm',(0,4,7,10,13,16,19,22,25,28,31,34,37,40,42),.14,0,-1)
        movie('揉眼打哈欠','clips/B02.webm',(0,6,10,13,16,18,20,22,24,26,28,30,32,35,39,46,55),.14,1,-2)
        movie('翻小书','clips/C02.webm',(0,4,7,10,12,14,17,20,24,27,29,31,33,36,41,46),.25,0,-3)
        prop('掏出小手镜','mirror');prop('偷吃橘子糖','candy')
        movie('伸个懒腰','clips/B05.webm',(0,1,2,3,4,6,8,10,11,12,13,14,16,18,20,21,22),.25,0,-5)
    elif role=='fish':
        def fish(name,file,positions,wide,dy=0):
            movie(name,'assets/pets/fish/'+file+'.webm',positions,wide,0,dy)
        fish('翻看小本子','idle',(0,4,7,10,13,16,19,22,25,29,34,39,44,50,55,59),.30,-2)
        fish('用袖口揉眼','react',(0,3,6,8,10,12,15,18,22,25,28,30,33,37,42,49,59),.14,-1)
        prop('呆毛弹跳','hair');prop('偷喝凉水','sip');prop('拆开汽水糖','candy')
        fish('笑出小爱心','happy',(0,5,9,13,17,19,22,25,27,29,31,33,35,38,42,50,59),.42,-20)
    elif role=='gpt':
        def dragon(name,file,positions,wide,dy=0):
            movie(name,'assets/pets/gpt/actions/'+file+'.webm',positions,wide,0,dy)
        dragon('偷舔冰淇淋','吃冰淇淋融化',(0,4,7,10,14,18,22,26,30,34,37,40,44,48,52,56,59),.14)
        dragon('张嘴伸懒腰','超大伸懒腰',(0,8,13,16,20,23,26,29,32,35,38,41,44,48,53,59),.16,-4)
        dragon('闭眼吹小笛','吹笛子',(0,6,10,14,17,20,23,26,29,32,35,39,44,48,54,59),.16,-1)
        prop('小呆毛跳跳','hair');prop('对镜看龙角','mirror');prop('悄悄喝水','sip')
    else:
        def cat(name,file,positions,wide,dy=0):
            movie(name,'assets/pets/gemini/actions/'+file+'.webm',positions,wide,0,dy)
        cat('嘟嘴吹泡泡','嘟嘴吹泡泡特效',(0,6,11,15,18,21,24,26,28,30,32,35,38,42,47,54,59),.47,-10)
        cat('低头拧魔方','原地专心玩魔方',(0,5,9,13,16,18,20,23,26,29,32,35,39,43,48,52,59),.22,0)
        cat('伸腰竖猫耳','超大伸懒腰',(0,6,12,16,20,24,26,29,32,36,40,44,48,52,59),.21,-4)
        cat('害羞眨眼','点击回应-害羞惊讶',(0,4,7,10,13,16,19,23,26,30,34,38,43,48,53,59),.13)
        prop('照照星星镜','mirror');prop('偷吃莓果糖','candy')
    return rows


def main():
    manifest={}
    for role in CAMERA:
        rows=build(role)
        sheet=Image.new('RGBA',(CELL*max(len(f) for _,f in rows),CELL*len(rows)))
        moves=[]
        for y,(name,frames) in enumerate(rows):
            holds=[110]*len(frames);holds[0]=65;holds[-1]=75
            holds[len(frames)//2]=230
            for x,frame in enumerate(frames):sheet.paste(frame,(x*CELL,y*CELL))
            moves.append({'name':name,'holdMs':holds})
        sheet.save(PETS/role/'daily.png',optimize=True)
        manifest[role]={'file':'daily.png','tile':CELL,'moves':moves}
    (PETS/'dailies.json').write_text(json.dumps(manifest,ensure_ascii=False,separators=(',',':'))+'\n')


if __name__=='__main__':
    main()
