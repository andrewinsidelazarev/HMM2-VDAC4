// Native UI asset preparation: preserve battle button geometry, localize its text.
#include "ui_text.h"
static fheroes2::Sprite hmm_russian_battle_button(const fheroes2::Sprite &source,unsigned frame){
    fheroes2::Sprite result=source;
    if(frame!=0&&frame!=1&&frame!=4&&frame!=5)return result;
    const bool pressed=(frame&1)!=0;
    const int w=source.width(),h=source.height();
    const auto &empty=fheroes2::AGG::GetICN(ICN::EMPTY_GOOD_BUTTON,pressed?1:0);
    fheroes2::Image background(w,h);background.reset();fheroes2::Resize(empty,background);
    fheroes2::Copy(background,2,2,result,2,2,w-4,h-4);
    const char *caption=frame<2?"\xcf\xf0\xee\xef\xf3\xf1\xea":"\xc0\xe2\xf2\xee";
    fheroes2::Text text(caption,{fheroes2::FontSize::SMALL,fheroes2::FontColor::WHITE});
    // Glyph bearings/shadows can extend beyond Text::width(). Render with margins,
    // crop the actual painted bounds, then fit the whole result inside the button.
    fheroes2::Image padded(text.width()+12,text.height()+12);padded.reset();text.draw(6,6,padded);
    int x0=padded.width(),y0=padded.height(),x1=-1,y1=-1;
    for(int y=0;y<padded.height();++y)for(int x=0;x<padded.width();++x)
        if(padded.transform()[y*padded.width()+x]!=1){x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);}
    if(x1<x0||y1<y0)throw std::runtime_error("Empty Russian battle button label");
    fheroes2::Image label(x1-x0+1,y1-y0+1);label.reset();fheroes2::Copy(padded,x0,y0,label,0,0,label.width(),label.height());
    if(label.width()>w-8){fheroes2::Image fit(w-8,label.height());fit.reset();fheroes2::Resize(label,fit);label=std::move(fit);}
    fheroes2::Blit(label,result,(w-label.width())/2+(pressed?1:0),(h-label.height())/2+(pressed?1:0));
    std::printf("RU BATTLE button=%u size=%dx%d text=%dx%d\n",frame,w,h,text.width(),text.height());
    return result;
}
