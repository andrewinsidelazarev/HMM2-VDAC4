"""Rare state transitions identify input, route budget, map events and guards."""

def patch(name,s):
    if name not in ('localevent.cpp','game_startgame.cpp','heroes.cpp','heroes_action.cpp'):
        return s
    s='#ifdef HMM2_PACKED\n#include "hmm_move_trace.h"\n#endif\n'+s
    def once(a,b):
        nonlocal s
        assert s.count(a)==1,(name,a)
        s=s.replace(a,b,1)
    if name=='localevent.cpp':
        a='if(event.type==SDL_KEYDOWN&&!event.repeat)eventHandler.setStates(LocalEvent::HMM_MOVEMENT_STOP);'
        once(a,'if(event.type==SDL_KEYDOWN&&!event.repeat){hmm_move_input_note(1,event.keysym.sym);eventHandler.setStates(LocalEvent::HMM_MOVEMENT_STOP);}')
        a='if(buttonType==MouseButtonType::MOUSE_BUTTON_LEFT||buttonType==MouseButtonType::MOUSE_BUTTON_RIGHT)setStates(HMM_MOVEMENT_STOP);'
        once(a,'if(buttonType==MouseButtonType::MOUSE_BUTTON_LEFT||buttonType==MouseButtonType::MOUSE_BUTTON_RIGHT){hmm_move_input_note(2,int(buttonType));setStates(HMM_MOVEMENT_STOP);}')
    elif name=='game_startgame.cpp':
        a='            if ( le.hmmMovementStopPressed() ) {\n'
        once(a,a+'''                if(!stopHero){const auto *h=GetFocusHeroes();const auto p=le.getMouseCursorPos();char m[144];
                    std::snprintf(m,sizeof(m),"HMM2 STOPREQ type=%u code=%d age=%u tile=%d x=%d y=%d",hmm_move_input_kind,hmm_move_input_code,
                        unsigned(uint32_t(hmm_millis())-hmm_move_input_when),h?h->GetIndex():-1,p.x,p.y);hmm_log(m);}
''')
        a='''                            if ( stopHero ) {
                                hero->SetMove( false );'''
        once(a,'''                            if ( stopHero ) {
#ifdef HMM2_PACKED
                                const HmmMoveCause hmmStopCause("input");
#endif
                                hero->SetMove( false );''')
    elif name=='heroes.cpp':
        a='''        SetModes( ENABLEMOVE );
    }
    else {'''
        once(a,'''        SetModes( ENABLEMOVE );
#ifdef HMM2_PACKED
        hmm_move_trace("start",GetIndex(),_path.empty()?GetIndex():_path.GetDestinationIndex(),_path.GetAllowedSteps(),unsigned(_path.size()),GetMovePoints());
#endif
    }
    else {''')
        a='''        ResetModes( ENABLEMOVE );

        // Reset the hero sprite'''
        once(a,'''#ifdef HMM2_PACKED
        const char *why=hmm_move_reason;
        if(!why)why=_path.empty()?"end":!_path.isValidForMovement()?"invalid":!_path.hasAllowedSteps()?"points":"game";
        hmm_move_trace(why,GetIndex(),_path.empty()?GetIndex():_path.GetDestinationIndex(),_path.GetAllowedSteps(),unsigned(_path.size()),GetMovePoints());
#endif
        ResetModes( ENABLEMOVE );

        // Reset the hero sprite''')
        a='''        if ( !targets.empty() ) {
            SetMove( false );'''
        once(a,'''        if ( !targets.empty() ) {
#ifdef HMM2_PACKED
            const HmmMoveCause hmmGuardCause("guard");
#endif
            SetMove( false );''')
    elif name=='heroes_action.cpp':
        a='''        hero.SetMove( false );

        const Funds fundsToUpdate'''
        once(a,'''#ifdef HMM2_PACKED
        const HmmMoveCause hmmEventCause("map_event");
#endif
        hero.SetMove( false );

        const Funds fundsToUpdate''')
    return s
