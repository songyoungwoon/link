"""Generate AFTERLIGHT: original geometry, materials and offline daylight bakes.
Authoring only: Python 3.10+ and numpy. LINK itself needs neither dependency.
Original geometry and bakes; generated oak albedo only, no room-image backdrop.
"""
from __future__ import annotations
import argparse, hashlib, json, math, pathlib, struct, zlib
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
SUN = np.array([-.78, .56, -.25], dtype=np.float32)
SUN /= np.linalg.norm(SUN)
OCCLUDERS = []
ELLIPSOIDS = []
OAK = None
COUNT = 0
ASSETS = {}
OUT = None

def lit(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))

def fields(values):
    return "\n".join("    " + k + ": " + lit(v) + ";" for k,v in values.items())

def node(kind, identifier, values, children=()):
    global COUNT
    COUNT += 1
    identifier=identifier.replace("-","neg").replace(".","p")
    return "  " + kind + " " + identifier + " {\n" + fields(values) + "\n" + "\n".join(children) + "\n  }"

def element(identifier, geometry, position, style="plaster", **kw):
    return node("element",identifier,dict(geometry=geometry,position=position,style=style,**kw))

def box(identifier, position, size, style, rotation=None, **kw):
    if rotation is not None: kw["rotation"] = rotation
    return element(identifier,dict(type="box",size=size),position,style,**kw)

def cylinder(identifier, position, radius, height, style, top=None, **kw):
    return element(identifier,dict(type="cylinder",radius=radius,radiusTop=radius if top is None else top,height=height),position,style,**kw)

def sphere(identifier, position, radius, style, **kw):
    return element(identifier,dict(type="sphere",radius=radius),position,style,**kw)

def rotation(angles):
    x,y,z = np.radians(angles)
    rx=np.array([[1,0,0],[0,np.cos(x),-np.sin(x)],[0,np.sin(x),np.cos(x)]])
    ry=np.array([[np.cos(y),0,np.sin(y)],[0,1,0],[-np.sin(y),0,np.cos(y)]])
    rz=np.array([[np.cos(z),-np.sin(z),0],[np.sin(z),np.cos(z),0],[0,0,1]])
    return (rx@ry@rz).astype(np.float32)

def png(path, rgba):
    h,w=rgba.shape[:2]
    def chunk(name,data):
        return struct.pack(">I",len(data))+name+data+struct.pack(">I",zlib.crc32(name+data)&0xffffffff)
    raw=b"".join(b"\0"+row.tobytes() for row in rgba.astype(np.uint8))
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",w,h,8,6,0,0,0))+chunk(b"IDAT",zlib.compress(raw,7))+chunk(b"IEND",b""))

def read_png(path):
    data=path.read_bytes(); assert data[:8]==b"\x89PNG\r\n\x1a\n"
    pos=8; compressed=b""
    while pos<len(data):
        length=struct.unpack(">I",data[pos:pos+4])[0]
        kind=data[pos+4:pos+8]; body=data[pos+8:pos+8+length]; pos+=12+length
        if kind==b"IHDR": w,h,bits,color,_,_,interlaced=struct.unpack(">IIBBBBB",body)
        if kind==b"IDAT": compressed+=body
        if kind==b"IEND": break
    assert bits==8 and color in (2,6) and not interlaced
    channels=3 if color==2 else 4; stride=w*channels
    raw=zlib.decompress(compressed); output=np.zeros((h,stride),np.uint8)
    for y in range(h):
        at=y*(stride+1); mode=raw[at]; source=raw[at+1:at+1+stride]
        previous=output[y-1] if y else np.zeros(stride,np.uint8)
        if mode==0: output[y]=np.frombuffer(source,np.uint8)
        elif mode==1:
            for channel in range(channels): output[y,channel::channels]=np.cumsum(np.frombuffer(source,np.uint8)[channel::channels]).astype(np.uint8)
        elif mode==2: output[y]=(np.frombuffer(source,np.uint8).astype(np.uint16)+previous).astype(np.uint8)
        else:
            row=bytearray(stride)
            for i in range(stride):
                left=row[i-channels] if i>=channels else 0; up=int(previous[i]); corner=int(previous[i-channels]) if i>=channels else 0
                if mode==3: predictor=(left+up)//2
                elif mode==4:
                    value=left+up-corner; a=abs(value-left); b=abs(value-up); d=abs(value-corner)
                    predictor=left if a<=b and a<=d else up if b<=d else corner
                else: raise ValueError("Unsupported PNG filter")
                row[i]=(source[i]+predictor)&255
            output[y]=np.frombuffer(row,np.uint8)
    return output.reshape(h,w,channels)[...,:3].astype(np.float32)/255

