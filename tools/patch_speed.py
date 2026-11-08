"""Codex: компактный выбор исходного имени объекта без изменения перевода и множественного числа."""
import re

def patch(name, source):
    if name != 'mp2.cpp':
        return source
    start=source.index('const char * MP2::StringObject(')
    end=source.index('\nbool MP2::isDayLife(', start)
    body=source[start:end]
    body=body.replace('    switch ( objectType ) {', '''    // Codex: один общий вызов перевода вместо копии его кода в каждом case.
    // Строки, четыре формы множественного числа и неизвестные типы прежние.
    const char * name = nullptr;
    switch ( objectType ) {''',1)
    body,n=re.subn(r'return _\( ("(?:[^"\\]|\\.)*") \);',r'name = \1; break;',body)
    assert n>100,n
    assert body.count('    return nullptr;')==1
    body=body.replace('    return nullptr;','    return name ? _( name ) : nullptr;')
    return source[:start]+body+source[end:]
