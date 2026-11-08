"""Execute FT812 display-list control flow for the independent RGB oracle.
The actual command program, including CALL/RETURN, is sent unchanged to Slot1.
"""
def execute(program):
    pc=0;stack=[];flat=[];extent=0
    for _ in range(131072):
        assert 0<=pc<len(program),('FT PC',pc,len(program))
        v=int(program[pc]);extent=max(extent,pc+1);pc+=1
        op=v>>24
        if op==0x1d:
            assert not (v&3) and len(stack)<4
            stack.append(pc);pc=(v&65535)//4
        elif op==0x1e:
            assert not (v&3)
            pc=(v&65535)//4
        elif op==0x24:
            assert stack,'FT RETURN underflow'
            pc=stack.pop()
        else:
            flat.append(v)
            if v==0:return flat,extent
    raise AssertionError('FT program did not terminate')

if __name__=='__main__':
    a=[0x1d000010,0x1d000010,0,0,0x04ffffff,0x24000000]
    assert execute(a)==([0x04ffffff,0x04ffffff,0],6)
    print('FT CALL/RETURN execution OK')
