"""Codex: воспроизводимый платформенный вариант; эталонная копия исходников не изменяется."""
from pathlib import Path
import re,os
from compile_probe import ROOT,SRC
BASE=Path(os.environ.get('HMM2_STAGE1_BASE','E:/zx/cache/tmp/hmm2_esp/stage1'))
STAGED=BASE/'src'

def replace(s,a,b,count=-1):
    assert a in s,a[:150]
    return s.replace(a,b,count)

def prepare():
    for p in SRC.rglob('*'):
        if not p.is_file() or p.suffix not in ('.h','.cpp','.c'):continue
        rel=p.relative_to(SRC);s=p.read_text(encoding='utf-8')
        if 'std::time(' in s or 'std::time (' in s:
            s='#include "hmm_clock.h"\n'+re.sub(r'std::time\s*\(', 'hmm_time(', s)
        if p.name in ('monster_info.cpp','artifact_info.cpp'):
            from prepare_stage1_stack import transform as stack_transform
            s=stack_transform(p.name,s)
        if p.name=='map_object_info.cpp':
            from build_stage1_object_table import transform
            s=transform(s)
        if p.name=='battle_arena.cpp':
            s=replace(s,'namespace\n{','''#ifdef HMM2_PACKED
#include "hmm_battle_cache.inc"
#endif
namespace
{''',1)
            s=replace(s,'        _interface->fullRedraw();','''        _interface->fullRedraw();
#ifdef HMM2_PACKED
        // Codex: рабочий набор ожидания загружается до первого хода, без изменения анимаций.
        hmm_warm_battle(*_attackingArmy,*_defendingArmy);
#endif''',1)
        if p.name=='logging.h':
            begin=s.index('enum\n');enumend=s.index('#if defined( TARGET_NINTENDO_SWITCH )',begin)
            ns=s.index('namespace Logging\n{\n    const char *')
            nsend=s.index('#if defined( _WIN32 ) && defined( WITH_DEBUG )',ns)
            tail=s[s.index('namespace Logging\n{\n    // This structure'):]
            s=s[:s.index('#pragma once')]+'#pragma once\n#include "hmm_text.h"\n#include "hmm_platform.h"\n'+s[begin:enumend]+s[ns:nsend]+'''
#define COUT(x) {}
#define VERBOSE_LOG(x) {}
#define ERROR_LOG(x) { HmmText _hmm_log; _hmm_log << x; hmm_log(_hmm_log.str().c_str()); }
#define DEBUG_LOG(x,y,z)
#define IS_DEVEL() false
bool IS_DEBUG(int,int);
'''+tail
        if p.name=='dialog_recruit.cpp':
            signature='Troop Dialog::RecruitMonster( const Monster & monster0, const uint32_t available, const bool allowDowngradedMonster, const int32_t windowOffsetY )\n{'
            s='#include "hmm_platform.h"\n#ifdef HMM2_HOST\nvoid hmm_test_recruit_enter();void hmm_test_recruit_leave();\n#endif\n'+s
            s=replace(s,signature,signature+'\n    hmm_log("HMM2: recruit begin");struct RecruitTrace {~RecruitTrace(){hmm_log("HMM2: recruit end");}}recruitTrace;\n#ifdef HMM2_HOST\n    ::hmm_test_recruit_enter();struct RecruitTestLeave {~RecruitTestLeave(){::hmm_test_recruit_leave();}}recruitTestLeave;\n#endif')
        if p.name=='system.h':
            s=s.replace('#include <filesystem>','')
            s=re.sub(r'    std::string fsPathToString\([^\n]+\n','',s)
        if p.name=='timing.h':
            from patch_timing import patch as patch_timing
            s=patch_timing(s)
        if p.name=='tools.h':
            s=s.replace('        const std::locale currentGlobalLocale;','        // Codex: английский интерфейс использует сравнение в исходной C-локали.')
            s=s.replace('std::tolower( *li, currentGlobalLocale )','static_cast<CharType>(std::tolower(static_cast<unsigned char>(*li)))')
            s=s.replace('std::tolower( *ri, currentGlobalLocale )','static_cast<CharType>(std::tolower(static_cast<unsigned char>(*ri)))')
        if p.name=='tinyconfig.cpp':
            s=s[:s.index('fheroes2::Point TinyConfig::PointParams')]+(ROOT/'main/hmm_config_tail.cpp').read_text(encoding='utf-8')
            s=s.replace('#include <regex>','')
        if p.name=='image.h':
            s=s.replace('        uint8_t * transform()', '        virtual uint8_t * transform()').replace('        const uint8_t * transform() const','        virtual const uint8_t * transform() const')
            s=s.replace('return _singleLayer ? nullptr : _data.get() + width() * height();','return _singleLayer ? nullptr : image() + width() * height();')
            s=s.replace('return !_data;','return _width == 0 || _height == 0;')
            s=replace(s,'    private:\n        void copy( const Image & image );','''    protected:
        // Codex: размеры неизменяемого кадра; пиксели предоставляет производный кэш.
        void _hmmSetDimensions(int32_t w,int32_t h,bool single){_width=w;_height=h;_singleLayer=single;}
    private:
        void copy( const Image & image );''')
        if p.name=='image.cpp':
            s=s.replace('if ( !image._data ) {','if ( image.empty() ) {')
            s=s.replace('memcpy( _data.get(), image._data.get(),','memcpy( _data.get(), image.image(),')
            s=replace(s,'memcpy( _data.get(), image.image(), _singleLayer ? imageSize : imageSize * 2 );',
                'memcpy(_data.get(),image.image(),imageSize);\n        if(!_singleLayer)memcpy(_data.get()+imageSize,image.transform(),imageSize);')
            s=replace(s,'        _singleLayer = image._singleLayer;','        const bool layerChanged = _singleLayer != image._singleLayer;\n        _singleLayer = image._singleLayer;')
            s=replace(s,'if ( image._width != _width || image._height != _height ) {','if ( layerChanged || image._width != _width || image._height != _height ) {')
            for name,flip in [('Blit','flip'),('Copy','false')]:
                start=s.index('    void '+name+'( const Image & in, int32_t inX')
                at=s.index('        const int32_t widthIn = in.width();',start)
                s=s[:at]+'#ifdef HMM2_PACKED\n        hmm_gpu_offer(in,inX,inY,out,outX,outY,width,height,'+flip+','+('true' if name=='Copy' else 'false')+');\n#endif\n'+s[at:]
            start=s.index('    void ApplyRawPalette(')
            at=s.index('        const int32_t widthIn = in.width();',start)
            s=s[:at]+'#ifdef HMM2_PACKED\n        hmm_gpu_offer(in,inX,inY,out,outX,outY,width,height,false,false,palette);\n#endif\n'+s[at:]
            s=replace(s,'    void Copy( const Image & in, Image & out )\n    {','''    void Copy( const Image & in, Image & out )
    {
#ifdef HMM2_PACKED
        hmm_gpu_offer(in,0,0,out,0,0,in.width(),in.height(),false,true);
#endif''')
            # Codex: остальные изменяющие примитивы помечают точную область CPU;
            # неизвестный прямой доступ Display::image помечается отдельно.
            primitive=r'(^    (?:[\w:<>,*& ]+\s+)?([\w:]+)\(([^;{}]*)\)(?: const)?(?: noexcept)?\n    \{)'
            def touch(m):
                name,args=m[2],m[3]
                # Клод: addGradientShadowForArea (тень окна) сама точек не трогает — только зовёт ApplyTransform, а тот сам решает: заливка
                # FT812 или своя пометка участка. Пометка всей функции была на весь экран (у неё нет x/y/width/height) и оставалась висеть:
                # следующая запись точек без своей пометки растеризовала бы весь экран и разворачивала карту под окном.
                if name in ('Blit','Copy','ApplyRawPalette','ApplyPalette','ApplyAlpha','addGradientShadow','addGradientShadowForArea') or '::' in name:return m[0]
                target=None
                for arg in args.split(','):
                    found=re.search(r'\bImage & (out|image|output)\b',arg)
                    if found and 'const' not in arg:target=found[1];break
                if not target:return m[0]
                # Клод: преобразование и узор перехода рамки окна — слоями FT812 (hmm_gpu_fill с маской 2–9, hmmDitherLayer) до пометки
                # участка CPU: они больше не читают и не пишут точки Display, а значит, не разворачивают карту под окном.
                # Таблицы 0 и 1 (всё в чёрный) — прежним путём: маска 1 у заливки значит «прозрачно».
                if name=='ApplyTransform':
                    return m[0]+'\n#ifdef HMM2_PACKED\n        if(transformId>=2&&hmm_gpu_fill(image,x,y,width,height,0,transformId))return;\n        hmm_gpu_touch(image,x,y,width,height);\n#endif'
                if name=='CreateDitheringTransition':
                    return m[0]+'\n#ifdef HMM2_PACKED\n        if(hmmDitherLayer(in,inX,inY,out,outX,outY,width,height,isVertical,isReverse))return;\n        hmm_gpu_touch(out,outX,outY,width,height);\n#endif'
                has=lambda n: bool(re.search(r'\b'+n+r'\b',args))
                if all(has(n) for n in ['outX','outY','width','height']):roi='outX,outY,width,height'
                elif all(has(n) for n in ['x','y','width','height']):roi='x,y,width,height'
                elif has('roi'):roi='roi.x,roi.y,roi.width,roi.height'
                elif name in ('SetPixel','SetTransformPixel') and has('x'):roi='x,y,1,1'
                else:roi='0,0,'+target+'.width(),'+target+'.height()'
                if name=='AlphaBlit':
                    # Клод: короткие AlphaBlit только передают вызов полному: пометка всего изображения там была лишней.
                    # Полный сначала предлагает слой FT812 с COLOR_A (hmm_gpu_alpha) и лишь при отказе метит участок для CPU.
                    if roi!='outX,outY,width,height':return m[0]
                    return m[0]+'\n#ifdef HMM2_PACKED\n        if(alphaValue!=0&&alphaValue!=255){if(hmm_gpu_alpha(in,inX,inY,out,outX,outY,width,height,alphaValue,flip))return;hmm_gpu_touch(out,outX,outY,width,height);}\n#endif'
                return m[0]+'\n#ifdef HMM2_PACKED\n        hmm_gpu_touch('+target+','+roi+');\n#endif'
            s=re.sub(primitive,touch,s,flags=re.M)
            # Codex: исходный пиксельный алгоритм тени исполняется на малом участке.
            # Полный буфер Display (300 КиБ) не помещался в непрерывный блок GateHell.
            a=s.index('    void addGradientShadow(');b=s.index('    void addGradientShadowForArea(',a)
            shadow=s[a:b]
            # Клод: тень кнопки на экране — слоем FT812 (hmmShadowLayer), фон под ней процессор не растеризует.
            marker='    void addGradientShadow( const Sprite & in, Image & out, const Point & outPos, const Point & shadowOffset )\n    {\n'
            shadow=replace(shadow,marker,marker+'#ifdef HMM2_PACKED\n        if(hmmShadowLayer(in,out,outPos,shadowOffset))return;\n#endif\n')
            marker='        const int32_t maxY = inHeight + absOffsetY;'
            shadow=replace(shadow,marker,marker+"""
#ifdef HMM2_PACKED
        const int32_t hmm_x=outStartOffset%outWidth,hmm_y=outStartOffset/outWidth;
        /* Клод: участок — только у однослойного out (экран, 300 КиБ). Двухслойный out алгоритм обрабатывает на месте, как оригинал:
         * участок ровно во весь out копировался полным Copy, а тот у двух двухслойных картинок ставит приёмнику преобразование 0
         * (ветка оригинала с assert(in.singleLayer())). Кусок тени кнопки (hmmShadowLayer) становился непрозрачным цветом 0 —
         * у FT812 индекс 0 прозрачен, и тень больших кнопок окна файла пропадала (сборка 91, снята). */
        const bool hmm_region=hmm_gpu_active()&&out.singleLayer()&&hmm_x+maxX<=outWidth&&hmm_y+maxY<=out.height();
        Image hmm_shadow;
        if(hmm_region){
            if(out.singleLayer())hmm_shadow._disableTransformLayer();
            hmm_shadow.resize(maxX,maxY);Copy(out,hmm_x,hmm_y,hmm_shadow,0,0,maxX,maxY);
            hmm_gpu_touch(hmm_shadow,0,0,maxX,maxY);
        }else hmm_gpu_touch(out,hmm_x,hmm_y,maxX,maxY);
        const int32_t hmm_stride=hmm_region?maxX:outWidth;
#else
        const int32_t hmm_stride=outWidth;
#endif""")
            shadow=replace(shadow,'uint8_t * transformOut = out.singleLayer() ? nullptr : ( out.transform() + outStartOffset );',"""uint8_t * transformOut = out.singleLayer() ? nullptr : (
#ifdef HMM2_PACKED
            hmm_region ? hmm_shadow.transform() :
#endif
            out.transform() + outStartOffset );""")
            shadow=replace(shadow,'uint8_t * imageOut = out.image() + outStartOffset;',"""uint8_t * imageOut =
#ifdef HMM2_PACKED
            hmm_region ? hmm_shadow.image() :
#endif
            out.image() + outStartOffset;""")
            shadow=replace(shadow,'const int32_t outOffset = x + y * outWidth;','const int32_t outOffset = x + y * hmm_stride;')
            at=shadow.rindex('    }')
            shadow=shadow[:at]+"""#ifdef HMM2_PACKED
        if(hmm_region)Copy(hmm_shadow,0,0,out,hmm_x,hmm_y,maxX,maxY);
#endif
"""+shadow[at:]
            s=s[:a]+shadow+s[b:]
        if p.name=='image.cpp':
            # Codex: неподдержанные FT преобразования исполняются исходным CPU-кодом
            # полосами по 16 строк; Display::image не требует полного блока 300 КиБ.
            helper="""
#ifdef HMM2_PACKED
    template<class DrawStrip> bool hmmCpuStrips(const Image &in,Image &out,int32_t x,int32_t y,int32_t w,int32_t h,const DrawStrip &draw){
        if(!hmm_gpu_active()||&in==&out||(out.height()<=16&&out.width()==w&&!x&&!y))return false;
        for(int32_t row=0;row<h;row+=16){
            const int32_t rows=std::min<int32_t>(16,h-row);Image part;
            if(out.singleLayer())part._disableTransformLayer();
            part.resize(w,rows);Copy(out,x,y+row,part,0,0,w,rows);
            draw(part,row,rows);Copy(part,0,0,out,x,y+row,w,rows);
        }
        hmm_gpu_touch(out,0,0,0,0);return true;
    }
    /* Клод: узор перехода рамки окна (CreateDitheringTransition) — слоем FT812, без чтения и записи точек Display. Исходный алгоритм
     * только пишет точки узора поверх out; он исполняется дважды в пустые буферы (0 и 255) — точка, одинаковая в обоих, записана.
     * Из них двухслойный кусок (остальные точки прозрачны) уходит в out обычным Blit, то есть слоем GPU. Прежде запись в Display
     * разворачивала всю карту под окном и растеризовала её спрайты процессором (окно загрузки на Alteris — десятки секунд). */
    /* Клод: кэш готовых узоров рамки (снимков FT812) по спрайту, участку и направлению: копии вертикальной рамки окна и повторное
     * открытие того же окна берут готовый снимок, без чтения спрайта рамки (WINLOSE, SURDRBKG) с SD. Наименее нужный вытесняется. */
    struct HmmDither {uint32_t key=0;int32_t x=0,y=0,w=0,h=0;uint8_t flags=0;uint32_t used=0;Image image;};
    HmmDither hmmDither[24];uint32_t hmmDitherClock;
    bool hmmDitherLayer(const Image &in,int32_t inX,int32_t inY,Image &out,int32_t outX,int32_t outY,int32_t width,int32_t height,const bool isVertical,const bool isReverse){
        /* Клод: источник — сам out (фон окна размножается из себя, renderBackgroundImage): прежним путём CPU. На экране 640×480
         * этого не бывает: STONEBAK 640×480, копий фона внутри окна не требуется. */
        if(&in==&out||!hmm_gpu_layer_target(out))return false;
        if(!Verify(in,inX,inY,out,outX,outY,width,height))return true;
        const size_t area=static_cast<size_t>(width)*static_cast<size_t>(height);
        uint32_t key=0;const uint8_t flags=uint8_t(1|(isVertical?2:0)|(isReverse?4:0));HmmDither *slot=nullptr;
        if(hmm_resource_id(in,key)){
            for(auto &e:hmmDither)if(e.key==key&&e.flags==flags&&e.x==inX&&e.y==inY&&e.w==width&&e.h==height&&hmm_gpu_tracked(e.image)){
                e.used=++hmmDitherClock;Blit(e.image,0,0,out,outX,outY,width,height);return true;}
            slot=&hmmDither[0];for(auto &e:hmmDither)if(e.used<slot->used)slot=&e;
            slot->image.clear();slot->key=key;slot->flags=flags;slot->x=inX;slot->y=inY;slot->w=width;slot->h=height;slot->used=++hmmDitherClock;
        }
        Image buffer,local;buffer._disableTransformLayer();Image &layer=slot?slot->image:local;
        const bool saved=hmm_gpu_cpu_begin();
        buffer.resize(width,height);layer.resize(width,height);
        uint8_t *a=buffer.image(),*pixels=layer.image(),*transform=layer.transform();
        for(int pass=0;pass<2;++pass){
            std::memset(a,pass?255:0,area);
            CreateDitheringTransition(in,inX,inY,buffer,0,0,width,height,isVertical,isReverse);
            if(!pass)std::memcpy(pixels,a,area);
        }
        for(size_t i=0;i<area;++i)transform[i]=pixels[i]==a[i]?0:1;
        hmm_gpu_cpu_end(saved);
        Blit(layer,0,0,out,outX,outY,width,height);
        // Клод: снимок в кэше живёт в списке слоёв; свой буфер точек картинке больше не нужен.
        if(slot)layer._hmmRelease();
        return true;
    }
    /* Клод: тень кнопки (addGradientShadow) на экране — слоем FT812. Сила тени в точке (таблицы 2–5) зависит только от маски
     * кнопки, а на однослойном экране тень — это преобразование фона. Исходный алгоритм считает её в пустой двухслойный кусок
     * (точки тени получают преобразование 2–5, остальные прозрачны), а кусок уходит в out обычным Blit: FT812 рисует эти точки
     * чёрным с альфой, как тени спрайтов. Прежде тень снимала участок экрана в hmm_shadow и растеризовала его процессором —
     * с крупным фоном окна, который распаковывался с SD с начала потока, на каждую кнопку окна.
     * Клод: кусок — пустой двухслойный (reset: точки 0, преобразование 1), как пустые куски оригинала (makeShadow); тень в нём
     * считается на месте (hmm_region в addGradientShadow — только для однослойного out). */
    bool hmmShadowLayer(const Sprite &in,Image &out,const Point &outPos,const Point &shadowOffset){
        if(!hmm_gpu_layer_target(out)||in.empty()||(shadowOffset.x==0&&shadowOffset.y==0)||outPos.x<0||outPos.y<0)return false;
        const int32_t lx=std::max<int32_t>(0,-std::min<int32_t>(shadowOffset.x,0)-in.x()),ly=std::max<int32_t>(0,-std::min<int32_t>(shadowOffset.y,0)-in.y());
        const int32_t sx=lx+std::min<int32_t>(shadowOffset.x,0)+in.x(),sy=ly+std::min<int32_t>(shadowOffset.y,0)+in.y();
        const int32_t w=sx+in.width()+std::abs(shadowOffset.x),h=sy+in.height()+std::abs(shadowOffset.y);
        if(w<=0||h<=0)return true;
        Image layer;
        const bool saved=hmm_gpu_cpu_begin();
        layer.resize(w,h);layer.reset();
        addGradientShadow(in,layer,Point(lx,ly),shadowOffset);
        hmm_gpu_cpu_end(saved);
        Blit(layer,0,0,out,outPos.x-lx,outPos.y-ly,w,h);
        return true;
    }
    /* Клод: тень книги заклинаний (spellBookRedrawLists → addShadow → makeShadow) — без больших буферов. Оригинал читает маску всей
     * книги и пишет тень в новую двухслойную картинку: на плате это два куска по 320–350 КБ одним блоком (книга 484×360 в два слоя —
     * 348 480 байт). Крупные запросы берут одну область (хвост арены спрайтов), её занимал растр книги, и второй кусок в раздробленной
     * PSRAM не нашёлся: bad_alloc, «Game main failed» (сборки 94 и 95, 04.11, щелчок по книге в инвентаре героя).
     * Теперь выход — слои: прозрачная заливка, затем тень клетками 32×32 (каждая — маленький двухслойный кусок, посчитанный исходным
     * правилом: точка тени там, где маска книги, сдвинутая на смещение тени, равна 0), и только в клетках, где хоть одна точка тени не
     * закрыта непрозрачной точкой самой книги (её addShadow кладёт сверху). Маска книги читается полосами: на каждую полосу выхода
     * в 32 строки растеризуется только полоса книги (Blit слоями в пустую полосу; 468×48 в два слоя — 45 КБ), а не вся книга. */
    bool hmmMakeShadow(const Sprite &in,const Point &shadowOffset,const uint8_t transformId,Sprite &out){
        if(!hmm_gpu_active()||in.singleLayer())return false;
#ifdef HMM2_HOST
        // Клод: на ПК — прежний путь для сверки кадров.
        if(std::getenv("HMM2_NO_HMM_SHADOW"))return false;
#endif
        const int32_t width=in.width(),height=in.height(),dx=-shadowOffset.x,dy=shadowOffset.y;
        out=Sprite(width+dx,height+dy,in.x()+shadowOffset.x,in.y());out.reset();
        const int32_t widthOut=out.width(),heightOut=out.height();
        for(int32_t ty=0;ty<heightOut;ty+=32){
            const int32_t th=std::min<int32_t>(32,heightOut-ty);
            // Строки маски для полосы: тень — ty-dy…ty+th-1-dy, закрытие самой книгой — ty…ty+th-1.
            const int32_t r0=std::max<int32_t>(0,ty-dy),r1=std::min<int32_t>(height,ty+th);
            if(r1<=r0)continue;
            Image band;band.resize(width,r1-r0);band.reset();
            Blit(in,0,r0,band,0,0,width,r1-r0);
            const uint8_t *mask=band.transform();
            auto opaque=[&](int32_t x,int32_t y){return y>=r0&&y<r1&&x>=0&&x<width&&mask[(y-r0)*width+x]==0;};
            auto shadow=[&](int32_t x,int32_t y){return opaque(x,y-dy);};
            auto covered=[&](int32_t x,int32_t y){return opaque(x-dx,y);};
            for(int32_t tx=0;tx<widthOut;tx+=32){
                const int32_t tw=std::min<int32_t>(32,widthOut-tx);
                bool visible=false;
                for(int32_t y=ty;y<ty+th&&!visible;++y)for(int32_t x=tx;x<tx+tw;++x)if(shadow(x,y)&&!covered(x,y)){visible=true;break;}
                if(!visible)continue;
                Image tile;
                const bool saved=hmm_gpu_cpu_begin();
                tile.resize(tw,th);tile.reset();
                uint8_t *transform=tile.transform();
                for(int32_t y=0;y<th;++y)for(int32_t x=0;x<tw;++x)if(shadow(tx+x,ty+y))transform[y*tw+x]=transformId;
                hmm_gpu_cpu_end(saved);
                Blit(tile,0,0,out,tx,ty,tw,th);
            }
        }
        return true;
    }
#endif
"""
            pos=s.index('    void addGradientShadow(');s=s[:pos]+helper+s[pos:]
            # Клод: makeShadow (тень книги заклинаний) — слоями FT812 клетками, без второго большого буфера (hmmMakeShadow).
            marker='        // Shadow has (-x, +y) offset.\n        Sprite out( width - shadowOffset.x'
            s=replace(s,marker,'#ifdef HMM2_PACKED\n        {Sprite hmm_out;if(hmmMakeShadow(in,shadowOffset,transformId,hmm_out))return hmm_out;}\n#endif\n'+marker)
            offer='        hmm_gpu_offer(in,inX,inY,out,outX,outY,width,height,flip,false);'
            s=replace(s,offer,offer+'\n        if(hmmCpuStrips(in,out,outX,outY,width,height,[&](Image &part,int32_t row,int32_t rows){Blit(in,inX,inY+row,part,0,0,width,rows,flip);}))return;')
            a=s.index('    void AlphaBlit( const Image & in, int32_t inX');b=s.index('        const int32_t widthIn = in.width();',a)
            s=s[:b]+"""#ifdef HMM2_PACKED
        if(hmmCpuStrips(in,out,outX,outY,width,height,[&](Image &part,int32_t row,int32_t rows){AlphaBlit(in,inX,inY+row,part,0,0,width,rows,alphaValue,flip);}))return;
#endif
"""+s[b:]
        if p.name=='ui_castle.cpp':
            s=replace(s,'''        const fheroes2::Sprite & image
            = isImagePlayerColorDependent( icnId ) ? getModifiedByColorImage( icnId, icnIndex, castle.GetColor() ) : AGG::GetICN( icnId, icnIndex );''','''#ifdef HMM2_PACKED
        // Codex: условный оператор создавал копию даже неизменённого ICN.
        // Цветные флаги обрабатываем неперекрывающимися полосами с исходной альфа.
        const fheroes2::Sprite & image = AGG::GetICN(icnId,icnIndex);
        if(isImagePlayerColorDependent(icnId)){
            const auto regions=getColorEffectiveAreas(icnId,icnIndex);
            const auto palette=getModifiedPaletteByPlayerColor(originalCastleFlagStartColorId,originalCastleFlagColorLength,castle.GetColor());
            for(int32_t y=0;y<image.height();y+=32){
                fheroes2::Image strip;if(image.singleLayer())strip._disableTransformLayer();
                strip.resize(image.width(),std::min<int32_t>(32,image.height()-y));
                Copy(image,0,y,strip,0,0,strip.width(),strip.height());
                for(const auto &region:regions){const auto r=region^fheroes2::Rect(0,y,strip.width(),strip.height());
                    ApplyPalette(strip,r.x,r.y-y,strip,r.x,r.y-y,r.width,r.height,palette);
                }
                const fheroes2::Rect sourceRoi(offset.x+image.x(),offset.y+image.y()+y,strip.width(),strip.height());
                const auto r=renderArea^sourceRoi;
                AlphaBlit(strip,r.x-sourceRoi.x,r.y-sourceRoi.y,Display::instance(),r.x,r.y,r.width,r.height,alpha);
            }
            return;
        }
#else
        const fheroes2::Sprite & image
            = isImagePlayerColorDependent( icnId ) ? getModifiedByColorImage( icnId, icnIndex, castle.GetColor() ) : AGG::GetICN( icnId, icnIndex );
#endif''')
        if p.name=='heroes_indicator.cpp':
            # Codex: позиция и размер индикатора задаются SetPos до первого рисования.
            # Полный снимок дисплея в базовом конструкторе тут же заменялся малым ROI.
            s=replace(s,'    , _back( fheroes2::Display::instance() )','''#ifdef HMM2_PACKED
    , _back( fheroes2::Display::instance(), 0, 0, 0, 0 )
#else
    , _back( fheroes2::Display::instance() )
#endif''')
        if p.name in ('image.cpp','image_tool.cpp','image_palette.cpp','ui_tool.cpp','ui_text.cpp','ui_font.cpp'):
            s='#include "hmm_resources.h"\n'+s
            # Codex: закрепляем входы до выхода из исходного пиксельного примитива.
            pattern=r'(^    (?:[\w:<>,*& ]+\s+)?([\w:]+)\([^;{}]*\)(?: const)?(?: noexcept)?\n    \{)'
            def guard(m):
                if m[2].endswith('::image'):return m[0]
                return m[0]+'\n        const HmmImagePins hmm_pins;'
            s=re.sub(pattern,guard,s,flags=re.M)
        if p.name=='ui_tool.cpp':
            # Клод: затемнение/проявление всего экрана (fadeDisplay) — слоем FT812: список экрана не меняется, поверх него (под курсором)
            # чёрный прямоугольник с альфой шага (hmm_gpu_fade). Прежде каждый шаг копировал экран во временную картинку и
            # перекрашивал её палитрой (ApplyAlpha): плотная карта разворачивалась в ~960 слоёв, список FT812 переполнялся, и каждый
            # шаг рисовался клетками процессора на весь экран — перед окном героя на Alteris (плата, сборки 93/94) запрос 348 КБ под
            # клетки не нашёл места, игра завершилась. Цвет шага — c·α точно, а не ближайший цвет палитры (различие только в кадрах
            # затемнения); курсор не темнеет, как у оригинала. Конец затемнения в чёрный — исходной заливкой Fill. Только весь экран и
            # только полное затемнение/проявление; остальное — прежним путём.
            s=replace(s,'''        const fheroes2::Rect fadeRoi( roi ^ fheroes2::Rect( 0, 0, display.width(), display.height() ) );
''','''        const fheroes2::Rect fadeRoi( roi ^ fheroes2::Rect( 0, 0, display.width(), display.height() ) );
#ifdef HMM2_PACKED
        if((endAlpha==fullDarkAlpha||endAlpha==fullBrightAlpha)&&hmm_gpu_fade_ready(fadeRoi.x,fadeRoi.y,fadeRoi.width,fadeRoi.height)){
            double fadeLevel=startAlpha;const uint32_t fadeDelay=fadeTimeMs/frameCount;const double fadeStep=(fadeLevel-endAlpha)/static_cast<double>(frameCount-1);
            uint32_t fadeFrame=0;LocalEvent &fadeEvents=LocalEvent::Get();
            Game::passCustomAnimationDelay(fadeDelay);
            while(fadeEvents.HandleEvents(Game::isCustomDelayNeeded(fadeDelay))){
                if(Game::validateCustomAnimationDelay(fadeDelay)){
                    if(fadeFrame==frameCount)break;
                    const uint8_t fadeAlpha=static_cast<uint8_t>(std::round(fadeLevel));
                    if(fadeAlpha==0){Fill(display,fadeRoi.x,fadeRoi.y,fadeRoi.width,fadeRoi.height,0);hmm_gpu_fade(0);}
                    else hmm_gpu_fade(uint8_t(255-fadeAlpha));
                    display.render(fadeRoi);
                    fadeLevel-=fadeStep;++fadeFrame;
                }
            }
            hmm_gpu_fade(0);return;
        }
#endif
''')
        if p.name=='screen.cpp':
            s=(ROOT/'main/hmm_screen_prefix.cpp').read_text(encoding='utf-8')+'\n'+s[s.index('namespace fheroes2\n{\n    void BaseRenderEngine::'):]
            s='#include "hmm_resources.h"\n'+s
            s=replace(s,'    uint8_t * Display::image()\n    {','''    uint8_t * Display::image()
    {
#ifdef HMM2_PACKED
        hmm_gpu_raw();
#endif''')
        if p.name=='rand.cpp':
            s='#include "hmm_platform.h"\n'+s
            s=replace(s,'    thread_local std::random_device rd;\n    thread_local PCG32 gen( rd );','    static PCG32 gen( static_cast<uint64_t>(hmm_millis()) + 1 );')
        if p.name=='rand.h':
            s=replace(s,'        std::hash<Value> hasher;\n        seed ^= hasher( v ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );','''#ifdef _WIN64
        std::hash<Value> hasher;
        seed ^= hasher( v ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
#else
        // Codex: std::hash чисел зависит от STL и разрядности. Для игровых seed повторяем FNV-1a эталона MSVC x64.
        static_assert(std::is_integral_v<Value> || std::is_enum_v<Value>);
        uint64_t hash=UINT64_C(14695981039346656037);
        const auto *bytes=reinterpret_cast<const unsigned char*>(&v);
        for(size_t i=0;i<sizeof(v);i++)hash=(hash^bytes[i])*UINT64_C(1099511628211);
        seed ^= hash + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
#endif''')
        if p.name=='game_mainmenu.cpp':
            s='#include "hmm_resources.h"\n'+s
            s=replace(s,'fheroes2::GameMode Game::MainMenu( const bool isFirstGameRun )\n{',
                'fheroes2::GameMode Game::MainMenu( const bool isFirstGameRun )\n{\n#ifdef HMM2_PACKED\n    hmm_gpu_status(nullptr,0,0,false);\n    hmm_resource_menu_cache(true);\n#endif')
            s=replace(s,'        case fheroes2::GameMode::START_BATTLE_ONLY_MODE:',
                '        case fheroes2::GameMode::START_BATTLE_ONLY_MODE:\n#ifdef HMM2_PACKED\n            hmm_resource_menu_cache(false);\n#endif')
        if p.name=='world_loadmap.cpp':
            s='#ifdef HMM2_PACKED\n#include "hmm_resources.h"\n#endif\n'+s
            s=replace(s,'    Reset();\n    Defaults();','''#ifdef HMM2_PACKED
    // Codex: новая игра, как загрузка сохранения, отдаёт также восстановимые пиксельные буферы.
    // Списки происхождения остаются: изображение восстановится при следующем CPU-доступе.
    hmm_release_for_world();
#endif
    Reset();
    Defaults();''')
            s=replace(s,'    std::vector<MP2::MP2AddonInfo> vec_mp2addons( addonCount );','    std::vector<uint8_t> addonBytes;')
            s=replace(s,'    for ( auto & mp2addon : vec_mp2addons ) {\n        MP2::loadAddon( fs, mp2addon );\n    }',
                '    // Codex: компактная таблица addon читается один раз. Повторные seek\n'
                '    // между тайлами и addon вытесняли две страницы и давали 200 МиБ на GateHell.\n'
                '    if(addonCount)addonBytes=fs.getRaw(addonCount*MP2::MP2_ADDON_STRUCTURE_SIZE);\n'
                '    ROStreamBuf addonStream(addonBytes);')
            s=replace(s,'vec_mp2addons.size() <= addonIndex','addonCount <= addonIndex')
            s=replace(s,'            const auto & objectInfo = vec_mp2addons[addonIndex];',
                '            addonStream.seek(addonIndex*MP2::MP2_ADDON_STRUCTURE_SIZE);\n'
                '            MP2::MP2AddonInfo objectInfo;MP2::loadAddon(addonStream,objectInfo);')
            s=replace(s,'addonIndex = vec_mp2addons[addonIndex].nextAddonIndex;','addonIndex = objectInfo.nextAddonIndex;')
            s=replace(s,'    fs.seek( afterAddonInfoPos );',
                '    // Codex: временная таблица освобождается до построения регионов и героев.\n'
                '    std::vector<uint8_t>().swap(addonBytes);\n    fs.seek( afterAddonInfoPos );')
        if p.name=='world_regions.h':
            s=replace(s,'#include <vector>','''#include <vector>
#ifdef HMM2_PACKED
#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>
#include "hmm_sequence.inc"
#endif''')
            s=replace(s,'    std::vector<MapRegionNode> _nodes;','''#ifdef HMM2_PACKED
    // Codex: очередь роста региона не требует второго массива на 16 КиБ при удвоении.
    Sequence<MapRegionNode> _nodes;
#else
    std::vector<MapRegionNode> _nodes;
#endif''')
        if p.name=='world_regions.cpp':
            s=replace(s,'    _nodes.reserve( expectedSize );','''#ifdef HMM2_PACKED
    // Codex: страницы очереди растут по факту; малым островам достаточно 1/4/16 узлов.
    (void)expectedSize;
#else
    _nodes.reserve( expectedSize );
#endif''')
            s=replace(s,'    _nodes.emplace_back( mapIndex );','    _nodes.push_back(MapRegionNode(mapIndex));')
            marker='''        for ( uint32_t adjacent : reg._neighbours ) {
            _regions[adjacent]._neighbours.insert( reg._id );
        }'''
            s=replace(s,marker,marker+'''
#ifdef HMM2_PACKED
        // Codex: после завершения построения ИИ читает номера и соседство регионов; очередь больше нигде не используется.
        Sequence<MapRegionNode>().swap(reg._nodes);
        reg._lastProcessedNode=0;
#endif''')
            last=s.rfind('}')
            s=s[:last]+'''#ifdef HMM2_PACKED
    // Codex: временные очереди построены; страницы отдаются до первого кадра мира.
    Sequence<MapRegionNode>::release_unused();
#endif
'''+s[last:]
        if p.name=='ai_planner_hero.cpp':
            # Codex: исходный расчёт штрафов ИИ сохраняется; 93312 байт больше не нужны одним куском.
            s=(ROOT/'main/hmm_threat_array.inc').read_text(encoding='utf-8')+'\n'+s
            s=replace(s,'    const std::vector<double> enemyThreatPenalties =',
                '    const HmmThreatArray enemyThreatPenalties =')
            s=replace(s,'        std::vector<double> result( world.getSize(), 0.0 );',
                '        HmmThreatArray result(world.getSize());')
        if p.name=='game_io.cpp':
            s=replace(s,'namespace\n{',(ROOT/'main/hmm_save.inc').read_text(encoding='utf-8')+'\nnamespace\n{',1)
            first=s.index('    RWStreamBuf dataStream;')
            s=s[:first]+s[first:].replace('    RWStreamBuf dataStream;','''#ifdef HMM2_PACKED
    // Codex: буферы сжатия живут в PSRAM до выхода из Save, а не на стеке задачи.
    const auto hmmStream = std::make_unique<HmmZipWriter>(fileStream);
    auto & dataStream = *hmmStream;
#else
    RWStreamBuf dataStream;
#endif''',1)
            s=replace(s,'    if ( dataStream.fail() || !Compression::zipStreamBuf( dataStream, fileStream ) ) {','''#ifdef HMM2_PACKED
    if ( dataStream.fail() || !dataStream.finish() ) {
#else
    if ( dataStream.fail() || !Compression::zipStreamBuf( dataStream, fileStream ) ) {
#endif''')
            s=replace(s,'''    RWStreamBuf dataStream;
    dataStream.setBigendian( true );

    if ( !Compression::unzipStream( fileStream, dataStream ) ) {''','''#ifdef HMM2_PACKED
    // Codex: буферы распаковки живут в PSRAM; формат и порядок чтения прежние.
    const auto hmmStream = std::make_unique<HmmZipReader>(fileStream);
    auto & dataStream = *hmmStream;
    if ( dataStream.fail() ) {
#else
    RWStreamBuf dataStream;
    dataStream.setBigendian( true );
    if ( !Compression::unzipStream( fileStream, dataStream ) ) {
#endif''')
            # Клод: кэши PSRAM отдаются до чтения мира, а не по нехватке посреди загрузки.
            s=replace(s,'    dataStream >> World::Get() >> conf >> GameOver::Result::Get();','''#ifdef HMM2_PACKED
    hmm_release_for_world();
#endif
    dataStream >> World::Get() >> conf >> GameOver::Result::Get();''')
            s=replace(s,'    if ( dataStream.fail() || endOfDataMarker != saveFileMagicNumber ) {','''    if ( dataStream.fail() || endOfDataMarker != saveFileMagicNumber
#ifdef HMM2_PACKED
         || !dataStream.finish()
#endif
    ) {''')
        if p.name=='localevent.cpp':
            s='#include "hmm_platform.h"\n'+s
            s=s.replace('SDL_PollEvent( &event )','hmm_poll_event( &event )')
        if p.name=='fheroes2.cpp':
            a=s.index('    const bool isPortBridgeOption');b=s.index('\n    try {',a)
            s=s[:a]+'    const bool isPortBridgeOption = false;\n'+s[b:]
            a=s.index('        if ( isPortBridgeOption ) {');b=s.index('        const DisplayInitializer',a)
            s=s[:a]+s[b:]
            s=s.replace('int main( int argc, char ** argv )','extern "C" int hmm_game_main( int argc, char ** argv )')
            s=s.replace('Game::mainGameLoop( conf.isFirstGameRun(), isProbablyDemoVersion() )','Game::mainGameLoop( false, isProbablyDemoVersion() )')
            a=s.index('#if defined( _WIN32 )\n    assert( argc == __argc );');b=s.index('#endif',a)
            s=s[:a]+s[b+6:]
            s=s.replace('        Game::Init();','        Game::Init();\n#ifdef HMM2_HOST\n        extern int hmm_host_ready();\n        if(hmm_host_ready())return 0;\n#endif')
        if p.name=='smk_decoder.h':
            # Codex: CPU-эталон сохраняет исходный класс libsmacker; адаптация DAT относится к ELF/GPU.
            original_smk_header=s
            # Клод: класс оригинала с источником кадров из HMM2.DAT (main/hmm_clip.cpp) вместо libsmacker.
            s=replace(s,'#include "smacker.h"\n','')
            s=replace(s,'~SMKVideoSequence() = default;','~SMKVideoSequence();')
            s=replace(s,'    int32_t _heightScaleFactor{ 1 };\n','')
            s=replace(s,'    std::unique_ptr<struct smk_t, void ( * )( struct smk_t * )> _videoFile{ nullptr, smk_close };\n',
                '    std::vector<uint32_t> _table;\n    uint32_t _base{ 0 };\n    unsigned _slot{ 0 };\n'
                '    mutable uint32_t _decoded{ ~0u };\n    mutable uint32_t _address{ 0 };\n    mutable unsigned _shown{ 0 };\n    mutable uint64_t _clockFrom{ 0 };\n')
            s=replace(s,'    unsigned long _currentFrameId{ 0 };','    mutable unsigned long _currentFrameId{ 0 };')
            s='#ifdef HMM2_PACKED\n'+s+'\n#else\n'+original_smk_header+'\n#endif\n'
        if p.name=='game_video.cpp':
            # Клод: ролики лежат в каталоге HMM2.DAT, поиск по папкам игры не нужен.
            a=s.index('    // Anim2 directory is used');b=s.index('    void playAudio(')
            s=s[:a]+s[b:]
            a=s.index('    bool getVideoFilePath(');b=s.index('    bool ShowVideo(')
            s=s[:a]+'''    bool getVideoFilePath( const std::string & fileName, std::string & path )
    {
        path = "video/";
        for ( const char c : fileName ) {
            path += static_cast<char>( c >= 'A' && c <= 'Z' ? c + 32 : c );
        }
        return hmm_is_file( path.c_str() ) != 0;
    }

'''+s[b:]
            s='#include "hmm_platform.h"\n'+s
        if p.name=='game_newgame.cpp':
            # Клод: рамка выбора кампании поверх кадра. DrawRect читал бы точки экрана и закрывал кадр заливкой;
            # четыре полосы Fill ложатся слоями поверх кадра, геометрия и цвет оригинала те же.
            s=replace(s,'''                fheroes2::DrawRect( display, roi, 51 );
                fheroes2::DrawRect( display, { roi.x - 1, roi.y - 1, roi.width + 2, roi.height + 2 }, 51 );''','''                fheroes2::Fill( display, roi.x - 1, roi.y - 1, roi.width + 2, 2, 51 );
                fheroes2::Fill( display, roi.x - 1, roi.y + roi.height - 1, roi.width + 2, 2, 51 );
                fheroes2::Fill( display, roi.x - 1, roi.y + 1, 2, roi.height - 2, 51 );
                fheroes2::Fill( display, roi.x + roi.width - 1, roi.y + 1, 2, roi.height - 2, 51 );''')
            # Codex: отсутствие IVY*.SMK обрабатывает исходный движок: предупреждение и переход в Price of Loyalty.
        if p.name=='interface_gamearea.cpp':
            s=replace(s,'    TileUnfitRenderObjectInfo tileUnfit;', '''#ifdef HMM2_PACKED
    const HmmMapRecordFogScope hmm_fog_scope(renderFog&&!isPuzzleDraw);
#endif
    TileUnfitRenderObjectInfo tileUnfit;''',1)
            # Клод: окно карты — кольцевая картинка RAM_G в координатах карты (hmm_gpu_map_view);
            # кадр строит сам движок, на FT досылаются только изменившиеся точки.
            s=replace(s,'#include "interface_gamearea.h"','#include "interface_gamearea.h"\n#ifdef HMM2_PACKED\n#include "hmm_resources.h"\n#endif',1)
            s=replace(s,'''void Interface::GameArea::Redraw( fheroes2::Image & dst, int flag, bool isPuzzleDraw ) const
{
''','''void Interface::GameArea::Redraw( fheroes2::Image & dst, int flag, bool isPuzzleDraw ) const
{
#ifdef HMM2_PACKED
    // Клод: на время перерисовки окна слои карты идут прямо в список режима карты (hmm_canvas.inc).
    const HmmMapScope hmm_map_scope( dst, !isPuzzleDraw, _windowROI.x, _windowROI.y, _windowROI.width, _windowROI.height, _topLeftTileOffset.x,
                                     _topLeftTileOffset.y );
#endif
''')
        if p.name=='interface_gamearea.cpp':
            # Codex: динамические части героев/монстров и туман сохраняют позиции в Redraw.
            # Codex: точная сигнатура проверяется ниже, чтобы обновление оригинала не потеряло границу.
            # Codex: исходная геометрия и выбор кадров остаются в GameArea; штатный CPU-путь сохраняется.
            calls=[
                ('    fheroes2::AlphaBlit( src, overlappedRoi.x - imageRoi.x, overlappedRoi.y - imageRoi.y, dst,',
                 '    if(hmm_gpu_map_sprite(src,overlappedRoi.x-imageRoi.x,overlappedRoi.y-imageRoi.y,dst,overlappedRoi.x,overlappedRoi.y,overlappedRoi.width,overlappedRoi.height,flip,false,alpha))return;'),
                ('    fheroes2::AlphaBlit( src, srcRoi.x + overlappedRoi.x - imageRoi.x, srcRoi.y + overlappedRoi.y - imageRoi.y, dst,',
                 '    if(hmm_gpu_map_sprite(src,srcRoi.x+overlappedRoi.x-imageRoi.x,srcRoi.y+overlappedRoi.y-imageRoi.y,dst,overlappedRoi.x,overlappedRoi.y,overlappedRoi.width,overlappedRoi.height,flip,false,alpha))return;'),
                ('    fheroes2::Copy( src, overlappedRoi.x - imageRoi.x, overlappedRoi.y - imageRoi.y, dst,',
                 '    if(hmm_gpu_map_sprite(src,overlappedRoi.x-imageRoi.x,overlappedRoi.y-imageRoi.y,dst,overlappedRoi.x,overlappedRoi.y,overlappedRoi.width,overlappedRoi.height,false,true))return;')]
            for marker,code in calls:
                assert s.count(marker)==1
                s=s.replace(marker,'#ifdef HMM2_PACKED\n'+code+'\n#endif\n'+marker)
            marker='    else if ( renderFog ) {\n'
            assert s.count(marker)==1
            s=s.replace(marker,marker+'#ifdef HMM2_PACKED\n        const HmmMapKind hmm_fog(2);\n#endif\n')
            marker='    void renderImagesOnTiles('
            at=s.index('\n    {',s.index(marker))+6
            s=s[:at]+'\n#ifdef HMM2_PACKED\n        const HmmMapKind hmm_dynamic(1);\n#endif'+s[at:]
        if p.name=='maps_tiles_render.cpp':
            s='#ifdef HMM2_PACKED\n#include "hmm_resources.h"\n#endif\n'+s
            # Codex: выбирают фазу по-прежнему renderObjectPart/renderMainObject, включая Magic Garden.
            for marker in ['        const uint32_t secondaryFrameIndex =','        uint32_t secondaryFrameIndex =']:
                assert s.count(marker)==1
                s=s.replace(marker,'#ifdef HMM2_PACKED\n        const HmmMapKind hmm_animation(1);\n#endif\n'+marker)
            marker='    void redrawFlyingGhostsOnMap('
            at=s.index('\n    {',s.index(marker))+6
            s=s[:at]+'\n#ifdef HMM2_PACKED\n        const HmmMapKind hmm_animation(1);\n#endif'+s[at:]
        if p.name=='game_mainmenu.cpp':
            for name in ['Editor::menuMain( false )','Editor::menuMain( true )','Editor::menuLoadMap()']:s=replace(s,name,'fheroes2::GameMode::MAIN_MENU')
        if p.name in ('screen.cpp','ui_scrollbar.cpp','game_auto_playtest.h'):s=s.replace('std::clamp(', 'std::clamp<int32_t>(').replace('std ::clamp(', 'std::clamp<int32_t>(')
        if p.name in ('editor_interface.cpp','map_random_generator_helper.cpp'):
            s=s.replace('std::max( 0, tilePos.','std::max<int32_t>( 0, tilePos.').replace('std::max( 1, static_cast<int32_t>( openTiles.size() )','std::max<int32_t>( 1, static_cast<int32_t>( openTiles.size() )')
        if p.name=='editor_interface_panel.cpp':s=s.replace('int & objectIndex','int32_t & objectIndex')
        if p.name=='maps_tiles.cpp':s=s.replace('bool & isActionObject ) {','bool & isActionObject ) -> int32_t {')
        if p.name=='maps_tiles.h':
            s='#include "hmm_node_allocator.h"\n'+s
            s=s.replace('std::list<ObjectPart>','HmmMapList<ObjectPart>')
        if p.name in ('serialize.h','serialize.cpp'):
            s=s.replace('template <class Type>\n    IStreamBase & operator>>( std::list<Type>', 'template <class Type, class Allocator>\n    IStreamBase & operator>>( std::list<Type, Allocator>')
            s=s.replace('template <class Type>\n    OStreamBase & operator<<( const std::list<Type>', 'template <class Type, class Allocator>\n    OStreamBase & operator<<( const std::list<Type, Allocator>')
            s='#include "hmm_platform.h"\n'+s
            for f in ['fopen','fclose','fread','fwrite','ftell','fseek']:s=s.replace('std::'+f,'hmm_'+f)
            s=s.replace('std::FILE','hmm_file')
        if p.name not in ('battle_port_bridge.cpp','audio_xmi2mid.cpp','logging.cpp'):
            for inc in ['iostream','sstream','iomanip','ostream']:s=s.replace('#include <'+inc+'>','#include "hmm_text.h"')
            s=s.replace('std::ostringstream','HmmText').replace('std::stringstream','HmmText')
            for manip in ['hex','dec','setw','setfill','endl']:s=re.sub(r'\bstd\s*::'+manip+r'\b','hmmfmt::'+manip,s)
        from patch_stage1_audio import patch as audio_patch
        s=audio_patch(p.name,s)
        from patch_loading_cursor import patch as loading_patch
        s=loading_patch(p.name,s)
        from patch_stage1_canvas import patch
        s=patch(p.name,s)
        from patch_fast_frame import patch as fast_patch
        s=fast_patch(p.name,s)
        from patch_speed import patch as speed_patch
        s=speed_patch(p.name,s)
        from patch_battle_cursor import patch as battle_cursor_patch
        s=battle_cursor_patch(p.name,s)
        from patch_map_squares import patch as map_squares_patch
        s=map_squares_patch(p.name,s)
        from patch_walk_ft import patch as walk_ft_patch
        s=walk_ft_patch(p.name,s)
        from patch_movement_input import patch as movement_input_patch
        s=movement_input_patch(p.name,s)
        from patch_ring_ft import patch as ring_ft_patch
        s=ring_ft_patch(p.name,s)
        from patch_move_trace import patch as move_trace_patch
        s=move_trace_patch(p.name,s)
        from patch_env_audio import patch as env_audio_patch
        s=env_audio_patch(p.name,s)
        from patch_monster_recipes import patch as monster_recipes_patch
        s=monster_recipes_patch(p.name,s)
        dest=STAGED/rel;dest.parent.mkdir(parents=True,exist_ok=True)
        if not dest.exists() or dest.read_text(encoding='utf-8')!=s:dest.write_text(s,encoding='utf-8')
    return STAGED
if __name__=='__main__':print(prepare())
