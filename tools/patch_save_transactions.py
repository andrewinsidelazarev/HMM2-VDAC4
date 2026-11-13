"""Keep save publication in the reproducible staged engine transformation."""
def patch(name,s):
    if name!='game_io.cpp':return s
    close='''    fileStream.close();
    if ( fileStream.fail() ) {
        return false;
    }
'''
    if close not in s:
        needle='    if ( !autoSave ) {'
        assert s.count(needle)==1
        s=s.replace(needle,'    // Codex: успех сохранения подтверждается только после сброса буфера и SYNC.\n'+close+'\n'+needle)
    publish='''    // Publish only after serialization, compression, flush and SYNC all succeed.
    if ( !hmm_save_commit() ) {
        return false;
    }
'''
    assert 'hmm_save_commit' not in s and s.count(close)==1
    return s.replace(close,close+publish)