def grain(a,b):
    return np.sin(a*127.1+b*311.7)*43758.5453 % 1

def smoothstep(a,b,x):
    q=np.clip((x-a)/(b-a),0,1)
    return q*q*(3-2*q)

def surface_color(p,material,u,v):
    x,y,z=p[...,0],p[...,1],p[...,2]
    fine=grain(u*911.7,v*1273.2)-.5
    broad=np.sin(x*2.3+z*1.7)*np.sin(y*3.4-z*.7)
    if material in ("oak","floor"):
        a,b=(x,z) if material=="floor" else (u,v)
        board=np.floor((x+2.7)/.15) if material=="floor" else np.zeros_like(a)
        tx=((a*.63+grain(board,13)*.71)%1*(OAK.shape[1]-1)).astype(np.int32)
        ty=((b*.63+board*.113)%1*(OAK.shape[0]-1)).astype(np.int32)
        rgb=OAK[ty,tx].copy()
        rgb+=(fine*.006)[...,None]
        if material=="floor":
            board=np.floor((x+2.7)/.15)
            ends=np.floor((z+2.3+(board%3)*.43)/1.37)
            rgb+=(grain(board,ends)-.5)[...,None]*.075
            edge=(x+2.7)% .15
            joint=(1-smoothstep(.0007,.0023,edge))+smoothstep(.1477,.1493,edge)
            end=(z+2.3+(board%3)*.43)%1.37
            joint=np.maximum(joint,(1-smoothstep(.0006,.002,end)))
            rgb*=1-joint[...,None]*.27
        return np.clip(rgb,0,1)
    if material in ("linen","rug","paper"):
        base={"linen":[.51,.53,.405],"rug":[.72,.675,.56],"paper":[.89,.864,.79]}[material]
        weave=np.sin(u*1450)*np.sin(v*1400)*.013
        if material=="rug":
            weave+=np.sin((u+v)*335)*np.sin(u*26)*.015
        return np.clip(np.array(base)+(weave+fine*.018+broad*.006)[...,None],0,1)
    if material=="cover":
        rgb=np.broadcast_to(np.array([.20,.29,.265]),p.shape).copy()
        rgb+=(fine*.018)[...,None]
        trim=((abs(u)>.105)&(abs(u)<.109))|((abs(v)>.155)&(abs(v)<.159))
        rgb[trim]=[.62,.53,.35]
        for k in range(4):
            stripe=(abs(v-(-.03+k*.017))<.0012)&(abs(u)<(.052-.007*k))
            rgb[stripe]=[.66,.59,.43]
        return rgb
    rgb=np.broadcast_to(np.array([.82,.795,.73]),p.shape).copy()
    rgb+=(fine*.009+broad*.012+np.sin(x*28+z*11)*.002)[...,None]
    if material=="outside": rgb*=1.06
    return np.clip(rgb,0,1)

def ray_blocked(p,d,maximum,skip):
    blocked=np.zeros(p.shape[:-1],bool)
    for identifier,lo,hi in OCCLUDERS:
        if identifier==skip: continue
        t0=np.full(p.shape[:-1],-1e8,np.float32)
        t1=np.full(p.shape[:-1],1e8,np.float32)
        for axis in range(3):
            if abs(d[axis])<1e-5:
                t1=np.where((p[...,axis]<lo[axis])|(p[...,axis]>hi[axis]),-1,t1)
            else:
                a=(lo[axis]-p[...,axis])/d[axis]
                b=(hi[axis]-p[...,axis])/d[axis]
                t0=np.maximum(t0,np.minimum(a,b))
                t1=np.minimum(t1,np.maximum(a,b))
        blocked|=(t1>=np.maximum(t0,.003))&(t0<maximum)
    for center,radius in ELLIPSOIDS:
        relative=(p-center)/radius; direction=d/radius
        a=np.dot(direction,direction); b=2*np.sum(relative*direction,axis=-1); cc=np.sum(relative*relative,axis=-1)-1
        discriminant=b*b-4*a*cc
        t=(-b-np.sqrt(np.maximum(discriminant,0)))/(2*a)
        blocked|=(discriminant>=0)&(t>.003)&(t<maximum)
    return blocked

