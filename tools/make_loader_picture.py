"""Codex: новая сцена, оригинальные логотип и полоса в одном JPEG 1024×768.
Codex: единая раскладка создаёт координаты SPG и ELF; дополнительные текстуры не нужны."""
from pathlib import Path
import io,json,struct,sys
from PIL import Image,ImageOps
AGG=Path('E:/zx/HMM2/Assets/Original/DATA/HEROES2.AGG')
PAGE0=0x40  # Codex: FastOpen займёт страницы таблицей DAT уже после распаковки картинки.
PAGE=16384
ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'tools/splash'


def layout():
    spec=json.loads((ASSETS/'layout.json').read_text(encoding='utf-8'))
    x0,y0,x1,y1=spec['bar'];ix,iy=spec['inset']
    assert spec['canvas']==[1024,768] and 0<=x0<x1<=1024 and 0<=y0<y1<=768
    constants=dict(LOADER_X=x0+ix,LOADER_TOP=y0+iy,LOADER_BOTTOM=y1-iy,
                   LOADER_FILL=x1-x0-2*ix,LOADER_MASK_ALPHA=spec['mask_alpha'])
    return spec,constants


def repeat_tile(canvas,tile,box):
    x0,y0,x1,y1=box
    for y in range(y0,y1,tile.height):
        for x in range(x0,x1,tile.width):
            canvas.paste(tile.crop((0,0,min(tile.width,x1-x),min(tile.height,y1-y))),(x,y))


def scale_lettering(image,size,mode):
    # Codex: целочисленное увеличение сохраняет форму исходного пикселя, Lanczos затем точно подгоняет заданный пользователем размер.
    if mode=='nearest':return image.resize(size,Image.Resampling.NEAREST)
    if mode=='pixel_lanczos':image=image.resize((image.width*3,image.height*3),Image.Resampling.NEAREST)
    return image.resize(size,Image.Resampling.LANCZOS)


def compose(scene=None, palette_mode=None, lettering_filter=None):
    spec,_=layout()
    # Codex: сцена проходит через исходное разрешение игры 640×480: убираем лишнее зерно генератора, сохраняя качество JPEG 85.
    image=Image.open(ASSETS/(scene or spec['scene'])).convert('RGB').resize((640,480),Image.Resampling.LANCZOS).resize(tuple(spec['canvas']),Image.Resampling.LANCZOS)
    # Codex: сохраняем положение рыцаря из одобренного превью; верх ранее сдвинутой сцены продолжается зеркальным фрагментом того же мрамора.
    offset=spec.get('scene_offset_y',0)
    if offset:
        shifted=Image.new('RGB',image.size);shifted.paste(ImageOps.flip(image.crop((0,0,image.width,offset))),(0,0));shifted.paste(image,(0,offset));image=shifted
    mode=lettering_filter or spec['logo_filter']
    logo=Image.open(ASSETS/'logo_editor_original.png').convert('RGBA')
    lx,ly,lw,lh=spec['logo'];logo=scale_lettering(logo,(lw,lh),mode)
    image.paste(logo,(lx,ly),logo)
    # Codex: подзаголовок независим от верхнего логотипа, P уже выровнена по нижней линии остальных букв.
    subtitle=Image.open(ASSETS/'subtitle_campaign_aligned.png').convert('RGBA')
    sx,sy,sw,sh=spec['subtitle'];subtitle=scale_lettering(subtitle,(sw,sh),mode)
    image.paste(subtitle,(sx,sy),subtitle)
    # Codex: исходная надпись ZX EVOLUTION из DOOM располагается сразу под верхним логотипом; чужой красный фон удалён маской.
    zx=Image.open(ASSETS/'zx_evolution_original.png').convert('RGBA')
    zx_x,zx_y,zx_w,zx_h=spec['zx_evolution'];zx=zx.resize((zx_w,zx_h),Image.Resampling.LANCZOS)
    image.paste(zx,(zx_x,zx_y),zx)
    x0,y0,x1,y1=spec['bar'];px,py=spec['panel_border']
    w,h=x1-x0+2*px,y1-y0+2*py
    stone=Image.open(ASSETS/'stone_original.png').convert('RGB');panel=Image.new('RGB',(w,h))
    # Codex: тёмный камень взят из внутренней части SURDRBKE; рамку задаёт золотой SYSTEM, без повторения черепов и самоцветов.
    repeat_tile(panel,stone,(0,0,w,h))
    image.paste(panel,(x0-px,y0-py))
    # Codex: завитки VIEWARMY лежат вне затемняемой области; на обоих концах шкалы они всегда целые.
    ornament=Image.open(ASSETS/'bar_ornament_original.png').convert('RGB').resize((18,h),Image.Resampling.LANCZOS)
    image.paste(ornament,(x0-18,y0-py));image.paste(ImageOps.mirror(ornament),(x1,y0-py))
    # Codex: SYSTEM, кадр 0: золотые торцы сохраняют ширину; удлиняется только середина готовой полосы.
    bar=Image.open(ASSETS/'bar_original.png').convert('RGB');cap=spec['inset'][0]
    for crop,x,width in [((0,0,cap,bar.height),x0,cap),((cap,0,bar.width-cap,bar.height),x0+cap,x1-x0-2*cap),((bar.width-cap,0,bar.width,bar.height),x1-cap,cap)]:
        image.paste(bar.crop(crop).resize((width,y1-y0),Image.Resampling.NEAREST),(x,y0))
    mode=palette_mode or spec.get('palette','fullcolor')
    if mode=='kb':
        # Codex: фиксированная палитра игры с диффузией ошибки сохраняется отдельным вариантом для сравнения тумана.
        palette=Image.new('P',(1,1));palette.putpalette(bytes(min(255,v*4) for v in (ASSETS/'KB.PAL').read_bytes()))
        image=image.quantize(palette=palette,dither=Image.Dither.FLOYDSTEINBERG)
    return image

