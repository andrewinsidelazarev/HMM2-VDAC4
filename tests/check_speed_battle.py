"""Codex: все пиксели доски и границы; старый полный перебор против выбора столбцов."""
from pathlib import Path
import argparse,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from patch_battle_cursor import patch

def function(text,start):
    at=text.index(start);left=text.index('{',at);depth=1;right=left+1
    while depth:
        depth+=(text[right]=='{')-(text[right]=='}');right+=1
    return text[at:right]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',required=True,type=Path);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    src=Path(os.environ['HMM2_STAGE1_BASE'])/'src/fheroes2/battle'
    cell=(src/'battle_cell.cpp').read_text(encoding='utf-8')
    # Codex: эталон берём до платформенных правок, чтобы повторный тест не сравнивал два ускоренных варианта.
    board=(ROOT/'third_party/openhmm2/src/fheroes2/battle/battle_board.cpp').read_text(encoding='utf-8')
    header=r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
#define HMM2_PACKED
namespace fheroes2{struct Point{int32_t x,y;Point(int32_t a=0,int32_t b=0):x(a),y(b){}bool operator==(const Point&p)const{return x==p.x&&y==p.y;}};struct Rect{int32_t x,y,width,height;};}
namespace Battle{
enum class AttackDirection{UNKNOWN,CENTER,TOP_LEFT,TOP,TOP_RIGHT,RIGHT,BOTTOM_RIGHT,BOTTOM,BOTTOM_LEFT,LEFT};
struct Cell{static constexpr int widthPx=44,heightPx=52;int32_t _index;fheroes2::Rect _pos{};fheroes2::Point _coord[7];explicit Cell(int i):_index(i){}void SetArea(const fheroes2::Rect&);AttackDirection GetTriangleDirection(const fheroes2::Point&)const;bool isPositionIncludePoint(const fheroes2::Point&p)const{return GetTriangleDirection(p)!=AttackDirection::UNKNOWN;}int32_t GetIndex()const{return _index;}const fheroes2::Rect&GetPos()const{return _pos;}};
struct Board:std::vector<Cell>{static constexpr int widthInCells=11,heightInCells=9,sizeInCells=99;int32_t original_index(const fheroes2::Point&)const;int32_t fast_index(const fheroes2::Point&)const;};
}
const int32_t cellHeightVerSide=32,infl=12;
'''
    body=function(cell,'bool inABC(')+'\n'+function(cell,'void Battle::Cell::SetArea(')+'\n'+function(cell,'Battle::AttackDirection Battle::Cell::GetTriangleDirection(')
    body+='\n'+function(board,'int32_t Battle::Board::GetIndexAbsPosition(').replace('GetIndexAbsPosition','original_index')
    body+='\n'+function(patch('battle_board.cpp',board),'int32_t Battle::Board::GetIndexAbsPosition(').replace('GetIndexAbsPosition','fast_index')
    tail=r'''
int main(){uint64_t points=0;Battle::Board board;
 for(int i=0;i<99;++i)board.emplace_back(i);
 for(const fheroes2::Point offset: {fheroes2::Point{0,0},{-500,-800},{120,-20},{50,100},{3000,4000}}){
  for(auto &c:board)c.SetArea({offset.x,offset.y,640,480});
  for(int y=-40;y<=540;++y)for(int x=-40;x<=700;++x){fheroes2::Point p(x+offset.x,y+offset.y);
   int a=board.original_index(p),b=board.fast_index(p);if(a!=b){std::printf("mismatch %d %d: %d %d\n",p.x,p.y,a,b);return 1;}++points;
  }
 }
 Battle::Board empty;if(empty.original_index({0,0})!=empty.fast_index({0,0}))return 2;
 std::printf("{\"points\":%llu,\"offsets\":5,\"different\":0}\n",points);
}
'''
    source=a.out/'battle.cpp';source.write_text(header+body+tail,encoding='utf-8')
    vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207');kit=Path('C:/Program Files (x86)/Windows Kits/10')
    inc=[vc/'include',*[kit/'Include/10.0.26100.0'/p for p in ('ucrt','um','shared')]];libs=[vc/'lib/x64',*[kit/'Lib/10.0.26100.0'/p/'x64' for p in ('ucrt','um')]]
    exe=a.out/'battle.exe'
    with (a.out/'build.log').open('wb') as log:subprocess.run([str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/EHsc','/std:c++17','/utf-8',*['/I'+str(p) for p in inc],str(source),'/Fo'+str(a.out/'battle.obj'),'/Fe'+str(exe),'/link',*['/LIBPATH:'+str(p) for p in libs]],cwd=a.out,stdout=log,stderr=subprocess.STDOUT,check=True)
    result=json.loads(subprocess.check_output([str(exe)],cwd=a.out,text=True));(a.out/'result.json').write_text(json.dumps(result,indent=2));print(result)

if __name__=='__main__':main()
