// Codex: палитры только добавляются и живут до выгрузки модуля. Страницы по
// восемь таблиц (2 КиБ) не занимают пул временных массивов и не требуют
// непрерывного блока 16/32 КиБ после долгой игры. Индексы таблиц неизменны.
class PaletteTables {
    using Row=std::array<uint8_t,256>;
    std::vector<std::unique_ptr<Row[]>> blocks;
    size_t count=0;
public:
    size_t size()const{return count;}
    const Row &operator[](size_t i)const{return blocks[i>>3][i&7];}
    void push_back(const Row &row){
        if(!(count&7))blocks.push_back(std::unique_ptr<Row[]>(new Row[8]));
        blocks[count>>3][count&7]=row;++count;
    }
};