def agg_file(raw,name):
    count=struct.unpack_from('<H',raw)[0];names=len(raw)-count*15
    for i in range(count):
        if raw[names+i*15:names+(i+1)*15].split(b'\0')[0].decode('ascii').upper()==name:
            _,offset,size=struct.unpack_from('<3I',raw,2+i*12);return raw[offset:offset+size]
    raise SystemExit('нет в AGG: '+name)

def icn_frame(icn,index,palette):
    _,_,w,h,_,offset=struct.unpack_from('<hhHHBI',icn,6+index*13)
    data=icn[6+offset:];pixels=bytearray(w*h);x=y=at=0
    while at<len(data) and y<h:
        c=data[at];at+=1
        if c==0:y+=1;x=0
        elif c<0x80:pixels[y*w+x:y*w+x+c]=data[at:at+c];at+=c;x+=c
        elif c==0x80:break
        elif c<0xc0:x+=c-0x80
        elif c==0xc0:
            n=data[at];at+=1
            if n&3:x+=n&3
            else:x+=data[at];at+=1
        elif c==0xc1:n=data[at];pixels[y*w+x:y*w+x+n]=bytes([data[at+1]])*n;at+=2;x+=n
        else:n=c-0xc0;pixels[y*w+x:y*w+x+n]=bytes([data[at]])*n;at+=1;x+=n
    image=Image.frombytes('P',(w,h),bytes(pixels));image.putpalette(bytes(min(255,v*4) for v in palette));return image.convert('RGB')

def build(out):
    image=compose();spec,constants=layout()
    # Codex: оптимизированные таблицы Хаффмана сокращают размер без изменения качества и baseline-формата.
    buffer=io.BytesIO();image.convert('RGB').save(buffer,'JPEG',quality=85,subsampling=2,optimize=True,progressive=False);jpg=buffer.getvalue()
    if len(jpg)>16*PAGE:raise SystemExit(f'Codex: JPEG превышает 16 страниц: {len(jpg)}')
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    for old in out.glob('ldrpic_*.bin'):old.unlink()
    data=jpg+bytes(-len(jpg)%4);pages=(len(data)+PAGE-1)//PAGE;blocks=[]
    for k in range(pages):
        p=out/f'ldrpic_{k:02d}.bin';p.write_bytes(data[k*PAGE:(k+1)*PAGE]);blocks.append((PAGE0+k,p))
    (out/'ldrpic.inc').write_text('; Codex: координаты из tools/splash/layout.json; не редактировать вручную.\n'+f'LDRPIC_PAGE0 EQU #{PAGE0:02X}\nLDRPIC_PAGES EQU {pages}\nLDRPIC_LAST EQU {len(data)-(pages-1)*PAGE}\n'+''.join(f'{k} EQU {v}\n' for k,v in constants.items()),encoding='utf-8')
    header='// Codex: координаты из tools/splash/layout.json; создаёт tools/make_loader_picture.py.\n'+''.join(f'constexpr unsigned {k}={v};\n' for k,v in constants.items())
    target=ROOT/'main/hmm_splash.inc'
    if not target.exists() or target.read_text(encoding='utf-8')!=header:target.write_text(header,encoding='utf-8')
    (out/'loader.jpg').write_bytes(jpg)
    (out/'loader_layout.json').write_text(json.dumps(dict(spec=spec,constants=constants,jpeg_bytes=len(jpg),pages=pages),indent=2)+'\n',encoding='utf-8')
    image.save(out/'loader_preview.png');print('картинка загрузки:',len(jpg),'байт JPEG, страниц',pages);return blocks
if __name__=='__main__':build(sys.argv[1])