def daylight(p,normal,skip):
    n=np.array(normal,np.float32)
    center=np.array([-2.7,1.78,-.32],np.float32)-p
    dist=np.maximum(np.linalg.norm(center,axis=-1),.1)
    cosine=np.maximum(np.sum(center*n,axis=-1)/dist,0)
    sky=cosine*np.exp(-dist/7)
    ambient=np.broadcast_to(np.array([.41,.43,.46]),p.shape).copy()
    ambient+=sky[...,None]*np.array([.60,.65,.70])
    ambient+=(np.maximum(n[1],0)*.10)
    visibility=np.zeros(p.shape[:-1],np.float32)
    if np.dot(n,SUN)>0:
        for dx,dz in ((0,0),(.006,.004),(-.006,-.004)):
            d=SUN+np.array([dx,0,dz],np.float32)
            t=(-2.70-p[...,0])/d[0]
            wy=p[...,1]+d[1]*t
            wz=p[...,2]+d[2]*t
            passed=(t>0)&(wy>.90)&(wy<2.65)&(wz>-1.85)&(wz<1.20)
            passed&=~ray_blocked(p+n*.004,d,t+.05,skip)
            # The vertical and horizontal oak mullions cast actual geometric shadows.
            visibility+=passed.astype(np.float32)/3
        ambient+=visibility[...,None]*max(float(np.dot(n,SUN)),0)*np.array([1.85,1.53,1.10])
    ao=np.ones(p.shape[:-1],np.float32)
    for identifier,lo,hi in OCCLUDERS:
        if identifier==skip: continue
        delta=np.maximum(np.maximum(lo-p,p-hi),0)
        distance=np.linalg.norm(delta,axis=-1)
        ao*=1-.15*np.exp(-distance/.075)
    for center,radius in ELLIPSOIDS:
        distance=np.maximum(np.linalg.norm((p-center)/radius,axis=-1)-1,0)*min(radius)
        ao*=1-.19*np.exp(-distance/.035)
    # Room-corner and wall/floor contact, independent of camera.
    edge=np.minimum(2.7-abs(p[...,0]),2.3-abs(p[...,2]))
    if n[1]>.5: ao*=.77+.23*smoothstep(0,.20,edge)
    if abs(n[1])<.5: ao*=.86+.14*smoothstep(0,.16,p[...,1])
    return ambient*ao[...,None]

def bake(identifier,center,size,angles,material,skip="",density=290):
    r=rotation(angles)
    w=min(2048,max(32,int(size[0]*density)))
    h=min(2048,max(32,int(size[1]*density)))
    # Preserve density/aspect if either edge is capped.
    scale=min(1,2048/max(size[0]*density,size[1]*density))
    w=max(32,int(size[0]*density*scale)); h=max(32,int(size[1]*density*scale))
    rgba=np.empty((h,w,4),np.uint8)
    uu=(np.arange(w,dtype=np.float32)+.5)/w*size[0]-size[0]/2
    for row in range(0,h,96):
        vv=size[1]/2-(np.arange(row,min(h,row+96),dtype=np.float32)+.5)/h*size[1]
        u,v=np.broadcast_arrays(uu[None,:],vv[:,None])
        p=np.array(center,np.float32)+u[...,None]*r[:,0]+v[...,None]*r[:,1]
        color=surface_color(p,material,u,v)
        light=daylight(p,r[:,2],skip) if material!="outside" else np.ones_like(p)*1.02
        linear=np.power(color,2.2)*light
        rgb=np.power(1-np.exp(-linear*1.45),1/2.2)
        rgba[row:row+len(vv),:,:3]=np.uint8(np.clip(rgb*255,0,255))
        rgba[row:row+len(vv),:,3]=255
    name="assets/"+identifier+".png"
    png(OUT/name,rgba)
    ASSETS[name]={"width":w,"height":h}
    return name

def plane(identifier,center,size,angles=(0,0,0),material="plaster",skip="",local=None,density=290,**kw):
    authored_opacity=kw.pop('opacity',None)
    image=bake(identifier,center,size,angles,material,skip,density)
    return element(identifier,dict(type="plane",size=size),local if local is not None else center,"sheer" if authored_opacity is not None else "baked",rotation=list(angles),image=image,classes=["surface"],**kw)

def occluder(identifier,center,size):
    c=np.array(center); s=np.array(size)/2
    OCCLUDERS.append((identifier,c-s,c+s))

