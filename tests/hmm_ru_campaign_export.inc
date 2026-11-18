// Native preparation only. Keep the original large campaign background on SD;
// the runtime overlays only these small, localized pieces in RAM_G.
static fheroes2::Sprite hmm_campaign_label(const fheroes2::Sprite &source,const fheroes2::Rect &area,
        const fheroes2::Rect &sample,const char *key,bool yellow){
    fheroes2::Sprite out=fheroes2::Crop(source,area.x,area.y,area.width,area.height);
    out.setPosition(area.x,area.y);
    // Reuse original blank material of the same panel; no blur/solid paint.
    for(int32_t y=0;y<area.height;y+=sample.height)for(int32_t x=0;x<area.width;x+=sample.width)
        fheroes2::Copy(source,sample.x,sample.y,out,x,y,std::min(sample.width,area.width-x),std::min(sample.height,area.height-y));
    if(key){
        const fheroes2::Text text(_(key),yellow?fheroes2::FontType::normalYellow():fheroes2::FontType::normalWhite());
        const int32_t width=text.width();
        if(width>area.width-4||text.height()>area.height-2)throw std::runtime_error("campaign label does not fit");
        text.draw((area.width-width)/2,(area.height-text.height())/2,out);
    }
    return out;
}
static std::vector<fheroes2::Sprite> hmm_campaign_labels(){
    std::vector<fheroes2::Sprite> result;
    for(int id:{ICN::CAMPBKGE,ICN::CAMPBKGG,ICN::X_CMPBKG}){
        const auto &source=fheroes2::AGG::GetICN(id,0);const bool yellow=id!=ICN::CAMPBKGE;
        if(id!=ICN::X_CMPBKG)result.push_back(hmm_campaign_label(source,{31,30,361,37},{33,30,28,37},
            id==ICN::CAMPBKGE?"Campaign header|Archibald":"Campaign header|Roland",yellow));
        result.push_back(hmm_campaign_label(source,{420,30,190,20},{577,30,28,20},"Campaign header|Days spent",yellow));
        result.push_back(hmm_campaign_label(source,{36,83,120,29},{21,83,15,29},"Campaign header|Scenario",yellow));
        result.push_back(hmm_campaign_label(source,{420,74,190,22},{420,119,155,22},"Campaign header|Awards",yellow));
        result.push_back(hmm_campaign_label(source,{420,180,190,22},{420,238,150,22},"Campaign header|Choice",yellow));
    }
    const int buttons[]={ICN::BUTTON_SMALL_OKAY_GOOD,ICN::BUTTON_SMALL_OKAY_EVIL,
        ICN::BUTTON_SMALL_CANCEL_GOOD,ICN::BUTTON_SMALL_CANCEL_EVIL,
        ICN::BUTTON_SMALL_MIN_GOOD,ICN::BUTTON_SMALL_MIN_EVIL,
        ICN::BUTTON_SMALL_MAX_GOOD,ICN::BUTTON_SMALL_MAX_EVIL};
    for(int id:buttons){
        const auto &in=fheroes2::AGG::GetICN(id,0);
        const int32_t lx=std::max<int32_t>(0,5-in.x()),ly=std::max<int32_t>(0,-in.y());
        const int32_t w=lx-5+in.x()+in.width()+5,h=ly+in.y()+in.height()+5;
        fheroes2::Sprite shadow(w,h,-lx,-ly);shadow.reset();
        fheroes2::addGradientShadow(in,shadow,{lx,ly},{-5,5});result.push_back(std::move(shadow));
    }
    return result;
}
static fheroes2::Sprite hmm_campaign_title(const fheroes2::Sprite &source,unsigned i){
    const char *titles[]={"Campaign header|Price of Loyalty","Campaign header|Descendants","Campaign header|Wizard's Isle","Campaign header|Voyage Home"};
    auto out=source;
    auto middle=hmm_campaign_label(source,{6,6,source.width()-12,source.height()-12},{8,6,30,source.height()-12},titles[i-15],true);
    fheroes2::Copy(middle,0,0,out,6,6,middle.width(),middle.height());
    return out;
}
