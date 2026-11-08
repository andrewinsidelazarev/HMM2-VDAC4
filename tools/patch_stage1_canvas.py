"""Codex: внедрение отложенных слоёв в копию OpenHMM2; исходники эталона не меняются."""
def insert(s,signature,code):
    at=s.index('\n    {',s.index(signature))+6
    return s[:at]+'\n#ifdef HMM2_PACKED\n'+code+'\n#endif\n'+s[at:]

def patch(name,s):
    if name=='dialog_hotkeys.cpp':
        s=s.replace('#include <vector>','#include <vector>\n#ifdef HMM2_HOST\n#include <cstdlib>\n#endif')
        s=s.replace('#include "ui_window.h"','#include "ui_window.h"\n#ifdef HMM2_PACKED\n#include "hmm_resources.h"\n#endif')
        s=s.replace('        display.render( background.totalArea() );','        display.render( background.totalArea() );\n#ifdef HMM2_HOST\n        if ( std::getenv( "HMM2_HOTKEYS_NESTED" ) )\n            listbox.ActionListPressRight( hotKeyEvents.front() );\n#endif')
        marker='        listbox.updateScrollBarImage();'
        assert s.count(marker)==1
        return s.replace(marker,marker+'\n#ifdef HMM2_PACKED\n        hmm_gpu_window_blits( listRoi.x, listRoi.y + 3, listRoi.width, listRoi.height - 4, fheroes2::getFontHeight( fheroes2::FontSize::NORMAL ) );\n#endif')
    if name=='battle_interface.h':
        marker='        void OpenAutoModeDialog( const Unit & unit, Actions & actions );'
        assert s.count(marker)==1
        return s.replace(marker,'''#ifdef HMM2_HOST
    public:
        void hmmTestOpenAutoMode( const Unit & unit, Actions & actions ) { OpenAutoModeDialog( unit, actions ); }
    private:
#endif
'''+marker)
    if name=='image.cpp':
        marker='    void ImageRestorer::restore()\n    {\n'
        assert s.count(marker)==1
        s=s.replace(marker,marker+'#ifdef HMM2_PACKED\n        if(!hmm_gpu_restore_allowed())return;\n        try {\n#endif\n')
        marker='        Copy( _copy, 0, 0, _image, _x, _y, _width, _height );'
        assert s.count(marker)==1
        s=s.replace(marker,marker+'\n#ifdef HMM2_PACKED\n        } catch(...) { hmm_window_restore_failed(); }\n#endif')
    if name=='view_world.cpp':
        from patch_view_world_ft import patch as patch_view_world
        return patch_view_world(s)
    if name=='game_interface.h':
        marker='    private:\n        AdventureMap();'
        assert s.count(marker)==1
        return s.replace(marker, '#ifdef HMM2_HOST\n    public:\n        fheroes2::GameMode hmmTestHumanTurn(){return HumanTurn(true);}\n#endif\n'+marker)
    if name=='ui_window.h':
        old='        Image & _output;'
        assert s.count(old)==1
        return s.replace(old, '#ifdef HMM2_PACKED\n        HmmMessageLayer _hmmLayer;\n#endif\n'+old)
    if name=='ui_window.cpp':
        old='        : _output( output )'
        assert s.count(old)==3
        # Fullscreen views retain their logical surface and their own screen
        # transition. Only an actual popup owns the three-frame alpha fade.
        for layer in ('_hmmLayer( output, width < output.width() || height < output.height() )',
                      '_hmmLayer( output, x > 0 || y > 0 || x + width < output.width() || y + height < output.height() )',
                      '_hmmLayer( output )'):
            s=s.replace(old, '        :\n#ifdef HMM2_PACKED\n          '+layer+',\n#endif\n          _output( output )',1)
        return s
    if name=='dialog.h':
        for old in ('        std::unique_ptr<fheroes2::ImageRestorer> _restorer;',):
            assert s.count(old)==1
            s=s.replace(old, '#ifdef HMM2_PACKED\n        HmmMessageLayer _hmmLayer;\n#endif\n'+old)
        return s
    scopes={
        'battle_dialogs.cpp': ['int Battle::Arena::DialogBattleHero(', 'bool Battle::DialogBattleSurrender('],
        'dialog_armyinfo.cpp': ['int Dialog::ArmyInfo('],
        'dialog_gameinfo.cpp': ['void Dialog::GameInfo('],
        'dialog_selectscenario.cpp': ['const Maps::FileInfo * Dialog::SelectScenario('],
        'dialog_thievesguild.cpp': ['void Dialog::ThievesGuild('],
        'dialog_quickinfo.cpp': ['    void showQuickInfo( const Castle &', '    void showQuickInfo( const HeroBase &'],
        'ui_map_interface.cpp': ['    void displayStandardPopupWindow('],
        'spell_book.cpp': ['Spell SpellBook::Open(', 'void SpellBook::Edit('],
    }
    if name in scopes:
        import re
        s='#include "hmm_resources.h"\n'+s
        for signature in scopes[name]:
            at=s.index(signature)
            body=re.search(r'\n\s*\{\n',s[at:])
            assert body is not None
            pos=at+body.end()
            # The guild/oracle is a full screen, including when replacing
            # a castle screen without creating StandardWindow. Other scopes
            # use source ICNs with an outer shadow or a shadowed FrameBox.
            layer='HmmMessageLayer hmm_message_layer( fheroes2::Display::instance(), false );' if name=='dialog_thievesguild.cpp' else 'HmmMessageLayer hmm_message_layer;'
            s=s[:pos]+'#ifdef HMM2_PACKED\n    '+layer+'\n#endif\n'+s[pos:]
        return s
    if name=='screen.cpp':
        # Клод: программный курсор — последним слоем DL FT812, а не Crop/Blit/Copy по Display.
        # Возвращённый Copy кусок под курсором был новым непрозрачным слоем; при движении мыши
        # меню засыпалось сотнями кусков фона (группа 279) до переполнения DL (плата и стенд ПК 28.10).
        old='''            const Sprite backup = Crop( *this, cursorROI.x, cursorROI.y, cursorROI.width, cursorROI.height );'''
        end='''            Copy( backup, 0, 0, *this, backup.x(), backup.y(), backup.width(), backup.height() );'''
        assert s.count(old)==1 and s.count(end)==1
        a=s.index(old);b=s.index(end)+len(end)
        s=s[:a]+'''#ifdef HMM2_PACKED
            hmm_gpu_cursor( &cursorImage, cursorROI.x, cursorROI.y );
            _renderFrame( getBoundaryRect( temp, cursorROI ) );
            if ( _postprocessing ) {
                _postprocessing();
            }
            hmm_gpu_cursor( nullptr, 0, 0 );
#else
'''+s[a:b]+'''
#endif'''+s[b:]
        return s
    if name=='image.h':
        s='#include "hmm_resources.h"\n#include <cstring>\n'+s
        s=s.replace('virtual ~Image() = default;', '''virtual ~Image() {
#ifdef HMM2_PACKED
            hmm_gpu_drop(*this);
#endif
        }
#ifdef HMM2_PACKED
        // Codex: ключ ресурса без RTTI; обычное изображение ресурса не имеет.
        virtual uint32_t _hmmResourceKey() const {return 0;}
        const uint8_t *_hmmPeek() const {return _data.get();}
        // Клод: буфер пикселей изображения со списком слоёв — только кэш; список описывает его полностью.
        void _hmmRelease() const {_data.reset();}
        uint8_t *_hmmData() const {
            if(!_data && _width && _height){
                const size_t area=size_t(_width)*_height;
                _data.reset(new uint8_t[area*(_singleLayer?1:2)]{});
                if(!_singleLayer)std::memset(_data.get()+area,1,area);
            }
            return _data.get();
        }
#endif''')
        s=s.replace('std::unique_ptr<uint8_t[]> _data;', 'mutable std::unique_ptr<uint8_t[]> _data;')
    if name!='image.cpp':return s
    # Codex: ранее перехват только записывал команду; теперь успешный перехват завершает примитив.
    import re
    s=re.sub(r'        hmm_gpu_offer\(([^\n]*)\);',r'        if(hmm_gpu_offer(\1))return;',s)
    s=s.replace('''#ifdef HMM2_PACKED
        if(hmm_gpu_offer(in,0,0,out,0,0,in.width(),in.height(),false,true))return;
#endif''','')
    # Клод: полная копия Copy(in,out) идёт слоями после resize, когда размер out уже верный.
    # Без перехвата бой каждый кадр рисовал поле 640×443 на CPU и снимал его в PSRAM
    # (RedrawCover: Copy(_battleGround,_mainSurface)) — отсюда bad_alloc в QEMU.
    # Двухслойный не-ресурс в однослойный оставлен CPU: там копируются цвета под прозрачными точками.
    full='''        out.resize( width, height );

        // We do a full copy of an image.'''
    assert s.count(full)==1
    s=s.replace(full,'''        out.resize( width, height );
#ifdef HMM2_PACKED
        {uint32_t hmm_key;if((in.singleLayer()||hmm_resource_id(in,hmm_key))&&hmm_gpu_offer(in,0,0,out,0,0,width,height,false,true))return;}
#endif

        // We do a full copy of an image.''')
    s=insert(s,'    uint8_t * Image::image()', '        auto *p=_hmmData();hmm_gpu_pixels(*this,p,true);return p;')
    s=insert(s,'    const uint8_t * Image::image() const', '        auto *p=_hmmData();hmm_gpu_pixels(*this,p,false);return p;')
    s=insert(s,'    void Image::clear()', '        hmm_gpu_drop(*this);')
    s=insert(s,'    void Image::fill(', '        if(hmm_gpu_fill(*this,0,0,_width,_height,value)){_data.reset();return;}')
    s=insert(s,'    void Image::reset()', '        if(hmm_gpu_fill(*this,0,0,_width,_height,0,_singleLayer?0:1)){_data.reset();return;}')
    s=insert(s,'    void Image::copy(', '''        if(hmm_gpu_active()){
            clear();_width=image._width;_height=image._height;_singleLayer=image._singleLayer;
            if(!image.empty())hmm_gpu_offer(image,0,0,*this,0,0,_width,_height,false,true);
            return;
        }''')
    # Codex: resize сохраняет только геометрию; пиксельный буфер выделяется при первом CPU-доступе.
    start=s.index('    void Image::resize(');at=s.index('        const size_t size =',start)
    s=s[:at]+'''#ifdef HMM2_PACKED
        hmm_gpu_drop(*this);_data.reset();_width=width_;_height=height_;return;
#endif
'''+s[at:]
    s=insert(s,'    Image::Image( Image && image ) noexcept', '        hmm_gpu_move(*this,image);')
    s=insert(s,'    Image & Image::operator=( Image && image ) noexcept', '        if(this!=&image)hmm_gpu_move(*this,image);')
    s=insert(s,'    void Fill( Image & image,', '        if(hmm_gpu_fill(image,x,y,width,height,colorId))return;')
    s+='\n#ifdef HMM2_PACKED\nconst uint8_t *hmm_image_transform_table(){return transformTable;}\n#endif\n'
    return s