def tbox(identifier,center,size,material="oak",shadow=True,density=300):
    if shadow and not any(x[0]==identifier for x in OCCLUDERS): occluder(identifier,center,size)
    w,h,d=size; c=np.array(center)
    specs=[("front",[0,0,d/2],[w,h],[0,0,0]),
           ("back",[0,0,-d/2],[w,h],[0,180,0]),
           ("right",[w/2,0,0],[d,h],[0,90,0]),
           ("left",[-w/2,0,0],[d,h],[0,-90,0]),
           ("top",[0,h/2,0],[w,d],[-90,0,0])]
    faces=[]
    if material!="linen":
        for suffix,offset,extent,angles in specs:
            faces.append(plane(identifier+"_"+suffix,(c+offset).tolist(),extent,angles,material,identifier,local=offset,density=density))
    if material=="linen":
        # Soft 35 mm rolled edges: true cylindrical and spherical geometry.
        radius=.035; faces=[]
        specs=[("front",[0,0,d/2],[w-2*radius,h-2*radius],[0,0,0]),
               ("back",[0,0,-d/2],[w-2*radius,h-2*radius],[0,180,0]),
               ("right",[w/2,0,0],[d-2*radius,h-2*radius],[0,90,0]),
               ("left",[-w/2,0,0],[d-2*radius,h-2*radius],[0,-90,0]),
               ("top",[0,h/2,0],[w-2*radius,d-2*radius],[-90,0,0])]
        for suffix,offset,extent,angles in specs:
            faces.append(plane(identifier+"_"+suffix,(c+offset).tolist(),extent,angles,material,identifier,local=offset,density=density))
        faces.append(box(identifier+"_core",[0,0,0],[w-2*radius,h-2*radius,d-2*radius],"linen"))
        for sx in (-1,1):
            for sy in (-1,1):
                faces.append(cylinder(identifier+"_long_"+str(sx)+"_"+str(sy),[sx*(w/2-radius),sy*(h/2-radius),0],radius,d-2*radius,"linen",rotation=[90,0,0]))
        for sz in (-1,1):
            for sy in (-1,1):
                faces.append(cylinder(identifier+"_end_"+str(sz)+"_"+str(sy),[0,sy*(h/2-radius),sz*(d/2-radius)],radius,w-2*radius,"linen",rotation=[0,0,90]))
                for sx in (-1,1):
                    faces.append(sphere(identifier+"_corner_"+str(sx)+"_"+str(sy)+"_"+str(sz),[sx*(w/2-radius),sy*(h/2-radius),sz*(d/2-radius)],radius,"linen"))
    return node("component",identifier,dict(position=center),faces)

