"""Отмена маршрута только новым нажатием, а не автоповтором/отпусканием."""

def patch(name,s):
    if name=='localevent.h':
        marker='    bool isAnyKeyPressed() const\n'
        assert s.count(marker)==1
        s=s.replace(marker,'''#ifdef HMM2_PACKED
    bool hmmMovementStopPressed() const {return (_actionStates&HMM_MOVEMENT_STOP)!=0;}
#endif
'''+marker)
        marker='        DRAG_ONGOING = ( 1 << 7 ),'
        assert s.count(marker)==1
        return s.replace(marker,marker+'\n#ifdef HMM2_PACKED\n        HMM_MOVEMENT_STOP = ( 1 << 8 ),\n#endif')
    if name=='localevent.cpp':
        marker='    resetStates( KEY_PRESSED );\n'
        at=s.index(marker,s.index('bool LocalEvent::HandleEvents('))
        s=s[:at]+s[at:].replace(marker,marker+'#ifdef HMM2_PACKED\n    resetStates( HMM_MOVEMENT_STOP );\n#endif\n',1)
        marker='            eventHandler.onKeyboardEvent( key, getKeyModifierFromSDL( event.keysym.mod ), state );'
        assert s.count(marker)==1
        s=s.replace(marker,'''#ifdef HMM2_PACKED
            if(event.type==SDL_KEYDOWN&&!event.repeat)eventHandler.setStates(LocalEvent::HMM_MOVEMENT_STOP);
#endif
'''+marker)
        marker='    if ( isPressed ) {\n'
        at=s.index(marker,s.index('void LocalEvent::onMouseButtonEvent('))
        s=s[:at]+s[at:].replace(marker,marker+'''#ifdef HMM2_PACKED
        if(buttonType==MouseButtonType::MOUSE_BUTTON_LEFT||buttonType==MouseButtonType::MOUSE_BUTTON_RIGHT)setStates(HMM_MOVEMENT_STOP);
#endif
''',1)
        return s
    if name=='game_startgame.cpp':
        marker='            if ( le.isAnyKeyPressed() || le.MouseClickLeft() || le.isMouseRightButtonPressed() ) {'
        assert s.count(marker)==1
        return s.replace(marker,'''#ifdef HMM2_PACKED
            // Autorepeat of the start key and delayed button release are not a new stop command.
            if ( le.hmmMovementStopPressed() ) {
#else
'''+marker+'\n#endif')
    return s
