// Two installed languages need no scrolling list or per-language image cache.
// The normal window layer supplies FT812 sprites, shadows and popup-only fade.
namespace fheroes2 {
SupportedLanguage selectLanguage(const std::vector<SupportedLanguage> &languages,const SupportedLanguage currentLanguage,const bool isGameLanguage){
    if(languages.empty())return currentLanguage;
    if(languages.size()==1){
        if(isGameLanguage)Settings::Get().setGameLanguage(getLanguageAbbreviation(languages.front()));
        return languages.front();
    }
    const CursorRestorer cursor(true,::Cursor::POINTER);
    auto &display=Display::instance();auto &conf=Settings::Get();
    StandardWindow window(300,170,true,display);const Rect roi=window.activeArea();
    const Text title(isGameLanguage?_("Select Game Language:"):_("Select Language:"),FontType::normalWhite());
    title.draw(roi.x+(roi.width-title.width())/2,roi.y+12,display);
    const unsigned count=std::min(unsigned(languages.size()),2u);
    const Rect rows(roi.x+20,roi.y+43,roi.width-40,int(count)*27);
    window.applyTextBackgroundShading(rows);ImageRestorer rowBackground(display,rows.x,rows.y,rows.width,rows.height);
    Button okay,cancel;window.renderOkayCancelButtons(okay,cancel);
    unsigned selected=currentLanguage==languages[0]?0:count-1;
    auto draw=[&](){rowBackground.restore();for(unsigned i=0;i<count;++i){
        const LanguageSwitcher ownLanguage(languages[i]);
        Text text(getLanguageName(languages[i]),i==selected?FontType::normalYellow():FontType::normalWhite());
        text.draw(rows.x+(rows.width-text.width())/2,rows.y+int(i)*27+4,display);
    }display.render();};
    draw();auto &events=LocalEvent::Get();
    while(events.HandleEvents()){
        okay.drawOnState(events.isMouseLeftButtonPressedAndHeldInArea(okay.area()));
        cancel.drawOnState(events.isMouseLeftButtonPressedAndHeldInArea(cancel.area()));
        if(events.MouseClickLeft(cancel.area())||Game::HotKeyPressEvent(Game::HotKeyEvent::DEFAULT_CANCEL))return currentLanguage;
        if(events.MouseClickLeft(okay.area())||Game::HotKeyPressEvent(Game::HotKeyEvent::DEFAULT_OKAY)){
            const auto choice=languages[selected];
            if(isGameLanguage){
                const auto previous=conf.getGameLanguage();
                if(!conf.setGameLanguage(getLanguageAbbreviation(choice)))return currentLanguage;
                if(previous!=conf.getGameLanguage()&&!conf.Save(Settings::configFileName)){
                    fheroes2::showStandardTextMessage("",_("Unable to save the language setting."),Dialog::OK);
                }
            }
            return choice;
        }
        unsigned next=selected;
        if(Game::HotKeyPressEvent(Game::HotKeyEvent::DEFAULT_UP))next=0;
        if(Game::HotKeyPressEvent(Game::HotKeyEvent::DEFAULT_DOWN))next=count-1;
        for(unsigned i=0;i<count;++i)if(events.MouseClickLeft(Rect(rows.x,rows.y+int(i)*27,rows.width,27)))next=i;
        if(next!=selected){selected=next;draw();}
    }
    return currentLanguage;
}
}