def build_world():
    architecture=[]; furniture=[]; details=[]
    # Build the complete shadow scene before any textures are evaluated.
    for ident,c,s in [
        ("bench",[-2.29,.26,-.35],[.65,.50,2.80]),
        ("cushion",[-2.26,.585,-.35],[.69,.16,2.70]),
        ("table",[-1.25,.685,-.42],[.92,.045,1.02]),
        ("cabinet",[.75,.51,-2.02],[2.80,.78,.42]),
        ("sideboard",[2.34,.47,-.10],[.48,.88,1.65]),
        ("window_vertical",[-2.67,1.775,-.325],[.12,1.75,.055]),
        ("window_cross",[-2.67,1.82,-.325],[.12,.04,3.05]),
    ]: occluder(ident,c,s)
    for x in (-1.59,-.91):
        for z in (-.80,-.04):
            occluder("leg_"+str(x)+str(z),[x,.335,z],[.055,.67,.055])
    occluder("reading_book",[-.92,.723,-.16],[.264,.03,.366])
    occluder("tea_cup_shadow",[-1.32,.773,-.67],[.10,.082,.10])
    occluder("art_shadow",[.60,1.88,-2.279],[.88,1.09,.025])
    ELLIPSOIDS.extend([(np.array([-1.47,.782,-.30]),np.array([.097,.0747,.097])),
                       (np.array([-1.34,.720,-.68]),np.array([.20,.0125,.14]))])
    shadow_rng=np.random.default_rng(42); shadow_x=-.46
    for i in range(13):
        width=float(shadow_rng.uniform(.028,.065)); height=float(shadow_rng.uniform(.18,.26))
        shadow_rng.uniform(-2,2)
        occluder("book_shadow_"+str(i),[shadow_x+width/2,.90+height/2,-2.015],[width,height,.17])
        shadow_x+=width+.008
    architecture.append(plane("oak_floor",[0,0,0],[5.4,4.6],[-90,0,0],"floor",density=380))
    architecture.append(plane("back_plaster",[0,1.475,-2.3],[5.4,2.95],material="plaster",density=330))
    architecture.append(plane("right_plaster",[2.7,1.475,0],[4.6,2.95],[0,-90,0],density=330))
    architecture.append(plane("front_plaster",[0,1.475,2.3],[5.4,2.95],[0,180,0],density=240))
    architecture.append(plane("ceiling",[0,2.95,0],[5.4,4.6],[90,0,0],density=220))
    for ident,c,s in [
        ("window_apron",[-2.7,.45,-.325],[3.05,.90]),
        ("window_header",[-2.7,2.80,-.325],[3.05,.30]),
        ("window_left_pier",[-2.7,1.475,-2.075],[.45,2.95]),
        ("window_right_pier",[-2.7,1.475,1.75],[1.10,2.95])]:
        architecture.append(plane(ident,c,s,[0,90,0],density=300))
    # Dark, slim skirting and deliberate shadow reveals.
    architecture+= [
        box("back_skirting",[0,.035,-2.285],[5.4,.07,.018],"skirting"),
        box("right_skirting",[2.685,.035,0],[.018,.07,4.6],"skirting"),
        box("ceiling_reveal",[0,2.925,-2.292],[5.4,.012,.012],"reveal")]
    furniture.append(tbox("bench",[-2.29,.26,-.35],[.65,.50,2.8]))
    furniture.append(tbox("cushion",[-2.26,.585,-.35],[.69,.16,2.7],"linen",density=370))
    furniture.append(tbox("table",[-1.25,.685,-.42],[.92,.045,1.02],density=600))
    for i,(x,z) in enumerate(( (x,z) for x in (-1.59,-.91) for z in (-.80,-.04))):
        furniture.append(box("table_leg_"+str(i),[x,.335,z],[.055,.67,.055],"oak",rotation=[0,0,3 if x< -1.2 else -3]))
    furniture.append(tbox("cabinet",[.75,.51,-2.02],[2.8,.78,.42],density=390))
    furniture.append(tbox("sideboard",[2.34,.47,-.1],[.48,.88,1.65],density=380))
    furniture.append(box("cabinet_plinth",[.75,.06,-2.02],[2.66,.12,.33],"dark_oak"))
    furniture.append(box("sideboard_plinth",[2.36,.015,-.10],[.40,.03,1.55],"dark_oak"))
    for i,x in enumerate((-.17,.75,1.67)):
        furniture.append(box("cabinet_reveal_"+str(i),[x,.49,-1.801],[.006,.70,.004],"reveal"))
        furniture.append(box("cabinet_pull_"+str(i),[x+.20,.68,-1.787],[.15,.012,.025],"brass"))
    for i,z in enumerate((-.62,.20)):
        furniture.append(box("sideboard_gap_"+str(i),[2.095,.46,z],[.005,.74,.005],"reveal"))
    # Original woven rug with light and contact shadows in the same world space.
    furniture.append(plane("woven_rug",[.45,.007,.48],[2.1,2.45],[-90,0,0],"rug",density=470))
    for i in range(20):
        x=-.55+i*.105
        details.append(box("rug_fringe_"+str(i),[x,.009,1.73],[.008,.004,.07],"rug"))
    # Window casing and actual reveal depth. Courtyard geometry is beyond the opening.
    for ident,c,s in [
        ("window_sill",[-2.58,.895,-.325],[.35,.055,3.25]),
        ("window_head",[-2.63,2.675,-.325],[.18,.055,3.20]),
        ("window_jamb_a",[-2.64,1.785,-1.875],[.17,1.83,.055]),
        ("window_jamb_b",[-2.64,1.785,1.225],[.17,1.83,.055])]:
        furniture.append(tbox(ident,c,s,density=350))
    details.append(box("window_vertical",[-2.67,1.775,-.325],[.12,1.75,.055],"window_oak"))
    details.append(box("window_cross",[-2.67,1.82,-.325],[.12,.04,3.05],"window_oak"))
    architecture.append(plane("courtyard_wall",[-5.5,1.50,-.2],[7.2,3.2],[0,90,0],"outside",density=140))
    architecture.append(plane("courtyard_ground",[-4.1,-.14,-.2],[3.0,7.2],[-90,0,0],"outside",density=140))
    for i in range(6):
        details.append(box("courtyard_joint_"+str(i),[-5.494,.30+i*.46,-.2],[.008,.008,7.0],"outside_joint"))
    # An olive branch is actual geometry with parallax, rather than a window photograph.
    details.append(cylinder("courtyard_pot",[-4.42,.13,.18],.28,.54,"terracotta",top=.39))
    details.append(cylinder("courtyard_soil",[-4.42,.406,.18],.35,.015,"soil"))
    details.append(cylinder("olive_trunk",[-4.42,1.05,.18],.027,1.32,"bark",top=.015,rotation=[0,0,-9]))
    for j in range(4):
        cx=-4.42+(j-1.5)*.16; cy=1.28+j*.19; cz=.18+(-1)**j*.19
        details.append(cylinder("olive_branch_"+str(j),[cx,cy,cz],.009,.72,"bark",top=.003,rotation=[28+j*8,0,(-1)**j*39]))
        for k in range(7):
            t=(k-3)*.12
            details.append(sphere("olive_leaf_"+str(j)+"_"+str(k),[cx+t,cy+.21+abs(t)*.24,cz+math.sin(k*2)*.15],1,"olive",scale=[.028,.012,.11],rotation=[k*23,k*47,j*18]))
    # Back-wall original tactile composition. No borrowed artwork or fonts.
    art=np.zeros((640,512,4),np.uint8)
    yy,xx=np.mgrid[0:640,0:512]
    art[...,:3]=np.uint8(np.clip(np.array([208,199,179])+(grain(xx,yy)-.5)[...,None]*6,0,255))
    ellipse=((xx-257)/132)**2+((yy-302)/177)**2
    ring=(ellipse>.83)&(ellipse<1.05)&(xx>155+np.sin(yy*.01)*25)
    art[ring,:3]=[62,69,60]; art[...,3]=255
    png(OUT/"assets/ink-study.png",art); ASSETS["assets/ink-study.png"]={"width":512,"height":640}
    details.append(element("ink_study",dict(type="plane",size=[.83,1.04]),[.60,1.88,-2.284],"baked",image="assets/ink-study.png",classes=["surface"]))
    details.append(element("art_frame",dict(type="frame",size=[.88,1.09],border=.022,depth=.025),[.60,1.88,-2.281],"oak"))
    # Reading objects have real volume, lids, lips, pages and small intentional imperfections.
    rng=np.random.default_rng(42)
    bx=-.46
    for i in range(13):
        width=float(rng.uniform(.028,.065)); height=float(rng.uniform(.18,.26))
        details.append(box("shelf_book_"+str(i),[bx+width/2,.90+height/2,-2.015],[width,height,.17],["book_olive","book_clay","paper","book_charcoal"][i%4],rotation=[0,0,float(rng.uniform(-2,2))]))
        details.append(box("book_spine_rule_"+str(i),[bx+width/2,.935,-1.927],[width*.55,.002,.002],"paper"))
        bx+=width+.008
    details.append(cylinder("cabinet_bowl",[1.62,.955,-2.02],.15,.11,"ceramic",top=.20))
    details.append(cylinder("bowl_interior",[1.62,1.012,-2.02],.18,.007,"ceramic_inside"))
    details.append(sphere("bowl_fruit",[1.60,1.04,-2.01],.062,"ochre",scale=[1,.85,1]))
    details.append(cylinder("tea_tray",[-1.34,.72,-.68],.20,.025,"dark_oak",scale=[1,1,.7]))
    for ident,c in (("tea_cup",[-1.32,.773,-.67]),):
        details.append(cylinder(ident,c,.047,.082,"ceramic",top=.055))
        details.append(cylinder("tea_surface",[c[0],c[1]+.042,c[2]],.046,.002,"tea"))
        details.append(element("cup_handle",dict(type="frame",size=[.070,.065],border=.012,depth=.014),[c[0]+.062,c[1]+.005,c[2]],"ceramic"))
    details.append(sphere("teapot",[-1.47,.782,-.30],.097,"ceramic",scale=[1,.77,1]))
    details.append(cylinder("teapot_lid",[-1.47,.855,-.30],.065,.012,"ceramic"))
    details.append(sphere("teapot_knob",[-1.47,.873,-.30],.016,"dark_oak"))
    details.append(cylinder("teapot_spout",[-1.37,.81,-.30],.031,.15,"ceramic",top=.015,rotation=[0,0,-58]))
    details.append(element("teapot_handle",dict(type="frame",size=[.10,.12],border=.018,depth=.015),[-1.58,.80,-.30],"ceramic"))
    # A readable physical book; the cover rotates at the spine, not through its centre.
    bookcenter=np.array([-1.05,.736,-.16])
    details.append(box("book_pages",[-.92,.721,-.16],[.252,.022,.35],"paper"))
    details.append(plane("book_print",[-.92,.734,-.16],[.25,.345],[-90,0,0],"paper",density=1400))
    cover=[
        box("book_cover_edge",[.13,0,0],[.264,.007,.366],"book_olive"),
        plane("book_cover_top",(bookcenter+np.array([.13,.004,0])).tolist(),[.264,.366],[-90,0,0],"cover",local=[.13,.004,0],density=1500)]
    book=node("component","book_hinge",dict(position=bookcenter.tolist(),state={"isOpen":False},script="scripts/book.script"),cover)
    # Small table lamp, switched by clicking its base. Its local warm pool is pre-baked.
    lampx,lampz=2.31,-.52
    glow=np.zeros((512,384,4),np.uint8); gy,gx=np.mgrid[-1:1:512j,-1:1:384j]
    glow[...,:3]=[255,187,103]; glow[...,3]=np.uint8(np.exp(-gx*gx*3-gy*gy*2.7)*68)
    png(OUT/"assets/lamp-pool.png",glow); ASSETS["assets/lamp-pool.png"]={"width":384,"height":512}
    pool=element("lamp_pool",dict(type="plane",size=[1.30,1.55]),[2.695,1.50,lampz],"lamp_pool",rotation=[0,-90,0],image="assets/lamp-pool.png",classes=["surface"],visible=False)
    details.append(pool)
    lamp=[
        cylinder("lamp_toggle",[lampx,.926,lampz],.13,.032,"brass"),
        cylinder("lamp_stem",[lampx,1.132,lampz],.012,.39,"brass"),
        cylinder("lamp_shade",[lampx,1.43,lampz],.23,.27,"lamp_linen",top=.14),
        sphere("lamp_bulb",[lampx,1.305,lampz],.043,"bulb"),
        cylinder("lamp_shade_interior",[lampx,1.293,lampz],.203,.002,"lamp_inside")]
    lamp=node("component","lamp_control",dict(state={"isOn":False},script="scripts/lamp.script"),lamp)
    # Curtain pleats are depth-varying geometry, with only a 6 mm breathing movement.
    curtain=[]
    for i in range(13):
        z=.80+i*.031; x=-2.43+math.sin(i*1.35)*.024
        curtain.append(plane("curtain_pleat_"+str(i),[x,1.78,z],[.042,1.85],[0,90,0],"paper",local=[x,1.78,z],density=300,opacity=.84))
    breeze=node("component","curtain_breeze",dict(position=[0,0,0],script="scripts/breeze.script"),curtain)
    scene="scene {\n"+node("object","architecture",{},[node("component","envelope",{},architecture)])+"\n"+node("object","furniture",{},[node("component","joinery",{},furniture)])+"\n"+node("object","still_life",{},[node("component","objects",{},details+[book,lamp,breeze])])+"\n}\n"
    (OUT/"scene.element").write_text(scene,encoding="utf-8",newline="\n")
    (OUT/"scripts").mkdir(exist_ok=True)
    (OUT/"scripts/book.script").write_text("""on onStart(ctx) { store angle = 0; ctx.setState({isOpen: false}); }
on onUpdate(ctx, deltaSeconds) {
  let goal = 0;
  if (ctx.getState().isOpen) { goal = 135; }
  angle = angle + clamp(goal - angle, -deltaSeconds * 150, deltaSeconds * 150);
  ctx.setTransform({rotation: [0,0,angle]});
}
on onInteract(ctx, event) { ctx.setState({isOpen: !ctx.getState().isOpen}); }
on onDestroy(ctx) { ctx.log("AFTERLIGHT book released"); }
""",encoding="utf-8",newline="\n")
    (OUT/"scripts/lamp.script").write_text("""on onStart(ctx) { ctx.setState({isOn: false}); }
on onUpdate(ctx, deltaSeconds) { }
on onInteract(ctx, event) {
  let enabled = !ctx.getState().isOn;
  ctx.setState({isOn: enabled});
  ctx.setTransform("lamp_pool", {visible: enabled});
  if (enabled) { ctx.setStyle("lamp_bulb", {emission: 1.6}); }
  else { ctx.clearStyle("lamp_bulb"); }
}
on onDestroy(ctx) { ctx.log("AFTERLIGHT lamp released"); }
""",encoding="utf-8",newline="\n")
    (OUT/"scripts/breeze.script").write_text("""on onStart(ctx) { store phase = 0; }
on onUpdate(ctx, deltaSeconds) {
  phase = (phase + deltaSeconds * .23) % 2;
  let wave = phase;
  if (phase > 1) { wave = 2 - phase; }
  ctx.setTransform({position: [wave * .006,0,0]});
}
on onDestroy(ctx) { ctx.log("AFTERLIGHT curtain released"); }
""",encoding="utf-8",newline="\n")
    styles={
        "plaster":("#D4CBBB",.95,0),"linen":("#949780",1,0),"baked":("#FFFFFF",1,0),
        "oak":("#9D7950",.64,0),"window_oak":("#B69A72",.75,0),
        "dark_oak":("#534535",.7,0),"skirting":("#6C5D49",.8,0),
        "reveal":("#453F35",1,0),"rug":("#AFA58E",1,0),
        "brass":("#AA9167",.39,.55),"terracotta":("#99785D",.9,0),
        "soil":("#3B3B2D",1,0),"bark":("#81775A",1,0),
        "olive":("#788269",.9,0),"outside_joint":("#B3B09E",1,0),
        "book_olive":("#596459",.9,0),"book_clay":("#A17E66",.9,0),
        "paper":("#DED7C3",.95,0),"book_charcoal":("#484D46",.9,0),
        "ceramic":("#D4CBB5",.30,0),"ceramic_inside":("#9F9A85",.5,0),
        "tea":("#514432",.18,0),"ochre":("#AE9255",.62,0),
        "bulb":("#F6DDA9",.5,0),"lamp_inside":("#615C4B",1,0),"lamp_linen":("#C5B599",1,0)}
    text="defaults { color: \"#D5CEBE\"; roughness: .85; }\n"
    for name,(color,rough,metal) in styles.items():
        text+="material "+name+" { color: "+lit(color)+"; roughness: "+str(rough)+"; metalness: "+str(metal)+"; }\n"
    text+='material lamp_pool { color: "#FFFFFF"; opacity: .999; }\n'
    text+='material sheer { color: "#FFFFFF"; opacity: .84; }\n'
    (OUT/"appearance.style").write_text(text,encoding="utf-8",newline="\n")
    manifest=dict(version="0.1",id="afterlight",name="AFTERLIGHT · 오후의 방",elements="scene.element",style="appearance.style",scripts=["scripts/book.script","scripts/lamp.script","scripts/breeze.script"],entry={"position":[1.48,1.62,1.65],"target":[-.95,1.22,-.75]},environment={"background":"#D8DFDC","grid":False},walk={"min":[-.52,0,-1.42],"max":[1.70,3,1.87],"eyeHeight":1.62,"speed":1.35},views=[{"name":"Afternoon","position":[1.48,1.62,1.65],"target":[-.95,1.22,-.75]},{"name":"Reading","position":[-.35,1.62,.32],"target":[-1.30,.78,-.38]},{"name":"Daylight","position":[1.20,1.62,-1.12],"target":[-2.35,1.5,-.35]}])
    (OUT/"world.link").write_text("world {\n"+fields(manifest)+"\n}\n",encoding="utf-8",newline="\n")
    metadata={"world":"afterlight","nodes":COUNT,"images":len(ASSETS),"assets":ASSETS,"technique":"Generated oak albedo and procedural surfaces; static daylight, occlusion and contact-shadow bake on actual 3D planes; native 3D objects and Script interactions. No live global illumination or photographic backdrop.","generator":"scripts/generate_afterlight.py"}
    (OUT/"bake-info.json").write_text(json.dumps(metadata,indent=2)+"\n",encoding="utf-8",newline="\n")
    assert COUNT<=500 and len(ASSETS)<=128
    total=sum(p.stat().st_size for p in OUT.rglob("*") if p.is_file())
    print(f"AFTERLIGHT: {COUNT} nodes, {len(ASSETS)} images, {total/1048576:.2f} MiB written to {OUT}",flush=True)

def main():
    global OUT, OAK
    OAK=read_png(ROOT/'scripts'/'materials'/'afterlight-oak.png')
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=pathlib.Path,default=ROOT/"native"/"worlds"/"afterlight")
    args=parser.parse_args()
    OUT=args.output.resolve()
    OUT.mkdir(parents=True,exist_ok=True)
    build_world()

if __name__=="__main__": main()
