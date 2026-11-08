/* Codex: те же таблицы объектов из исходного populateObjectData, записанные генератором.
 * Все группы, поля и порядок сохраняются; вместо тысяч инструкций — общий цикл. */
static void hmmPopulateObjects(){
    const unsigned char *cursor=hmm_object_table;
    const auto read=[&cursor](){uint32_t value=0,shift=0;unsigned byte;do{byte=*cursor++;value|=(byte&127)<<shift;shift+=7;}while(byte&128);return value;};
    const auto readPart=[&read](Maps::ObjectPartInfo &part){
        uint32_t x=read(),y=read();part.tileOffset={static_cast<int32_t>((x>>1)^uint32_t(-int32_t(x&1))),static_cast<int32_t>((y>>1)^uint32_t(-int32_t(y&1)))};
        part.icnIndex=read();part.objectType=static_cast<MP2::MapObjectType>(read());
        part.icnType=static_cast<MP2::ObjectIcnType>(read());part.animationFrames=static_cast<uint8_t>(read());
    };
    for(auto &group:objectData){
        const uint32_t count=read();group.reserve(count);
        for(uint32_t i=0;i<count;i++){
            Maps::ObjectInfo object{static_cast<MP2::MapObjectType>(read())};
            object.metadata[0]=read();object.metadata[1]=read();
            const uint32_t ground=read(),top=read();object.groundLevelParts.reserve(ground);object.topLevelParts.reserve(top);
            for(uint32_t j=0;j<ground;j++){
                Maps::LayeredObjectPartInfo part{MP2::OBJ_ICN_TYPE_UNKNOWN,0,{0,0},MP2::OBJ_NONE,Maps::OBJECT_LAYER};
                readPart(part);part.layerType=static_cast<Maps::ObjectLayerType>(read());object.groundLevelParts.push_back(part);
            }
            for(uint32_t j=0;j<top;j++){
                Maps::ObjectPartInfo part{MP2::OBJ_ICN_TYPE_UNKNOWN,0,{0,0},MP2::OBJ_NONE};readPart(part);object.topLevelParts.push_back(part);
            }
            group.push_back(std::move(object));
        }
    }
    assert(cursor==hmm_object_table+sizeof(hmm_object_table));
}
